(()=>{
 'use strict';
 // Spotify 1.3.9.2055: EventSender registry service and UBI wrapper, audited
 // from its V8 snapshot. Discovery is late: constructors and uploads can run
 // before the React registry is reachable. Never advertise early protection.
 // The existing collector calls this observer; no additional polling or walk.
 const object=value=>value!==null&&(typeof value==='object'||typeof value==='function');
 function own(value,key){
  if(!object(value))return;
  try{const d=Object.getOwnPropertyDescriptor(value,key);return d&&'value'in d?d.value:undefined;}catch{}
 }
 function method(value,key){
  try{for(let depth=0;object(value)&&depth<6;depth++,value=Object.getPrototypeOf(value)){
   const d=Object.getOwnPropertyDescriptor(value,key);
   if(d)return 'value'in d&&typeof d.value==='function'?d.value:undefined;
  }}catch{}
 }
 if(own(window,'__floggfyPrivacyObserve')!==undefined)return;
 const options=typeof __floggfyPrivacyOptions==='object'?__floggfyPrivacyOptions:{};
 const enabled=category=>options.block_telemetry===true&&options['block_'+category]===true;
 const senders=new WeakMap(),ubis=new WeakMap(),timers=new WeakMap();
 let senderCount=0,senderFailed=0,ubiCount=0,ubiFailed=0,lastStatus='';
 function report(){
  const status='FLOGGFY_PRIVACY_V1:sender='+(senderCount>0&&senderFailed===0?1:0)+' ubi='+(ubiCount>0&&ubiFailed===0&&senderFailed===0?1:0)+' early=0';
  if(status===lastStatus)return;
  lastStatus=status;
  try{console.log(status);}catch{}
 }
 const noop=()=>undefined,resolved=()=>Promise.resolve(),flushed=()=>Promise.resolve({fired:true});
 const interaction=()=>({interactionId:null,pageInstanceId:null});
 function replace(target,key,replacement){
  if(!method(target,key))return false;
  try{
   const d=Object.getOwnPropertyDescriptor(target,key);
   if(d&&d.value===replacement)return true;
   if(d&&(!('value'in d)||d.writable!==true))return false;
   Object.defineProperty(target,key,d?{...d,value:replacement}:{value:replacement,writable:true,configurable:true});
   return own(target,key)===replacement;
  }catch{return false;}
 }
 function canReplace(target,key){
  if(!method(target,key))return false;
  try{const d=Object.getOwnPropertyDescriptor(target,key);return d?('value'in d&&d.writable===true):Object.isExtensible(target);}catch{return false;}
 }
 const senderMethods=['storeEvent','sendEvents','sendESStats','initSendingEvents','lastFlush'];
 function senderShape(sender){
  const uploaders=own(sender,'uploaders');
  return object(sender)&&object(own(sender,'storageManager'))&&object(own(sender,'eventsManager'))&&
   object(own(sender,'environmentConfig'))&&object(own(uploaders,'authorized'))&&object(own(uploaders,'unauthorized'))&&
   senderMethods.every(key=>method(sender,key));
 }
 function suppressSender(sender){
  if(!senderShape(sender))return;
  let ok=true;
  for(const [key,value] of [['storeEvent',noop],['sendEvents',resolved],['sendESStats',resolved],['initSendingEvents',noop],['lastFlush',flushed]]){
   // Continue after a read-only method: other writable sinks still help, but
   // coverage remains partial. Do not touch generic send/flush or prototypes.
   if(!replace(sender,key,value))ok=false;
  }
  const interval=own(sender,'sendEventsInterval');
  try{const d=Object.getOwnPropertyDescriptor(sender,'sendEventsInterval');if(d&&!('value'in d))ok=false;}catch{ok=false;}
  if(interval!==undefined&&interval!==null&&interval!==0){
   if(!Number.isSafeInteger(interval)||interval<0)ok=false;
   else if(timers.get(sender)!==interval){
    try{clearInterval(interval);timers.set(sender,interval);}catch{ok=false;}
   }
  }
  const previous=senders.get(sender);
  if(previous===undefined)senderCount++;
  else if(!previous)senderFailed--;
  if(!ok)senderFailed++;
  senders.set(sender,ok);
 }
 function suppressUBI(wrapper){
  const logger=own(wrapper,'ubiLogger'),sender=own(logger,'eventSender');
  if(!senderShape(sender)||!method(logger,'logInteraction')||!method(logger,'logImpression')||
   !object(own(wrapper,'currentLocationProviderByChain'))||!object(own(wrapper,'pageTransitionLoggerByChain'))||
   !['logInteraction','logImpression','logImpressionV2','logImpressionSegment','logPageTransition','getPageTransitionLoggerByChain'].every(key=>method(wrapper,key)))return;
  suppressSender(sender);
  const replaced=replace(wrapper,'logInteraction',interaction);
  const ok=replaced&&senders.get(sender)===true;
  // Module 65025 stops visibility polling only after logImpressionV2 yields
  // a truthy impressionId; module 40499's legacy logImpression returns that
  // identity as a scalar. Preserve both methods and segment/local bookkeeping.
  // Navigation also updates UI state. Their shared EventSender suppresses
  // event storage/transmission without inventing IDs or changing these flows.
  // ubi=1 means the observed wrapper's interaction entry and sender sinks are
  // protected, not that all UBI producers have stopped running.
  const previous=ubis.get(wrapper);
  if(previous===undefined)ubiCount++;
  else if(!previous)ubiFailed--;
  if(!ok)ubiFailed++;
  ubis.set(wrapper,ok);
 }
 function observe(value){
  installRequests();
  if(!enabled('event_reports')||!object(value))return;
  try{
   const map=own(value,'_map');
   // Native Map.get avoids getters, custom iteration and registry.resolve.
   try{suppressSender(own(Map.prototype.get.call(map,Symbol.for('EventSender')),'instance'));}catch{}
   suppressUBI(value);
  }catch{}
  report();
 }
 // Match telemetry_policy.h's audited destinations. Counters contain no URL
 // or payload; only the separate dump opt-in captures cancelled request data.
 // A zero master or category leaves its callers intact.
 function count(category){try{console.log('FLOGGFY_PRIVACY_COUNT_V1:'+category+'=1');}catch{}}
 const route=(path,root)=>path===root||path.startsWith(root+'/');
 function classify(url){
  if(typeof url!=='string'||!url.length||url.length>4096||/[\x00-\x20\x7f]/.test(url))return;
  const match=/^(https?|sp):\/\/([^/\\?#]*)([^?#]*)/i.exec(url);if(!match)return;
  const scheme=match[1].toLowerCase();let host=match[2].slice(match[2].lastIndexOf('@')+1).toLowerCase();
  const port=host.indexOf(':');
  if(port!==-1){const value=host.slice(port+1);if(!/^\d{1,5}$/.test(value)||+value<1||+value>65535)return;host=host.slice(0,port);}
  if(host.endsWith('.'))host=host.slice(0,-1);
  const segments=[],decoded=match[3].replace(/%([0-9a-f]{2})/gi,(all,hex)=>{
   const c=String.fromCharCode(parseInt(hex,16));return /[a-z0-9._~\/\\-]/i.test(c)?c:all;
  }).replace(/\\/g,'/');
  for(const segment of decoded.split('/')){if(segment==='..')segments.pop();else if(segment&&segment!=='.')segments.push(segment);}
  const path='/'+segments.join('/'),http=scheme==='http'||scheme==='https';
  const regional=/^([a-z0-9](?:[a-z0-9-]*[a-z0-9])?)-spclient\.spotify\.com$/.exec(host);
  const spclient=http&&(host==='spclient.wg.spotify.com'||regional&&regional[1].length+8<=63);
  if(spclient&&route(path,'/gabo-receiver-service'))return 'event_reports';
  if(spclient&&(route(path,'/melody/v1/msg')||route(path,'/melody/unauth/v1/msg')))return 'playback_reports';
  if(spclient&&path==='/track-error/v1/errors')return 'track_errors';
  if(spclient&&(path==='/v1/telemetry/sandbox'||path==='/v1/telemetry/turn-termination'))return 'ai_telemetry';
  if(http&&host==='www.spotify.com'&&route(path,'/api/growth-events'))return 'growth_analytics';
  if(http&&host==='crashdump.spotify.com')return 'crash_reports';
  if(scheme==='sp'&&host==='logging'&&['/v1','/v2','/v3'].some(root=>route(path,root)))return 'internal_logging';
  if(http&&host==='o22381.ingest.sentry.io'&&route(path,'/api/114855'))return 'sentry';
 }
 // Use branded platform getters, never caller-controlled url/toString getters.
 const urlGetter=typeof URL==='function'?Object.getOwnPropertyDescriptor(URL.prototype,'href')?.get:undefined;
 const requestGetter=typeof Request==='function'?Object.getOwnPropertyDescriptor(Request.prototype,'url')?.get:undefined;
 function address(value,request=false){
  if(typeof value==='string'){
   if(value.length>4096)return;
   if(value.includes('://'))return value;
   try{return new URL(value,window.location.href).href;}catch{return;}
  }
  if(object(value)){
   if(request&&requestGetter)try{return requestGetter.call(value);}catch{}
   if(urlGetter)try{return urlGetter.call(value);}catch{}
  }
 }
 const blocked=(value,request=false)=>{const category=classify(address(value,request));return category&&enabled(category)?category:undefined;};
 // Dumping is a separate opt-in. Only already-blocked branches call capture;
 // no allowed request body or credential header is inspected. Every record is
 // bounded before crossing the console bridge, and capture never permits send.
 const dumpLimit=262144;let dumpRecords=0,pendingBlobs=0;
 const getter=(type,key)=>typeof type==='function'?Object.getOwnPropertyDescriptor(type.prototype,key)?.get:undefined;
 const requestMethod=getter(typeof Request==='undefined'?undefined:Request,'method');
 const requestHeaders=getter(typeof Request==='undefined'?undefined:Request,'headers');
 const requestBody=getter(typeof Request==='undefined'?undefined:Request,'body');
 const headersGet=typeof Headers==='function'?Headers.prototype.get:undefined;
 const blobSize=getter(typeof Blob==='undefined'?undefined:Blob,'size'),blobType=getter(typeof Blob==='undefined'?undefined:Blob,'type');
 const blobSlice=typeof Blob==='function'?Blob.prototype.slice:undefined,blobBuffer=typeof Blob==='function'?Blob.prototype.arrayBuffer:undefined;
 const fileName=getter(typeof File==='undefined'?undefined:File,'name');
 const bufferSize=getter(ArrayBuffer,'byteLength');
 const typed=Object.getPrototypeOf(Uint8Array.prototype);
 const typedGet=key=>Object.getOwnPropertyDescriptor(typed,key).get;
 const typedBuffer=typedGet('buffer'),typedOffset=typedGet('byteOffset'),typedSize=typedGet('byteLength');
 const viewBuffer=getter(DataView,'buffer'),viewOffset=getter(DataView,'byteOffset'),viewSize=getter(DataView,'byteLength');
 const encoder=typeof TextEncoder==='function'?new TextEncoder():undefined;
 function field(value,key){
  if(!object(value))return {present:false};
  try{const d=Object.getOwnPropertyDescriptor(value,key);
   if(d)return 'value'in d?{present:true,value:d.value}:{present:true,unsupported:true};
   let prototype=Object.getPrototypeOf(value);for(let depth=0;prototype&&depth<6;depth++,prototype=Object.getPrototypeOf(prototype))if(Object.getOwnPropertyDescriptor(prototype,key))return {present:true,unsupported:true};
   return prototype?{present:true,unsupported:true}:{present:false};
  }catch{return {present:true,unsupported:true};}
 }
 function headerValues(headers,state){
  if(headers==null)return;
  if(headersGet)try{state.type=headersGet.call(headers,'content-type')||'';state.encoding=headersGet.call(headers,'content-encoding')||'';return;}catch{}
  // Never enumerate arbitrary objects, invoke iterators, or read credential
  // values. Standard pair arrays have a fixed 128-entry inspection budget.
  try{
   if(Array.isArray(headers)){
    const length=field(headers,'length').value;if(!Number.isSafeInteger(length)){state.flags|=8;return;}
    if(length>128)state.flags|=8;
    for(let i=0;i<Math.min(length,128);i++){
     const entry=field(headers,String(i)),pair=entry.value;
     if(entry.unsupported||!Array.isArray(pair)||field(pair,'length').value!==2){state.flags|=8;continue;}
     const key=field(pair,'0');if(key.unsupported||typeof key.value!=='string'){state.flags|=8;continue;}
     if(key.value.length>16)continue;const lower=key.value.toLowerCase();
     if(lower!=='content-type'&&lower!=='content-encoding')continue;
     const value=field(pair,'1');if(value.unsupported||typeof value.value!=='string'){state.flags|=8;continue;}
     const name=lower==='content-type'?'type':'encoding',prefix=state[name]?state[name]+', ':'';
     state[name]=(prefix+value.value.slice(0,1024)).slice(0,1024);if(prefix.length+value.value.length>1024)state.flags|=8;
    }
    return;
   }
   if(!object(headers)){state.flags|=8;return;}
   // Missing type/encoding is valid (including authorization-only objects).
   // Branded Headers supports arbitrary casing beyond these common spellings.
   for(const [name,keys] of [['type',['content-type','Content-Type','CONTENT-TYPE']],['encoding',['content-encoding','Content-Encoding','CONTENT-ENCODING']]]){
    for(const key of keys){const item=field(headers,key);if(!item.present)continue;
     if(item.unsupported||typeof item.value!=='string')state.flags|=8;else state[name]=item.value;
     break;
    }
   }
  }catch{state.flags|=8;}
 }
 function fetchMetadata(input,init,state){
  state.method='GET';let branded=false;
  if(requestMethod)try{state.method=requestMethod.call(input);branded=true;headerValues(requestHeaders.call(input),state);}catch{}
  const verb=field(init,'method');if(verb.present){if(typeof verb.value==='string')state.method=verb.value;else{state.method='';state.flags|=8;}}
  const headers=field(init,'headers');if(headers.present){state.type='';state.encoding='';if(headers.unsupported)state.flags|=8;else headerValues(headers.value,state);}
  state.body=field(init,'body');
  if(branded&&(!state.body.present||!state.body.unsupported&&state.body.value==null))try{if(requestBody.call(input)!==null)state.body={present:true,unsupported:true};}catch{state.body={present:true,unsupported:true};}
 }
 function base64(bytes){let binary='';for(let i=0;i<bytes.length;i+=8192)binary+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(binary);}
 function limitedText(text,limit,state){
  if(typeof text!=='string'){state.flags|=8;return '';}
  if(text.length>limit){state.flags|=8;text=text.slice(0,limit);if(/[\uD800-\uDBFF]$/.test(text))text=text.slice(0,-1);}
  return text;
 }
 function emitDump(state,bytes=new Uint8Array()){
  try{
   const url=state.url.replace(/^([^:]+:\/\/)[^/\\?#]*@/,'$1').split(/[?#]/,1)[0];
   const metadata=[limitedText(state.method,64,state),limitedText(url,4096,state),limitedText(state.type,1024,state),limitedText(state.encoding,1024,state)];
   const line='FLOGGFY_PRIVACY_DUMP_V1:'+state.hook+'\n'+state.timestamp+'\n'+metadata.map(text=>base64(encoder.encode(text))).join('\n')+'\n'+state.flags+'\n'+state.reported+'\n'+base64(bytes);
   if(line.length<=400000)console.log(line);
  }catch{} // A dump failure must never weaken the already-selected block.
 }
 function stringBody(text,state){
  if(!state.type)state.type='text/plain;charset=UTF-8';
  // Count at most 1 Mi UTF-16 units. Larger strings are explicitly omitted,
  // with an unknown reported length, rather than scanning unbounded input.
  if(text.length>1048576){state.flags|=6;emitDump(state);return;}
  const bytes=new Uint8Array(dumpLimit),result=encoder.encodeInto(text,bytes);
  if(result.read<text.length)state.flags|=2;
  let length=0;for(let i=0;i<text.length;i++){
   const code=text.charCodeAt(i);if(code<128)length++;else if(code<2048)length+=2;
   else if(code>=0xd800&&code<=0xdbff&&i+1<text.length&&text.charCodeAt(i+1)>=0xdc00&&text.charCodeAt(i+1)<=0xdfff){length+=4;i++;}else length+=3;
  }
  state.reported=length;emitDump(state,bytes.subarray(0,result.written));
 }
 function capture(hook,url,prepare){
  if(options.block_telemetry!==true||options.dump_telemetry!==true||dumpRecords>=32||!encoder||typeof btoa!=='function')return;
  dumpRecords++;
  const state={hook,url,timestamp:Date.now(),method:'',type:'',encoding:'',flags:0,reported:0,body:{present:false}};
  try{
   prepare(state);const body=state.body;
   if(!body.present||!body.unsupported&&body.value==null){emitDump(state);return;}
   state.flags|=1;if(body.unsupported){state.flags|=4;emitDump(state);return;}
   const value=body.value;
   if(typeof value==='string'){stringBody(value,state);return;}
   let bytes,size;
   try{size=bufferSize.call(value);bytes=new Uint8Array(value,0,Math.min(size,dumpLimit));}catch{}
   if(!bytes&&ArrayBuffer.isView(value)){
    try{size=typedSize.call(value);bytes=new Uint8Array(typedBuffer.call(value),typedOffset.call(value),Math.min(size,dumpLimit));}
    catch{size=viewSize.call(value);bytes=new Uint8Array(viewBuffer.call(value),viewOffset.call(value),Math.min(size,dumpLimit));}
   }
   if(bytes){state.reported=size;if(size>dumpLimit)state.flags|=2;emitDump(state,bytes);return;}
   if(blobSize)try{size=blobSize.call(value);}catch{size=undefined;}
   if(size!==undefined){
    state.reported=size;if(size>dumpLimit)state.flags|=2;
    if(!state.type)state.type=blobType.call(value);
    let file=false;if(fileName)try{fileName.call(value);file=true;}catch{}
    if(file||pendingBlobs>=8||!blobSlice||!blobBuffer){state.flags|=4;emitDump(state);return;}
    pendingBlobs++;
    let task;try{task=blobBuffer.call(blobSlice.call(value,0,dumpLimit));}catch(error){pendingBlobs--;throw error;}
    Promise.resolve(task).then(buffer=>{pendingBlobs--;emitDump(state,new Uint8Array(buffer));},()=>{pendingBlobs--;state.flags|=4;emitDump(state);});return;
   }
   state.flags|=4;emitDump(state);
  }catch{state.flags|=4;emitDump(state);}
 }
 const capabilities={fetch:0,xhr:0,beacon:0,cosmos:0};let lastCapabilities='';
 function reportRequests(){
  const status='FLOGGFY_PRIVACY_V2:fetch='+capabilities.fetch+' xhr='+capabilities.xhr+' beacon='+capabilities.beacon+' cosmos='+capabilities.cosmos+' early=0';
  if(status!==lastCapabilities){lastCapabilities=status;try{console.log(status);}catch{}}
 }
 // The observer already runs during bounded collector discovery. Reuse it to
 // discover a late native bridge; do not install another timer or global setter.
 function installRequests(){
  if(options.block_telemetry!==true)return;
  if(!capabilities.fetch){
   const original=method(window,'fetch');
   if(original&&replace(window,'fetch',{fetch(...args){
    const category=blocked(args[0],true);if(!category)return Reflect.apply(original,this,args);
    count(category);capture('fetch',address(args[0],true),state=>fetchMetadata(args[0],args[1],state));return Promise.reject(new TypeError('Failed to fetch'));
   }}.fetch))capabilities.fetch=1;
  }
  if(!capabilities.beacon){
   const navigator=own(window,'navigator')||window.navigator,original=method(navigator,'sendBeacon');
   if(original&&replace(navigator,'sendBeacon',{sendBeacon(...args){
    const category=this===navigator&&blocked(args[0]);if(!category)return Reflect.apply(original,this,args);
    count(category);capture('beacon',address(args[0]),state=>{state.method='POST';state.body={present:args.length>1,value:args[1]};});return false;
   }}.sendBeacon))capabilities.beacon=1;
  }
  if(!capabilities.xhr){
   const prototype=own(own(window,'XMLHttpRequest'),'prototype');
   const open=method(prototype,'open'),send=method(prototype,'send'),abort=method(prototype,'abort'),dispatch=method(prototype,'dispatchEvent');
   if(open&&send&&abort&&dispatch&&typeof Event==='function'&&typeof DOMException==='function'&&
      canReplace(prototype,'open')&&canReplace(prototype,'send')){
    const requests=new WeakMap();
    const opened=replace(prototype,'open',{open(...args){
     const result=Reflect.apply(open,this,args);requests.set(this,{category:blocked(args[1]),url:address(args[1]),method:typeof args[0]==='string'?args[0]:'',type:'',encoding:'',flags:0,async:args[2]===undefined||Boolean(args[2])});return result;
    }}.open);
    const setHeader=method(prototype,'setRequestHeader');
    if(opened&&options.dump_telemetry===true&&setHeader)replace(prototype,'setRequestHeader',{setRequestHeader(...args){
     const result=Reflect.apply(setHeader,this,args),state=requests.get(this);
     if(state&&state.category&&typeof args[0]==='string'){const key=args[0].toLowerCase();if(key==='content-type'||key==='content-encoding'){
      const name=key==='content-type'?'type':'encoding';if(typeof args[1]==='string'){const prefix=state[name]?state[name]+', ':'';state[name]=(prefix+args[1].slice(0,1024)).slice(0,1024);if(prefix.length+args[1].length>1024)state.flags|=8;}else state.flags|=8;
     }}return result;
    }}.setRequestHeader);
    if(opened&&replace(prototype,'send',{send(...args){
     const state=requests.get(this);
     if(!state||!state.category||this.readyState!==1)return Reflect.apply(send,this,args);
     Reflect.apply(abort,this,[]);count(state.category);
     capture('xhr',state.url,dump=>{dump.method=state.method;dump.type=state.type;dump.encoding=state.encoding;dump.flags=state.flags;dump.body={present:args.length>0,value:args[0]};});
     if(!state.async)throw new DOMException('Blocked by privacy settings','NetworkError');
     // abort() on OPENED before send has no native events. Deliver cancellation,
     // never load/success, and invalidate the pending notification on a new open.
     queueMicrotask(()=>{if(requests.get(this)!==state)return;
      Reflect.apply(dispatch,this,[new Event('abort')]);Reflect.apply(dispatch,this,[new Event('loadend')]);
     });
    }}.send))capabilities.xhr=1;
   }
  }
  if(!capabilities.cosmos){
   const send=method(window,'sendCosmosRequest'),cancel=method(window,'cancelCosmosRequest');
   if(send&&cancel&&canReplace(window,'sendCosmosRequest')&&canReplace(window,'cancelCosmosRequest')){
    const tokens=new WeakSet(),pending=new WeakSet();
    const cancelled=replace(window,'cancelCosmosRequest',{cancelCosmosRequest(...args){
     if(object(args[0])&&tokens.has(args[0])){pending.delete(args[0]);return;}
     return Reflect.apply(cancel,this,args);
    }}.cancelCosmosRequest);
    if(cancelled&&replace(window,'sendCosmosRequest',{sendCosmosRequest(...args){
     const input=args[0],request=own(input,'request'),failure=own(input,'onFailure');let category,decoded;
     // Installed module 1508's verified bridge shape and failure(code,text)
     // callback. Unknown bridges, methods, requests and Hermes remain partial.
     if(typeof request==='string'&&request.length<=1048576&&typeof failure==='function'&&
        typeof own(input,'onSuccess')==='function'&&typeof own(input,'persistent')==='boolean'){
      try{decoded=JSON.parse(request);if(classify(decoded.uri)==='internal_logging'&&enabled('internal_logging'))category='internal_logging';}catch{}
     }
     if(!category)return Reflect.apply(send,this,args);
     const token=Object.freeze({});tokens.add(token);pending.add(token);count(category);
     capture('cosmos',decoded.uri,state=>{state.method=decoded.method;headerValues(decoded.headers,state);state.body=field(decoded,'body');});
     queueMicrotask(()=>{if(pending.delete(token))Reflect.apply(failure,undefined,[-1,'Blocked by privacy settings']);});return token;
    }}.sendCosmosRequest))capabilities.cosmos=1;
   }
  }
  reportRequests();
 }
 try{Object.defineProperty(window,'__floggfyPrivacyObserve',{value:observe,configurable:true});}catch{return;}
 installRequests();
 report();
})();
