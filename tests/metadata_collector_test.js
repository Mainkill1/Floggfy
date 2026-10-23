const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
const uri='spotify:track:0000000000000000000001';
let requests=0,resolutions=0,getters=0;
const forbidden=()=>{requests++;throw Error('network forbidden');};
const state={item:{uri,metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'200000'}}};
const player={getState:()=>state,getEvents:()=>({})};
const query={queryKey:['cached-track',uri],state:{data:{release_date:'2026-09-30',genres:['Rock'],lyrics:{lines:[{words:'one'},{words:'two é'}]}}}};
const wrong={uri:'spotify:track:0000000000000000000002',metadata:{genre:'WRONG'}};
const registry={_map:new Map([[Symbol.for('PlayerAPI'),{instance:player}]]),resolve:()=>{resolutions++;throw Error('no service construction');}};
const value={registry,query,wrong};
Object.defineProperty(value,'dangerous',{enumerable:true,get(){getters++;forbidden();}});
const main={__reactContainer$test:{memoizedProps:{value}}};
const output=[];let elapsed=0;const clock={now:()=>performance.now()+elapsed};
const context={window:{__floggfyNative:true,CosmosAsync:{get:forbidden}},document:{querySelector:()=>main},console:{info:x=>output.push(x)},performance:clock,fetch:forbidden,XMLHttpRequest:forbidden,WebSocket:forbidden,setInterval:()=>{},encodeURIComponent};
vm.runInNewContext(script,context);
for(let i=0;i<20;i++)context.window.__floggfyPoll();
assert(output.some(x=>x.includes('DATE=2026-09-30')&&x.includes('GENRE=Rock')&&x.includes('LYRICS=one%0Atwo%20%C3%A9')));
assert(!output.some(x=>x.includes('WRONG')));
const currentMessages=()=>output.filter(x=>x.startsWith('FLOGGFY_PLAYBACK_V1:'));
assert(currentMessages().some(x=>x.includes('title=Song')));
const count=output.length;context.window.__floggfyPoll();assert.equal(output.length,count);
elapsed=16000;context.window.__floggfyPoll();assert.equal(output.length,count+2);
query.state.data.lyrics.lines.push({words:'cached update'});
context.window.__floggfyPoll();assert(output.at(-1).includes('cached%20update'));
state.item={uri:'spotify:track:0000000000000000000003',metadata:{title:'Other',artist_name:'Artist',album_title:'Album',duration:'100000'}};
context.window.__floggfyPoll();assert(currentMessages().at(-1).includes('title=Other'));assert(!output.at(-1).includes('DATE=')&&!output.at(-1).includes('LYRICS='));
state.item.metadata.year='2024';state.item.metadata.genre='Jazz';
context.window.__floggfyPoll();assert(output.at(-1).includes('YEAR=2024')&&output.at(-1).includes('GENRE=Jazz'));
const trap=o=>{Object.defineProperty(o,'month',{enumerable:true,get(){getters++;forbidden();}});return o;};
state.playbackQuality={bitrateLevel:'VERY_HIGH',targetBitrateLevel:'LOSSLESS'};
context.window.__floggfyPoll();assert(output.at(-1).includes('playback_quality=very_high'));
state.playbackQuality.bitrateLevel='LOSSLESS';context.window.__floggfyPoll();
assert(output.at(-1).includes('playback_quality=lossless'));
state.playbackQuality.bitrateLevel=99;context.window.__floggfyPoll();
assert(!output.at(-1).includes('playback_quality=')); // Unknown enum, never use target quality.
for(const [i,level] of ['low','normal','high','very_high','lossless','lossless_24'].entries()){
 state.playbackQuality.bitrateLevel=i+1;context.window.__floggfyPoll();
 assert(output.at(-1).includes('playback_quality='+level));
}
state.playbackQuality.bitrateLevel='HIFI24';context.window.__floggfyPoll();
assert(output.at(-1).includes('playback_quality=lossless_24'));
state.playbackQuality=undefined;state.playback_quality={bitrate_level:'HIGH'};
context.window.__floggfyPoll();assert(output.at(-1).includes('playback_quality=high'));
const qualityTrap={};Object.defineProperty(qualityTrap,'bitrate_level',{get(){getters++;forbidden();}});
state.playback_quality=qualityTrap;context.window.__floggfyPoll();
assert(!output.at(-1).includes('playback_quality='));
state.item.metadata.year=undefined;state.item.album={date:trap({year:2026})};
Object.defineProperty(state.item,'lyrics',{enumerable:true,get(){getters++;forbidden();}});
context.window.__floggfyPoll();assert(output.at(-1).includes('DATE=2026'));
const lyric={};Object.defineProperty(lyric,'lines',{enumerable:true,get(){getters++;forbidden();}});
const fresh={uri:'spotify:track:0000000000000000000004',metadata:{title:'Fresh',artist_name:'Artist',album_title:'Album',duration:'100000',lyrics:lyric}};
state.item=fresh;context.window.__floggfyPoll();assert(!output.at(-1).includes('LYRICS='));
fresh.metadata['artist_name:1']='Guest';
fresh.metadata['album_artist_name:1']='Guest';fresh.metadata.album_artist_name='Artist';
fresh.metadata.album_label='Example Records';fresh.metadata.publisher='Example Publishing';
fresh.metadata.album_track_count='0';
fresh.album={releaseDate:{isoString:'2024-05-06T00:00:00Z'}};
context.window.__floggfyPoll();
assert(output.at(-1).includes('ARTIST=Artist%3B%20Guest'));
assert(output.at(-1).includes('ALBUMARTIST=Artist%3B%20Guest'));
assert(output.at(-1).includes('DATE=2024-05-06'));
assert(output.at(-1).includes('LABEL=Example%20Records'));
assert(output.at(-1).includes('PUBLISHER=Example%20Publishing'));
assert(!output.at(-1).includes('TRACKTOTAL=0'));
fresh.metadata.year='2024';context.window.__floggfyPoll();
assert(output.at(-1).includes('DATE=2024-05-06')); // Do not replace a full date with a coarse year.
const indexed=[];Object.defineProperty(indexed,'0',{get(){getters++;forbidden();}});fresh.metadata.genres=indexed;
context.window.__floggfyPoll();
// The current-item footer includes episodes, even without music album tags.
const musicMessages=output.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).length;
state.item={uri:'spotify:episode:0000000000000000000005',name:'Episode',duration:{milliseconds:4030000},metadata:{show_name:'Show'}};
state.playbackQuality={bitrateLevel:'HIGH'};state.playback_quality=undefined;
context.window.__floggfyPoll();
assert(currentMessages().at(-1).includes('title=Episode'));
assert(currentMessages().at(-1).includes('uri=spotify%3Aepisode%3A'));
assert(currentMessages().at(-1).includes('playback_quality=high'));
state.playbackQuality={bitrateLevel:0,targetBitrateLevel:5,targetBitrateAvailable:false};
context.window.__floggfyPoll();
assert(currentMessages().at(-1).includes('title=Episode'));
assert(!currentMessages().at(-1).includes('playback_quality='));
assert.equal(output.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).length,musicMessages);
Object.defineProperty(state,'item',{get(){getters++;forbidden();},configurable:true});context.window.__floggfyPoll();
assert.equal(requests,0);assert.equal(resolutions,0);assert.equal(getters,0);
assert(!/AuthorizationAPI|registry\.resolve\(|\.fetchQuery\(|_accessToken/.test(script));
console.log('PASS: cached metadata, exact track association, cache updates, missing fields, zero requests/service resolutions/accessor reads');

// Current Spotify normalizes lyrics to data.lyrics [{start,end,text}].
const normalizedOut=[];
const normalizedContext={...context,window:{Spicetify:{Player:player}},console:{info:x=>normalizedOut.push(x)}};
Object.defineProperty(state,'item',{value:{uri,metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'200000'}},configurable:true,writable:true});
value.query={trackUri:uri,data:{lyrics:[{start:0,end:10,text:'normalized one'},{start:10,end:20,text:'normalized two é 音'}]}};
vm.runInNewContext(script,normalizedContext);
assert(normalizedOut.some(x=>x.includes('LYRICS=normalized%20one%0Anormalized%20two%20%C3%A9%20%E9%9F%B3')));

async function automatic(){
 const calls=[],messages=[];let now=0,timers=0,pending=[];
 const localState={item:{uri,metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'200000',image_url:'spotify:image:small'}}};
 const localPlayer={getState:()=>localState,getEvents:()=>({})};
 const builder={build(){let host,path;return {withHost(x){host=x;return this},withPath(x){path=x;return this},withQueryParameters(){return this},send(){calls.push(host+path);return new Promise((resolve,reject)=>pending.push({resolve,reject,path}));}}}};
 const services={_map:new Map([[Symbol('PlayerAPI'),{instance:localPlayer}],[Symbol('RequestBuilder'),{instance:builder}]])};
 const c={...context,window:{__floggfyConfig:{metadata:true,lyrics:true}},document:{querySelector:()=>({__reactContainer$test:{memoizedProps:{registry:services}}})},performance:{now:()=>now},console:{info:x=>messages.push(x)},setInterval:()=>{timers++;}};
 vm.runInNewContext(script,c);for(let i=0;i<10;i++)c.window.__floggfyPoll();
 assert.equal(timers,0,'native polling must be the sole timer');
 assert.equal(calls.length,2,'one metadata and one lyrics request while panel is closed');
 assert(calls.some(x=>x.endsWith('/metadata/4/track/00000000000000000000000000000001')));
 const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();c.window.__floggfyPoll();};
 pending.find(x=>x.path.includes('metadata')).resolve({body:{artist:[{name:'Artist'},{name:'Guest'}],album:{gid:'c'.repeat(32),artist:[{name:'Album Artist'}],label:'Label',date:{year:2025,month:2,day:3},cover_group:{image:[{file_id:'a'.repeat(40),width:64,height:64},{file_id:'b'.repeat(40),width:640,height:640}]}},number:4,disc_number:2,external_id:[{type:'isrc',id:'TESTISRC'}],language_of_performance:['en']}});
 pending.find(x=>x.path.includes('color-lyrics')).resolve({body:{lyrics:{lines:[{words:'automatic one'},{words:'automatic two'}]}}});
 await flush();
 assert.equal(calls.length,3,'full album details fetched once');
 pending[2].resolve({body:{gid:'c'.repeat(32),disc:[{number:1,track:[{},{}]},{number:2,track:[{}]}],copyright:[{type:'C',text:'Synthetic Copyright'},{type:'P',text:'Synthetic Recording'}]}});await flush();
 let last=messages.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).at(-1);
 for(const part of ['LYRICS=automatic%20one%0Aautomatic%20two','DATE=2025-02-03','ARTIST=Artist%3B%20Guest','ISRC=TESTISRC','TRACKNUMBER=4','DISCNUMBER=2','LANGUAGE=en','DISCTOTAL=2','TRACKTOTAL=3','COPYRIGHT=Synthetic%20Copyright','PHONOGRAMCOPYRIGHT=Synthetic%20Recording','artwork_url=https%3A%2F%2Fi.scdn.co%2Fimage%2F'+ 'b'.repeat(40)])assert(last.includes(part),part);
 c.window.__floggfyConfig.lyrics=false;await flush();last=messages.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).at(-1);assert(!last.includes('LYRICS='));
 c.window.__floggfyConfig.lyrics=true;await flush();assert.equal(calls.length,3,'reuse completed cache');
 // Switching tracks before replies must never attach old lyrics or art.
 localState.item={uri:'spotify:track:0000000000000000000002',metadata:{title:'Two',artist_name:'Artist',album_title:'Album',duration:'200000'}};
 c.window.__floggfyPoll();assert.equal(calls.length,5);
 localState.item={uri:'spotify:track:0000000000000000000003',metadata:{title:'Three',artist_name:'Artist',album_title:'Album',duration:'200000'}};
 c.window.__floggfyPoll();pending[3].resolve({body:{album:{label:'STALE'}}});pending[4].resolve({body:{lyrics:{lines:[{words:'STALE'}]}}});await flush();
 assert(!messages.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).at(-1).includes('STALE'));
 // Failure is bounded to one delayed retry, never a per-poll request storm.
 pending[5].reject({status:500});pending[6].reject({status:404});await flush();assert.equal(calls.length,7);
 now=6000;await flush();assert.equal(calls.length,8,'404 is terminal, metadata retries once');pending[7].reject({status:500});await flush();now=60000;await flush();assert.equal(calls.length,8);
 c.window.__floggfyConfig.metadata=false;localState.item.uri='spotify:track:0000000000000000000004';await flush();assert.equal(calls.length,8);
 console.log('PASS: automatic authenticated metadata/lyrics, bounded requests/retries, toggles, track isolation and artwork');
}
async function disabledAndTimeout(){
 let now=0;const calls=[],messages=[],config={metadata:false,lyrics:false};
 const local={item:{uri,metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'200000'}}};
 const localPlayer={getState:()=>local,getEvents:()=>({})};
 const builder={build(){let path,signal;return {withHost(){return this},withPath(p){path=p;return this},withQueryParameters(){return this},withAbortSignal(s){signal=s;return this},send(){calls.push({path,signal});return new Promise(()=>{});}}}};
 const services={_map:new Map([[Symbol('PlayerAPI'),{instance:localPlayer}],[Symbol('RequestBuilder'),{instance:builder}]])};
 const main={__reactContainer$test:{memoizedProps:{registry:services}}};
 const c={...context,window:{__floggfyConfig:config},document:{querySelector:()=>main},performance:{now:()=>now},console:{info:x=>messages.push(x)},AbortController};
 vm.runInNewContext(script,c);c.window.__floggfyPoll();assert.equal(calls.length,0,'disabled before first tick');
 config.metadata=true;c.window.__floggfyPoll();assert.equal(calls.length,1,'Lyrics=0 permits metadata only');
 now=12001;c.window.__floggfyPoll();assert(calls[0].signal.aborted,'timeout aborts transport');assert.equal(calls.length,1);
 now=18000;c.window.__floggfyPoll();assert.equal(calls.length,2);
 config.lyrics=true;c.window.__floggfyPoll();assert.equal(calls.length,3);
 config.lyrics=false;c.window.__floggfyPoll();assert(calls[2].signal.aborted,'live switch cancels lyrics');
 now=40000;c.window.__floggfyPoll();assert.equal(calls.length,3,'timed out metadata retries are capped');
 config.metadata=false;c.window.__floggfyPoll();assert(!messages.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).at(-1).includes('LYRICS='));
 console.log('PASS: disabled startup, metadata-only mode, request timeout and live cancellation');
}
async function physicalTransportBound(){
 for(const supportsAbort of [false,true]){
  let now=0;const sends=[],config={metadata:true,lyrics:true};
  const local={item:{uri,metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'200000'}}};
  const player={getState:()=>local,getEvents:()=>({})};
  const builder={build(){const request={withHost(){return this},withPath(){return this},withQueryParameters(){return this},send(){return new Promise(resolve=>sends.push(resolve));}};if(supportsAbort)request.withAbortSignal=function(){return this;};return request;}};
  const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:player}],[Symbol('RequestBuilder'),{instance:builder}]])};
  const main={__reactContainer$test:{memoizedProps:{registry}}};
  const c={...context,window:{__floggfyConfig:config},document:{querySelector:()=>main},performance:{now:()=>now},console:{info:()=>{}},AbortController};
  vm.runInNewContext(script,c);
  for(let i=2;i<85;i++){
   now+=13000;local.item.uri='spotify:track:'+String(i).padStart(22,'0');
   config.metadata=false;c.window.__floggfyPoll();config.metadata=true;
   config.lyrics=i%2===0;c.window.__floggfyPoll();
  }
  assert.equal(sends.length,4,'unsettled sends retain physical slots through timeouts, settings and cache evictions; abort support='+supportsAbort);
  sends[0]({body:{lyrics:{lines:[{words:'stale reply'}]}}});for(let i=0;i<8;i++)await Promise.resolve();
  c.window.__floggfyPoll();assert.equal(sends.length,5,'settled stale reply releases exactly one physical slot');
  now+=13000;c.window.__floggfyPoll();assert.equal(sends.length,5,'new timeout cannot release a live physical slot');
 }
 console.log('PASS: physical request bound survives uncancellable/ignored-abort transports, eviction, toggles and stale completion');
}
(async()=>{await automatic();await disabledAndTimeout();await physicalTransportBound();})().catch(e=>{console.error(e);process.exitCode=1;});
