"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const test = require("node:test");

const script = fs.readFileSync(require.resolve("../native/marked_playlist_sort.js"), "utf8");
const playlist = "spotify:playlist:" + "P".repeat(22);
const a = "spotify:track:" + "A".repeat(22);
const b = "spotify:track:" + "B".repeat(22);
const order = [{uid: "first", uri: a}, {uid: "second", uri: b}, {uid: "duplicate", uri: a}];

function fixture() {
    const window = {};
    vm.runInNewContext(script, {window});
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

test("unchanged order is not traversed again on a heartbeat or a later context track", () => {
    const {update, player, current, commands} = fixture();
    let readCount = 0;
    const view = new Proxy(order, {get(target, key, receiver) {
        if (/^\d+$/.test(String(key))) ++readCount;
        return Reflect.get(target, key, receiver);
    }});
    update(player, playlist, view);
    assert.ok(readCount >= order.length);
    const reads = readCount;
    current.item = {...order[0], provider: "context"};
    update(player, playlist, view);
    assert.equal(readCount, reads);
    assert.equal(commands.length, 1);
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
