const fs=require('fs'),vm=require('vm');

const script=fs.readFileSync('native/metadata_collector.js','utf8');
const now=1793300000000;
const state={
 timestamp:now,
 positionAsOfTimestamp:12500,
 duration:180000,
 isPaused:false,
 isBuffering:false,
 playbackId:'playback-1',
 item:{
  uri:'spotify:track:0000000000000000000001',
  metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'180000'}
 }
};
const messages=[];
const player={getState:()=>state,getEvents:()=>({addListener(){},removeListener(){}})};
const sandbox={
 window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{
  PlayerAPI:player,
  ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}
 }}},
 document:{querySelector:()=>null},
 console:{info:value=>messages.push(value)},
 performance:{now:()=>now},
 Date:{now:()=>now},
 encodeURIComponent
};
vm.runInNewContext(script,sandbox);
const prefix='FLOGGFY_TIMELINE_V1:';
if(process.argv[2]==='--transition'||process.argv[2]==='--reused-transition'){
 if(process.argv[2]==='--transition'){state.playbackId='';sandbox.window.__floggfyPoll();}
 state.isBuffering=true;
 state.playbackId=process.argv[2]==='--transition'?'successor-stage':'playback-1';
 state.item={...state.item,uri:'spotify:track:0000000000000000000002'};
 sandbox.window.__floggfyPoll();
 const transition=messages.filter(value=>value.startsWith(prefix)).at(-1);
 if(!transition)throw new Error('collector did not emit a buffering transition');
 process.stdout.write(transition.slice(prefix.length));
 process.exit(0);
}
const message=messages.find(value=>value.startsWith(prefix));
if(!message)throw new Error('collector did not emit a Ready timeline');
process.stdout.write(message.slice(prefix.length));
