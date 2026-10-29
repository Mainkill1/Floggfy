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
state.isBuffering=true;listeners.get('update')({data:state});assert.equal(last().get('blocked'),'buffering');
state.isBuffering=false;listeners.get('update')({data:state});assert.equal(last().get('position'),'6');
now+=500;state.isPaused=true;state.positionAsOfTimestamp=7000;state.timestamp=now-10000;
context.window.__floggfyPoll();assert.equal(last().get('position'),'7');assert.equal(last().get('paused'),'1');
local=false;now+=500;context.window.__floggfyPoll();assert.equal(last().get('blocked'),'remote-playback');
local=true;state.isPaused=false;state.isBuffering=true;now+=500;context.window.__floggfyPoll();assert.equal(last().get('blocked'),'buffering');
state.item=undefined;now+=500;context.window.__floggfyPoll();assert.equal(last().get('blocked'),'buffering');
state.item={uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'180000'}};
state.isBuffering=false;local=true;state.timestamp=now+10000;now+=500;context.window.__floggfyPoll();assert.equal(last().get('unavailable'),'timeline-fields');
Object.defineProperty(state,'positionAsOfTimestamp',{get(){getters++;throw Error('getter forbidden');},configurable:true});
now+=500;context.window.__floggfyPoll();assert.equal(getters,0);
assert(!output.some(x=>x.startsWith('FLOGGFY_METADATA_V1:')));assert.equal(requests,0);
console.log('PASS: client timeline, URI, pause/buffering/local flags, bounded clocks and Metadata=0 without getters or requests');
