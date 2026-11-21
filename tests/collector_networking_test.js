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
 for(let i=0;i<5;i++)poll();assert.equal(sends,0);
 context.window.__floggfyEnrich(state.item.uri,{metadata:true,album:true,lyrics:true});assert.equal(sends,2);
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
async function liveFallbacks(){
 const output=[],sends=[];let now=10;
 const config={metadata:true,lyrics:true,fetchMissingMetadata:false,fetchMissingLyrics:false};
 const uri='spotify:track:0000000000000000000001';
 const item={uri,metadata:{title:'Song',artist_name:'Artist',album_title:'Album',duration:'100000'}};
 const player={getState:()=>({isPaused:false,item})};
 const builder={build(){let path;return {withHost(){return this;},withPath(value){path=value;return this;},withQueryParameters(){return this;},
  send(){return new Promise((resolve,reject)=>sends.push({path,resolve,reject}));}};}};
 const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:player}],[Symbol('RequestBuilder'),{instance:builder}]])};
 const main={__reactContainer$test:{memoizedProps:{registry}}};
 const window={__floggfyConfig:config};
 const poll=()=>window.__floggfyPoll();
 const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
 vm.runInNewContext(script,{window,document:{querySelector:()=>main},performance:{now:()=>now},console:{info:x=>output.push(x)},encodeURIComponent});
 window.__floggfyEnrich(uri,{metadata:true,album:true,lyrics:true});poll();assert.equal(sends.length,0);
 config.fetchMissingMetadata=true;poll();assert.equal(sends.length,1);assert(sends[0].path.includes('/track/'));
 config.fetchMissingMetadata=false;poll();
 sends[0].resolve({status:200,body:{album:{gid:'0123456789abcdef0123456789abcdef'},external_id:[{type:'isrc',id:'TESTISRC'}]}});
 await flush();poll();assert.equal(sends.length,1);
 assert(output.some(line=>line.startsWith('FLOGGFY_METADATA_V1:')&&line.includes('TESTISRC')));
 config.fetchMissingLyrics=true;poll();assert.equal(sends.length,2);assert(sends[1].path.includes('/color-lyrics/'));
 config.fetchMissingLyrics=false;sends[1].reject({status:503});await flush();
 now=10000;poll();assert.equal(sends.length,2);
 config.fetchMissingLyrics=true;poll();assert.equal(sends.length,3);assert(sends[2].path.includes('/color-lyrics/'));
 config.fetchMissingMetadata=true;poll();assert.equal(sends.length,4);assert(sends[3].path.includes('/album/'));
}
(async()=>{await liveFallbacks();await scenario(false);await scenario(true);console.log('PASS: enrichment I/O summaries, correlation, safe status/latency, default opt-out and no idle spam');})().catch(e=>{console.error(e);process.exitCode=1;});
