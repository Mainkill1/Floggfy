const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
let now=1793300000000,local=true,getters=0,requests=0;
const state={timestamp:now-5000,positionAsOfTimestamp:1000,duration:180000,isPaused:false,isBuffering:false,speed:1,
 item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'180000'}}};
const listeners=new Map();const events={addListener:(type,callback)=>listeners.set(type,callback),removeListener:type=>listeners.delete(type)};
const player={getState:()=>state,getEvents:()=>events};
const connect={getConnectState:()=>({activeDevice:{isLocal:local}})};
const output=[];
const context={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:player,ConnectDevicesAPI:connect}}},
 document:{querySelector:()=>null},console:{info:x=>output.push(x)},performance:{now:()=>now},Date:{now:()=>now},
 setInterval:()=>{},encodeURIComponent,fetch:()=>{requests++;throw Error('network forbidden');}};
vm.runInNewContext(script,context);
const last=()=>{const value=output.filter(x=>x.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1);assert(value,'client timeline must be emitted without Windows/metadata');return new URLSearchParams(value.slice('FLOGGFY_TIMELINE_V1:'.length));};
assert.equal(last().get('position'),'6');assert.equal(last().get('paused'),'0');assert.equal(last().get('local'),'1');
assert.equal(last().get('uri'),'spotify:track:0000000000000000000001');
assert.equal(typeof listeners.get('update'),'function','app updates must preserve fast transitions between polls');
assert.equal(typeof listeners.get('action'),'function','local skip evidence must be observed even when debug logging is disabled');
listeners.get('action')({data:{action:'skipToNext'}});
state.isBuffering=true;listeners.get('update')({data:state});assert.equal(last().get('blocked'),'buffering');
assert.equal(last().get('manual'),'1','the buffering transition immediately following a local skip is disqualified');
state.isBuffering=false;listeners.get('update')({data:state});assert.equal(last().get('position'),'6');
now+=500;state.isPaused=true;state.positionAsOfTimestamp=7000;state.timestamp=now-10000;
context.window.__floggfyPoll();assert.equal(last().get('position'),'7');assert.equal(last().get('paused'),'1');
local=false;now+=500;context.window.__floggfyPoll();assert.equal(last().get('blocked'),'remote-playback');
local=true;state.isPaused=false;state.isBuffering=true;now+=500;context.window.__floggfyPoll();assert.equal(last().get('blocked'),'buffering');
state.item=undefined;now+=500;context.window.__floggfyPoll();assert.equal(last().get('blocked'),'buffering');
state.item={uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'180000'}};
state.isBuffering=false;local=true;state.timestamp=now+10000;now+=500;context.window.__floggfyPoll();assert.equal(last().get('unavailable'),'timeline-fields');
state.timestamp=now;state.positionAsOfTimestamp=1000;state.speed=4;now+=500;context.window.__floggfyPoll();
assert.equal(last().get('speed'),'4');assert.equal(last().get('position'),'3','reported playback speed advances the bounded timeline');
state.speed=3;now+=500;context.window.__floggfyPoll();assert.equal(last().get('unavailable'),'timeline-fields');
state.speed=1;
state.item={uri:'',metadata:{title:'Unidentified audio'}};state.isPaused=false;state.isBuffering=false;
state.timestamp=now;state.positionAsOfTimestamp=1000;now+=500;context.window.__floggfyPoll();
assert.equal(last().get('unavailable'),'track-unavailable');
assert.equal(last().get('active'),'1','local unpaused URI-less audio must be marked as an active unidentified item');
state.isPaused=true;state.timestamp=now;now+=500;context.window.__floggfyPoll();
assert.equal(last().get('unavailable'),'track-unavailable');
assert.equal(last().get('active'),null,'paused URI-less media is unavailable, not an active interstitial');
state.item={uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'180000'}};
state.isPaused=false;state.timestamp=now;
Object.defineProperty(state,'positionAsOfTimestamp',{get(){getters++;throw Error('getter forbidden');},configurable:true});
now+=500;context.window.__floggfyPoll();assert.equal(getters,0);
assert(!output.some(x=>x.startsWith('FLOGGFY_METADATA_V1:')));assert.equal(requests,0);
console.log('PASS: client timeline, URI, pause/buffering/local flags, bounded clocks and Metadata=0 without getters or requests');
{
 const coldState={timestamp:now,positionAsOfTimestamp:0,duration:30000,isPaused:false,isBuffering:false,
  playbackId:'unknown-active',item:{uri:'',metadata:{title:'Unidentified audio'}}};
 const coldMessages=[];
 const coldPlayer={getState:()=>coldState,getEvents:()=>({addListener(){},removeListener(){}})};
 const cold={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:coldPlayer,
  ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}}}},document:{querySelector:()=>null},
  console:{info:value=>coldMessages.push(value)},performance:{now:()=>now},Date:{now:()=>now},
  setInterval:()=>{},encodeURIComponent};
 vm.runInNewContext(script,cold);
 const marker=coldMessages.find(value=>value.startsWith('FLOGGFY_TIMELINE_V1:'));
 assert(marker,'cold-start URI-less active playback must emit an unidentified marker');
 const fields=new URLSearchParams(marker.slice('FLOGGFY_TIMELINE_V1:'.length));
 assert.equal(fields.get('unavailable'),'track-unavailable');assert.equal(fields.get('active'),'1');
}
console.log('PASS: cold-start URI-less active playback is marked without prior track state');
// Producer evidence must remain unknown if action registration is absent or
// failed; only an observed instance transition during local playing qualifies.
for(const mode of ['observed','blank','readyblank','reused','missing','throws','manual','seek','paused','same','unavailable','replacement']) {
 let clock=1793300000000;
 let current={timestamp:clock,positionAsOfTimestamp:4000,duration:16000,isPaused:mode==='paused',isBuffering:false,
  playbackId:mode==='readyblank'?'':'one',item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album'}}};
 const handlers=new Map(),messages=[];
 const api={getState:()=>current};
 if(mode!=='missing')api.getEvents=()=>({addListener(type,fn){if(type==='action'&&mode==='throws')throw Error('registration failed');handlers.set(type,fn);},removeListener:type=>handlers.delete(type)});
 const sandbox={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:api,ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}}}},
  document:{querySelector:()=>null},console:{info:line=>messages.push(line)},performance:{now:()=>clock},Date:{now:()=>clock},setInterval:()=>{},encodeURIComponent};
 vm.runInNewContext(script,sandbox);sandbox.window.__floggfyPoll();
 if(mode==='manual')handlers.get('action')({data:{action:'skipToNext'}});
 if(mode==='seek')handlers.get('action')({data:{action:'seekTo'}});
 if(mode==='unavailable'){
  current={...current,item:undefined};sandbox.window.__floggfyPoll();
  current={...current,item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album'}}};
 }
 if(mode==='replacement')sandbox.window.Spicetify.Platform.PlayerAPI={...api};
 clock+=mode==='seek'?15000:mode==='manual'?20000:mode==='observed'?30000:500;
 current={...current,timestamp:clock,isBuffering:true,playbackId:mode==='same'||mode==='reused'?'one':mode==='blank'?'':'two',
  item:{...current.item,uri:mode==='same'?current.item.uri:'spotify:track:0000000000000000000002'}};
 sandbox.window.__floggfyPoll();
 const message=messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1);
 const fields=new URLSearchParams(message.slice('FLOGGFY_TIMELINE_V1:'.length));
 assert.equal(fields.get('natural'),mode==='observed'?'1':null,mode);
 assert.equal(fields.get('pending'),mode==='blank'||mode==='readyblank'||mode==='reused'?'1':null,mode);
 assert.equal(fields.get('controlled'),mode==='manual'||mode==='seek'?'1':null,mode);
 assert.equal(fields.get('monitoring'),mode==='missing'||mode==='throws'?'0':'1',mode);
}
console.log('PASS: natural transition evidence requires successful action monitoring, playing state and changed instance');

// An acknowledged pause belongs to the current playback instance. It must not
// survive resume and poison the natural transition at the end of the track.
{
 let clock=1793300000000;
 let current={timestamp:clock,positionAsOfTimestamp:4000,duration:180000,isPaused:false,isBuffering:false,
  playbackId:'pause-track',item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album'}}};
 const handlers=new Map(),messages=[];
 const api={getState:()=>current,getEvents:()=>({addListener:(type,fn)=>handlers.set(type,fn),removeListener:type=>handlers.delete(type)})};
 const sandbox={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:api,ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}}}},
  document:{querySelector:()=>null},console:{info:line=>messages.push(line)},performance:{now:()=>clock},Date:{now:()=>clock},setInterval:()=>{},encodeURIComponent};
 vm.runInNewContext(script,sandbox);sandbox.window.__floggfyPoll();
 handlers.get('action')({data:{action:'pause'}});
 clock+=500;current={...current,timestamp:clock,positionAsOfTimestamp:4500,isPaused:true};sandbox.window.__floggfyPoll();
 handlers.get('action')({data:{action:'resume'}});
 clock+=500;current={...current,timestamp:clock,positionAsOfTimestamp:4500,isPaused:false};sandbox.window.__floggfyPoll();
 clock+=177500;current={...current,timestamp:clock,positionAsOfTimestamp:179500};sandbox.window.__floggfyPoll();
 clock+=500;current={...current,timestamp:clock,positionAsOfTimestamp:0,isBuffering:true,playbackId:'next-track',
  item:{...current.item,uri:'spotify:track:0000000000000000000002'}};sandbox.window.__floggfyPoll();
 const message=messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1);
 const fields=new URLSearchParams(message.slice('FLOGGFY_TIMELINE_V1:'.length));
 assert.equal(fields.get('natural'),'1');
 assert.equal(fields.get('controlled'),null,'acknowledged pause must not attach to a later natural transition');
}
console.log('PASS: acknowledged pause/resume does not poison natural completion');

// A control belongs to the playback instance that was current when observed.
// A Ready successor acknowledges it even when no buffering event was emitted.
{
 let clock=1793300000000;
 let current={timestamp:clock,positionAsOfTimestamp:4000,duration:60000,isPaused:false,isBuffering:false,speed:10,
  playbackId:'first',item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'A',artist_name:'Artist',album_title:'Album'}}};
 const handlers=new Map(),messages=[];
 const api={getState:()=>current,getEvents:()=>({addListener:(type,fn)=>handlers.set(type,fn),removeListener:type=>handlers.delete(type)})};
 const sandbox={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:api,ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}}}},
  document:{querySelector:()=>null},console:{info:line=>messages.push(line)},performance:{now:()=>clock},Date:{now:()=>clock},setInterval:()=>{},encodeURIComponent};
 vm.runInNewContext(script,sandbox);sandbox.window.__floggfyPoll();
 handlers.get('action')({data:{action:'skipToNext'}});
 clock+=100;current={...current,timestamp:clock,positionAsOfTimestamp:0,playbackId:'second',
  item:{uri:'spotify:track:0000000000000000000002',metadata:{title:'B',artist_name:'Artist',album_title:'Album'}}};
 sandbox.window.__floggfyPoll();
 const successor=new URLSearchParams(messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1).slice('FLOGGFY_TIMELINE_V1:'.length));
 assert.equal(successor.get('prior_control'),'1','the successor must reject A while remaining independently eligible');
 for(let step=1;step<=12;++step){clock+=500;current={...current,timestamp:clock,positionAsOfTimestamp:step*5000};sandbox.window.__floggfyPoll();}
 current={...current,positionAsOfTimestamp:60000,isBuffering:true,playbackId:'third',
  item:{uri:'spotify:track:0000000000000000000003',metadata:{title:'C',artist_name:'Artist',album_title:'Album'}}};
 sandbox.window.__floggfyPoll();
 const fields=new URLSearchParams(messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1).slice('FLOGGFY_TIMELINE_V1:'.length));
 assert.equal(fields.get('controlled'),null,'A control must not attach to B completion');
 assert.equal(fields.get('manual'),null,'A skip must be retired by the Ready B successor');
 assert.equal(fields.get('natural'),'1','B can provide independent natural-transition evidence');
}
console.log('PASS: a Ready successor retires control evidence from the prior playback instance');

// A control observed after buffering has no prior Ready instance to attach to.
// Carry it on the successor Ready snapshot instead of silently retiring it.
{
 let clock=1793300000000;
 let current={timestamp:clock,positionAsOfTimestamp:59000,duration:60000,isPaused:false,isBuffering:false,
  playbackId:'first',item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'A',artist_name:'Artist',album_title:'Album'}}};
 const handlers=new Map(),messages=[];
 const api={getState:()=>current,getEvents:()=>({addListener:(type,fn)=>handlers.set(type,fn),removeListener:type=>handlers.delete(type)})};
 const sandbox={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:api,ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}}}},
  document:{querySelector:()=>null},console:{info:line=>messages.push(line)},performance:{now:()=>clock},Date:{now:()=>clock},setInterval:()=>{},encodeURIComponent};
 vm.runInNewContext(script,sandbox);sandbox.window.__floggfyPoll();
 clock+=100;current={...current,timestamp:clock,positionAsOfTimestamp:0,isBuffering:true,playbackId:'second',
  item:{uri:'spotify:track:0000000000000000000002',metadata:{title:'B',artist_name:'Artist',album_title:'Album'}}};
 sandbox.window.__floggfyPoll();
 handlers.get('action')({data:{action:'seekTo'}});
 clock+=100;current={...current,timestamp:clock,positionAsOfTimestamp:3000,isBuffering:false};sandbox.window.__floggfyPoll();
 const fields=new URLSearchParams(messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1).slice('FLOGGFY_TIMELINE_V1:'.length));
 assert.equal(fields.get('controlled'),'1','gap control must reach the successor Ready observation');
}
console.log('PASS: a control during the buffering gap invalidates successor recovery');

// Resume acknowledges only pause. It cannot erase a skip, seek, or unknown
// control observed while a natural transition is waiting for its Ready stage.
for(const action of ['skipToNext','seekTo']){
 let clock=1793300000000;
 let current={timestamp:clock,positionAsOfTimestamp:59000,duration:60000,isPaused:false,isBuffering:false,
  playbackId:'first',item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'A',artist_name:'Artist',album_title:'Album'}}};
 const handlers=new Map(),messages=[];
 const api={getState:()=>current,getEvents:()=>({addListener:(type,fn)=>handlers.set(type,fn),removeListener:type=>handlers.delete(type)})};
 const sandbox={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:api,ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}}}},
  document:{querySelector:()=>null},console:{info:line=>messages.push(line)},performance:{now:()=>clock},Date:{now:()=>clock},setInterval:()=>{},encodeURIComponent};
 vm.runInNewContext(script,sandbox);sandbox.window.__floggfyPoll();
 clock+=100;current={...current,timestamp:clock,positionAsOfTimestamp:0,isBuffering:true,playbackId:'',
  item:{uri:'spotify:track:0000000000000000000002',metadata:{title:'B',artist_name:'Artist',album_title:'Album'}}};
 sandbox.window.__floggfyPoll();
 handlers.get('action')({data:{action}});handlers.get('action')({data:{action:'resume'}});
 clock+=100;current={...current,timestamp:clock,isBuffering:false,playbackId:'second'};sandbox.window.__floggfyPoll();
 const fields=new URLSearchParams(messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1).slice('FLOGGFY_TIMELINE_V1:'.length));
 assert.equal(fields.get('controlled'),'1',action+' must survive an unrelated resume');
}
console.log('PASS: resume preserves unacknowledged skip and seek evidence');

// Stock Spotify has no Spicetify global. Resolve its already-created services
// and keep polling even if its optional event API is unavailable.
{
 let current={timestamp:now,positionAsOfTimestamp:0,duration:180000,isPaused:false,isBuffering:false,
  playbackId:'first-play',item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Same title',artist_name:'Same artist',album_title:'Same album'}}};
 let unavailable=false,reads=0;
 const stockPlayer={getState(){reads++;if(unavailable)throw Error('temporary player failure');return current;}};
 const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:stockPlayer}],
  [Symbol('ConnectDevicesAPI'),{instance:{getConnectState:()=>({activeDevice:{isLocal:true}})}}]])};
 const main={'__reactContainer$test':{memoizedProps:{registry}}},messages=[];
 const stock={window:{__floggfyConfig:{metadata:false}},document:{querySelector:selector=>selector==='#main'?main:null},
  console:{info:value=>messages.push(value)},performance:{now:()=>now},Date:{now:()=>now},encodeURIComponent,
  fetch:()=>{throw Error('playback polling must not use network');}};
 const latest=()=>{const message=messages.filter(value=>value.startsWith('FLOGGFY_TIMELINE_V1:')).at(-1);
  assert(message,'stock Spotify must emit its own playback state');return new URLSearchParams(message.slice('FLOGGFY_TIMELINE_V1:'.length));};
 vm.runInNewContext(script,stock);
 assert.equal(latest().get('uri'),current.item.uri);
 assert.equal(latest().get('instance'),'first-play');
 assert.equal(latest().get('local'),'1');
 now+=500;stock.window.__floggfyPoll();
 assert.equal(latest().get('position'),'0.5','polling must advance progress without update events');
 current={...current,timestamp:now,positionAsOfTimestamp:0,playbackId:'second-play',
  item:{...current.item,uri:'spotify:track:0000000000000000000002'}};
 now+=500;stock.window.__floggfyPoll();
 assert.equal(latest().get('uri'),current.item.uri,'identical labels must not hide a different track URI');
 assert.equal(latest().get('instance'),'second-play');
 current={...current,timestamp:now,playbackId:'repeat-play'};
 now+=500;stock.window.__floggfyPoll();
 assert.equal(latest().get('uri'),current.item.uri);
 assert.equal(latest().get('instance'),'repeat-play','a repeated track must expose its new playback instance');
 current={...current,timestamp:now,positionAsOfTimestamp:12345,isPaused:true};
 now+=500;stock.window.__floggfyPoll();
 assert.equal(latest().get('paused'),'1');assert.equal(latest().get('position'),'12.345');
 unavailable=true;now+=500;stock.window.__floggfyPoll();
 assert.equal(latest().get('unavailable'),'player-exception');
 unavailable=false;now+=500;stock.window.__floggfyPoll();
 assert.equal(latest().get('uri'),current.item.uri,'a later successful poll must restore app state');
 assert(reads>=6);
 console.log('PASS: stock Spotify registry polling without Spicetify/events, URI disambiguation, progress, pause and recovery');
}
