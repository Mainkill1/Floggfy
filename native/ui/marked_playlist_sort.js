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
        let nativeRollback;
        const ownedPlayPages = new WeakSet();

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

        function ownsItems(view, items) {
            return own(view.ref, "current") === view.source && own(view.source, "items") === items;
        }

        function restore(active) {
            if (!active?.owned) return;
            const {owned, original, view} = active;
            if (!ownsItems(view, owned)) return;
            let current = nativeView();
            if (current && (!sameProvider(current, view) || current.source !== view.source)) return;
            if (grid?.isConnected !== false && !clearSelection(current?.list || view.list)) return false;
            current = nativeView();
            if (current && (!sameProvider(current, view) || current.source !== view.source)) return;
            if (!ownsItems(view, owned)) return;
            const originalOrder = active.playItems && own(view.source, "invalid") === false &&
                own(view.source, "id") === active.id ? original : null;
            view.source.items = original;
            redraw(view.dispatch);
            return originalOrder;
        }

        function playbackItems(entries) {
            const items = [];
            for (const entry of entries) {
                const value = own(entry, "value");
                // Keep unavailable rows visible, but follow Spotify's playback eligibility.
                if (own(value, "isPlayable") === false) continue;
                const uid = own(value, "uid"), uri = own(value, "uri");
                if (!uid || typeof uri !== "string" || !uri.startsWith("spotify:")) return null;
                items.push({uid, uri});
            }
            return items;
        }

        function updatePlayback(items, restore = false) {
            window.__floggfyUpdateMarkedPlayback?.(window.__floggfyMarkedCurrentPlayer?.(), playlistUri, items, restore);
        }

        function writable(object, key) {
            const field = Object.getOwnPropertyDescriptor(object, key);
            return field ? field.writable === true : Object.isExtensible(object);
        }

        function preparePlayback(data) {
            const active = session;
            const context = own(data, "context"), options = own(data, "options");
            const trace = window.__floggfyMarkedTrace;
            trace?.("prepare", {active: !!active?.playItems, mode, origin: own(own(data, "origin"), "featureIdentifier"),
                playlist: own(context, "uri"), url: !!own(context, "url"), pages: !!own(context, "pages"),
                shuffle: own(options, "shuffle"), uid: own(own(options, "skipTo"), "uid"),
                index: own(own(options, "skipTo"), "index"), current: !!active && !!currentFor(active)}, true);
            if (!active?.playItems || active.mode !== mode || own(data, "action") !== "play" ||
                own(context, "uri") !== playlistUri ||
                ((own(context, "url") || own(context, "pages")) && !ownedPlayPages.has(own(context, "pages"))) ||
                !options || own(options, "shuffle") ||
                !currentFor(active)) return;

            const skip = own(options, "skipTo");
            const uid = own(skip, "uid");
            const uri = own(skip, "uri");
            let index = 0;
            if (uid != null || uri != null || own(skip, "index") != null) {
                // A URI can occur more than once. Use the clicked occurrence and
                // disregard its index in the old native order.
                index = active.playItems.findIndex(item => item.uid === uid && (!uri || item.uri === uri));
                if (index < 0) {trace?.("prepare-skipped", {reason: "occurrence-not-found"}, true);return;}
            }
            if (!active.playItems.length || !writable(context, "url") || !writable(context, "pages") ||
                !writable(options, "skipTo")) return;
            const item = active.playItems[index];
            // A playlist URI alone asks Spotify to rebuild the order from its
            // playlist service. A URL with a loaded page uses the context player.
            context.url = "context://" + playlistUri;
            context.pages = [{items: active.playItems}];
            ownedPlayPages.add(context.pages);
            // The library player converts numeric indices into 150-track pages.
            // Our supplied page is complete: anchor its exact occurrence by UID.
            options.skipTo = {uid: item.uid, uri: item.uri, pageIndex: 0};
            trace?.("prepared", {playlist: playlistUri, uid: item.uid, uri: item.uri, index,
                rows: active.playItems.length, next_uid: active.playItems[index + 1]?.uid,
                next_uri: active.playItems[index + 1]?.uri}, true);
        }

        function selectionFailed(previousMode) {
            if (error !== selectionError || mode !== previousMode) {
                mode = previousMode;
                error = selectionError;
                changed();
            }
            return false;
        }

        function reset(message = "") {
            let originalOrder;
            try {
                originalOrder = restore(session);
                if (originalOrder === false) return selectionFailed(mode);
            } catch {
                message = unsupported;
            }
            const notify = mode !== "none" || loading || error !== message;
            if (session?.playItems) updatePlayback(originalOrder ? playbackItems(originalOrder) : null, !!originalOrder);
            session = null;
            mode = "none";
            loading = false;
            error = message;
            if (notify) changed();
            return true;
        }

        function currentFor(active, view = nativeView()) {
            if (!sameProvider(view, active.view) || !sameSort(view, active.view)) return null;
            const expected = active.owned || active.original;
            if (!expected) return view;
            return view.source === active.view.source && own(view.source, "invalid") === false &&
                own(view.source, "id") === active.id && own(view.source, "items") === expected ? view : null;
        }

        function refresh() {
            // While onSort is clearing the native column, its own commits are expected.
            if (!session?.view) return true;
            try {
                if (currentFor(session)) return true;
                reset();
            } catch {
                reset(unsupported);
            }
            return false;
        }

        async function advanceNativeSort(change, previous) {
            const available = () => !change.abandoned && grid === change.grid && grid.isConnected !== false;
            if (!available()) return null;
            const current = nativeView();
            if (!sameProvider(current, change.original) || current.source !== previous.source ||
                !sameSort(current, previous)) return null;
            current.onSort(change.original.column);
            for (let attempt = 0; attempt < 40; ++attempt) {
                await delay();
                if (!available()) return null;
                const view = nativeView();
                if (!view) continue;
                if (!sameProvider(view, change.original) ||
                    (view.column != null && view.column !== change.original.column)) return null;
                if (!sameSort(view, previous)) return view;
            }
            return null;
        }

        async function cycleNativeSort(change, view, reached, continueCycle) {
            const seenOrders = new Set();
            for (let step = 0; view && step <= 16 && continueCycle(); ++step) {
                if (reached(view, step)) return view;
                // Native callbacks may cycle multiple orders for the same column.
                const order = view.column == null ? null : view.order;
                if (step === 16 || seenOrders.has(order)) return null;
                seenOrders.add(order);
                view = await (change.commit = advanceNativeSort(change, view));
            }
            return null;
        }

        function clearNativeSort(active, view) {
            if (view.column == null || view.order === 0) return view;
            const change = active.nativeSort = {grid, original: view};
            // Some views always keep a native sort, so accept a full cycle.
            return cycleNativeSort(change, view, (next, step) => next.column == null || next.order === 0 ||
                (step > 0 && sameSort(next, view)), () => session === active);
        }

        function cancel({nativeSort = false} = {}) {
            const change = session?.nativeSort;
            if (nativeSort) {
                if (change) change.abandoned = true;
                if (nativeRollback) nativeRollback.abandoned = true;
            }
            if (!reset()) return false;
            if (change?.commit && !change.abandoned) {
                // The native callback can still be committing when Saved is cancelled.
                nativeRollback = change;
                change.rollback = change.commit.then(view => cycleNativeSort(change, view,
                    next => sameSort(next, change.original), () => !change.abandoned))
                    .catch(() => { /* Spotify no longer exposes the previous sort. */ })
                    .finally(() => { if (nativeRollback === change) nativeRollback = null; });
            }
            return true;
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
                rows.push({entry, uri: canonical});
            }
            return {original: items, rows, uris};
        }

        async function load(active) {
            try {
                if (nativeRollback) await nativeRollback.rollback;
                if (session !== active) return;
                let view = nativeView();
                if (!view) return reset(unsupported);
                // Reuse only a previously completed native fetch whose source,
                // generation and restored order are still owned by this page.
                let data;
                if (loaded && currentFor(loaded, view) &&
                    loaded.original.length === loaded.count && own(view.cache, "nrValidItems") === loaded.count) {
                    data = complete(view, cachedValues(view));
                }
                if (!data) {
                    const firstLoad = !loaded;
                    loaded = null;
                    view = await clearNativeSort(active, view);
                    if (session !== active) return;
                    if (!view) return reset(unsupported);
                    active.view = view;
                    // A fully cached page already has every row needed for sorting.
                    // Keep the existing refetch on later source/generation changes.
                    if (firstLoad && own(view.source, "invalid") === false)
                        data = complete(view, cachedValues(view));
                    if (!data) {
                        const result = await view.cache.cacheAll();
                        if (session !== active) return;
                        // Spotify may commit a replacement source on the following render.
                        await new Promise(resolve => {
                            window.requestAnimationFrame(() => window.requestAnimationFrame(resolve));
                        });
                        if (session !== active) return;
                        view = currentFor(active);
                        if (!view) return reset();
                        // Older Spotify versions fill the cache without returning the items.
                        data = own(view.source, "invalid") === false &&
                            complete(view, result === undefined ? cachedValues(view) : own(result, "items"));
                    }
                    if (!data) return reset(incomplete);
                    // The full fetch can replace the source and advance its generation.
                    loaded = {view, id: own(view.source, "id"), original: data.original, count: data.rows.length};
                }
                Object.assign(active, data, {view, id: loaded.id});
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
                if (nativeRollback) nativeRollback.abandoned = true;
                loaded = null;
                grid = nextGrid;
                playlistUri = nextUri;
                return true;
            },
            cycle() {
                if (mode === "ascending") {
                    cancel();
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
            preparePlayback,
            syncPlayback() {
                if (session?.playItems && session.mode === mode) {
                    window.__floggfyUpdateMarkedPlayback?.bindPlay?.(window.__floggfyMarkedCurrentPlayer?.());
                    updatePlayback(session.playItems);
                }
            },
            retryPlaybackRestore() {
                window.__floggfyUpdateMarkedPlayback?.retryRestore?.(window.__floggfyMarkedCurrentPlayer?.());
            },
            get state() { return {mode, loading, error}; },
            get uris() { return session?.uris || null; },
            apply(dates) {
                if (!refresh()) return false;
                const active = session;
                if (!active?.uris) return false;

                // Saving changes the displayed date, not the user's chosen
                // playlist order or the queue already playing from that order.
                if (active.owned && active.mode === mode) return true;
                const timestamps = new Map();
                for (const uri of active.uris) {
                    if (!dates.has(uri)) return false;
                    const date = dates.get(uri)?.utc || "";
                    timestamps.set(uri, date);
                }

                const direction = mode === "ascending" ? 1 : -1;
                const ordered = active.rows.slice().sort((first, second) => {
                    const a = timestamps.get(first.uri) || "";
                    const b = timestamps.get(second.uri) || "";
                    if (a === b) return 0;
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
                        if (active.owned) return selectionFailed(active.mode);
                        reset(selectionError);
                        return false;
                    }
                    if (!currentFor(active)) {
                        reset();
                        return false;
                    }
                    view.source.items = ordered;
                    active.nativeSort = null;
                    active.owned = ordered;
                    active.playItems = playbackItems(ordered);
                    const modeChanged = active.mode !== mode;
                    active.mode = mode;
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
            reset: cancel
        };
    };
})();

// The context player's UpdateContext command takes protobuf context pages
// (tracks). The UI play command takes renderer context pages (items).
(() => {
    "use strict";
    if (typeof window.__floggfyUpdateMarkedPlayback === "function") return;

    let order;
    let identities;
    let lastUpdate;
    let savedTarget;
    let pendingRestore;
    const playBindings = new WeakMap();
    let queueTrace;
    let aliasSession;
    const queueAliases = new Map();
    const traces = new Map();
    const trace = (stage, fields, repeat = false) => {
        if (window.__floggfyConfig?.debug !== true) return;
        try {
            const text = "stage=" + stage + Object.entries(fields).map(([key, value]) =>
                " " + key + "=" + String(value ?? "missing").replace(/[^A-Za-z0-9:._/-]/g, "_").slice(0, 80)).join("");
            if (!repeat && traces.get(stage) === text) return;
            traces.set(stage, text);
            console.info("FLOGGFY_SAVED_V1:" + text);
        } catch { /* Diagnostics cannot affect playback. */ }
    };
    window.__floggfyMarkedTrace = trace;

    function traceQueue(player, state, items) {
        if (window.__floggfyConfig?.debug !== true || !state?.item) return;
        const snapshot = player?._queue?.getInternalQueue?.();
        if (!snapshot || queueTrace?.items === items && queueTrace.uid === state.item.uid &&
            queueTrace.revision === snapshot.queueRevision) return;
        queueTrace = {items, uid: state.item.uid, revision: snapshot.queueRevision};
        const index = items.findIndex(row => row.uid === playlistUid(state.item));
        const next = index < 0 ? undefined : items[index + 1];
        const raw = snapshot.nextTracks?.find(row => playlistUid(row.contextTrack) === next?.uid);
        trace("queue-evidence", {uid: state.item.uid, revision: String(snapshot.queueRevision),
            expected_uid: next?.uid, expected_uri: next?.uri, found: !!raw,
            removed: raw?.removed?.join("_"), blocked: raw?.blocked?.join("_"),
            position: Math.round(state.positionAsOfTimestamp || 0)}, true);
        for (const [index, row] of (snapshot.nextTracks || []).slice(0, 3).entries())
            trace("queue-row-" + index, {uid: row.contextTrack?.uid, uri: row.contextTrack?.uri,
                provider: row.provider, removed: row.removed?.join("_"), blocked: row.blocked?.join("_")}, true);
    }

    function release() {
        order = undefined;
        identities = undefined;
        lastUpdate = undefined;
    }

    function readState(player) {
        try {
            if (typeof player?.getState === "function" && typeof player.updateContext === "function") {
                return player.getState();
            }
        } catch {
            // Keep the last verified identity until the player is readable again.
        }
    }

    function activePlaylist(state, uri) {
        return state?.context?.uri === uri && typeof state.sessionId === "string" && !!state.sessionId;
    }

    function playlistUid(track) {
        const mapped = queueAliases.get(track?.uid);
        return mapped?.uri === track?.uri ? mapped.uid : track?.uid;
    }

    function confirmQueue(snapshot, wanted, previousIds) {
        if (!snapshot || snapshot.nextTracks.length < wanted.length) return false;
        if (!wanted.every((row, index) => {
            const actual = snapshot.nextTracks[index].contextTrack;
            return actual?.uri === row.uri && !!actual.uid && (row.queueUid ? actual.uid === row.queueUid :
                previousIds && !previousIds.has(actual.uid));
        })) return false;
        wanted.forEach((row, index) => queueAliases.set(snapshot.nextTracks[index].contextTrack.uid, row));
        return true;
    }

    async function replacePending(player, state, items, attempt) {
        const queue = player?._queue;
        const client = player?._contextPlayer || queue?._client;
        if (typeof queue?.getInternalQueue !== "function" || typeof client?.setQueue !== "function") {
            trace("queue", {reason: "unsupported"});return;
        }
        const current = queue.getInternalQueue();
        const revision = current?.queueRevision;
        const live = readState(player);
        const index = items.findIndex(row => row.uid === playlistUid(state.item) && row.uri === state.item.uri);
        if (live?.sessionId !== state.sessionId || live?.item?.uid !== state.item.uid ||
            live.shuffle !== false || live.smartShuffle === true) throw Error("Playback changed before SetQueue");
        if (index < 0 || current?.track?.contextTrack?.uid !== state.item.uid ||
            current.track.contextTrack.uri !== state.item.uri || current.queueRevision == null ||
            !Array.isArray(current.nextTracks) || !Array.isArray(current.prevTracks) ||
            current.nextTracks.some(track => !["context", "queue"].includes(track.provider))) {
            trace("queue", {reason: "unverified-queue"});
            throw Object.assign(Error("Queue snapshot has not caught up"), {queueChanged: true});
        }
        const manual = current.nextTracks.filter(track => track.provider === "queue");
        const available = new Map(current.nextTracks.filter(track => track.provider === "context")
            .map(track => [playlistUid(track.contextTrack), track]));
        // SetQueue edits a loaded queue. A playlist UID outside it is silently
        // ignored by Spotify; new entries must omit UID and use its assigned ID.
        // Refill a bounded upcoming window at each song start.
        const upcoming = items.slice(index + 1, index + 51);
        const next = upcoming.map(row => {
            const existing = available.get(row.uid);
            return existing?.contextTrack.uri === row.uri ? existing :
                {provider: "context", contextTrack: {uri: row.uri, metadata: {}}};
        });
        const wanted = [...manual.map(row => ({...row.contextTrack, queueUid: row.contextTrack.uid})),
            ...upcoming.map((row, index) => ({...row, queueUid: next[index].contextTrack.uid}))];
        if (confirmQueue(current, wanted)) {
            trace("queue-result", {reason: "verified", uid: state.item.uid, rows: wanted.length});
            return;
        }
        trace("queue-request", {uid: state.item.uid, rows: next.length, manual: manual.length,
            next_uid: next[0]?.contextTrack.uid, next_uri: next[0]?.contextTrack.uri}, true);
        const previousIds = new Set(current.nextTracks.map(row => row.contextTrack?.uid));
        const result = await client.setQueue({nextTracks: [...manual, ...next],
            prevTracks: current.prevTracks, queueRevision: BigInt(revision.toString())});
        if (result?.error !== 0) {
            trace("queue-result", {reason: "rejected", code: result?.error, detail: result?.reasons}, true);
            throw Error("SetQueue rejected");
        }
        for (let poll = 0; poll < 30; ++poll) {
            if (lastUpdate !== attempt || readState(player)?.sessionId !== state.sessionId)
                throw Error("Queue request superseded");
            const snapshot = queue.getInternalQueue();
            if (snapshot.queueRevision !== revision && confirmQueue(snapshot, wanted, previousIds)) {
                trace("queue-result", {reason: "verified", uid: state.item.uid, rows: wanted.length,
                    next_uri: upcoming[0]?.uri}, true);
                return;
            }
            await new Promise(resolve => setTimeout(resolve, 50));
        }
        trace("queue-result", {reason: "order-mismatch", uid: state.item.uid,
            expected: upcoming[0]?.uri, actual: queue.getInternalQueue()?.nextTracks?.[manual.length]?.contextTrack?.uri}, true);
        throw Error("Spotify did not retain the requested order");
    }

    function updateOrder(player, playlistUri, state, orderedItems, completed) {
        const {sessionId: session, item} = state;
        if (aliasSession !== session) {
            aliasSession = session;
            queueAliases.clear();
        }
        if (state.shuffle !== false || state.smartShuffle === true) {
            // Native shuffle may replace the loaded context for this session.
            lastUpdate = undefined;
            return;
        }
        if (item?.provider !== "context") return;

        if (order !== orderedItems) {
            order = orderedItems;
            identities = undefined;
            lastUpdate = undefined;
            const byUid = new Map();
            for (const row of order) {
                if (typeof row?.uid !== "string" || !row.uid || typeof row.uri !== "string" ||
                    !row.uri || byUid.has(row.uid)) return;
                byUid.set(row.uid, row.uri);
            }
            identities = byUid;
        }
        const uid = playlistUid(item);
        if (!identities?.has(uid) || identities.get(uid) !== item.uri) return;
        if (lastUpdate?.player === player && lastUpdate.session === session &&
            lastUpdate.uid === item.uid && lastUpdate.uri === playlistUri && Date.now() < lastUpdate.retryAt) return true;

        // Use the active session and let Spotify retain its manual queue and transport state.
        const context = {uri: playlistUri, metadata: state.context.metadata, isLoading: false,
            pages: [{tracks: order}]};
        const attempt = {player, session, uid: item.uid, uri: playlistUri, retryAt: Infinity};
        const failed = error => {
            attempt.retryAt = Date.now() + (error?.queueChanged ? 100 : 3000);
            trace("context-result", {reason: "rejected", detail: error?.message}, true);
        };
        lastUpdate = attempt;
        if (!completed) savedTarget = attempt;
        try {
            trace("context-request", {playlist: playlistUri, session, uid: item.uid, rows: order.length}, true);
            const contextClient = player?._contextPlayer || player?._queue?._client;
            // Newer clients rebuild playlist contexts internally. Edit their real
            // pending queue; retain UpdateContext for older player interfaces.
            const result = typeof contextClient?.setQueue === "function" &&
                typeof player?._queue?.getInternalQueue === "function" ?
                replacePending(player, state, order, attempt) : player.updateContext(session, context);
            Promise.resolve(result).then(() => trace("context-result", {reason: "resolved"}, true), () => {});
            if (completed) {
                Promise.resolve(result).then(completed, failed);
            } else if (result && typeof result.catch === "function") {
                result.catch(failed);
            }
        } catch {
            failed();
        }
        return true;
    }

    function discardRestore(restore) {
        if (!restore || pendingRestore !== restore) return;
        pendingRestore = undefined;
        if (order === restore.items) release();
    }

    function restoreWhenReady(player, state) {
        if (!pendingRestore) return;
        const restore = pendingRestore;
        const session = state?.sessionId;
        const uri = state?.context?.uri;
        if ((player && restore.player !== player) || (session && session !== restore.session) ||
            (uri && uri !== restore.uri)) {
            discardRestore(restore);
            return;
        }
        if (restore.player !== player || !activePlaylist(state, restore.uri)) return;
        // An added song or active shuffle leaves no safe context occurrence to anchor.
        if (state.item?.provider !== "context" || state.shuffle !== false || state.smartShuffle === true) return;
        const completed = () => discardRestore(restore);
        if (!updateOrder(player, restore.uri, state, restore.items, completed)) completed();
    }

    const update = (player, playlistUri, orderedItems, restore = false) => {
        if (!orderedItems) {
            savedTarget = undefined;
            release();
            return;
        }
        try {
            if (!Array.isArray(orderedItems) || !/^spotify:playlist:[A-Za-z0-9]{22}$/.test(playlistUri)) return;
            if (!restore && pendingRestore?.uri === playlistUri) {
                // The renewed sort may be cleared again before the player is readable.
                const {player, session, uri} = pendingRestore;
                savedTarget = {player, session, uri};
                discardRestore(pendingRestore);
            }
            const state = readState(player);
            try { traceQueue(player, state, orderedItems); } catch { /* Diagnostics cannot block queue repair. */ }
            trace("state", {playlist: state?.context?.uri, session: state?.sessionId,
                uri: state?.item?.uri, uid: state?.item?.uid, provider: state?.item?.provider,
                shuffle: state?.shuffle, smart: state?.smartShuffle,
                next_uri: state?.nextItems?.[0]?.uri, next_uid: state?.nextItems?.[0]?.uid,
                next_provider: state?.nextItems?.[0]?.provider,
                get_state: typeof player?.getState, update_context: typeof player?.updateContext});
            if (restore) {
                let target = savedTarget?.uri === playlistUri ? savedTarget : null;
                savedTarget = undefined;
                if (!target && activePlaylist(state, playlistUri)) {
                    target = {player, session: state.sessionId, uri: playlistUri};
                }
                if (!target) return;
                release();
                pendingRestore = {player: target.player, session: target.session, uri: target.uri, items: orderedItems};
                restoreWhenReady(player, state);
            } else if (activePlaylist(state, playlistUri)) {
                updateOrder(player, playlistUri, state, orderedItems);
            }
        } catch {
            // An older player may lack a readable active context.
        }
    };
    update.retryRestore = player => {
        const restore = pendingRestore;
        if (!restore) return;
        try {
            const state = restore.player === player ? readState(player) : undefined;
            restoreWhenReady(player, state);
        } catch {
            // A temporarily unreadable state cannot establish a session change.
        }
    };
    update.bindPlay = player => {
        if (!player || typeof player.play !== "function") {trace("bind", {reason: "play-unavailable"});return;}
        if (playBindings.get(player) === player.play) return;
        const field = Object.getOwnPropertyDescriptor(player, "play");
        if (field ? !("value" in field) || !field.writable : !Object.isExtensible(player)) {
            trace("bind", {reason: "play-readonly"});return;
        }
        const original = player.play;
        function play(...args) {
            trace("native-play", {args: args.length, receiver: this === player,
                playlist: args[0]?.uri, origin: args[1]?.featureIdentifier, uid: args[2]?.skipTo?.uid}, true);
            const request = {action: "play", context: args[0], origin: args[1], options: args[2] || {}};
            const pages = request.context?.pages;
            try {
                if (this === player) window.__floggfyMarkedPlay?.(request);
                if (request.context?.pages !== pages)
                    args[2] = request.options;
            } catch { /* Preparation cannot change native return/exception behavior. */ }
            return Reflect.apply(original, this, args);
        }
        try {
            Object.defineProperty(player, "play", field ? {...field, value: play} :
                {value: play, writable: true, configurable: true});
            playBindings.set(player, play);
            trace("bind", {reason: "installed"}, true);
        } catch {trace("bind", {reason: "install-failed"});}
    };
    window.__floggfyUpdateMarkedPlayback = update;
})();
