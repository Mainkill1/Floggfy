const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
const output=[];let getters=0,requests=0;
const forbidden=()=>{requests++;throw Error('unexpected request');};
const state={item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'200000'}},
 isPaused:true,isBuffering:false,isActiveDevice:false,playbackId:'PRIVATE_PLAYBACK_ID',device:{id:'PRIVATE_DEVICE_ID'},
 sessionId:'PRIVATE_SESSION_ID',error:{code:42,status:503,body:'PRIVATE_RESPONSE_BODY'}};
const player={getState:()=>state};
const context={window:{Spicetify:{Player:player},__floggfyConfig:{metadata:false,debug:false}},
 document:{querySelector:()=>null},performance,console:{info:x=>output.push(x)},encodeURIComponent,
 fetch:forbidden,XMLHttpRequest:forbidden,WebSocket:forbidden};
vm.runInNewContext(script,context);
const poll=()=>context.window.__floggfyPoll();
const observations=()=>output.filter(x=>x.startsWith('FLOGGFY_STATUS:client obs '));
const last=()=>observations().at(-1);
assert.equal(observations().length,0);
context.window.__floggfyConfig.debug='true';poll();assert.equal(observations().length,0);
context.window.__floggfyConfig.debug=true;poll();
assert.equal(observations().length,1);
assert(last().includes('paused=1 buf=0 active=0 code=42 status=503 inst=0 dev=0'));
poll();poll();assert.equal(observations().length,1); // Paused ticks are silent.
state.isBuffering=true;poll();assert.equal(observations().length,2);assert(last().includes('buf=1'));
state.playbackId='PRIVATE_NEXT_PLAYBACK';poll();assert(last().includes('inst=1 dev=0'));
let count=observations().length;poll();assert.equal(observations().length,count);
state.device.id='PRIVATE_NEXT_DEVICE';poll();assert(last().includes('inst=0 dev=1'));
state.isPaused=false;poll();assert(last().includes('paused=0'));
// All newly inspected fields use own data descriptors, including config.debug.
const trap=(o,key)=>Object.defineProperty(o,key,{configurable:true,enumerable:true,get(){getters++;throw Error('getter forbidden');}});
for(const key of ['isPaused','isBuffering','isActiveDevice','playbackId','error','errorCode','errorStatus','sessionId'])trap(state,key);
trap(state.device,'id');poll();
assert(last().includes('paused=? buf=? active=? code=? status=? inst=? dev=?'));
count=observations().length;poll();assert.equal(observations().length,count);
trap(context.window.__floggfyConfig,'debug');poll();assert.equal(observations().length,count);
Object.defineProperty(context.window.__floggfyConfig,'debug',{value:true,writable:true,configurable:true});poll();
assert.equal(observations().length,++count); // Debug-off resets baseline.
state.item=undefined;poll();assert.equal(observations().length,count);
state.item={uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'200000'}};
poll();assert.equal(observations().length,++count); // Missing item resets baseline.
// Numeric strings, oversized values, inherited flags and arbitrary error text
// are unavailable observations; they never become output through coercion.
for(const key of ['isPaused','isBuffering','isActiveDevice','playbackId','error','errorCode','errorStatus'])delete state[key];
Object.setPrototypeOf(state,{isPaused:true,isActiveDevice:true});
state.error={code:'PRIVATE_ERROR',status:Infinity};poll();
assert(last().includes('paused=?')&&last().includes('active=?')&&last().includes('code=? status=?'));
state.error={code:-99999,status:-99999};poll();
assert(last().includes('code=-99999 status=-99999'));
trap(state.error,'code');trap(state.error,'status');poll();
assert(last().includes('code=? status=?'));
assert(observations().every(x=>x.length<100));
assert(!observations().some(x=>/PRIVATE_|session|token|response|remote|network/i.test(x)));
assert(output.some(x=>x.startsWith('FLOGGFY_PLAYBACK_V1:')&&x.includes('title=Song')));
assert(!output.some(x=>x.startsWith('FLOGGFY_METADATA_V1:'))); // Metadata=0 remains respected.
assert.equal(getters,0);assert.equal(requests,0);
console.log('PASS: debug-only bounded raw observations, private change flags, deduplication and guarded fields');
