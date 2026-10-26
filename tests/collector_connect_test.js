// One VM gate for guarded cached Connect observations and passive local actions.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
let network=0,getters=0,added=0,removed=0;
const output=[],listeners=new Map();
const forbidden=()=>{network++;throw Error('unexpected network/control request');};
const events={addListener(type,fn){assert.equal(type,'action');assert(!listeners.has(fn));listeners.set(fn,type);added++;},
 removeListener(type,fn){assert.equal(type,'action');assert(listeners.delete(fn));removed++;}};
const playerState={isPaused:false,item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'100000'}}};
const player={getState:()=>playerState,getEvents:()=>events,pause:forbidden,resume:forbidden};
let connectState={connectionStatus:'NOT_CONNECTED',activeDevice:{id:'PRIVATE_LOCAL_ID',name:'PRIVATE_PC_NAME',isLocal:true},devices:[]};
const connect={getConnectState:()=>connectState,transferPlayback:forbidden,startActiveDiscovery:forbidden};
// Discover through the same already-created registry used on unmodified clients.
const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:player}],[Symbol('ConnectDevicesAPI'),{instance:connect}]])};
const main={__reactContainer$test:{memoizedProps:{registry}}};
const config={metadata:false,debug:false,connect:true};
const context={window:{__floggfyConfig:config},document:{querySelector:()=>main},performance,
 console:{info:x=>output.push(x)},encodeURIComponent,fetch:forbidden,WebSocket:forbidden,XMLHttpRequest:forbidden};
vm.runInNewContext(script,context);
const poll=()=>context.window.__floggfyPoll();
const relevant=()=>output.filter(x=>/^FLOGGFY_STATUS:(connect |control )/.test(x));
const last=()=>relevant().at(-1);
const has=s=>relevant().some(x=>x.includes(s));
const action=a=>{for(const fn of listeners.keys())assert.equal(fn({data:{action:a}}),undefined);};
assert.equal(relevant().length,0);assert.equal(added,0);
config.debug=true;poll();assert.equal(added,1);assert(has('target=local')&&has('initiator=unknown'));
let count=relevant().length;poll();poll();assert.equal(relevant().length,count);assert.equal(added,1);
playerState.isPaused=true;poll();assert(has('state=paused target=local initiator=unknown'));
count=relevant().length;for(let i=0;i<12;i++)poll();assert.equal(relevant().length,count);
// Local requests are observations only; their callback never prevents the action.
action('pause');assert.equal(relevant().length,count);poll();assert(has('request=pause source=local-client'));
connectState={connectionStatus:'CONNECTED',activeDevice:{id:'PRIVATE_REMOTE_ID',isLocal:false,name:'PRIVATE_REMOTE_NAME'},devices:[]};
poll();assert(has('target=remote device_changed=1 controller=unknown'));
playerState.isPaused=false;poll();assert(has('state=playing target=remote initiator=unknown'));
assert(!relevant().some(x=>x.includes('source=remote')||x.includes('initiator=remote')));
for(let i=0;i<40;i++)action('skipToNext');poll();assert(has('observations_omitted='));
const trap=(o,k)=>Object.defineProperty(o,k,{configurable:true,get(){getters++;throw Error('getter forbidden');}});
trap(connectState.activeDevice,'id');trap(connectState.activeDevice,'isLocal');trap(connectState,'connectionStatus');poll();
assert(has('state=unknown target=unknown'));
action('PRIVATE_ACTION');poll();assert(!relevant().some(x=>x.includes('PRIVATE_ACTION')));
config.connect=false;poll();assert.equal(removed,1);assert.equal(listeners.size,0);
count=relevant().length;action('pause');poll();assert.equal(relevant().length,count);
config.connect=true;poll();assert.equal(added,2);
// Optional methods may disappear or throw without breaking the existing footer.
connect.getConnectState=()=>{throw Error('changed API');};poll();assert(has('state=unknown target=unknown'));
const data={};trap(data,'action');for(const fn of listeners.keys())fn({data});poll();
trap(config,'connect');poll();assert.equal(listeners.size,0);
// If an optional API refuses cleanup, retain one inert binding, never stack
// listeners or let that old callback participate in command dispatch.
Object.defineProperty(config,'connect',{value:true,writable:true,configurable:true});
events.removeListener=()=>{throw Error('changed cleanup API');};poll();
assert.equal(listeners.size,1);const registrations=added;
config.connect=false;poll();config.connect=true;poll();
assert.equal(added,registrations);count=relevant().length;
action('pause');poll();assert.equal(relevant().length,count);assert.equal(listeners.size,1);
assert.equal(getters,0);assert.equal(network,0);
assert(relevant().every(x=>x.length<100));
assert(!relevant().some(x=>/PRIVATE_|session|token|response/i.test(x)));
assert(output.some(x=>x.startsWith('FLOGGFY_PLAYBACK_V1:')&&x.includes('title=Song')));
assert(!output.some(x=>x.startsWith('FLOGGFY_METADATA_V1:')));
console.log('PASS: cached playback target, honest unknown controller, passive local requests, paused dedup, bounded queue, opt-out cleanup and privacy');
