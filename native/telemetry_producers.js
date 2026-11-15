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
 // Match telemetry_policy.h's audited destinations; URL payloads never cross
 // the console boundary. A zero master or category leaves its callers intact.
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
    count(category);return Promise.reject(new TypeError('Failed to fetch'));
   }}.fetch))capabilities.fetch=1;
  }
  if(!capabilities.beacon){
   const navigator=own(window,'navigator')||window.navigator,original=method(navigator,'sendBeacon');
   if(original&&replace(navigator,'sendBeacon',{sendBeacon(...args){
    const category=this===navigator&&blocked(args[0]);if(!category)return Reflect.apply(original,this,args);
    count(category);return false;
   }}.sendBeacon))capabilities.beacon=1;
  }
  if(!capabilities.xhr){
   const prototype=own(own(window,'XMLHttpRequest'),'prototype');
   const open=method(prototype,'open'),send=method(prototype,'send'),abort=method(prototype,'abort'),dispatch=method(prototype,'dispatchEvent');
   if(open&&send&&abort&&dispatch&&typeof Event==='function'&&typeof DOMException==='function'&&
      canReplace(prototype,'open')&&canReplace(prototype,'send')){
    const requests=new WeakMap();
    const opened=replace(prototype,'open',{open(...args){
     const result=Reflect.apply(open,this,args);requests.set(this,{category:blocked(args[1]),async:args[2]===undefined||Boolean(args[2])});return result;
    }}.open);
    if(opened&&replace(prototype,'send',{send(...args){
     const state=requests.get(this);
     if(!state||!state.category||this.readyState!==1)return Reflect.apply(send,this,args);
     Reflect.apply(abort,this,[]);count(state.category);
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
     const input=args[0],request=own(input,'request'),failure=own(input,'onFailure');let category;
     // Installed module 1508's verified bridge shape and failure(code,text)
     // callback. Unknown bridges, methods, requests and Hermes remain partial.
     if(typeof request==='string'&&request.length<=1048576&&typeof failure==='function'&&
        typeof own(input,'onSuccess')==='function'&&typeof own(input,'persistent')==='boolean'){
      try{const decoded=JSON.parse(request);if(classify(decoded.uri)==='internal_logging'&&enabled('internal_logging'))category='internal_logging';}catch{}
     }
     if(!category)return Reflect.apply(send,this,args);
     const token=Object.freeze({});tokens.add(token);pending.add(token);count(category);
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
