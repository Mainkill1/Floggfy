// Run the real renderer collector; replace only Spotify's external player API.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
function fixture({enabled=true,paused=true,local=true,mode='resume',pending=false}={}) {
 let now=1793300000000,settle;
 const calls=[],handlers=new Map();
 const state={timestamp:now,positionAsOfTimestamp:1000,duration:180000,isPaused:paused,isBuffering:false,
  playbackId:'one',item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'Song',artist_name:'Artist',album_title:'Album'}}};
 const api={getState:()=>state,getEvents:()=>({addListener:(k,v)=>handlers.set(k,v),removeListener:k=>handlers.delete(k)})};
 const wrapper={origin:api};
 const invoke=function(){assert.strictEqual(this,mode==='wrapper'?wrapper:api);calls.push(now);
  if(mode==='throws')throw Error('unavailable');
  return pending||mode==='reject'?{then:(ok,fail)=>{settle=mode==='reject'?fail:ok;}}:undefined;
 };
 if(['resume','throws','reject'].includes(mode))api.resume=invoke;
 if(mode==='play')api.play=invoke;
 if(mode==='wrapper')wrapper.play=invoke;
 api.togglePlayPause=()=>assert.fail('Never Pause must never toggle playback');
 const config={metadata:false,neverPause:enabled};
 const connect={getConnectState:()=>({activeDevice:{isLocal:local}})};
 const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:api}],[Symbol('ConnectDevicesAPI'),{instance:connect}]])};
 const main={__reactContainer$test:{memoizedProps:{registry}}};
 const sandbox={window:{__floggfyConfig:config,...(mode==='wrapper'?{Spicetify:{Player:wrapper}}:{})},
  document:{querySelector:s=>s==='#main'?main:null},console:{info:()=>{}},performance:{now:()=>now},Date:{now:()=>now},encodeURIComponent};
 vm.runInNewContext(script,sandbox);
 return {calls,state,config,tick:(advance=0)=>{now+=advance;state.timestamp=now;sandbox.window.__floggfyPoll();},settle:()=>settle?.()};
}
for(const mode of ['resume','play','wrapper']) {
 const f=fixture({mode});assert.equal(f.calls.length,1,`${mode}: paused local playback must resume`);
 f.tick();f.tick(1999);assert.equal(f.calls.length,1,'repeated polls cannot flood commands');
 f.tick(1);assert.equal(f.calls.length,2,'a still-paused client retries after two seconds');
 f.state.isPaused=false;f.tick(2000);assert.equal(f.calls.length,2,'playing clients get no command');
 f.state.isPaused=true;f.config.neverPause=false;f.tick(2000);assert.equal(f.calls.length,2,'switching off stops recovery');
 f.config.neverPause=true;f.tick();assert.equal(f.calls.length,3,'enabling takes effect without reload');
}
for(const values of [{enabled:false},{enabled:null},{enabled:1},{paused:false},{paused:null},{local:false},{local:null},{mode:'missing'}]) {
 const f=fixture(values);f.tick(5000);assert.equal(f.calls.length,0,JSON.stringify(values));
}
{
 const f=fixture({enabled:false});delete f.config.neverPause;f.tick(5000);
 assert.equal(f.calls.length,0,'missing options must never enable playback control');
}
for(const mode of ['throws','reject']) {
 const f=fixture({mode});f.settle();f.tick(2000);assert.equal(f.calls.length,2,'command failures allow bounded retry');
}
{
 const f=fixture({pending:true});f.tick(10000);assert.equal(f.calls.length,1,'unsettled play commands cannot overlap');
 f.settle();f.tick();assert.equal(f.calls.length,2,'settled commands allow retry');
}
{
 const f=fixture({enabled:false});f.state.item=undefined;f.state.isBuffering=true;
 f.config.neverPause=true;f.tick();assert.equal(f.calls.length,1,'paused buffering/terminal states still resume without track metadata');
}
console.log('PASS: Never Pause resumes local paused Spotify, retries without overlap, stops when disabled and never toggles playback');
