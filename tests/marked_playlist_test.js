"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const { JSDOM } = require("jsdom");

const source = fs.readFileSync(require.resolve("../native/ui/marked_playlist.js"), "utf8");
const playlistId = "A".repeat(22);
const a = "spotify:track:" + "B".repeat(22);
const b = "spotify:track:" + "C".repeat(22);
const c = "spotify:track:" + "D".repeat(22);

function fixture(setup) {
    const { window } = new JSDOM("", {
        url: "https://open.spotify.com/playlist/" + playlistId,
        runScripts: "outside-only",
        pretendToBeVisual: true
    });
    const { document } = window;
    const frames = [];
    const requests = [];
    window.requestAnimationFrame = fn => frames.push(fn);
    window.console.log = message => {
        assert.match(message, /^FLOGGFY_MARKED_V1:/);
        const [request, ...uris] = message.slice("FLOGGFY_MARKED_V1:".length).split("|");
        requests.push({ request, uris });
    };
    document.body.innerHTML = `
        <section data-testid="playlist-page" data-test-uri="spotify:playlist:${playlistId}">
            <div role="grid" aria-colcount="3">
                <div role="row" class="wide">
                    <div role="columnheader" aria-colindex="1">#</div>
                    <div role="columnheader" aria-colindex="2">Title</div>
                    <div role="columnheader" aria-colindex="3">Duration</div>
                </div>
                <div role="row" class="wide" data-track-uri="${a}">
                    <div role="gridcell" aria-colindex="1">1</div>
                    <div role="gridcell" aria-colindex="2">Same title</div>
                    <div role="gridcell" aria-colindex="3">3:00</div>
                </div>
            </div>
        </section>`;
    const grid = document.querySelector('[role="grid"]');
    const row = grid.lastElementChild;
    const template = "[first] 16px minmax(120px, 4fr) [last] 96px";
    const nativeStyle = window.getComputedStyle.bind(window);
    window.getComputedStyle = element => element.classList.contains("wide") ? {
        display: "grid",
        gridTemplateColumns: element.style.gridTemplateColumns || "16px 500px [last] 96px",
        getPropertyValue: property => property === "--grid-template-columns" ? template : ""
    } : nativeStyle(element);
    Object.defineProperty(grid, "clientWidth", { configurable: true, value: 760 });
    setup?.(window, grid, row);
    window.eval(source);
    function flush() {
        const scheduled = frames.splice(0);
        scheduled.forEach(fn => fn());
    }
    function poll(revision = 1, enabled = true) {
        window.__floggfyMarkedPoll(enabled, revision);
        flush();
    }
    function reply(request, dates, revision = 1) {
        window.__floggfyMarkedReply({ request: request.request, revision, dates });
        flush();
    }
    return { window, document, grid, row, requests, poll, reply, flush };
}

{
    const { window, grid, row, requests, poll, reply, document } = fixture();
    const duplicate = row.cloneNode(true);
    const unknown = row.cloneNode(true);
    unknown.removeAttribute("data-track-uri");
    grid.append(duplicate, unknown);
    window.eval(source);
    poll();
    assert.equal(grid.firstElementChild.lastElementChild.textContent, "Saved");
    assert.equal(grid.getAttribute("aria-colcount"), "4");
    assert.equal(grid.firstElementChild.lastElementChild.getAttribute("aria-colindex"), "4");
    assert.match(row.style.gridTemplateColumns, /var\(--grid-template-columns\)/);
    assert.deepEqual(requests.map(entry => entry.uris), [[a]]);
    assert.match(requests[0].request, /^[A-Za-z0-9_-]{1,64}$/);
    const firstDate = "2026-07-10T18:21:00Z";
    reply(requests[0], [[a, firstDate]]);
    assert.equal(row.lastElementChild.textContent, new window.Date(firstDate).toLocaleDateString());
    assert.equal(row.lastElementChild.textContent, duplicate.lastElementChild.textContent);
    assert.equal(unknown.lastElementChild.textContent, "");
    assert.ok(row.lastElementChild.title);

    row.setAttribute("data-track-uri", b);
    poll();
    assert.equal(row.lastElementChild.textContent, "");
    assert.equal(row.lastElementChild.title, "");
    assert.deepEqual(requests[1].uris, [b]);
    row.setAttribute("data-track-uri", c);
    reply(requests[1], [[b, firstDate]]);
    assert.equal(row.lastElementChild.textContent, "");
    assert.deepEqual(requests[2].uris, [c]);
    reply(requests[0], [[c, firstDate]]);
    assert.equal(row.lastElementChild.textContent, "");
    reply(requests[2], [[c, firstDate]]);
    assert.ok(row.lastElementChild.textContent);

    poll(2);
    assert.equal(row.lastElementChild.textContent, "");
    assert.deepEqual(new Set(requests[3].uris), new Set([a, c]));
    reply(requests[2], [[c, firstDate]]);
    assert.equal(row.lastElementChild.textContent, "");
    const navigated = document.querySelector("[data-test-uri]");
    navigated.setAttribute("data-test-uri", "spotify:playlist:" + "Z".repeat(22));
    window.history.pushState({}, "", "/playlist/" + "Z".repeat(22));
    poll(2);
    assert.equal(requests.length, 5);
    reply(requests[3], [[c, firstDate]], 2);
    assert.equal(row.lastElementChild.textContent, "");

    poll(2, false);
    assert.equal(grid.getAttribute("aria-colcount"), "3");
    assert.equal(row.style.gridTemplateColumns, "");
    assert.equal(row.children.length, 3);
    assert.equal(grid.firstElementChild.children.length, 3);
    reply(requests[4], [[c, firstDate]], 2);
    assert.equal(row.children.length, 3);
    window.close();
}

{
    const { window, grid, row, requests, poll } = fixture();
    const cell = row.lastElementChild;
    let getterCalls = 0;
    const createFiber = (parent, props) => ({ return: parent, memoizedProps: props });
    const staleRoot = {};
    const activeRoot = { alternate: staleRoot };
    staleRoot.alternate = activeRoot;
    activeRoot.stateNode = staleRoot.stateNode = { current: activeRoot };
    let staleParent = staleRoot;
    let activeParent = activeRoot;
    for (let depth = 0; depth < 290; depth++) {
        const staleAncestor = createFiber(staleParent);
        const activeAncestor = createFiber(activeParent);
        staleAncestor.alternate = activeAncestor;
        activeAncestor.alternate = staleAncestor;
        staleParent.child = staleAncestor;
        activeParent.child = activeAncestor;
        staleParent = staleAncestor;
        activeParent = activeAncestor;
    }
    // Both return links lead to the committed root. Child membership resolves
    // which copy of the row actually belongs to it.
    const staleRow = createFiber(activeParent, { item: { uri: a } });
    const activeRow = createFiber(activeParent, { item: { uri: b } });
    staleRow.alternate = activeRow;
    activeRow.alternate = staleRow;
    staleParent.child = staleRow;
    activeParent.child = activeRow;
    const staleHost = createFiber(staleRow, {});
    const activeHost = createFiber(activeRow, {});
    staleHost.alternate = activeHost;
    activeHost.alternate = staleHost;
    staleRow.child = staleHost;
    activeRow.child = activeHost;
    row["__reactFiber$fixture"] = staleHost;
    Object.defineProperty(activeRow.memoizedProps, "trackUri", { get() { getterCalls++; } });
    row.removeAttribute("data-track-uri");
    poll();
    assert.deepEqual(requests.map(entry => entry.uris), [[b]]);
    assert.equal(getterCalls, 0);

    // A bailout can share one child list across both parent alternates.
    activeParent.child = staleParent.child;
    poll(2);
    assert.deepEqual(requests.map(entry => entry.uris), [[b], [a]]);
    cell.remove();
    poll();
    assert.equal(row.children.length, 2);
    assert.equal(row.style.gridTemplateColumns, "");
    Object.defineProperty(grid, "clientWidth", { value: 300 });
    poll();
    assert.equal(grid.firstElementChild.children.length, 3);
    window.close();
}

{
    const { window, document, grid, row, requests, poll, reply } = fixture();
    grid.setAttribute("data-testid", "playlist-tracklist");
    const alternate = grid.cloneNode(true);
    alternate.removeAttribute("data-testid");
    Object.defineProperty(alternate, "clientWidth", { value: 900 });
    grid.parentElement.prepend(alternate);

    const presentation = document.createElement("div");
    presentation.setAttribute("role", "presentation");
    presentation.setAttribute("data-testid", "tracklist-row");
    presentation.classList.add("wide");
    presentation.append(...row.children);
    row.append(presentation);
    row.classList.remove("wide");
    const freshRoot = {};
    freshRoot.stateNode = { current: freshRoot };
    const item = { return: freshRoot, memoizedProps: { index: 0, uri: b, contextUri: "spotify:playlist:" + playlistId } };
    const indexedRow = { return: item, memoizedProps: { rowIndex: 1 } };
    row.__reactFiber$fixture = { return: indexedRow, memoizedProps: {} };
    row.removeAttribute("data-track-uri");

    poll();
    assert.deepEqual(requests.map(entry => entry.uris), [[b]]);
    reply(requests[0], [[b, "2026-07-10T18:21:00Z"]]);
    assert.ok(presentation.lastElementChild.textContent);
    assert.equal(row.childElementCount, 1);
    assert.equal(alternate.firstElementChild.childElementCount, 3);
    window.close();
}

{
    const { window, document, row, requests, poll, reply, flush } = fixture();
    let now = 1000;
    let hidden = false;
    window.performance.now = () => now;
    Object.defineProperty(document, "visibilityState", { get: () => hidden ? "hidden" : "visible" });
    const date = "2026-07-10T18:21:00Z";
    poll();
    reply(requests[0], [[a, date]]);
    const displayed = row.lastElementChild.textContent;
    assert.ok(displayed);
    now += 29_999;
    poll();
    assert.equal(requests.length, 1);
    hidden = true;
    now++;
    poll();
    assert.equal(requests.length, 1);
    hidden = false;
    document.dispatchEvent(new window.Event("visibilitychange"));
    flush();
    assert.equal(requests.length, 2);
    assert.equal(row.lastElementChild.textContent, displayed);
    reply(requests[1], []);
    assert.equal(row.lastElementChild.textContent, "");
    poll();
    assert.equal(requests.length, 2);
    now += 30_000;
    poll();
    assert.equal(requests.length, 3);
    reply(requests[2], [[a, date]]);
    assert.equal(row.lastElementChild.textContent, displayed);
    window.close();
}

{
    const { window, grid, row, requests, poll, reply } = fixture();
    let now = 1000;
    window.performance.now = () => now;
    const second = row.cloneNode(true);
    second.setAttribute("data-track-uri", b);
    grid.append(second);
    poll();
    assert.deepEqual(requests[0].uris, [a, b]);

    grid.insertBefore(second, row);
    now += 3000;
    poll();
    now += 3000;
    poll();
    assert.deepEqual(requests, [requests[0], requests[0], requests[0]]);
    reply(requests[0], [[a, "2026-07-10T18:21:00Z"]]);
    assert.ok(row.lastElementChild.textContent);
    assert.equal(second.lastElementChild.textContent, "");
    assert.equal(requests.length, 3);
    window.close();
}

async function testObserverAndLinks() {
    const { window, document, grid, row, requests, poll, reply, flush } = fixture();
    const title = row.children[1];
    const link = document.createElement("a");
    link.setAttribute("data-testid", "internal-track-link");
    link.href = "/track/" + a.split(":").pop();
    title.replaceChildren(link);
    row.removeAttribute("data-track-uri");
    poll();
    reply(requests[0], [[a, "2026-07-10T18:21:00Z"]]);
    assert.ok(row.lastElementChild.textContent);

    link.href = "/track/" + b.split(":").pop();
    await Promise.resolve();
    flush();
    assert.equal(row.lastElementChild.textContent, "");
    assert.deepEqual(requests[1].uris, [b]);

    const section = grid.parentElement;
    section.remove();
    await Promise.resolve();
    flush();
    assert.equal(row.children.length, 3);
    assert.equal(grid.getAttribute("aria-colcount"), "3");
    reply(requests[1], [[b, "2026-07-10T18:21:00Z"]]);
    assert.equal(row.children.length, 3);
    window.close();
}

testObserverAndLinks().then(() => console.log("marked playlist tests passed"))
    .catch(error => { console.error(error); process.exitCode = 1; });

// Cached album/single pages use their own page kind and have no native sort.
// Their saved-date lookup must work without any network/API data.
{
    const {window,grid,row,requests,poll,reply}=fixture((window,grid)=>{
        const page=grid.parentElement;
        page.setAttribute("data-testid","album-page");
        page.removeAttribute("data-test-uri");
        window.history.replaceState({},"","/album/"+playlistId);
        const root={stateNode:{}};root.stateNode.current=root;
        page.__reactFiber$test={return:{memoizedProps:{uri:"spotify:album:"+playlistId},return:root}};
        Object.defineProperty(window.navigator,"onLine",{value:false});
        window.fetch=()=>assert.fail("saved dates must use the local catalog offline");
    });
    poll();
    assert.equal(grid.firstElementChild.lastElementChild.textContent,"Saved");
    assert.equal(grid.firstElementChild.lastElementChild.querySelector("button"),null);
    assert.deepEqual(requests[0].uris,[a]);
    const date="2026-11-20T18:00:00Z";
    reply(requests[0],[[a,date]]);
    assert.equal(row.lastElementChild.textContent,new window.Date(date).toLocaleDateString());
    window.history.replaceState({},"","/album/"+"Z".repeat(22));
    poll();
    assert.equal(grid.querySelectorAll('[data-floggfy-saved-column]').length,0);
    window.close();
}

// A Saved sort queries all playlist IDs, not just mounted rows, in bounded batches.
{
    const ids = [a, ...Array.from({length: 299}, (_, i) => "spotify:track:" + String(i).padStart(22, "0"))];
    let control;
    let now = 100;
    const test = fixture(window => {
        window.performance.now = () => now;
        window.__floggfyCreateMarkedSort = ({changed}) => control = {
            state: {mode: "none", loading: false, error: ""}, uris: null, applied: null,
            setContext(grid) { if (!grid) { this.reset(); this.released = true; } }, refresh() {},
            cycle() {
                this.state.mode = this.state.mode === "none" ? "descending" : this.state.mode === "descending" ? "ascending" : "none";
                this.uris = this.state.mode === "none" ? null : new Set(ids);changed();
            },
            reset() {this.state.mode="none";this.uris=null;changed();},
            apply(dates) {if(this.uris && [...this.uris].every(uri=>dates.has(uri))){this.applied=new Map(dates);return true;}return false;}
        };
    });
    const {window,grid,row,requests,poll,reply,flush}=test;
    const nativeHeader=grid.firstElementChild.children[2];
    nativeHeader.setAttribute("aria-sort","descending");
    nativeHeader.innerHTML+=' <svg aria-hidden="true" style="--encore-icon-fill:var(--text-bright-accent);visibility:visible"></svg>';
    const nativeArrow=nativeHeader.querySelector("svg");
    poll();
    const button=grid.firstElementChild.lastElementChild.querySelector("button");
    assert.equal(button.type,"button");
    button.click();flush();
    assert.equal(grid.firstElementChild.lastElementChild.getAttribute("aria-sort"),"descending");
    let batch=requests.at(-1);
    assert.equal(batch.uris.length,256);
    assert.equal(button.getAttribute("aria-busy"),"true");
    const date="2026-11-20T18:00:00Z";
    reply(batch,[[a,date],[ids[255],date]]);
    assert.equal(control.applied,null);
    batch=requests.at(-1);
    assert.equal(batch.uris.length,44);
    assert.ok(!batch.uris.includes(a));
    reply(batch,[[ids[299],"2026-11-21T18:00:00Z"]]);
    assert.equal(control.applied.size,300);
    // Isolate request lookup and header painting from our fixture adapter.
    const fixtureApply = control.apply;
    control.apply = () => true;
    let scopeReads = 0;
    const scope = control.uris;
    scope[Symbol.iterator] = function* () {
        ++scopeReads;
        yield* Set.prototype.values.call(this);
    };
    poll();
    assert.equal(scopeReads,0,"fresh completed lookup does not traverse the playlist on a heartbeat");
    control.apply = fixtureApply;
    assert.equal(control.applied.get(ids[299]).utc,"2026-11-21T18:00:00Z");
    assert.equal(button.getAttribute("aria-busy"),"false");
    assert.equal(nativeHeader.getAttribute("aria-sort"),"none");
    assert.equal(nativeArrow.style.visibility,"hidden");
    assert.equal(row.lastElementChild.textContent,new window.Date(date).toLocaleDateString());
    grid.firstElementChild.children[1].dispatchEvent(new window.MouseEvent("click",{bubbles:true}));flush();
    assert.equal(control.state.mode,"none");
    assert.equal(nativeHeader.getAttribute("aria-sort"),"descending");
    assert.equal(nativeArrow.style.visibility,"visible");
    const beforeResort = requests.length;
    button.click();flush();
    assert.equal(requests.length,beforeResort,"sorting again reuses off-screen dates");
    assert.equal(button.getAttribute("aria-busy"),"false");
    now += 29_999;
    poll();
    assert.equal(requests.length,beforeResort);
    ++now;
    poll();
    assert.equal(requests.length,beforeResort+1,"an unchanged playlist refreshes its dates at expiry");
    assert.equal(requests.at(-1).uris.length,256);
    const beforeRevision = requests.length;
    poll(2);
    assert.ok(requests.length>beforeRevision,"catalog changes still refresh cached dates");
    poll(1,false);
    assert.equal(control.state.mode,"none");
    assert.equal(control.released,true,"disabling releases the cached native playlist");
    assert.equal(grid.querySelectorAll('[data-floggfy-saved-column]').length,0);
    window.close();
}

// Liked Songs and some playlist views omit the DOM URI but expose it on the
// committed native playlist provider. Never infer identity from a display title.
{
    const {window,grid,requests,poll,reply} = fixture((window,grid) => {
        const page=grid.parentElement;
        page.removeAttribute("data-test-uri");
        const root={stateNode:{}};
        root.stateNode.current=root;
        page.__reactFiber$test={return:{memoizedProps:{uri:"spotify:playlist:"+playlistId},return:root}};
    });
    poll();
    assert.equal(grid.firstElementChild.lastElementChild.textContent,"Saved");
    reply(requests.at(-1),[[a,"2026-11-20T18:00:00Z"]]);
    assert.ok(grid.lastElementChild.lastElementChild.textContent);
    delete grid.parentElement.__reactFiber$test;
    poll();
    assert.equal(grid.querySelectorAll('[data-floggfy-saved-column]').length,0);
    window.close();
}

// Failed legacy restoration cannot apply the old session to a newly visible grid.
for (const navigation of [false, true]) {
    let blocked = false;
    let releases = 0;
    let applied = 0;
    const {window, grid, row, document, poll} = fixture(window => {
        window.__floggfyCreateMarkedSort = () => ({
            uris: null,
            state: {mode: "none", loading: false, error: ""},
            setContext(grid) {
                if (grid) return !blocked;
                releases++;
                return !blocked;
            },
            refresh() {},
            apply() { applied++; return false; }
        });
    });
    poll();
    const prior = applied;
    blocked = true;
    let replacement;
    if (navigation) {
        const section = grid.parentElement;
        replacement = grid.cloneNode(true);
        const nextSection = section.cloneNode(false);
        nextSection.append(replacement);
        Object.defineProperty(replacement, "clientWidth", {value: 760});
        replacement.querySelectorAll("[data-floggfy-saved-column]").forEach(cell => cell.remove());
        for (const row of replacement.children) row.style.gridTemplateColumns = "";
        replacement.setAttribute("aria-colcount", "3");
        section.removeAttribute("data-testid");
        document.body.append(nextSection);
    }
    poll(1, navigation);
    assert.equal(applied, prior);
    assert.equal(grid.querySelectorAll("[data-floggfy-saved-column]").length, 0);
    assert.equal(replacement?.querySelectorAll("[data-floggfy-saved-column]").length || 0, 0);
    assert.equal(releases, 1);
    blocked = false;
    poll(1, navigation);
    assert.equal(releases, 2);
    assert.equal(applied, prior + Number(navigation));
    assert.equal(row.querySelectorAll("[data-floggfy-saved-column]").length, 0);
    window.close();
}
