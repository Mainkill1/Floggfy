(() => {
    "use strict";

    if (typeof window.__floggfyCreateMarkedSort === "function") return;

    const trackUri = /^spotify:track:[A-Za-z0-9]{22}$/;
    const unsupported = "Saved sorting is unavailable for this playlist.";
    const incomplete = "Could not load the full playlist for Saved sorting.";
    const delay = () => new Promise(resolve => setTimeout(resolve, 25));

    window.__floggfyCreateMarkedSort = ({own, currentFiber, changed}) => {
        let grid;
        let playlistUri;
        let mode = "none";
        let loading = false;
        let error = "";
        let session;
        let loaded;

        function provider(fiber) {
            let source;
            let ref;
            let dispatch;
            let hook = own(fiber, "memoizedState");
            for (let count = 0; hook && count < 64; ++count, hook = own(hook, "next")) {
                const state = own(hook, "memoizedState");
                const candidate = own(state, "current");
                const items = candidate && Object.getOwnPropertyDescriptor(candidate, "items");
                if (items?.writable && Array.isArray(items.value) &&
                    Array.isArray(own(candidate, "validItems")) && typeof own(candidate, "invalid") === "boolean" &&
                    Number.isSafeInteger(own(candidate, "id"))) {
                    if (source) return null;
                    source = candidate;
                    ref = state;
                }
                const update = own(own(hook, "queue"), "dispatch");
                if (typeof state === "number" && typeof update === "function") {
                    if (dispatch) return null;
                    dispatch = update;
                }
            }
            return !hook && source && dispatch ? {source, ref, dispatch} : null;
        }

        function matchingPrefix(cache, source) {
            const valid = own(source, "validItems");
            const last = Math.min(3, valid.length - 1);
            if (last < 0) return own(cache, "nrValidItems") === 0;
            const values = cache.getItems(0, last, {cachedOnly: true});
            return Array.isArray(values) && values.length === last + 1 &&
                values.every((value, index) => value && value === own(valid[index], "value"));
        }

        const selectionError = "Could not clear the playlist selection for Saved sorting.";

        function clearSelection(list) {
            try {
                if (typeof own(list, "clearSelected") === "function") {
                    list.clearSelected();
                    if (typeof own(list, "getSelected") !== "function") return true;
                } else {
                    const selected = list.getSelected();
                    if (!Array.isArray(selected)) return false;
                    if (selected.length === 0) return true;
                    grid.dispatchEvent(new window.KeyboardEvent("keydown", {key: "Escape", bubbles: true}));
                }
                const remaining = list.getSelected();
                return Array.isArray(remaining) && remaining.length === 0;
            } catch {
                return false;
            }
        }

        function nativeView() {
            if (!grid) return null;
            const key = Object.getOwnPropertyNames(grid).find(name => name.startsWith("__reactFiber$"));
            let fiber = key && currentFiber(own(grid, key));
            let view;
            for (let depth = 0; fiber && depth < 96; ++depth, fiber = own(fiber, "return")) {
                if (view) {
                    const owner = provider(fiber);
                    if (owner && matchingPrefix(view.cache, owner.source)) return {...view, ...owner};
                    continue;
                }
                const props = own(fiber, "memoizedProps");
                const cache = own(props, "itemsCache");
                const sort = own(props, "sortState");
                const column = own(sort, "column");
                const order = own(sort, "order");
                const onSort = own(props, "onSort");
                const list = own(own(props, "outerRef"), "current");
                if (own(props, "canFetchAllTracks") !== true ||
                    !["getItem", "getItems", "cacheAll", "invalidateCache"].every(name => typeof own(cache, name) === "function") ||
                    !Number.isSafeInteger(own(cache, "nrValidItems")) || own(cache, "nrValidItems") < 0 ||
                    own(cache, "hasItems") === undefined || typeof onSort !== "function" ||
                    (typeof own(list, "clearSelected") !== "function" && typeof own(list, "getSelected") !== "function") ||
                    typeof own(list, "scrollToIndex") !== "function" ||
                    ((column != null || order != null) && !Number.isSafeInteger(order)) ||
                    (column != null && typeof column !== "string")) continue;
                view = {cache, list, onSort, column, order};
            }
            return null;
        }

        function sameProvider(view, expected) {
            return view && view.ref === expected.ref && view.dispatch === expected.dispatch;
        }

        function sameSort(view, expected) {
            return view && view.column === expected.column && view.order === expected.order;
        }

        function redraw(dispatch) {
            dispatch(value => value + 1);
        }

        function restore(active) {
            if (!active?.owned) return;
            const {owned, original, view} = active;
            const {ref, source, dispatch} = view;
            if (own(ref, "current") !== source || own(source, "items") !== owned) return;
            const current = nativeView();
            if (current && (!sameProvider(current, view) || current.source !== source)) return;
            if (grid?.isConnected !== false && !clearSelection(current?.list || view.list)) return false;
            if (own(ref, "current") !== source || own(source, "items") !== owned) return;
            source.items = original;
            redraw(dispatch);
        }

        function reset(message = "") {
            try {
                if (restore(session) === false) {
                    if (error !== selectionError) {
                        error = selectionError;
                        changed();
                    }
                    return false;
                }
            } catch {
                message = unsupported;
            }
            const notify = mode !== "none" || loading || error !== message;
            session = null;
            mode = "none";
            loading = false;
            error = message;
            if (notify) changed();
            return true;
        }

        function currentFor(active) {
            const view = nativeView();
            if (!sameProvider(view, active.view) || !sameSort(view, active.view)) return null;
            const expected = active.owned || active.original;
            if (!expected) return view;
            return view.source === active.view.source && own(view.source, "invalid") === false &&
                own(view.source, "id") === active.id && own(view.source, "items") === expected ? view : null;
        }

        function refresh() {
            // While onSort is clearing the native column, its own commits are expected.
            if (!session?.view) return;
            try {
                if (!currentFor(session)) reset();
            } catch {
                reset(unsupported);
            }
        }

        async function clearNativeSort(active, view) {
            const initiatingView = view;
            const seenOrders = new Set();
            for (let step = 0; step < 16 && view.column != null && view.order !== 0; ++step) {
                // Native callbacks may cycle multiple orders for the same column.
                if (seenOrders.has(view.order)) return null;
                seenOrders.add(view.order);
                const previous = view;
                view.onSort(view.column);
                for (let attempt = 0; attempt < 40; ++attempt) {
                    await delay();
                    if (session !== active) return null;
                    view = nativeView();
                    if (view && !sameProvider(view, initiatingView)) return null;
                    if (view && !sameSort(view, previous)) break;
                }
                if (!view || sameSort(view, previous) ||
                    (view.column != null && view.column !== previous.column)) return null;
                // Some views require a native sort. Use the starting order once
                // the native callback has completed a full cycle without neutral.
                if (sameSort(view, initiatingView)) return view;
            }
            return view.column == null || view.order === 0 ? view : null;
        }

        function cachedValues(view) {
            const count = own(view.cache, "nrValidItems");
            return count ? view.cache.getItems(0, count - 1, {cachedOnly: true}) : [];
        }

        function complete(view, values) {
            const items = own(view.source, "items");
            const valid = own(view.source, "validItems");
            if (!Array.isArray(values) || values.length < own(view.cache, "nrValidItems") ||
                values.length !== valid.length || items.length !== valid.length) return null;

            const uids = new Set();
            const uris = new Set();
            const rows = [];
            for (let index = 0; index < items.length; ++index) {
                const entry = items[index];
                const value = own(entry, "value");
                const uid = own(value, "uid");
                if (value !== values[index] || entry !== valid[index] ||
                    typeof uid !== "string" || !uid || uids.has(uid)) return null;
                uids.add(uid);
                const uri = own(value, "uri");
                const canonical = typeof uri === "string" && trackUri.test(uri) ? uri : "";
                if (canonical) uris.add(canonical);
                rows.push({entry, uri: canonical, index});
            }
            return {original: items, rows, uris};
        }

        async function load(active) {
            try {
                let view = nativeView();
                if (!view) {
                    reset(unsupported);
                    return;
                }
                // Reuse only a previously completed native fetch whose source,
                // generation and restored order are still owned by this page.
                if (loaded && sameProvider(view, loaded.view) && sameSort(view, loaded.view) &&
                    view.source === loaded.view.source && own(view.source, "invalid") === false &&
                    own(view.source, "id") === loaded.id && own(view.source, "items") === loaded.original &&
                    loaded.original.length === loaded.count && own(view.cache, "nrValidItems") === loaded.count) {
                    const data = complete(view, cachedValues(view));
                    if (data) {
                        active.view = view;
                        active.id = loaded.id;
                        Object.assign(active, data);
                        loading = false;
                        changed();
                        return;
                    }
                }
                loaded = null;
                view = await clearNativeSort(active, view);
                if (session !== active) return;
                if (!view) {
                    reset(unsupported);
                    return;
                }
                active.view = view;
                const result = await view.cache.cacheAll();
                if (session !== active) return;
                // Spotify may commit a replacement source on the following render.
                await new Promise(resolve => {
                    window.requestAnimationFrame(() => window.requestAnimationFrame(resolve));
                });
                if (session !== active) return;
                const current = currentFor(active);
                if (!current) {
                    reset();
                    return;
                }
                // Older Spotify versions fill the cache without returning the items.
                const data = own(current.source, "invalid") === false &&
                    complete(current, result === undefined ? cachedValues(current) : own(result, "items"));
                if (!data) {
                    reset(incomplete);
                    return;
                }
                // The full fetch can replace the source and advance its generation.
                active.view = current;
                active.id = own(current.source, "id");
                Object.assign(active, data);
                loaded = {view: current, id: active.id, original: data.original, count: data.rows.length};
                loading = false;
                changed();
            } catch {
                if (session === active) reset(incomplete);
            }
        }

        return {
            setContext(nextGrid, nextUri) {
                if (grid === nextGrid && playlistUri === nextUri) return true;
                if (!reset()) return false;
                loaded = null;
                grid = nextGrid;
                playlistUri = nextUri;
                return true;
            },
            cycle() {
                if (mode === "ascending") {
                    reset();
                    return;
                }
                if (mode === "descending") {
                    mode = "ascending";
                    changed();
                    return;
                }
                if (!grid || !playlistUri) return;
                session = {};
                mode = "descending";
                loading = true;
                error = "";
                changed();
                void load(session);
            },
            refresh,
            get state() { return {mode, loading, error}; },
            get uris() { return session?.uris || null; },
            apply(dates) {
                refresh();
                const active = session;
                if (!active?.uris) return false;

                const timestamps = new Map();
                for (const uri of active.uris) {
                    if (!dates.has(uri)) return false;
                    timestamps.set(uri, dates.get(uri)?.utc || "");
                }
                if (active.mode === mode &&
                    [...timestamps].every(([uri, date]) => active.timestamps.get(uri) === date)) return true;

                const direction = mode === "ascending" ? 1 : -1;
                const ordered = active.rows.slice().sort((first, second) => {
                    const a = timestamps.get(first.uri) || "";
                    const b = timestamps.get(second.uri) || "";
                    if (a === b) return first.index - second.index;
                    if (!a || !b) return a ? -1 : 1;
                    return (a < b ? -1 : 1) * direction;
                }).map(row => row.entry);

                try {
                    const view = currentFor(active);
                    if (!view) {
                        reset();
                        return false;
                    }
                    if (!clearSelection(view.list)) {
                        if (!active.owned) {
                            reset(selectionError);
                        } else {
                            const notify = error !== selectionError || mode !== active.mode;
                            mode = active.mode;
                            error = selectionError;
                            if (notify) changed();
                        }
                        return false;
                    }
                    const expected = active.owned || active.original;
                    if (own(view.ref, "current") !== view.source || own(view.source, "items") !== expected) {
                        reset();
                        return false;
                    }
                    view.source.items = ordered;
                    active.owned = ordered;
                    const modeChanged = active.mode !== mode;
                    active.mode = mode;
                    active.timestamps = timestamps;
                    redraw(view.dispatch);
                    if (modeChanged) view.list.scrollToIndex(0);
                    if (error) {
                        error = "";
                        changed();
                    }
                    return true;
                } catch {
                    reset(unsupported);
                    return false;
                }
            },
            reset: () => reset()
        };
    };
})();
