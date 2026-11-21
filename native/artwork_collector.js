// Exact image references use Spotify's existing native image transport.
// No service construction, module IDs, credentials, or cache-file inspection.
(() => {
 'use strict';
 if(window.__floggfyArtwork)return;
 const service='spotify.image.esperanto.proto.Image',limit=4*1024*1024;
 let transport,active,physical=false;
 const own=(o,k)=>{try{return Object.getOwnPropertyDescriptor(o,k)?.value;}catch{return undefined;}};
 const object=o=>o!==null&&(typeof o==='object'||typeof o==='function');
 function method(o,k){try{for(let i=0;object(o)&&i<4;i++,o=Object.getPrototypeOf(o)){const value=own(o,k);if(typeof value==='function')return value;}}catch{} }
 const metadataEnabled=()=>own(own(window,'__floggfyConfig'),'metadata')===true;
 const enabled=()=>metadataEnabled()&&own(own(window,'__floggfyConfig'),'fetchMissingArtwork')!==false;
 function identify(o){return object(o)&&method(o,'callSingle')&&method(o,'callStream');}
 window.__floggfyArtworkObserve=o=>{
  if(!transport&&object(o)){
   for(const candidate of [own(o,'_esperantoTransport'),own(o,'transport'),own(o,'_transport'),own(o,'esperantoConnector'),o]){
    if(identify(candidate)){transport=candidate;break;}
   }
  }
  if(transport&&active&&!active.started)start(active);
 };
 function finish(job,status,reason,reference='',source=0,data){
  if(active!==job)return;
  clearTimeout(job.timer);active=undefined;
  const fields={id:job.id,uri:job.uri,reference,source,status,reason};
  if(data)fields.data=data;
  const payload=Object.entries(fields).map(([key,value])=>key+'='+(key==='data'?value:encodeURIComponent(String(value)))).join('&');
  console.info('FLOGGFY_ARTWORK_V1:'+payload);
 }
 function report(id,uri,reason){
  console.info('FLOGGFY_ARTWORK_V1:id='+id+'&uri='+encodeURIComponent(uri)+'&reference=&source=0&status=unavailable&reason='+reason);
 }
 function writeVarint(out,n){do{const low=n&127;n=Math.floor(n/128);out.push(low|(n?128:0));}while(n);}
 function strings(values){const out=[];for(const text of values){out.push(10);writeVarint(out,text.length);for(let i=0;i<text.length;i++)out.push(text.charCodeAt(i));}return Uint8Array.from(out);}
 // Small bounded protobuf reader for the installed Image service schema.
 // Unknown future fields are skipped; duplicate known fields are rejected.
 function fields(bytes,max,visit){
  if(!(bytes instanceof Uint8Array)||bytes.length>max)throw Error('invalid-response');
  let pos=0,count=0;
  function variable(){let value=0n;for(let i=0;i<10;i++){
   if(pos>=bytes.length)throw Error('invalid-response');const byte=bytes[pos++];
   if(i===9&&byte>1)throw Error('invalid-response');value|=BigInt(byte&127)<<BigInt(i*7);
   if(!(byte&128))return value;
  }throw Error('invalid-response');}
  while(pos<bytes.length){
   if(++count>256)throw Error('invalid-response');
   const tag=variable();if(tag>0xffffffffn||tag<8n)throw Error('invalid-response');
   const wire=Number(tag&7n),field=Number(tag>>3n);let value;
   if(wire===0)value=variable();
   else if(wire===2){const n=variable();if(n>BigInt(bytes.length-pos))throw Error('invalid-response');value=bytes.subarray(pos,pos+Number(n));pos+=Number(n);}
   else if(wire===1||wire===5){pos+=wire===1?8:4;if(pos>bytes.length)throw Error('invalid-response');}
   else throw Error('invalid-response');
   visit(field,wire,value);
  }
 }
 function cacheStatus(bytes){
  const cached=new Map();
  fields(bytes,65536,(field,wire,value)=>{
   if(field!==1)return;if(wire!==2)throw Error('invalid-response');
   let key='',yes=false,haveKey=false,haveValue=false;
   fields(value,1024,(field,wire,value)=>{
    if(field===1){
     if(wire!==2||haveKey||value.length>256)throw Error('invalid-response');haveKey=true;
     for(const c of value){if(c>127)throw Error('invalid-response');key+=String.fromCharCode(c);}
    }else if(field===2){if(wire!==0||haveValue||value>1n)throw Error('invalid-response');haveValue=true;yes=value===1n;}
   });
   if(!haveKey||cached.has(key)||cached.size>=32)throw Error('invalid-response');cached.set(key,yes);
  });return cached;
 }
 function imageData(bytes){
  let error=0,source=0,data;const seen=new Set();
  fields(bytes,limit+65536,(field,wire,value)=>{
   if(field<1||field>3)return;
   if(seen.has(field))throw Error('invalid-response');seen.add(field);
   if(field===3){if(wire!==2||value.length>limit)throw Error('invalid-response');data=value;}
   else{
    if(wire!==0||(field===2&&value>3n))throw Error('invalid-response');
    const number=Number(BigInt.asIntN(32,value));if(field===1)error=number;else source=number;
   }
  });
  if(source<0||source>3)throw Error('invalid-response');
  return {error,source,data};
 }
 function base64url(bytes){
  let out='';for(let i=0;i<bytes.length;i+=12288)out+=btoa(String.fromCharCode(...bytes.subarray(i,i+12288)));
  return out.replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,'');
 }
 async function call(job,methodName,payload){
  if(active!==job)return;
  if(!enabled())throw Error('disabled');
  // A timeout releases the logical job, never the physical in-flight bound.
  // A client promise that ignores cancellation cannot accumulate more calls.
  physical=true;
  try{return await method(job.transport,'callSingle').call(job.transport,{service,method:methodName,payload});}
  finally{physical=false;}
 }
 async function start(job){
  if(active!==job||job.started)return;
  job.started=true;job.transport=transport;
  let reference='',source=0,reason='cache-miss';
  try{
   const requests=job.references.flatMap(url=>[url,'spotify:image:'+url.slice(-40)]);
   const reply=await call(job,'GetCacheStatus',strings(requests));
   if(active!==job)return;if(!enabled())throw Error('disabled');
   const cached=cacheStatus(reply);
   let imageRequested=false;
   for(const url of job.references){
    const candidate=[url,'spotify:image:'+url.slice(-40)].find(key=>cached.get(key)===true);
    if(!candidate)continue;
    if(!enabled()){reason=imageRequested?'cache-miss':'disabled';break;}
    reference=url;
    imageRequested=true;
    // GetImage has no per-call cache-only flag. Report its returned source,
    // including NETWORK if the native cache changed after the status check.
    const reply=await call(job,'GetImage',strings([candidate]));
    if(active!==job)return;if(!metadataEnabled())throw Error('disabled');
    const result=imageData(reply);source=result.source;
    if(result.error){reason='native-error-'+result.error;continue;}
    if(!result.data?.length){reason='empty-image';continue;}
    finish(job,'ready',source===2?'cache-hit':'native-source',url,source,base64url(result.data));return;
   }
  }catch(error){
   const message=own(error,'message');reason=['disabled','invalid-response'].includes(message)?message:'request-failed';
   if(reason==='request-failed')transport=undefined;
  }
  finish(job,'unavailable',reason,reference,source);
 }
 window.__floggfyArtwork=(id,uri,references)=>{
  if(typeof id!=='string'||!/^[1-9][0-9]{0,19}$/.test(id)||typeof uri!=='string'||!/^spotify:(?:track|episode):[A-Za-z0-9]{22}$/.test(uri)||
     !Array.isArray(references)||references.length<1||references.length>8||references.some(x=>typeof x!=='string'||!/^https:\/\/i\.scdn\.co\/image\/[a-f0-9]{40}$/.test(x)))return false;
  if(!enabled()){report(id,uri,'disabled');return true;}
  if(active||physical){report(id,uri,'busy');return true;}
  const job={id,uri,references:[...new Set(references)],started:false};active=job;
  job.timer=setTimeout(()=>finish(job,'unavailable',job.started?'timeout':'service-unavailable'),7000);
  if(transport)start(job);
  return true;
 };
})();
