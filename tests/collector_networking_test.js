const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
async function scenario(enabled){
 const output=[],pending=[];let now=10,sends=0,getters=0;
 const config={metadata:true,lyrics:true,debug:true,networking:enabled};
 const state={isPaused:false,item:{uri:'spotify:track:0000000000000000000001',metadata:{title:'PRIVATE_TITLE',artist_name:'PRIVATE_ARTIST',album_title:'PRIVATE_ALBUM',duration:'100000'}}};
 const player={getState:()=>state};
 const builder={build(){return {withHost(){return this;},withPath(){return this;},withQueryParameters(){return this;},
  send(){sends++;return new Promise((resolve,reject)=>pending.push({resolve,reject}));}};}};
 const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:player}],[Symbol('RequestBuilder'),{instance:builder}]])};
 const main={__reactContainer$test:{memoizedProps:{registry}}};
 const context={window:{__floggfyConfig:config},document:{querySelector:()=>main},performance:{now:()=>now},
  console:{info:x=>output.push(x)},encodeURIComponent};
 vm.runInNewContext(script,context);
 const poll=()=>context.window.__floggfyPoll();
 for(let i=0;i<5;i++)poll();assert.equal(sends,2);
 const lines=()=>output.filter(x=>x.startsWith('FLOGGFY_NETWORK_V1:'));
 assert.equal(lines().length,enabled?2:0);
 const response={status:200,body:{album:{gid:'0123456789abcdef0123456789abcdef'},PRIVATE_TOKEN:'SECRET'}};
 Object.defineProperty(response,'headers',{get(){getters++;throw Error('must not inspect headers');}});
 now=35;pending[0].resolve(response);
 pending[1].reject({status:503,message:'PRIVATE_TOKEN',body:'PRIVATE_MEDIA'});
 await Promise.resolve();await Promise.resolve();await Promise.resolve();
 poll();assert.equal(sends,3);
 now=40;pending[2].resolve({status:'PRIVATE_TOKEN',body:{name:'PRIVATE_ALBUM'}});
 await Promise.resolve();await Promise.resolve();await Promise.resolve();
 if(enabled){
  assert(lines().some(x=>x.includes('kind=metadata phase=response code=200 elapsed_ms=25')));
  assert(lines().some(x=>x.includes('kind=lyrics phase=rejected code=503')));
  assert(lines().some(x=>x.includes('kind=album phase=response code=?')));
  assert(lines().every(x=>x.length<160&&!/PRIVATE|SECRET|https|spotify:|token|body|headers/.test(x)));
  const count=lines().length;for(let i=0;i<20;i++)poll();assert.equal(lines().length,count);
  config.networking=false;now=6000;poll();
  const after=lines().length;for(const p of pending)p.reject({status:401});
  await Promise.resolve();await Promise.resolve();assert.equal(lines().length,after);
 }else assert.equal(lines().length,0);
 assert.equal(getters,0);
}
(async()=>{await scenario(false);await scenario(true);console.log('PASS: enrichment I/O summaries, correlation, safe status/latency, default opt-out and no idle spam');})().catch(e=>{console.error(e);process.exitCode=1;});
