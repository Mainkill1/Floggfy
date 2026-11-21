"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const test = require("node:test");

const script = fs.readFileSync(require.resolve("../native/ui/marked_playlist_sort.js"), "utf8");
const playlist = "spotify:playlist:" + "P".repeat(22);
const a = "spotify:track:" + "A".repeat(22);
const b = "spotify:track:" + "B".repeat(22);
const order = [{uid: "first", uri: a}, {uid: "second", uri: b}, {uid: "duplicate", uri: a}];

function fixture() {
    const window = {};
    vm.runInNewContext(script, {window, setTimeout});
    const current = {sessionId: "session-one", context: {uri: playlist, metadata: {source: "playlist"}},
        item: {...order[2], provider: "context"}, shuffle: false, smartShuffle: false,
        isPaused: false, positionAsOfTimestamp: 54321, nextItems: [{uid: "manual", uri: b, provider: "queue"}]};
    const commands = [];
    const player = {getState() { return current; }, updateContext(session, context) {
        commands.push({session, context: JSON.parse(JSON.stringify(context))});
    }};
    return {update: window.__floggfyUpdateMarkedPlayback, player, current, commands};
}

test("updates ordered context tracks across sort directions and leaves playback and manual queue alone", () => {
    const {update, player, current, commands} = fixture();
    const saved = order.slice().reverse();
    update(player, playlist, saved);
    update(player, playlist, saved);
    assert.equal(commands.length, 1);
    assert.deepEqual(commands[0], {session: "session-one", context: {uri: playlist,
        metadata: current.context.metadata, isLoading: false, pages: [{tracks: saved}]}});
    update(player, playlist, order);
    assert.deepEqual(commands[1].context.pages[0].tracks, order);
    assert.equal(current.positionAsOfTimestamp, 54321);
    assert.deepEqual(current.nextItems, [{uid: "manual", uri: b, provider: "queue"}]);
    assert.equal(current.item.uid, "duplicate");
    current.sessionId = "session-two";
    update(player, playlist, order);
    assert.equal(commands[2].session, "session-two");
});

test("requires the active playlist, session, unshuffled context track, and matching unique UID and URI", () => {
    for (const patch of [{context: {uri: a}}, {sessionId: ""}, {item: {...order[2], provider: "queue"}},
        {item: {...order[2], provider: "autoplay"}}, {item: {...order[2], uri: b, provider: "context"}},
        {item: {uid: "absent", uri: a, provider: "context"}}, {shuffle: true}, {shuffle: null}, {smartShuffle: true}]) {
        const {update, player, current, commands} = fixture();
        Object.assign(current, patch);
        update(player, playlist, order);
        assert.equal(commands.length, 0);
    }
    const {update, player, commands} = fixture();
    update(player, playlist, [...order, {uid: "duplicate", uri: a}]);
    assert.equal(commands.length, 0);
});

test("defers while a queued song plays, restores a verified baseline explicitly and releases silently", () => {
    const {update, player, current, commands} = fixture();
    current.item.provider = "queue";
    update(player, playlist, order);
    assert.equal(commands.length, 0);
    current.item.provider = "context";
    update(player, playlist, order);
    update(null, playlist, null);
    assert.equal(commands.length, 1);
    const baseline = order.slice().reverse();
    update(player, playlist, baseline, true);
    assert.deepEqual(commands[1].context.pages[0].tracks, baseline);
});

test("unsupported players and disconnected or unreadable states leave playback alone", () => {
    const {update, player, commands} = fixture();
    update({}, playlist, order);
    update({getState: player.getState}, playlist, order);
    player.getState = () => { throw Error("unavailable"); };
    update(player, playlist, order);
    assert.equal(commands.length, 0);
});

test("checks each song start without rebuilding the identity map on heartbeats", () => {
    const {update, player, current, commands} = fixture();
    let readCount = 0;
    const view = new Proxy(order, {get(target, key, receiver) {
        if (/^\d+$/.test(String(key))) ++readCount;
        return Reflect.get(target, key, receiver);
    }});
    update(player, playlist, view);
    assert.ok(readCount >= order.length);
    const reads = readCount;
    update(player, playlist, view);
    assert.equal(readCount, reads);
    assert.equal(commands.length, 1);
    current.item = {...order[0], provider: "context"};
    update(player, playlist, view);
    assert.equal(commands.length, 2, "the next song needs its own queue check");
});

test("lets native shuffle own the context, then reapplies when normal order returns", () => {
    const {update, player, current, commands} = fixture();
    update(player, playlist, order);
    current.shuffle = true;
    update(player, playlist, order);
    assert.equal(commands.length, 1);
    current.shuffle = false;
    update(player, playlist, order);
    assert.equal(commands.length, 2);
});

test("a rejected command does not repeatedly retry on each render", async () => {
    const {update, player} = fixture();
    let calls = 0;
    player.updateContext = () => { ++calls; return Promise.reject(Error("unavailable")); };
    update(player, playlist, order);
    await new Promise(resolve => setImmediate(resolve));
    update(player, playlist, order);
    assert.equal(calls, 1);
});

test("prepares the play context before native dispatch and preserves its contract", () => {
    const {update, player} = fixture();
    let prepared = false, calls = 0;
    const result = Promise.resolve("native-result");
    const context = {uri: playlist}, origin = {featureIdentifier: "playlist"}, options = {};
    // Register a hook in the renderer realm, as marked_playlist.js does.
    const window = {};
    vm.runInNewContext(script, {window});
    window.__floggfyMarkedPlay = request => {
        assert.equal(request.context, context);assert.equal(request.origin, origin);
        request.context.url = "context://" + playlist;
        request.context.pages = [{items: order}];prepared = true;
    };
    player.play = function(c, o, opts) {
        ++calls;assert.equal(this, player);assert.ok(prepared);
        assert.equal(c, context);assert.equal(o, origin);assert.equal(opts, options);
        assert.equal(c.pages[0].items[1].uri, b);
        return result;
    };
    const bind = window.__floggfyUpdateMarkedPlayback.bindPlay;
    bind(player);const wrapped = player.play;bind(player);assert.equal(player.play, wrapped);
    assert.equal(player.play(context, origin, options), result);assert.equal(calls, 1);
    window.__floggfyMarkedPlay = undefined;
    player.play = () => {throw Error("native failure");};bind(player);
    assert.throws(() => player.play(context, origin, options), /native failure/);
    update.bindPlay(Object.freeze({play() {}}));
});

test("edits the pending queue directly, retaining manual queue and history", async () => {
    const {update, player, current} = fixture();
    const manual = {provider: "queue", contextTrack: {uid: "manual", uri: b}};
    const previous = [{provider: "context", contextTrack: order[0]}];
    const internal = {track: {contextTrack: current.item}, nextTracks: [manual],
        prevTracks: previous, queueRevision: 42n};
    let calls = 0;
    player._queue = {getInternalQueue: () => internal};
    player._contextPlayer = {
        updateContext() {assert.fail("new clients need queue edits, not a context refresh");},
        setQueue(request) {
            ++calls;assert.equal(request.queueRevision, 42n);assert.equal(request.prevTracks, previous);
            assert.equal(request.nextTracks[0], manual);
            internal.nextTracks = request.nextTracks.map((row, index) => row.contextTrack.uid ? row :
                {...row, contextTrack: {...row.contextTrack, uid: "q" + index}});
            ++internal.queueRevision;
            return Promise.resolve({error: 0});
        }
    };
    const sorted = [order[2], order[1], order[0]];
    update(player, playlist, sorted);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(calls, 1);
    assert.deepEqual(Array.from(internal.nextTracks, row => row.contextTrack.uri), [b, b, a]);
    assert.equal(current.item.uid, "duplicate");assert.equal(current.positionAsOfTimestamp, 54321);
});

test("does not overwrite a queue whose current track has not caught up", async () => {
    const {update, player} = fixture();
    player._queue = {getInternalQueue: () => ({track: {contextTrack: order[0]},
        nextTracks: [], prevTracks: [], queueRevision: 1n})};
    player._contextPlayer = {
        setQueue() {assert.fail("an old queue snapshot must not be replaced");}
    };
    update(player, playlist, order);
    await new Promise(resolve => setImmediate(resolve));
});

test("materializes missing songs without playlist UIDs and follows assigned queue IDs at each song start", async () => {
    const {update, player, current} = fixture();
    current.item = {...order[0], provider: "context"};
    const wrong = {provider: "context", contextTrack: {uid: "old-next", uri: a, metadata: {title: "old"}}};
    const internal = {track: {contextTrack: current.item}, nextTracks: [wrong], prevTracks: [], queueRevision: 1n};
    player._queue = {getInternalQueue: () => internal};
    let assigned = 0, writes = 0;
    player._contextPlayer = {setQueue(request) {
        ++writes;
        const known = new Map(internal.nextTracks.map(row => [row.contextTrack.uid, row]));
        // Observed native behavior: unknown nonempty UIDs are references, not inserts.
        internal.nextTracks = request.nextTracks.flatMap(row => row.contextTrack.uid ?
            known.has(row.contextTrack.uid) ? [known.get(row.contextTrack.uid)] : [] :
            [{...row, contextTrack: {...row.contextTrack, uid: "q" + ++assigned}}]);
        ++internal.queueRevision;
        return Promise.resolve({error: 0});
    }};
    update(player, playlist, order);
    await new Promise(resolve => setTimeout(resolve, 100));
    assert.deepEqual(Array.from(internal.nextTracks, row => row.contextTrack.uri), [b, a]);
    const next = internal.nextTracks.shift();
    current.item = {...next.contextTrack, provider: "context"};
    internal.track = next;++internal.queueRevision;
    update(player, playlist, order);
    await new Promise(resolve => setTimeout(resolve, 100));
    assert.equal(internal.nextTracks[0].contextTrack.uri, a);
    assert.equal(writes, 1, "an already correct next window needs no rewrite");
});
