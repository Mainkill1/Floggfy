(() => {
    "use strict";

    if (typeof window.__floggfyMarkedPoll === "function") return;

    const trackUri = /^spotify:track:[A-Za-z0-9]{22}$/;
    const playlistUri = /^spotify:playlist:[A-Za-z0-9]{22}$/;
    const pageUri = /^spotify:(?:playlist|album):[A-Za-z0-9]{22}$/;
    const maxUris = 256;
    const dateTtlMs = 30_000;
    const templateProperty = "grid-template-columns";
    const savedWidth = 104;
    const extendedTemplate = "var(--grid-template-columns) " + savedWidth + "px";
    const pageSelector = '[data-testid="playlist-page"], [data-testid="album-page"]';
    const instance = Math.random().toString(36).slice(2);
    const mounted = new Map();
    const nativeSortIndicators = new Map();
    let enabled = false;
    let revision;
    let epoch = 0;
    let sequence = 0;
    let scheduled = false;
    let context;
    let observedGrid;
    let columnCount;
    let dates = new Map();
    let dateGeneration = 0;
    let freshScope;
    let freshUntil = 0;
    let pending;
    let sorter;
    let releasePending = false;

    function own(object, key) {
        if (!object || typeof object !== "object") return undefined;
        return Object.getOwnPropertyDescriptor(object, key)?.value;
    }

    function uriFromProps(props) {
        const candidates = new Set([
            own(props, "trackUri"), own(props, "uri"),
            own(own(props, "item"), "uri"), own(own(props, "track"), "uri")
        ].filter(uri => typeof uri === "string" && trackUri.test(uri)));
        return candidates.size === 1 ? candidates.values().next().value : null;
    }

    const maxAncestors = 512;
    const maxSiblings = 256;

    function rootFrom(fiber) {
        for (let depth = 0; fiber && depth < maxAncestors; ++depth) {
            const parent = own(fiber, "return");
            if (!parent) {
                const current = own(own(fiber, "stateNode"), "current");
                return current === fiber || current === own(fiber, "alternate") ? current : undefined;
            }
            fiber = parent;
        }
        return undefined;
    }

    function branchInChildren(parent, branches) {
        let child = own(parent, "child");
        for (let count = 0; child && count < maxSiblings; ++count) {
            const index = branches.indexOf(child);
            if (index !== -1) return index;
            child = own(child, "sibling");
        }
        return -1;
    }

    function currentFiber(fiber) {
        // React's reflection code describes why alternate fibers can share a
        // return pointer or a bailed-out child list. Follow parent pairs in the
        // original branch order and use child membership to disambiguate.
        // https://github.com/react/react/blob/main/packages/react-reconciler/src/ReactFiberTreeReflection.js
        const alternate = own(fiber, "alternate");
        if (!alternate) return rootFrom(fiber) ? fiber : undefined;
        const origins = [fiber, alternate];
        let branches = origins;
        for (let depth = 0; depth < maxAncestors; ++depth) {
            const parents = branches.map(branch => own(branch, "return"));
            if (!parents[0]) {
                const current = own(own(branches[0], "stateNode"), "current");
                return origins[branches.indexOf(current)];
            }

            const [parent, other] = [parents[0], own(parents[0], "alternate")];
            if (!other) return undefined;
            const sameReturn = parents[0] === parents[1];
            const sharedChildren = own(parent, "child") === own(other, "child");
            if (!sameReturn && !sharedChildren) {
                if (parents[1] !== other) return undefined;
                branches = parents;
                continue;
            }

            const index = branchInChildren(parent, branches);
            if (sharedChildren) return index >= 0 && rootFrom(parent) ? origins[index] : undefined;
            const otherIndex = index < 0 ? branchInChildren(other, branches) : -1;
            if (index < 0 && otherIndex < 0) return undefined;
            branches = index === 0 || otherIndex === 1 ? [parent, other] : [other, parent];
        }
        return undefined;
    }

    function uriFromReact(row, gridNode, boundary) {
        for (const host of new Set([row, gridNode])) {
            for (const key of Object.getOwnPropertyNames(host)) {
                if (key.startsWith("__reactProps$")) {
                    const uri = uriFromProps(own(host, key));
                    if (uri) return uri;
                }
                if (!key.startsWith("__reactFiber$")) continue;
                let fiber = own(host, key);
                for (let depth = 0; fiber && depth < 8; ++depth) {
                    const node = own(fiber, "stateNode");
                    if (node === boundary || (depth && node?.nodeType === 1 && node.contains(row) && node !== row && node !== gridNode)) break;
                    const candidate = uriFromProps(own(fiber, "memoizedProps"));
                    const alternate = own(fiber, "alternate");
                    const alternateCandidate = uriFromProps(own(alternate, "memoizedProps"));
                    if (candidate || alternateCandidate) {
                        const current = currentFiber(fiber);
                        const uri = current === fiber ? candidate : current === alternate && alternateCandidate;
                        if (uri) return uri;
                    }
                    fiber = own(fiber, "return");
                }
            }
        }
        return "";
    }

    function identity(row, gridNode, grid) {
        for (const element of new Set([row, gridNode])) {
            for (const attribute of ["data-track-uri", "data-test-uri"]) {
                const uri = element.getAttribute(attribute);
                if (trackUri.test(uri)) return uri;
            }
        }
        const links = row.querySelectorAll('a[data-testid="internal-track-link"][href]');
        if (links.length === 1) {
            try {
                const url = new URL(links[0].getAttribute("href"), location.href);
                const match = /^\/track\/([A-Za-z0-9]{22})\/?$/.exec(url.pathname);
                if (url.origin === location.origin && match) return "spotify:track:" + match[1];
            } catch (_) {}
        }
        return uriFromReact(row, gridNode, grid);
    }

    function templateTracks(template) {
        // Explicit tracks may have line names and minmax() expressions.
        if (!template || /\b(?:repeat|subgrid|none)\b/.test(template)) return [];
        return template.replace(/\[[^\]]*\]/g, " ").trim().match(/[^\s(]+\([^()]*\)|[^\s()]+/g) || [];
    }

    function pixels(value) {
        return /^\d+(?:\.\d+)?px$/.test(value) ? parseFloat(value) : 0;
    }

    function minimumWidth(tracks, style) {
        let width = savedWidth + pixels(style.columnGap) * tracks.length +
            pixels(style.paddingLeft) + pixels(style.paddingRight);
        for (const track of tracks) {
            const minimum = /^(?:minmax\()?\s*(\d+(?:\.\d+)?px)(?:\s*,|$)/.exec(track);
            if (!minimum) return Infinity;
            width += pixels(minimum[1]);
        }
        return width;
    }

    function layout(row, role, expectedCount) {
        for (const node of [row, ...row.children]) {
            const cells = [...node.children].filter(child => child.getAttribute("role") === role &&
                child !== mounted.get(row)?.cell);
            if (cells.length < 3 || (expectedCount && cells.length !== expectedCount)) continue;
            const style = getComputedStyle(node);
            if (style.display !== "grid") continue;
            const template = style.getPropertyValue("--grid-template-columns").trim();
            const tracks = templateTracks(template);
            if (!/\[\s*last\s*\]/.test(template) || tracks.length !== cells.length) continue;
            const owned = mounted.get(row);
            const extra = owned?.node === node && node.style.getPropertyValue(templateProperty) === owned.written ? 1 : 0;
            // Resolved CSSOM includes the appended track once the override is installed.
            const resolved = templateTracks(style.gridTemplateColumns).length;
            if (resolved !== cells.length + extra &&
                !(extra && style.gridTemplateColumns === owned.written)) continue;
            if (cells.some(cell => getComputedStyle(cell).display === "none")) continue;
            return { node, count: cells.length, width: minimumWidth(tracks, style) };
        }
        return null;
    }

    function removeRow(row) {
        const previous = mounted.get(row);
        if (!previous) return;
        previous.cell.remove();
        const style = previous.node.style;
        if (style.getPropertyValue(templateProperty) === previous.written &&
            style.getPropertyPriority(templateProperty) === "") {
            if (previous.template) style.setProperty(templateProperty, previous.template, previous.priority);
            else style.removeProperty(templateProperty);
        }
        mounted.delete(row);
    }

    function restoreNativeSortIndicators() {
        for (const [cell, previous] of nativeSortIndicators) {
            if (cell.getAttribute("aria-sort") === "none") cell.setAttribute("aria-sort", previous.order);
            for (const [icon, value, priority] of previous.icons) {
                if (icon.style.visibility === "hidden") {
                    if (value) icon.style.setProperty("visibility", value, priority);
                    else icon.style.removeProperty("visibility");
                }
            }
        }
        nativeSortIndicators.clear();
    }

    function updateNativeSortIndicators(header, sorted) {
        restoreNativeSortIndicators();
        if (!sorted) return;
        // Some views always retain a native default sort. Saved owns the visible
        // order until reset; do not leave that default's arrow/ARIA claiming it.
        for (const cell of header.querySelectorAll('[role="columnheader"]:not([data-floggfy-saved-column])')) {
            const order = cell.getAttribute("aria-sort");
            if (order !== "ascending" && order !== "descending") continue;
            const icons = [...cell.querySelectorAll('svg[aria-hidden="true"]')]
                .filter(icon => icon.style.getPropertyValue("--encore-icon-fill").includes("--text-bright-accent"))
                .map(icon => [icon, icon.style.visibility, icon.style.getPropertyPriority("visibility")]);
            nativeSortIndicators.set(cell, {order, icons});
            cell.setAttribute("aria-sort", "none");
            for (const [icon] of icons) icon.style.visibility = "hidden";
        }
    }

    function resetLayout() {
        const released = sorter?.setContext(null, null) !== false;
        restoreNativeSortIndicators();
        for (const row of mounted.keys()) removeRow(row);
        if (columnCount && columnCount.grid.getAttribute("aria-colcount") === columnCount.written) {
            const { grid, original } = columnCount;
            if (original === null) grid.removeAttribute("aria-colcount");
            else grid.setAttribute("aria-colcount", original);
        }
        columnCount = undefined;
        return released;
    }

    function resetQuery() {
        ++epoch;
        pending = undefined;
        dates = new Map();
        ++dateGeneration;
        freshScope = undefined;
    }

    function setColumnCount(grid, nativeCount) {
        const current = grid.getAttribute("aria-colcount");
        const owned = columnCount?.grid === grid && current === columnCount.written;
        const original = owned ? columnCount.original : current;
        if (!owned && original !== null && original !== String(nativeCount)) return;
        const written = String(nativeCount + 1);
        if (current !== written) grid.setAttribute("aria-colcount", written);
        columnCount = { grid, original, written };
    }

    function paint(row, gridLayout, uri, header = false, sortable = true, missingDates = false) {
        const { node, count } = gridLayout;
        let entry = mounted.get(row);
        if (entry && (entry.node !== node || entry.count !== count || entry.cell.parentElement !== node ||
            node.style.getPropertyValue(templateProperty) !== entry.written)) {
            removeRow(row);
            entry = undefined;
        }
        if (!entry) {
            const cell = document.createElement("div");
            cell.setAttribute("role", header ? "columnheader" : "gridcell");
            cell.setAttribute("aria-colindex", String(count + 1));
            cell.setAttribute("data-floggfy-saved-column", "");
            cell.style.cssText = "align-self:center; overflow:hidden; white-space:nowrap; text-overflow:ellipsis;";
            cell.style.gridColumn = String(count + 1);
            const template = node.style.getPropertyValue(templateProperty);
            const priority = node.style.getPropertyPriority(templateProperty);
            node.style.setProperty(templateProperty, extendedTemplate);
            const written = node.style.getPropertyValue(templateProperty);
            if (written !== extendedTemplate) return;
            if (header && sortable) {
                const button = document.createElement("button");
                button.type = "button";
                button.style.cssText = "font:inherit;font-size:14px;color:inherit;background:none;border:0;padding:0;display:inline-flex;align-items:center;gap:6px;cursor:pointer;";
                button.addEventListener("click", event => {
                    event.stopPropagation();
                    sorter?.cycle();
                    schedule();
                });
                cell.append(button);
            }
            node.append(cell);
            entry = { cell, node, count, template, priority, written };
            mounted.set(row, entry);
        }
        if (header) {
            if (!sortable) {
                entry.cell.textContent = "Saved";
                entry.cell.title = "Date this track was last saved";
                return;
            }
            const state = sorter?.state || { mode: "none", loading: false, error: "Sorting is unavailable in this layout." };
            const button = entry.cell.firstElementChild;
            const waiting = state.loading || (sorter?.uris && missingDates);
            const label = waiting ? "Saved …" : "Saved";
            button.setAttribute("aria-busy", waiting ? "true" : "false");
            const arrow = state.mode === "descending" ? "▼" : state.mode === "ascending" ? "▲" : "";
            const key = label + arrow;
            if (button.dataset.sortLabel !== key) {
                button.replaceChildren(document.createTextNode(label));
                if (arrow) {
                    const icon = document.createElement("span");
                    icon.setAttribute("aria-hidden", "true");
                    icon.style.cssText = "font-size:11px;color:var(--text-bright-accent,#1ed760)";
                    icon.textContent = arrow;
                    button.append(icon);
                }
                button.dataset.sortLabel = key;
            }
            const hint = state.error || (waiting ? "Loading the complete playlist and saved dates" :
                state.mode === "descending" ? "Newest saved first; click for oldest first" :
                state.mode === "ascending" ? "Oldest saved first; click to restore playlist order" :
                "Sort the entire playlist by saved date, newest first");
            entry.cell.setAttribute("aria-sort", state.mode);
            button.title = hint;
            button.setAttribute("aria-label", "Saved. " + hint);
            return;
        }
        const date = dates.get(uri);
        const text = date?.label || "";
        const tooltip = date?.tooltip || "";
        if (entry.cell.textContent !== text) entry.cell.textContent = text;
        if (entry.cell.title !== tooltip) entry.cell.title = tooltip;
    }

    function discover(section) {
        let match;
        const grids = [...section.querySelectorAll('[role="grid"]')].filter(grid =>
            !grid.parentElement.closest('[role="grid"]') && grid.closest(pageSelector) === section);
        const primary = grids.filter(grid => grid.getAttribute("data-testid") === "playlist-tracklist");
        if (primary.length > 1) return null;
        for (const grid of primary.length ? primary : grids) {
            const rows = [...grid.querySelectorAll('[role="row"]')]
                .filter(row => row.closest('[role="grid"]') === grid);
            const headers = rows.map(row => ({ row, layout: layout(row, "columnheader") }))
                .filter(header => header.layout && header.layout.width <= grid.clientWidth);
            if (headers.length !== 1) continue;
            if (match) return null;
            match = { grid, rows, header: headers[0] };
        }
        return match;
    }

    function requestDates(visible) {
        const now = performance.now();
        if (freshScope === visible && now < freshUntil) return false;
        freshScope = undefined;
        // Keep recent off-screen lookups for this page so resetting a sort does
        // not discard its dates. Navigation/revision changes still clear them.
        const limit = Math.max(10_000, visible.size);
        for (const uri of dates.keys()) {
            if (dates.size <= limit) break;
            if (!visible.has(uri)) dates.delete(uri);
        }
        const missing = [];
        const expired = [];
        let nextExpiry = Infinity;
        for (const uri of visible) {
            const date = dates.get(uri);
            if (!date) {
                missing.push(uri);
                if (missing.length === maxUris) break;
            } else {
                const expires = date.checkedAt + dateTtlMs;
                nextExpiry = Math.min(nextExpiry, expires);
                if (expires <= now && expired.length < maxUris) expired.push(uri);
            }
        }
        const uris = missing.length ? missing : expired;
        if (document.visibilityState === "hidden") {
            pending = undefined;
            return missing.length > 0;
        }
        if (!uris.length) {
            pending = undefined;
            freshScope = visible;
            freshUntil = nextExpiry;
            return false;
        }
        const key = [...uris].sort().join("|");
        if (pending?.key === key) {
            if (now - pending.started < 3000) return missing.length > 0;
            pending.started = now;
        } else {
            const request = instance + "_" + epoch + "_" + (++sequence);
            pending = { request, key, uris, started: now };
        }
        console.log("FLOGGFY_MARKED_V1:" + pending.request + "|" + pending.uris.join("|"));
        return missing.length > 0;
    }

    function pageIdentity(section) {
        const uri = section?.getAttribute("data-test-uri");
        if (pageUri.test(uri)) return uri;
        if (!section) return null;
        // Liked Songs and album layouts omit data-test-uri. Their committed
        // React provider still carries the native page identity, even offline.
        const key = Object.getOwnPropertyNames(section).find(name => name.startsWith("__reactFiber$"));
        let fiber = key && currentFiber(own(section, key));
        for (let depth = 0; fiber && depth < 48; ++depth, fiber = own(fiber, "return")) {
            const candidate = own(own(fiber, "memoizedProps"), "uri");
            if (pageUri.test(candidate)) return candidate;
        }
        return null;
    }

    function reconcile(reply) {
        const pages = document.querySelectorAll(pageSelector);
        const section = pages.length === 1 ? pages[0] : null;
        const uri = pageIdentity(section);
        const route = /^\/(playlist|album)\/([A-Za-z0-9]{22})\/?$/.exec(location.pathname);
        const match = pageUri.test(uri) && (!route || uri === "spotify:" + route[1] + ":" + route[2]) && discover(section);
        if (!match || context?.section !== section || context?.uri !== uri ||
            context?.url !== location.href || context?.grid !== match.grid) {
            if (context) {
                if (!resetLayout()) return;
                resetQuery();
            }
            context = match ? { section, uri, url: location.href, grid: match.grid } : undefined;
        }
        if (observedGrid !== match?.grid) {
            resizeObserver?.disconnect();
            observedGrid = match?.grid;
            if (observedGrid) resizeObserver?.observe(observedGrid);
        }
        if (!match) return;

        const { grid, rows, header } = match;
        const sortable = playlistUri.test(uri);
        if (sorter?.setContext(sortable ? grid : null, sortable ? uri : null) === false) return;
        sorter?.refresh();
        setColumnCount(grid, header.layout.count);
        const layouts = new Map();
        const visible = new Set();
        for (const row of rows) {
            if (row === header.row) continue;
            const rowLayout = layout(row, "gridcell", header.layout.count);
            if (!rowLayout || rowLayout.width > grid.clientWidth) continue;
            const identityUri = identity(row, rowLayout.node, grid);
            if (identityUri) visible.add(identityUri);
            layouts.set(row, { rowLayout, identityUri });
        }
        for (const row of mounted.keys()) {
            if (row !== header.row && !layouts.has(row)) removeRow(row);
        }
        const lookupUris = sorter?.uris || visible;
        if (reply && pending?.request === reply.request && reply.revision === revision && Array.isArray(reply.dates)) {
            const returned = new Map(reply.dates.slice(0, maxUris).filter(pair => Array.isArray(pair)));
            const checkedAt = performance.now();
            for (const uri of pending.uris) {
                if (!lookupUris.has(uri)) continue;
                const timestamp = returned.get(uri);
                const date = typeof timestamp === "string" && new Date(timestamp);
                const display = date && Number.isFinite(date.getTime()) ? {
                    label: date.toLocaleDateString(),
                    tooltip: date.toLocaleString(),
                    utc: timestamp
                } : {};
                const previous = dates.get(uri);
                if (!previous || previous.utc !== display.utc) ++dateGeneration;
                dates.set(uri, { ...display, checkedAt });
            }
            pending = undefined;
        }
        const missingDates = requestDates(lookupUris);
        updateNativeSortIndicators(header.row, sorter?.apply(dates, dateGeneration));
        paint(header.row, header.layout, "", true, sortable, missingDates);
        for (const [row, { rowLayout, identityUri }] of layouts) {
            paint(row, rowLayout, visible.has(identityUri) ? identityUri : "");
        }
    }

    function run(reply) {
        if (!enabled) return;
        observer.disconnect();
        try {
            reconcile(reply);
        } finally {
            // Reconnect after our own DOM writes so they do not schedule a second pass.
            observer.observe(document, {
                subtree: true, childList: true, characterData: true, attributes: true,
                attributeFilter: ["role", "class", "style", "hidden", "href", "data-testid",
                    "data-test-uri", "data-track-uri", "aria-colcount"]
            });
        }
    }

    function schedule() {
        if (!enabled || scheduled) return;
        scheduled = true;
        requestAnimationFrame(() => {
            scheduled = false;
            run();
        });
    }

    const observer = new MutationObserver(records => {
        const touchesPlaylist = node => node.nodeType === 1 &&
            (node.matches(pageSelector) || node.querySelector(pageSelector));
        if (records.some(record => record.target.parentElement?.closest(pageSelector) ||
            record.target.matches?.(pageSelector) ||
            [...record.addedNodes, ...record.removedNodes].some(touchesPlaylist))) schedule();
    });
    const resizeObserver = typeof ResizeObserver === "function" ? new ResizeObserver(schedule) : null;

    sorter = window.__floggfyCreateMarkedSort?.({ own, currentFiber, changed: schedule });
    // Restore before Spotify handles another column's native sort event.
    document.addEventListener("click", event => {
        const header = event.target?.closest?.('[role="columnheader"]');
        if (header && !header.hasAttribute("data-floggfy-saved-column") &&
            context?.grid === header.closest('[role="grid"]')) {
            restoreNativeSortIndicators();
            sorter?.reset();
        }
    }, true);

    window.__floggfyMarkedPoll = (nextEnabled, nextRevision) => {
        if (!nextEnabled) {
            if (!enabled) {
                if (releasePending) releasePending = sorter?.setContext(null, null) === false;
                return;
            }
            enabled = false;
            observer.disconnect();
            document.removeEventListener("visibilitychange", schedule);
            resizeObserver?.disconnect();
            observedGrid = undefined;
            context = undefined;
            releasePending = !resetLayout();
            resetQuery();
            return;
        }
        if (!enabled) document.addEventListener("visibilitychange", schedule);
        if (!enabled || revision !== nextRevision) {
            revision = nextRevision;
            resetQuery();
        }
        releasePending = false;
        enabled = true;
        schedule();
    };

    window.__floggfyMarkedReply = reply => {
        if (reply && pending?.request === reply.request && reply.revision === revision) run(reply);
    };
})();
