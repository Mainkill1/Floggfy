"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const test = require("node:test");
const vm = require("node:vm");
const sourcePath = require("node:path").join(__dirname, "../native/ui/marked_playlist_sort.js");
const uri = index => "spotify:track:" + String(index).padStart(22, "0");
const tick = () => new Promise(resolve => setImmediate(resolve));

function fixture({count = 265, missingUid = false, duplicateUid = false, incomplete = false, replaceOnLoad = false, legacyCacheAll = false, delay, native = false, now = Date.now} = {}) {
    const entries = Array.from({length: count}, (_, index) => ({
        index,
        value: {uid: String(index), uri: index === 4 ? uri(3) : uri(index)}
    }));
    entries[1].value.uri = "spotify:episode:" + "7".repeat(22);
    if (missingUid) delete entries.at(-1).value.uid;
    if (duplicateUid) entries.at(-1).value.uid = entries[0].value.uid;
    const source = {items: entries, validItems: entries.slice(0, 3), invalid: false, id: 1, fetchedId: null};
    const sourceRef = {current: source};
    let renders = 0;
    let clears = 0;
    let scrolls = 0;
    const cache = {
        nrValidItems: count,
        hasItems: true,
        getItem: index => sourceRef.current.validItems[index]?.value,
        getItems: (first, last, options) => {
            assert.equal(options.cachedOnly, true);
            return sourceRef.current.validItems.slice(first, last + 1).map(entry => entry.value);
        },
        invalidateCache: () => assert.fail("local sorting must not invalidate the native cache"),
        async cacheAll() {
            if (delay) await delay;
            if (replaceOnLoad) sourceRef.current = {...sourceRef.current};
            const current = sourceRef.current;
            current.id++;
            current.validItems = entries.slice(0, incomplete ? -1 : undefined);
            if (legacyCacheAll) return;
            return {cacheStatus: "cached", items: current.validItems.map(entry => entry.value)};
        }
    };
    const numberHook = {memoizedState: 0, queue: {dispatch(update) {
        renders++;
        numberHook.memoizedState = update(numberHook.memoizedState);
        sourceRef.current.validItems = sourceRef.current.items.filter(entry => entry?.value);
    }}};
    const provider = {memoizedState: {memoizedState: sourceRef, next: numberHook}};
    const props = {
        itemsCache: cache,
        canFetchAllTracks: true,
        sortState: {column: native ? "TITLE_AND_ARTIST" : null, order: 1},
        onSort(column) {
            assert.equal(column, "TITLE_AND_ARTIST");
            const order = props.sortState.column === column ? props.sortState.order : 0;
            setTimeout(() => {
                props.sortState = order === 4 ? {column: null, order: 1} : {column, order: order + 1};
            }, 0);
        },
        outerRef: {current: {
            clearSelected() { clears++; },
            scrollToIndex(index) { assert.equal(index, 0); scrolls++; }
        }}
    };
    const grid = {"__reactFiber$test": {return: {memoizedProps: props, return: provider}}};
    const window = {setTimeout, clearTimeout, requestAnimationFrame: queueMicrotask};
    vm.runInNewContext(fs.readFileSync(sourcePath, "utf8"), {window, setTimeout, clearTimeout, Date: {now}});
    const controller = window.__floggfyCreateMarkedSort({
        own: (object, key) => object && Object.getOwnPropertyDescriptor(object, key)?.value,
        currentFiber: fiber => fiber,
        changed() {}
    });
    controller.setContext(grid, "spotify:playlist:fixture");
    const dates = new Map(entries.filter(entry => entry.index !== 1).map(entry => [entry.value.uri, {}]));
    dates.set(uri(0), {utc: "2026-01-01T00:00:00Z"});
    dates.set(uri(3), {utc: "2026-01-02T00:00:00Z"});
    dates.set(uri(count - 1), {utc: "2026-01-03T00:00:00Z"});
    return {controller, entries, source, sourceRef, cache, props, provider, dates, grid, window,
        counts: () => [renders, clears, scrolls]};
}

test("sorts the complete playlist, preserves duplicate-track entries and restores native order", async () => {
    const {controller, entries, source, dates, counts} = fixture();
    controller.cycle();
    await tick();
    assert.equal(controller.uris.size, 263);
    const lastDate = dates.get(uri(264));
    dates.delete(uri(264));
    assert.equal(controller.apply(dates), false);
    assert.equal(source.items, entries);
    dates.set(uri(264), lastDate);
    assert.equal(controller.apply(dates), true);
    assert.deepEqual(Array.from(source.items.slice(0, 7), entry => entry.index), [264, 3, 4, 0, 1, 2, 5]);
    assert.equal(source.items[0], entries[264]);
    assert.deepEqual(counts(), [1, 1, 1]);
    assert.equal(controller.apply(dates), true);
    assert.deepEqual(counts(), [1, 1, 1]);
    controller.cycle();
    assert.equal(controller.apply(dates), true);
    assert.deepEqual(Array.from(source.items.slice(0, 7), entry => entry.index), [0, 3, 4, 264, 1, 2, 5]);
    controller.cycle();
    assert.equal(controller.state.mode, "none");
    assert.equal(source.items, entries);
    assert.deepEqual(counts(), [3, 3, 2]);
});

test("uses Spotify's already complete cache without another full fetch", async () => {
    const {controller, entries, source, cache, dates} = fixture();
    source.validItems = entries.slice();
    cache.cacheAll = () => assert.fail("complete native playlist must not be fetched again");
    controller.cycle();
    await tick();
    assert.equal(controller.state.loading, false);
    assert.equal(controller.uris.size, 263);
    assert.equal(controller.apply(dates), true);
    assert.equal(source.items[0], entries[264]);
    controller.cycle();controller.cycle();
    assert.equal(source.items, entries);
});

test("new save dates retain the chosen order until the user changes sort direction", async () => {
    const {controller, dates, source} = fixture();
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates, 1), true);
    const originalGet = dates.get.bind(dates);
    let reads = 0;
    dates.get = key => { ++reads; return originalGet(key); };
    assert.equal(controller.apply(dates, 1), true);
    assert.equal(reads, 0);
    dates.set(uri(3), {utc: "2027-01-01T00:00:00Z"});
    assert.equal(controller.apply(dates, 2), true);
    assert.equal(source.items[0].index, 264, "saving a song must not move it during playback");
    controller.cycle();
    assert.equal(controller.apply(dates, 2), true);
    controller.cycle();controller.cycle();
    await tick();
    assert.equal(controller.apply(dates, 2), true);
    assert.equal(source.items[0].index, 3);
    source.items = source.items.slice();
    assert.equal(controller.apply(dates, 2), false);
});

test("clears a committed native sort through its existing callback", async () => {
    const {controller, props, dates} = fixture({native: true});
    controller.cycle();
    for (let i = 0; i < 100 && controller.state.loading; ++i) {
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    assert.equal(props.sortState.column, null);
    assert.equal(controller.apply(dates), true);
});

test("quickly cancelling Saved restores the native sort that was being cleared", async () => {
    const {controller, props} = fixture({native: true});
    const original = props.sortState;
    const onSort = props.onSort;
    let finish;
    const restored = new Promise(resolve => { finish = resolve; });
    props.onSort = column => {
        onSort(column);
        if (props.sortState.column === null) setTimeout(finish, 0);
    };
    controller.cycle();
    controller.cycle();
    controller.cycle();
    await Promise.race([restored, new Promise(resolve => setTimeout(resolve, 250))]);
    assert.equal(controller.state.mode, "none");
    assert.deepEqual(props.sortState, original);
});

test("a native column change stops an in-progress Saved cancellation", async () => {
    const {controller, props} = fixture({native: true});
    let sorts = 0;
    const onSort = props.onSort;
    props.onSort = column => {
        if (++sorts === 1) onSort(column);
        else props.sortState = {column: "ALBUM", order: 2};
    };
    controller.cycle();
    controller.cycle();
    controller.cycle();
    await new Promise(resolve => setTimeout(resolve, 110));
    assert.equal(sorts, 2);
    assert.deepEqual(props.sortState, {column: "ALBUM", order: 2});
});

test("a native header click owns the order while Saved cancellation is pending", async () => {
    const {controller, props} = fixture({native: true});
    controller.cycle();
    controller.cycle();
    controller.cycle();
    controller.reset({nativeSort: true});
    props.onSort("TITLE_AND_ARTIST");
    await new Promise(resolve => setTimeout(resolve, 110));
    assert.equal(controller.state.mode, "none");
    assert.deepEqual(props.sortState, {column: "TITLE_AND_ARTIST", order: 2});
});

test("reuses a complete playlist until Spotify changes its source or generation", async () => {
    const {controller, cache, dates, sourceRef} = fixture();
    const load = cache.cacheAll;
    let loads = 0;
    cache.cacheAll = async () => { loads++; return load(); };
    const sort = async () => {
        controller.cycle();
        await tick();
        assert.equal(controller.apply(dates), true);
        controller.reset();
    };
    await sort();
    await sort();
    assert.equal(loads, 1, "no repeated full fetch for an unchanged playlist");
    sourceRef.current.id++;
    await sort();
    assert.equal(loads, 2, "native invalidation requires a fresh full fetch");
    sourceRef.current = {...sourceRef.current};
    await sort();
    assert.equal(loads, 3, "replacement source requires a fresh full fetch");
    controller.setContext(null,null);
    assert.equal(controller.uris,null);
});

test("rejects missing or duplicate UIDs and incomplete caches", async () => {
    for (const options of [{missingUid: true}, {duplicateUid: true}, {incomplete: true}]) {
        const {controller, dates, source, entries} = fixture(options);
        controller.cycle();
        await tick();
        assert.equal(controller.state.loading, false);
        assert.ok(controller.state.error);
        assert.equal(controller.uris, null);
        assert.equal(controller.apply(dates), false);
        assert.equal(source.items, entries);
    }
});

test("cancels a pending cache load when the third click clears the sort", async () => {
    let finish;
    const delay = new Promise(resolve => { finish = resolve; });
    const {controller, dates, source, entries} = fixture({delay});
    controller.cycle();
    await tick();
    controller.cycle();
    assert.equal(controller.state.mode, "ascending");
    controller.cycle();
    finish();
    await tick();
    assert.equal(controller.state.mode, "none");
    assert.equal(controller.uris, null);
    assert.equal(controller.apply(dates), false);
    assert.equal(source.items, entries);
});

test("refresh restores owned rows on a native sort switch and avoids replaced source arrays", async () => {
    const {controller, dates, source, entries, props} = fixture();
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates), true);
    props.sortState = {column: "ALBUM", order: 1};
    controller.refresh();
    assert.equal(controller.state.mode, "none");
    assert.equal(source.items, entries);

    props.sortState = {column: null, order: 0};
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates), true);
    const replacement = entries.slice().reverse();
    source.items = replacement;
    controller.reset();
    assert.equal(source.items, replacement);
});

test("Saved relinquishes the rows if a native sort changes while clearing selection", async () => {
    const {controller, dates, source, entries, props} = fixture();
    controller.cycle();
    await tick();
    props.outerRef.current.clearSelected = () => { props.sortState = {column: "ALBUM", order: 1}; };
    assert.equal(controller.apply(dates), false);
    assert.equal(controller.state.mode, "none");
    assert.equal(source.items, entries);
});

test("an external sort blocks a Saved heartbeat when clearing selection must be retried", async () => {
    const {controller, dates, source, entries, props, grid, window} = fixture();
    const selected = [];
    legacySelection(props, grid, window, selected, false);
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates, 1), true);
    const sorted = source.items;

    selected.push(0);
    props.sortState = {column: "ALBUM", order: 1};
    assert.equal(controller.apply(dates, 1), false);
    assert.equal(source.items, sorted);
    selected.length = 0;
    assert.equal(controller.apply(dates, 1), false);
    assert.equal(controller.state.mode, "none");
    assert.equal(source.items, entries);
});

test("keeps the pending direction when Spotify returns a fresh cache object", async () => {
    let finish;
    const delay = new Promise(resolve => { finish = resolve; });
    const {controller, dates, source, props} = fixture({delay});
    controller.cycle();
    await tick();
    controller.cycle();
    props.itemsCache = {...props.itemsCache};
    finish();
    await tick();
    assert.equal(controller.apply(dates), true);
    assert.deepEqual(Array.from(source.items.slice(0, 4), entry => entry.index), [0, 3, 4, 264]);
});

test("restores the owned source when the playlist context changes", async () => {
    const {controller, dates, source, entries} = fixture();
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates), true);
    controller.setContext(null, null);
    assert.equal(controller.uris, null);
    assert.equal(source.items, entries);
});

test("uses the replacement source that Spotify commits during a full cache load", async () => {
    const {controller, dates, source, sourceRef, entries} = fixture({replaceOnLoad: true});
    controller.cycle();
    await tick();
    assert.ok(controller.uris);
    assert.equal(controller.apply(dates), true);
    assert.equal(source.items, entries);
    assert.equal(sourceRef.current.items[0], entries[264]);
    controller.reset();
    assert.equal(sourceRef.current.items, entries);
});

test("stops the native sort cycle when Spotify replaces the provider during a commit", async () => {
    const {controller, props, provider, source} = fixture({native: true});
    let replacementSorts = 0;
    props.onSort = () => {
        setTimeout(() => {
            provider.memoizedState.memoizedState = {current: source};
            props.sortState = {column: "TITLE_AND_ARTIST", order: 2};
            props.onSort = () => {
                replacementSorts++;
                props.sortState = {column: null, order: 1};
            };
        }, 0);
    };
    controller.cycle();
    for (let i = 0; i < 100 && controller.state.loading; ++i) {
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    assert.equal(replacementSorts, 0);
    assert.equal(controller.state.mode, "none");
    assert.equal(controller.state.loading, false);
});

test("rejects a native sort loop that never returns to its starting order", async () => {
    const {controller, props} = fixture({native: true});
    const orders = [];
    props.onSort = column => {
        const {order} = props.sortState;
        orders.push(order);
        props.sortState = {column, order: order === 3 ? 2 : order + 1};
    };
    controller.cycle();
    for (let i = 0; i < 100 && controller.state.loading; ++i) {
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    assert.deepEqual(orders, [1, 2, 3]);
    assert.equal(controller.state.mode, "none");
    assert.equal(controller.state.loading, false);
});

test("reuses the full native baseline when Date Added cycles without a neutral order", async () => {
    const {controller, props, dates, source, entries} = fixture();
    const orders = [];
    props.sortState = {column: "ADDED_AT", order: 2};
    props.onSort = column => {
        const {order} = props.sortState;
        orders.push(order);
        props.sortState = {column, order: order === 1 ? 2 : 1};
    };
    controller.cycle();
    for (let i = 0; i < 100 && controller.state.loading; ++i) {
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    assert.deepEqual(orders, [2, 1]);
    assert.equal(controller.apply(dates), true);
    assert.equal(source.items[0], entries[264]);
    controller.reset();
    assert.equal(source.items, entries);
    assert.equal(props.sortState.order, 2);
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates), true);
    assert.deepEqual(orders, [2, 1]);
});

function legacySelection(props, grid, window, selected, escapeWorks = true) {
    let escapes = 0;
    delete props.outerRef.current.clearSelected;
    props.outerRef.current.getSelected = () => selected;
    window.KeyboardEvent = class {
        constructor(type, options) { Object.assign(this, options, {type}); }
    };
    grid.dispatchEvent = event => {
        assert.equal(event.type, "keydown");
        assert.equal(event.key, "Escape");
        assert.equal(event.bubbles, true);
        escapes++;
        if (escapeWorks) selected.length = 0;
    };
    return () => escapes;
}

test("legacy playlist uses Escape only when rows are selected", async () => {
    for (const hasSelection of [false, true]) {
        const {controller, props, grid, window, dates, source, entries} = fixture();
        const selected = hasSelection ? [0] : [];
        const escapes = legacySelection(props, grid, window, selected);
        controller.cycle();
        await tick();
        assert.equal(controller.apply(dates), true);
        assert.equal(source.items[0], entries.at(-1));
        assert.equal(selected.length, 0);
        assert.equal(escapes(), Number(hasSelection));
        controller.reset();
        assert.equal(source.items, entries);
    }
});

test("legacy selection failure preserves the order and its ability to restore", async () => {
    const {controller, props, grid, window, dates, source, entries} = fixture();
    const selected = [0];
    const escapes = legacySelection(props, grid, window, selected, false);
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates), false);
    assert.equal(source.items, entries);
    assert.equal(escapes(), 1);

    selected.length = 0;
    if (controller.state.mode === "none") {
        controller.cycle();
        await tick();
    }
    assert.equal(controller.apply(dates), true);
    const sorted = source.items;
    selected.push(0);
    controller.cycle();
    assert.equal(controller.apply(dates), false);
    assert.equal(source.items, sorted);
    controller.reset();
    assert.equal(source.items, sorted);
    selected.length = 0;
    controller.reset();
    assert.equal(source.items, entries);
});

test("restores an owned legacy source after its selected grid detaches", async () => {
    const {controller, props, grid, window, dates, source, entries} = fixture();
    const selected = [];
    const escapes = legacySelection(props, grid, window, selected, false);
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates), true);

    selected.push(0);
    grid.isConnected = false;
    controller.setContext(null, null);
    assert.equal(source.items, entries);
    assert.equal(controller.state.mode, "none");
    assert.equal(escapes(), 0);
});

test("reads a full legacy cache after cacheAll returns no payload", async () => {
    const {controller, dates, sourceRef, entries} = fixture({legacyCacheAll: true, replaceOnLoad: true});
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates), true);
    assert.equal(sourceRef.current.items[0], entries.at(-1));
    controller.reset();
    assert.equal(sourceRef.current.items, entries);
});

test("refuses an incomplete legacy cache when cacheAll returns no payload", async () => {
    const {controller, dates, sourceRef, entries} = fixture({
        legacyCacheAll: true, replaceOnLoad: true, incomplete: true
    });
    controller.cycle();
    await tick();
    assert.ok(controller.state.error);
    assert.equal(controller.uris, null);
    assert.equal(controller.apply(dates), false);
    assert.equal(sourceRef.current.items, entries);
});

test("waits for Spotify to commit a replacement source after cacheAll resolves", async () => {
    const {controller, dates, window, sourceRef, source, entries} = fixture({legacyCacheAll: true});
    const frames = [];
    window.requestAnimationFrame = callback => frames.push(callback);
    controller.cycle();
    await tick();
    const replacement = {...sourceRef.current};
    frames.shift()?.();
    sourceRef.current = replacement;
    frames.shift()?.();
    await tick();
    controller.refresh();

    assert.equal(controller.apply(dates), true);
    assert.equal(source.items, entries);
    assert.equal(replacement.items[0], entries.at(-1));
    controller.reset();
    assert.equal(replacement.items, entries);
});

test("discards a full cache load when the playlist changes before its render settles", async () => {
    const {controller, dates, window, source, entries} = fixture();
    const frames = [];
    window.requestAnimationFrame = callback => frames.push(callback);
    controller.cycle();
    await tick();
    assert.equal(controller.state.loading, true);
    controller.setContext(null, null);
    frames.shift()();
    frames.shift()();
    await tick();

    assert.equal(controller.state.mode, "none");
    assert.equal(controller.uris, null);
    assert.equal(controller.apply(dates), false);
    assert.equal(source.items, entries);
});

test("playing an unsaved row advances in Saved order, including duplicate occurrences", async () => {
    const {controller, entries, dates, source, window} = fixture({count: 8});
    // Native playlist order is 23, 7 Years, 911. Saving 7 Years moves it
    // above the other two and leaves 911 immediately after 23.
    const [start, previouslyNext, expectedNext] = entries;
    previouslyNext.value.uri = uri(1);
    dates.set(uri(1), {utc: "2026-01-02T00:00:00Z"});
    dates.set(start.value.uri, {});
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates, 1), true);

    // Consume the context before emitting any action event. Late mutation of
    // event.data cannot change the already-dispatched native play request.
    const player = {play(context, origin, options) {
        return JSON.parse(JSON.stringify({context, origin, options}));
    }};
    window.__floggfyMarkedCurrentPlayer = () => player;
    window.__floggfyMarkedPlay = request => controller.preparePlayback(request);
    controller.syncPlayback();

    function play(entry, configure = () => {}) {
        const request = {
            action: "play", origin: {featureIdentifier: "playlist"},
            context: {uri: "spotify:playlist:fixture", playlistQueryOptions: {}},
            options: {skipTo: {uid: entry.value.uid, uri: entry.value.uri, index: entry.index}}
        };
        configure(request);
        const sent = player.play(request.context, request.origin, request.options);
        // A playlist URI with no URL goes to Spotify's playlist service.
        // Explicit context pages are consumed by its context player.
        const items = sent.context.url ? sent.context.pages[0].items : entries.map(row => row.value);
        return {items, index: sent.options.skipTo.index, request};
    }
    const {items, index} = play(start);
    assert.equal(items[index].uid, start.value.uid);
    assert.equal(items[index + 1].uid, expectedNext.value.uid);
    for (const origin of ["your_library", "playlist", "search", undefined]) {
        const started = play(start, request => {request.origin.featureIdentifier = origin;});
        assert.equal(started.items[started.index + 1].uid, expectedNext.value.uid,
            "matching playlist playback uses Saved order regardless of the UI origin label");
    }
    const displayedOrder = source.items;
    dates.set(start.value.uri, {utc: "2027-01-01T00:00:00Z"});
    assert.equal(controller.apply(dates, 2), true);
    assert.equal(source.items, displayedOrder, "completion retains the displayed and playing order");
    assert.equal(play(start).items[index + 1].uid, expectedNext.value.uid);
    for (const configure of [
        req => { req.context.uri = "spotify:playlist:other"; },
        req => { req.options.shuffle = true; },
        req => { req.options.skipTo.uid = "missing"; },
        req => { Object.freeze(req.options); },
    ]) {
        const {request} = play(start, configure);
        assert.equal(request.context.pages, undefined);
        assert.equal(request.context.url, undefined);
    }
    const duplicate = play(entries[4]);
    assert.equal(duplicate.items[duplicate.index].uid, entries[4].value.uid);
    controller.cycle();
    controller.apply(dates, 1);
    assert.equal(play(start).items[0].uid, entries[1].value.uid);
    controller.cycle();
    assert.equal(play(start).items[1].uid, previouslyNext.value.uid);
});

test("a Saved sort changes the active playlist context and neutral restores the baseline", async () => {
    const {controller, entries, dates, grid, window} = fixture({count: 8});
    const playlist = "spotify:playlist:" + "A".repeat(22);
    controller.setContext(grid, playlist);
    const updated = [];
    window.__floggfyMarkedCurrentPlayer = () => player;
    const player = {
        getState: () => ({sessionId: "current", context: {uri: playlist}, shuffle: false,
            item: {...entries[3].value, provider: "context"}}),
        updateContext(session, context) {
            assert.equal(session, "current");
            updated.push(Array.from(context.pages[0].tracks, item => item.uid));
        }
    };
    controller.cycle();
    await tick();
    controller.apply(dates, 1);
    controller.syncPlayback();
    assert.deepEqual(updated[0].slice(0, 4), ["7", "3", "4", "0"]);
    controller.cycle();
    controller.apply(dates, 1);
    controller.syncPlayback();
    assert.deepEqual(updated[1].slice(0, 4), ["0", "3", "4", "7"]);
    controller.cycle();
    assert.deepEqual(updated[2], entries.map(row => row.value.uid));
});

async function sortedPlayback({now, onUpdate} = {}) {
    const {controller, entries, dates, grid, window, source, props} = fixture({count: 8, now});
    const playlist = "spotify:playlist:" + "A".repeat(22);
    controller.setContext(grid, playlist);
    const original = entries.map(row => row.value.uid);
    const updated = [];
    const item = {...entries[3].value, provider: "context"};
    const state = {sessionId: "current", context: {uri: playlist}, shuffle: false, item};
    window.__floggfyMarkedCurrentPlayer = () => player;
    const player = {
        getState: () => state,
        updateContext(session, context) {
            updated.push({session, order: Array.from(context.pages[0].tracks, row => row.uid)});
            return onUpdate?.(updated.length);
        }
    };
    controller.cycle();
    await tick();
    controller.apply(dates, 1);
    controller.syncPlayback();
    assert.equal(updated.length, 1);
    assert.notDeepEqual(updated[0].order, original);
    return {controller, entries, dates, source, props, state, item, original, updated, player, window};
}

test("leaving a Saved sort restores the original active context", async () => {
    for (const reset of [controller => controller.reset(), controller => controller.setContext(null, null)]) {
        const {controller, entries, source, original, updated} = await sortedPlayback();
        assert.equal(reset(controller), true);
        assert.equal(source.items, entries);
        assert.deepEqual(updated[1], {session: "current", order: original});
    }
});

test("a native sort commit restores playback while Saved still owns the rows", async () => {
    const {controller, entries, source, props, original, updated} = await sortedPlayback();
    props.sortState = {column: "TITLE_AND_ARTIST", order: 2};
    controller.refresh();
    assert.equal(controller.state.mode, "none");
    assert.equal(source.items, entries);
    assert.deepEqual(updated[1], {session: "current", order: original});
    assert.deepEqual(props.sortState, {column: "TITLE_AND_ARTIST", order: 2});
});

test("a native sort keeps the replacement row array it committed", async () => {
    const {controller, entries, source, props, updated} = await sortedPlayback();
    const replacement = entries.slice().reverse();
    props.sortState = {column: "TITLE_AND_ARTIST", order: 2};
    source.items = source.validItems = replacement;
    controller.refresh();
    assert.equal(controller.state.mode, "none");
    assert.equal(source.items, replacement);
    assert.equal(updated.length, 1);
});

test("leaving during a player read failure restores only the previously observed session", async () => {
    for (const sessionId of ["current", "new"]) {
        const {controller, entries, source, state, original, updated, player} = await sortedPlayback();
        player.getState = () => { throw Error("temporarily unavailable"); };
        assert.equal(controller.setContext(null, null), true);
        assert.equal(source.items, entries);
        assert.equal(updated.length, 1);

        state.sessionId = sessionId;
        player.getState = () => state;
        controller.retryPlaybackRestore();
        state.sessionId = "current";
        controller.retryPlaybackRestore();
        if (sessionId === "current") assert.deepEqual(updated[1], {session: "current", order: original});
        else assert.equal(updated.length, 1);
    }
});

test("unreadable heartbeats keep a restore that was deferred by a queued track", async () => {
    const {controller, state, item, original, updated, player} = await sortedPlayback();
    state.item = {uid: "manual", uri: uri(30), provider: "queue"};
    assert.equal(controller.setContext(null, null), true);
    player.getState = () => { throw Error("temporarily unavailable"); };
    controller.retryPlaybackRestore();
    player.getState = () => undefined;
    controller.retryPlaybackRestore();
    assert.equal(updated.length, 1);

    state.item = item;
    player.getState = () => state;
    controller.retryPlaybackRestore();
    assert.deepEqual(updated[1], {session: "current", order: original});
});

test("a neutral cycle defers restoring the active context while a manually queued track plays", async () => {
    const {controller, entries, dates, source, state, item, original, updated} = await sortedPlayback();
    controller.cycle();
    controller.apply(dates, 1);
    controller.syncPlayback();
    state.item = {uid: "manual", uri: uri(30), provider: "queue"};
    controller.cycle();
    assert.equal(source.items, entries);
    controller.retryPlaybackRestore?.();
    assert.equal(updated.length, 2);

    state.item = item;
    controller.retryPlaybackRestore?.();
    assert.deepEqual(updated[2], {session: "current", order: original});
    controller.retryPlaybackRestore?.();
    assert.equal(updated.length, 3);
});

test("a deferred restore cannot change a new playback session", async () => {
    for (const next of [{sessionId: "new"}, {context: {uri: "spotify:playlist:" + "B".repeat(22)}}]) {
        const {controller, state, item, updated} = await sortedPlayback();
        state.item = {uid: "manual", uri: uri(30), provider: "queue"};
        assert.equal(controller.setContext(null, null), true);
        const before = {...state, item};

        Object.assign(state, next, {item});
        controller.retryPlaybackRestore?.();
        Object.assign(state, before);
        controller.retryPlaybackRestore?.();
        assert.equal(updated.length, 1);
    }
});

test("a failed context restore retries only after the command backoff", async () => {
    const failed = () => { throw Error("temporarily unavailable"); };
    for (const fail of [failed, () => Promise.resolve().then(failed)]) {
        let now = 1000;
        const {controller, original, updated} = await sortedPlayback({
            now: () => now,
            onUpdate: count => { if (count === 2) return fail(); }
        });
        assert.equal(controller.reset(), true);
        await tick();
        assert.equal(updated.length, 2);
        now += 2999;
        controller.retryPlaybackRestore();
        assert.equal(updated.length, 2);
        now++;
        controller.retryPlaybackRestore();
        assert.deepEqual(updated[2], {session: "current", order: original});
        await tick();
        now += 4000;
        controller.retryPlaybackRestore();
        assert.equal(updated.length, 3);
    }
});

test("a rejected restore does not retry against a replacement player", async () => {
    let now = 1000;
    const {controller, player, window, updated} = await sortedPlayback({
        now: () => now,
        onUpdate: count => { if (count === 2) return Promise.reject(Error("temporarily unavailable")); }
    });
    assert.equal(controller.reset(), true);
    await tick();
    now += 4000;
    window.__floggfyMarkedCurrentPlayer = () => ({...player});
    controller.retryPlaybackRestore();
    window.__floggfyMarkedCurrentPlayer = () => player;
    controller.retryPlaybackRestore();
    assert.equal(updated.length, 2);
});

test("resorting the active playlist supersedes its deferred restore", async () => {
    const {controller, dates, state, item, updated} = await sortedPlayback();
    state.item = {uid: "manual", uri: uri(30), provider: "queue"};
    assert.equal(controller.reset(), true);
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates, 1), true);
    controller.syncPlayback();

    state.item = item;
    controller.retryPlaybackRestore();
    assert.equal(updated.length, 1);
    controller.syncPlayback();
    assert.deepEqual(updated[1], updated[0]);
});

test("a renewed Saved sort supersedes a deferred restore while player reads are unavailable", async () => {
    const {controller, dates, state, item, player, updated} = await sortedPlayback();
    state.item = {uid: "manual", uri: uri(30), provider: "queue"};
    assert.equal(controller.reset(), true);
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates, 1), true);
    player.getState = () => { throw Error("temporarily unavailable"); };
    controller.syncPlayback();

    state.item = item;
    player.getState = () => state;
    controller.retryPlaybackRestore();
    assert.equal(updated.length, 1);
    controller.syncPlayback();
    assert.deepEqual(updated[1], updated[0]);
});

test("a renewed Saved sort can be cleared during an outage without losing its deferred restore", async () => {
    const {controller, dates, state, item, player, original, updated} = await sortedPlayback();
    state.item = {uid: "manual", uri: uri(30), provider: "queue"};
    assert.equal(controller.reset(), true);
    controller.cycle();
    await tick();
    assert.equal(controller.apply(dates, 1), true);
    player.getState = () => { throw Error("temporarily unavailable"); };
    controller.syncPlayback();
    assert.equal(controller.reset(), true);
    assert.equal(updated.length, 1);

    state.item = item;
    player.getState = () => state;
    controller.retryPlaybackRestore();
    assert.deepEqual(updated[1], {session: "current", order: original});
});
