const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
const uri='spotify:track:0000000000000000000001',output=[];
const state={item:{uri,metadata:{title:'Song',artist_name:'Lead','artist_name:1':'Guest',album_artist_name:'Album Lead',album_title:'Album',duration:'200000'}}};
const query={queryKey:['track',uri],state:{data:{artists:[{name:'Lead'},{name:'Guest'}],album:{artists:[{name:'Album Lead'},{name:'Guest'}]}}}};
const player={getState:()=>state,getEvents:()=>({})};
const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:player}]])};
const main={__reactContainer$test:{memoizedProps:{value:{query,registry}}}};
const context={window:{__floggfyNative:true},document:{querySelector:()=>main},performance,console:{info:x=>output.push(x)},setInterval:()=>{},encodeURIComponent};
vm.runInNewContext(script,context);
const fields=()=>Object.fromEntries(output.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).at(-1).split(':').slice(1).join(':').split('&').map(x=>x.split('=').map(decodeURIComponent)));
for(let i=0;i<8;i++)context.window.__floggfyPoll();
assert.equal(fields().ALBUMARTIST,'Album Lead');
assert.equal(fields().ARTIST,'Lead; Guest');
// More cached names never outrank the current explicit album artist.
query.state.data.album.artists.push({name:'Another guest'});context.window.__floggfyPoll();
assert.equal(fields().ALBUMARTIST,'Album Lead');
// Multiple explicitly identified album artists remain supported, not truncated.
state.item.metadata['album_artist_name:1']='Co-Album Lead';context.window.__floggfyPoll();
assert.equal(fields().ALBUMARTIST,'Album Lead; Co-Album Lead');
// Without an explicit current album artist, use actual album metadata, not
// the track-level artists. A later current value takes precedence immediately.
delete state.item.metadata.album_artist_name;delete state.item.metadata['album_artist_name:1'];
query.state.data.album.artists=[{name:'Cached Album Lead'}];context.window.__floggfyPoll();
assert.equal(fields().ALBUMARTIST,'Cached Album Lead');
assert.equal(fields().ARTIST,'Lead; Guest');
state.item.metadata.album_artist_name='Album Lead';context.window.__floggfyPoll();
assert.equal(fields().ALBUMARTIST,'Album Lead');
console.log('PASS: explicit album artist beats longer cached credits; track guests stay in ARTIST; genuine multiple album artists remain supported');
