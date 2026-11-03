const fs=require('fs'),vm=require('vm'),assert=require('assert');

const script=fs.readFileSync('native/metadata_collector.js','utf8');
const iterations=Number(process.argv[2]||10000);
let random=Number(process.argv[3]||4043931417)>>>0;
const next=()=>{random^=random<<13;random^=random>>>17;random^=random<<5;return random>>>0;};
const speeds=[1,2,4,8,10];
const uri=n=>'spotify:track:'+String(n).padStart(22,'0');

function fields(line){
 return new URLSearchParams(line.slice('FLOGGFY_TIMELINE_V1:'.length));
}

for(let run=0;run<iterations;++run){
 let clock=1793300000000+run*100000;
 const speed=speeds[next()%speeds.length],transition=next()%3,control=next()%4;
 const gapControl=control>=2&&transition!==0&&next()%2===0;
 let current={timestamp:clock,positionAsOfTimestamp:0,duration:16000,isPaused:false,isBuffering:false,
  speed,playbackId:'play-'+run+'-a',item:{uri:uri(run%9+1),metadata:{title:'A',artist_name:'Artist',album_title:'Album'}}};
 const messages=[],handlers=new Map();
 const api={getState:()=>current,getEvents:()=>({addListener:(type,fn)=>handlers.set(type,fn),removeListener:type=>handlers.delete(type)})};
 const sandbox={window:{__floggfyConfig:{metadata:false},Spicetify:{Platform:{PlayerAPI:api,ConnectDevicesAPI:{getConnectState:()=>({activeDevice:{isLocal:true}})}}}},
  document:{querySelector:()=>null},console:{info:line=>messages.push(line)},performance:{now:()=>clock},Date:{now:()=>clock},setInterval:()=>{},encodeURIComponent};
 vm.runInNewContext(script,sandbox);sandbox.window.__floggfyPoll();

 // Controls are stage evidence, not timers. Pause/resume is acknowledged on A;
 // skip/seek must reject A but must not leak into a later clean B transition.
 if(control===1){
  handlers.get('action')({data:{action:'pause'}});
  clock+=1+next()%30000;current={...current,timestamp:clock,isPaused:true};sandbox.window.__floggfyPoll();
  handlers.get('action')({data:{action:'resume'}});
  clock+=1+next()%30000;current={...current,timestamp:clock,isPaused:false};sandbox.window.__floggfyPoll();
 }else if(control===2&&!gapControl)handlers.get('action')({data:{action:'skipToNext'}});
 else if(control===3&&!gapControl)handlers.get('action')({data:{action:'seekTo'}});

 clock+=1+next()%30000;
 const outgoing=current.playbackId,nextUri=uri(run%9+10),nextId='play-'+run+'-b';
 current={...current,timestamp:clock,positionAsOfTimestamp:0,isBuffering:true,
  playbackId:transition===0?nextId:transition===1?'':outgoing,
  item:{uri:nextUri,metadata:{title:'B',artist_name:'Artist',album_title:'Album'}}};
 sandbox.window.__floggfyPoll();
 const blocked=fields(messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')&&line.includes('blocked=buffering')).at(-1));
 if(control>=2&&!gapControl){
  assert.equal(blocked.get('controlled'),'1');assert.equal(blocked.get('natural'),null);assert.equal(blocked.get('pending'),null);
 }else if(transition===0){
  assert.equal(blocked.get('natural'),'1');assert.equal(blocked.get('pending'),null);
 }else{
  assert.equal(blocked.get('pending'),'1');assert.equal(blocked.get('natural'),null);
 }

 if(gapControl){
  if(next()%2){
   clock+=1+next()%30000;current={...current,timestamp:clock,isBuffering:false,playbackId:outgoing,
    item:{uri:uri(run%9+1),metadata:{title:'A',artist_name:'Artist',album_title:'Album'}}};
   sandbox.window.__floggfyPoll();
  }
  handlers.get('action')({data:{action:control===2?'skipToNext':'seekTo'}});
  handlers.get('action')({data:{action:'resume'}});
 }
 clock+=1+next()%30000;current={...current,timestamp:clock,isBuffering:false,playbackId:nextId,
  item:{uri:nextUri,metadata:{title:'B',artist_name:'Artist',album_title:'Album'}}};
 sandbox.window.__floggfyPoll();
 const readyB=fields(messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')&&!line.includes('blocked=')).at(-1));
 if(gapControl)assert(readyB.get('prior_control')==='1'||readyB.get('controlled')==='1',
  JSON.stringify({run,speed,transition,control,gapControl,ready:Object.fromEntries(readyB)}));

 // B is an independent full playback. No action from A may poison B's end,
 // regardless of wall-clock spacing or capture speed.
 for(let step=1;step<=4;++step){
  clock+=1+next()%30000;current={...current,timestamp:clock,positionAsOfTimestamp:step*4000};sandbox.window.__floggfyPoll();
 }
 const thirdId='play-'+run+'-c';clock+=1+next()%30000;
 current={...current,timestamp:clock,positionAsOfTimestamp:0,isBuffering:true,playbackId:thirdId,
  item:{uri:uri(run%9+20),metadata:{title:'C',artist_name:'Artist',album_title:'Album'}}};
 sandbox.window.__floggfyPoll();
 const endB=fields(messages.filter(line=>line.startsWith('FLOGGFY_TIMELINE_V1:')&&line.includes('blocked=buffering')).at(-1));
 assert.equal(endB.get('natural'),'1');assert.equal(endB.get('controlled'),null);assert.equal(endB.get('manual'),null);

 // Feed every valid producer record to the native parser in the shell pipeline.
 for(const line of messages){
  if(!line.startsWith('FLOGGFY_TIMELINE_V1:'))continue;
  const payload=line.slice('FLOGGFY_TIMELINE_V1:'.length);
  if(payload.includes('uri=')&&(payload.includes('blocked=buffering')||payload.includes('title=')))process.stdout.write(payload+'\n');
 }
}
console.error(`PASS: ${iterations} collector lifecycle sequences across speeds, controls, blank/reused IDs and arbitrary stage spacing`);
