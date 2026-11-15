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
  if(!object(value))return;
  try{
   const map=own(value,'_map');
   // Native Map.get avoids getters, custom iteration and registry.resolve.
   try{suppressSender(own(Map.prototype.get.call(map,Symbol.for('EventSender')),'instance'));}catch{}
   suppressUBI(value);
  }catch{}
  report();
 }
 try{Object.defineProperty(window,'__floggfyPrivacyObserve',{value:observe,configurable:true});}catch{return;}
 report();
})();
