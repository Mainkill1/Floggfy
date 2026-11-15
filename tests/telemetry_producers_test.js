const fs=require('fs'),vm=require('vm'),assert=require('assert');
const path='native/telemetry_producers.js';
const script=fs.existsSync(path)?fs.readFileSync(path,'utf8'):'';
function boot(){
 const output=[],cancelled=[],window={};
 const context=vm.createContext({window,console:{log:line=>output.push(line)},clearInterval:id=>cancelled.push(id)});
 vm.runInContext(script,context);
 assert.equal(typeof window.__floggfyPrivacyObserve,'function','bootstrap installs bounded observer');
 return {context,window,output,cancelled,observe:window.__floggfyPrivacyObserve};
}
function sender(){
 const state={queue:[],uploads:0};
 class Sender {
  constructor(){this.storageManager={};this.eventsManager={};this.uploaders={authorized:{},unauthorized:{}};this.environmentConfig={};this.sendEventsInterval=42;}
  storeEvent(auth,event){state.queue.push(event);}
  async sendEvents(){state.uploads+=state.queue.length;state.queue=[];}
  async sendESStats(){state.uploads++;}
  initSendingEvents(){throw Error('timer restarted');}
  async lastFlush(){await this.sendESStats();await this.sendEvents();return {fired:true};}
  async flush(wait){await wait;await this.sendESStats();await this.sendEvents();}
  send(event){this.storeEvent(true,event);return Promise.resolve();}
  finalFlush(){return this.lastFlush();}
 }
 return {instance:new Sender,state};
}
function registry(instance,key=Symbol.for('EventSender')){return {_map:new Map([[key,{instance}]])};}
function ubi(eventSender){
 let sequence=0;
 // Audited module 40499 preserves the underlying logger's identity and emits
 // local bookkeeping. Legacy returns a scalar; V2 returns the complete object.
 return {ubiLogger:{eventSender,logInteraction(){throw Error('producer ran');},
   logImpression(){const result={impressionId:'impression-'+(++sequence),pagePresentationId:7};eventSender.send(result);return result;},
   logImpressionSegment(event){eventSender.send(event);}},emitted:[],
  currentLocationProviderByChain:{},pageTransitionLoggerByChain:{},location:null,
  logInteraction(){throw Error('producer ran');},
  logImpression(){const result=this.ubiLogger.logImpression();this.emitted.push(result.impressionId);return result.impressionId;},
  logImpressionV2(){const result=this.ubiLogger.logImpression();this.emitted.push(result.impressionId);return result;},
  logImpressionSegment(event){this.ubiLogger.logImpressionSegment(event);this.emitted.push('segment');},
  logPageTransition(location){this.location=location;},getPageTransitionLoggerByChain(){return null;}};
}
async function main(){
 const b=boot();assert.deepEqual(b.output,['FLOGGFY_PRIVACY_V1:sender=0 ubi=0 early=0']);
 const {instance:s,state}=sender();let resume;
 const capturedFlush=s.flush.bind(s),inFlight=capturedFlush(new Promise(resolve=>resume=resolve));
 s.storeEvent(true,'queued-before-observation');b.observe(registry(s));
 assert.deepEqual(b.cancelled,[42]);assert.equal(b.output.at(-1),'FLOGGFY_PRIVACY_V1:sender=1 ubi=0 early=0');
 await s.send('new-event');assert.deepEqual(state.queue,['queued-before-observation'],'producer must not append events');
 resume();await inFlight;assert.equal(state.uploads,0,'captured flush dispatches through suppressed methods after its await');
 await s.sendEvents();await s.sendESStats();s.initSendingEvents();
 assert.equal((await s.lastFlush()).fired,true);assert.equal((await s.finalFlush()).fired,true);
 assert.equal(state.uploads,0);const patched=s.storeEvent,count=b.output.length;
 b.observe(registry(s));vm.runInContext(script,b.context);assert.strictEqual(s.storeEvent,patched);
 assert.strictEqual(b.window.__floggfyPrivacyObserve,b.observe);assert.equal(b.output.length,count);assert.deepEqual(b.cancelled,[42]);
 const u=ubi(s);b.observe(u);assert.equal(b.output.at(-1),'FLOGGFY_PRIVACY_V1:sender=1 ubi=1 early=0');
 assert.equal(JSON.stringify(u.logInteraction()),'{"interactionId":null,"pageInstanceId":null}');
 // Module 65025 saves result.impressionId and stops polling only when every
 // visible entity has a truthy ID. A null replacement causes repeated logging.
 let polling=true,polls=0;const entity={};
 while(polling&&polls<3){polls++;if(!entity.impressionId){const result=u.logImpressionV2();entity.impressionId=result.impressionId;entity.pagePresentationId=result.pagePresentationId;}if(entity.impressionId)polling=false;}
 assert.equal(polling,false,'original impression identity must stop visibility polling');assert.equal(polls,1);
 assert.equal(entity.impressionId,'impression-1');assert.equal(entity.pagePresentationId,7);
 assert.equal(u.logImpression(),'impression-2','legacy caller receives scalar identity');
 assert.equal(u.logImpressionSegment({impressionId:entity.impressionId}),undefined);
 assert.deepEqual(u.emitted,['impression-1','impression-2','segment'],'local impression bookkeeping remains active');
 assert.deepEqual(state.queue,['queued-before-observation'],'impressions and segments never reach event storage');
 assert.equal(state.uploads,0);
 const location={pageId:'local'};u.logPageTransition(location);assert.strictEqual(u.location,location,'navigation state updates remain functional');
 for(const line of b.output)assert.match(line,/^FLOGGFY_PRIVACY_V1:sender=[01] ubi=[01] early=0$/);

 const untouched=boot(),other=sender();const original=other.instance.storeEvent;
 untouched.observe(other.instance);untouched.observe(registry(other.instance,Symbol.for('PlayerAPI')));
 untouched.observe({send(){},flush(){},logInteraction(){}});assert.strictEqual(other.instance.storeEvent,original);
 let getters=0;const evil={};for(const key of ['_map','ubiLogger','logInteraction'])Object.defineProperty(evil,key,{get(){getters++;throw Error('getter');}});
 untouched.observe(evil);const record={};Object.defineProperty(record,'instance',{get(){getters++;throw Error('instance getter');}});
 untouched.observe({_map:new Map([[Symbol.for('EventSender'),record]])});
 const bad=sender();Object.defineProperty(bad.instance,'storeEvent',{get(){getters++;throw Error('method getter');}});
 untouched.observe(registry(bad.instance));assert.equal(getters,0);assert.equal(untouched.output.length,1);
 assert.equal(Object.prototype.hasOwnProperty.call(Object.prototype,'storeEvent'),false);

 const partial=boot(),locked=sender();Object.defineProperty(locked.instance,'storeEvent',{value:locked.instance.storeEvent,writable:false});
 partial.observe(registry(locked.instance));assert.equal(partial.output.at(-1),'FLOGGFY_PRIVACY_V1:sender=0 ubi=0 early=0');
 await locked.instance.sendEvents();await locked.instance.sendESStats();assert.equal(locked.state.uploads,0,'writable sinks still suppressed when one method cannot be patched');
 assert.deepEqual(partial.cancelled,[42]);
 const lockedUBI=ubi(locked.instance);Object.defineProperty(lockedUBI,'logInteraction',{value:lockedUBI.logInteraction,writable:false});
 partial.observe(lockedUBI);assert.equal(partial.output.at(-1),'FLOGGFY_PRIVACY_V1:sender=0 ubi=0 early=0');
 assert.equal(lockedUBI.logImpression(),'impression-1');
 const unhealthy=boot(),unhealthySender=sender();Object.defineProperty(unhealthySender.instance,'storeEvent',{value:unhealthySender.instance.storeEvent,writable:false});
 unhealthy.observe(ubi(unhealthySender.instance));
 assert.equal(unhealthy.output.at(-1),'FLOGGFY_PRIVACY_V1:sender=0 ubi=0 early=0','UBI coverage requires its sender sinks to be protected');
 const unknownTimer=boot(),hidden=sender();
 Object.defineProperty(hidden.instance,'sendEventsInterval',{get(){getters++;throw Error('timer getter');}});
 unknownTimer.observe(registry(hidden.instance));assert.equal(getters,0);
 assert.equal(unknownTimer.output.at(-1),'FLOGGFY_PRIVACY_V1:sender=0 ubi=0 early=0','uninspectable timer keeps coverage partial');
 const failedTimer=boot(),running=sender();failedTimer.context.clearInterval=()=>{throw Error('unavailable');};
 failedTimer.observe(registry(running.instance));assert.equal(failedTimer.output.at(-1),'FLOGGFY_PRIVACY_V1:sender=0 ubi=0 early=0');
 const replacement=sender();b.observe(registry(replacement.instance));
 assert.deepEqual(b.cancelled,[42,42],'new sender instance is independently suppressed');
 await replacement.instance.send('new instance');assert.equal(replacement.state.queue.length,0);
 console.log('PASS: audited telemetry producer queues, timer, contracts, partial coverage, idempotence and getter isolation');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
