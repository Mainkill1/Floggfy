const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
const uri='spotify:track:0000000000000000000001';

function run({enabled=true,expected=uri,current=uri,local=true,paused=false,command=true}={}) {
 const now=1793300000000,calls=[];
 const state={timestamp:now,positionAsOfTimestamp:1000,duration:180000,isPaused:paused,isBuffering:false,
  playbackId:'playback-1',item:{uri:current,metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'180000'}}};
 const listeners=new Map();
 const player={getState:()=>state,getEvents:()=>({addListener:(type,fn)=>listeners.set(type,fn),removeListener:type=>listeners.delete(type)})};
 const wrapper={origin:player};if(command)wrapper.next=()=>calls.push(current);
 const config={metadata:false,lyrics:false,autoSkip:enabled,skipToken:1,skipUri:expected};
 const sandbox={window:{__floggfyConfig:config,Spicetify:{Player:wrapper,Platform:{PlayerAPI:player,
  ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:local}})}}}},document:{querySelector:()=>null},
  console:{info:()=>{}},performance:{now:()=>now},Date:{now:()=>now},encodeURIComponent};
 vm.runInNewContext(script,sandbox);
 sandbox.window.__floggfyPoll();
 config.skipToken=2;sandbox.window.__floggfyPoll();
 return calls;
}

assert.deepStrictEqual(run(),[uri,uri]); // Each token dispatches once; the repeated first token does not.
assert.deepStrictEqual(run({enabled:false}),[]);
assert.deepStrictEqual(run({expected:'spotify:track:0000000000000000000002'}),[]);
assert.deepStrictEqual(run({local:false}),[]);
assert.deepStrictEqual(run({paused:true}),[]);
assert.deepStrictEqual(run({command:false}),[]);
console.log('PASS: collector dispatches each current local Spotify auto-skip token once and fails closed');
