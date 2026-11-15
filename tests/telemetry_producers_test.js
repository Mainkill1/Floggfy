const fs=require('fs'),vm=require('vm'),assert=require('assert');
const path='native/telemetry_producers.js';
const script=fs.existsSync(path)?fs.readFileSync(path,'utf8'):'';
const defaults={block_telemetry:true,block_event_reports:true,block_playback_reports:true,block_growth_analytics:true,block_sentry:true,block_crash_reports:true,block_internal_logging:true,block_track_errors:true,block_ai_telemetry:true};
function boot(options={},window={}){
 const output=[],cancelled=[];
 const context=vm.createContext({window,__floggfyPrivacyOptions:{...defaults,...options},URL,Request,Headers,Blob,File,TextEncoder,btoa,Event,DOMException,queueMicrotask,console:{log:line=>output.push(line)},clearInterval:id=>cancelled.push(id)});
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
 await requestGuards();
 await rendererDumps();
 await headerPairDumps();
 const b=boot();assert(b.output.includes('FLOGGFY_PRIVACY_V1:sender=0 ubi=0 early=0'));
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
 for(const line of b.output)assert.match(line,/^FLOGGFY_PRIVACY_(?:V1:sender=[01] ubi=[01] early=0|V2:fetch=[01] xhr=[01] beacon=[01] cosmos=[01] early=0|COUNT_V1:[a-z_]+=1)$/);

 const untouched=boot(),other=sender();const original=other.instance.storeEvent;
 untouched.observe(other.instance);untouched.observe(registry(other.instance,Symbol.for('PlayerAPI')));
 untouched.observe({send(){},flush(){},logInteraction(){}});assert.strictEqual(other.instance.storeEvent,original);
 let getters=0;const evil={};for(const key of ['_map','ubiLogger','logInteraction'])Object.defineProperty(evil,key,{get(){getters++;throw Error('getter');}});
 untouched.observe(evil);const record={};Object.defineProperty(record,'instance',{get(){getters++;throw Error('instance getter');}});
 untouched.observe({_map:new Map([[Symbol.for('EventSender'),record]])});
 const bad=sender();Object.defineProperty(bad.instance,'storeEvent',{get(){getters++;throw Error('method getter');}});
 untouched.observe(registry(bad.instance));assert.equal(getters,0);assert.equal(untouched.output.filter(line=>line.startsWith('FLOGGFY_PRIVACY_V1:')).length,1);
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

// These exercise the injected script against native API boundaries. Restoring a
// blocked call, changing the receiver/arguments, or settling success fails them.
function networkWindow(){
 const calls=[],failure=new Error('native failure');
 class XHR extends EventTarget {
  constructor(){super();this.readyState=0;this.status=0;this.events=[];for(const name of ['abort','loadend'])this.addEventListener(name,()=>this.events.push(name));}
  open(method,url,async=true){if(url==='bad')throw failure;this.readyState=1;this.async=async;calls.push(['open',this,...arguments]);}
  setRequestHeader(name,value){calls.push(['header',this,name,value]);}
  send(body){if(this.readyState!==1)throw new DOMException('Invalid state','InvalidStateError');calls.push(['send',this,body]);}
  abort(){calls.push(['abort',this]);this.readyState=0;}
 }
 XHR.DONE=4;
 const window={location:{href:'https://www.spotify.com/'},XMLHttpRequest:XHR,
  fetch:function(...args){calls.push(['fetch',this,...args]);if(args[0]==='fail')throw failure;return Promise.resolve({ok:true});},
  navigator:{sendBeacon:function(...args){calls.push(['beacon',this,...args]);return true;}},
  sendCosmosRequest:function(...args){calls.push(['cosmos',this,...args]);if(args[0]&&args[0].throwNative)throw failure;return 27;},
  cancelCosmosRequest:function(...args){calls.push(['cancel',this,...args]);return 'cancelled';}};
 return {window,calls,failure,XHR};
}
const routes=[
 ['event_reports','https://spclient.wg.spotify.com/gabo-receiver-service/v3/events'],
 ['playback_reports','https://spclient.wg.spotify.com/melody/v1/msg/batch'],
 ['growth_analytics','https://www.spotify.com/api/growth-events/wwwanalytics'],
 ['sentry','https://o22381.ingest.sentry.io/api/114855/envelope/'],
 ['crash_reports','https://crashdump.spotify.com:443/upload'],
 ['internal_logging','sp://logging/v3/events'],
 ['track_errors','https://spclient.wg.spotify.com/track-error/v1/errors'],
 ['ai_telemetry','https://spclient.wg.spotify.com/v1/telemetry/sandbox']
];
async function requestGuards(){
 const fixture=networkWindow(),b=boot({},fixture.window),w=b.window;
 for(const [category,url] of routes){
  await assert.rejects(w.fetch(url,{method:'POST'}),error=>error.name==='TypeError');
  assert.equal(w.navigator.sendBeacon(url,'payload'),false);
  assert(b.output.includes('FLOGGFY_PRIVACY_COUNT_V1:'+category+'=1'));
 }
 assert.equal(fixture.calls.length,0,'blocked APIs never reach native send');
 for(const url of ['https://spclient.wg.spotify.com/melody/v1/license_url','https://accounts.spotify.com/login','https://spclient.wg.spotify.com/metadata/4/track/id','https://spclient.wg.spotify.com/color-lyrics/v2/track/id','https://spclient.wg.spotify.com/track-error/v1/errors-extra','https://example.com/melody/v1/msg/batch','sp://logging/v4/events','https://o22381.ingest.sentry.io/api/999/envelope/']){
  assert.equal((await w.fetch(url)).ok,true);assert.equal(w.navigator.sendBeacon(url),true);
 }
 const receiver={};const init={method:'GET'};await w.fetch.call(receiver,'https://example.com',init);
 assert.deepEqual(fixture.calls.at(-1),['fetch',receiver,'https://example.com',init]);
 assert.throws(()=>w.fetch('fail'),error=>error===fixture.failure);
 assert.throws(()=>new w.fetch('https://example.com'),TypeError,'fetch remains nonconstructible');
 await assert.rejects(w.fetch(new Request(routes[0][1])),error=>error.name==='TypeError');
 assert.equal(w.navigator.sendBeacon(new URL(routes[2][1])),false);
 assert.strictEqual(w.XMLHttpRequest,fixture.XHR,'native constructor and statics retained');assert.equal(w.XMLHttpRequest.DONE,4);
 const xhr=new w.XMLHttpRequest();xhr.open('POST',routes[1][1]);assert.equal(xhr.send('secret'),undefined);
 assert.equal(xhr.readyState,0);assert.equal(xhr.status,0);await Promise.resolve();assert.deepEqual(xhr.events,['abort','loadend']);
 assert.equal(fixture.calls.filter(call=>call[0]==='send').length,0);
 assert.throws(()=>xhr.send(),error=>error.name==='InvalidStateError');
 xhr.open('POST','https://example.com/allowed');xhr.send('allowed');assert.deepEqual(fixture.calls.at(-1),['send',xhr,'allowed']);
 for(const synchronous of [false,0,null,'']){xhr.open('POST',routes[0][1],synchronous);assert.throws(()=>xhr.send(),error=>error.name==='NetworkError','WebIDL false values keep synchronous error behavior');}
 assert.throws(()=>xhr.open('GET','bad'),error=>error===fixture.failure);
 const reopened=new w.XMLHttpRequest();reopened.open('POST',routes[0][1]);reopened.send();reopened.open('GET','https://example.com');await Promise.resolve();assert.deepEqual(reopened.events,[],'reopen invalidates queued cancellation notifications');
 let failures=0,successes=0;
 const send={request:JSON.stringify({uri:'sp://logging/v3/events',method:'POST',body:'{}',headers:{}}),persistent:false,onSuccess(){successes++;},onFailure(code,message){failures++;assert.equal(code,-1);assert.equal(typeof message,'string');}};
 const before=fixture.calls.length,token=w.sendCosmosRequest(send);assert(token);assert.equal(failures,0,'failure callback follows native asynchronous contract');
 assert.equal(w.cancelCosmosRequest(token),undefined);await Promise.resolve();assert.equal(failures,0,'cancelled native requests must not call their callbacks');assert.equal(successes,0);assert.equal(fixture.calls.length,before);
 w.sendCosmosRequest(send);await Promise.resolve();assert.equal(failures,1);assert.equal(w.cancelCosmosRequest(token),undefined);assert.equal(fixture.calls.length,before,'synthetic tokens never reach native cancellation, including repeat cancels');
 const allowed={...send,request:JSON.stringify({uri:'sp://player/v2/main',method:'GET',headers:{},body:''})};
 assert.equal(w.sendCosmosRequest.call(receiver,allowed),27);assert.deepEqual(fixture.calls.at(-1),['cosmos',receiver,allowed]);
 assert.equal(w.cancelCosmosRequest.call(receiver,27),'cancelled');assert.deepEqual(fixture.calls.at(-1),['cancel',receiver,27]);
 const malformed={...send,request:'bad json'};assert.equal(w.sendCosmosRequest(malformed),27);
 assert.throws(()=>w.sendCosmosRequest({throwNative:true}),error=>error===fixture.failure);
 const late=networkWindow(),sendOriginal=late.window.sendCosmosRequest,cancelOriginal=late.window.cancelCosmosRequest;
 delete late.window.sendCosmosRequest;delete late.window.cancelCosmosRequest;const discovered=boot({},late.window);
 discovered.window.sendCosmosRequest=sendOriginal;discovered.window.cancelCosmosRequest=cancelOriginal;discovered.observe({});
 assert.notStrictEqual(discovered.window.sendCosmosRequest,sendOriginal);discovered.window.sendCosmosRequest(send);await Promise.resolve();assert.equal(failures,2);
 const partial=networkWindow();Object.defineProperty(partial.window,'cancelCosmosRequest',{value:partial.window.cancelCosmosRequest,writable:false});const unsupported=boot({},partial.window);
 assert(unsupported.output.includes('FLOGGFY_PRIVACY_V2:fetch=1 xhr=1 beacon=1 cosmos=0 early=0'));
 assert.equal(unsupported.window.sendCosmosRequest(send),27,'unwrappable cancellation keeps entire Cosmos bridge untouched');
 const locked=networkWindow(),originalOpen=locked.XHR.prototype.open,originalCancel=locked.window.cancelCosmosRequest;
 Object.defineProperty(locked.XHR.prototype,'send',{value:locked.XHR.prototype.send,writable:false});
 Object.defineProperty(locked.window,'sendCosmosRequest',{value:locked.window.sendCosmosRequest,writable:false});
 const lockedBoot=boot({},locked.window);for(let i=0;i<50;i++)lockedBoot.observe({});
 assert.strictEqual(locked.XHR.prototype.open,originalOpen,'failed paired XHR installation never stacks open wrappers');
 assert.strictEqual(locked.window.cancelCosmosRequest,originalCancel,'failed paired Cosmos installation never stacks cancel wrappers');
 for(const [category,url] of routes){
  const current=networkWindow(),disabled=boot({['block_'+category]:false},current.window);
  assert.equal((await disabled.window.fetch(url)).ok,true,category+'=0 must restore fetch');
  assert.equal(disabled.window.navigator.sendBeacon(url),true);
  const xhr=new disabled.window.XMLHttpRequest();xhr.open('POST',url);xhr.send('restored');assert.equal(current.calls.at(-1)[0],'send',category+'=0 must restore XHR');
  if(category==='internal_logging')assert.equal(disabled.window.sendCosmosRequest(send),27,'internal logging zero restores Cosmos');
  const item=sender();disabled.observe(registry(item.instance));
  if(category==='event_reports'){await item.instance.send('restored');assert.equal(item.state.queue.length,1);await assert.rejects(disabled.window.fetch(routes[1][1]),error=>error.name==='TypeError');}
 }
 const off=networkWindow(),originalFetch=off.window.fetch,originalSend=off.window.sendCosmosRequest,disabled=boot({block_telemetry:false},off.window),producer=sender();disabled.observe(registry(producer.instance));
 assert.strictEqual(off.window.fetch,originalFetch);assert.strictEqual(off.window.sendCosmosRequest,originalSend);await producer.instance.send('restored');assert.equal(producer.state.queue.length,1);
 console.log('PASS: renderer category selection, request failures, XHR cancellation, Cosmos callbacks, late discovery and native forwarding');
}

function dumps(booted){return booted.output.filter(line=>line.startsWith('FLOGGFY_PRIVACY_DUMP_V1:')).map(line=>{
 assert(line.length<=400000,'renderer message remains bounded');const fields=line.slice('FLOGGFY_PRIVACY_DUMP_V1:'.length).split('\n');assert.equal(fields.length,9);
 const decode=index=>Buffer.from(fields[index],'base64').toString('utf8');
 return {hook:fields[0],timestamp:Number(fields[1]),method:decode(2),url:decode(3),type:decode(4),encoding:decode(5),flags:Number(fields[6]),reported:Number(fields[7]),body:Buffer.from(fields[8],'base64')};
});}
async function rendererDumps(){
 const net=networkWindow(),b=boot({dump_telemetry:true},net.window),payload='{"event":"media_playback_event_v2","title":"音楽 🎵"}';
 const url=routes[1][1]+'?token=private#fragment';
 await assert.rejects(b.window.fetch(url,{method:'POST',body:payload,headers:{'Content-Type':'application/json','Content-Encoding':'identity',Authorization:'never record'}}));
 const first=dumps(b)[0];assert(first,'blocked playback JSON must cross the dump bridge');
 assert.equal(first.hook,'fetch');assert.equal(first.method,'POST');assert.equal(first.url,routes[1][1]);assert.equal(first.type,'application/json');assert.equal(first.encoding,'identity');
 assert.equal(first.body.toString('utf8'),payload);assert.equal(first.reported,Buffer.byteLength(payload));assert.equal(first.flags,1);assert(Math.abs(first.timestamp-Date.now())<5000);assert.equal(net.calls.length,0);
 assert(!b.output.some(line=>line.includes('never record')||line.includes('token=private')));
 let credentialReads=0;const privateHeaders={'Content-Type':'application/json',get Authorization(){credentialReads++;throw Error('credential getter');}};await assert.rejects(b.window.fetch(url,{method:'POST',body:payload,headers:privateHeaders}));assert.equal(credentialReads,0);assert.equal(dumps(b).at(-1).body.toString(),payload);
 let reads=0;const accessor={get body(){reads++;throw Error('body getter');},get headers(){reads++;throw Error('headers getter');}};
 const disabled=boot({},networkWindow().window);await assert.rejects(disabled.window.fetch(url,accessor));assert.equal(dumps(disabled).length,0);assert.equal(reads,0);
 await b.window.fetch('https://example.com',accessor);assert.equal(reads,0,'allowed calls are not inspected by the dump path');
 await assert.rejects(b.window.fetch(url,accessor));assert.equal(reads,0);assert(dumps(b).at(-1).flags&4,'accessor body is explicitly unsupported');
 const binary=new Uint8Array([0,1,2,255]);assert.equal(b.window.navigator.sendBeacon(routes[2][1],binary),false);assert.deepEqual(dumps(b).at(-1).body,Buffer.from(binary));
 const blob=new Blob([binary],{type:'application/octet-stream'});assert.equal(b.window.navigator.sendBeacon(routes[2][1],blob),false);await new Promise(resolve=>setImmediate(resolve));
 assert.deepEqual(dumps(b).at(-1).body,Buffer.from(binary));assert.equal(dumps(b).at(-1).type,'application/octet-stream');
 const xhr=new b.window.XMLHttpRequest();xhr.open('POST',url);xhr.setRequestHeader('Content-Type','application/json');xhr.setRequestHeader('Content-Encoding','gzip');xhr.setRequestHeader('Authorization','secret');xhr.send(payload);
 const x=dumps(b).at(-1);assert.equal(x.hook,'xhr');assert.equal(x.body.toString(),payload);assert.equal(x.type,'application/json');assert.equal(x.encoding,'gzip');assert.equal(x.url,routes[1][1]);
 const request=new Request(url,{method:'POST',body:payload,headers:{'Content-Type':'application/json'}});await assert.rejects(b.window.fetch(request));
 const requestDump=dumps(b).at(-1);assert.equal(requestDump.method,'POST');assert.equal(requestDump.type,'application/json');assert(requestDump.flags&1);assert(requestDump.flags&4);assert.equal(requestDump.body.length,0);assert.equal(request.bodyUsed,false,'Request stream remains untouched');
 for(const body of [null,undefined]){await assert.rejects(b.window.fetch(request,{body}));const inheritedRequest=dumps(b).at(-1);assert.equal(inheritedRequest.flags&5,5,'null/undefined init body preserves unconsumed Request-body omission');assert.equal(inheritedRequest.body.length,0);assert.equal(request.bodyUsed,false);}
 const large='😀'.repeat(100000);await assert.rejects(b.window.fetch(url,{method:'POST',body:large}));
 const capped=dumps(b).at(-1);assert.equal(capped.body.length,262144);assert.equal(capped.reported,400000);assert(capped.flags&2);assert(!capped.body.toString().includes('\ufffd'),'UTF8 cap does not cut code points');
 const tooLarge='x'.repeat(1048577);await assert.rejects(b.window.fetch(url,{method:'POST',body:tooLarge}));const unknownLength=dumps(b).at(-1);assert.equal(unknownLength.reported,0);assert.equal(unknownLength.body.length,0);assert.equal(unknownLength.flags&7,7,'unbounded strings explicitly omit an unknown-length payload');
 const inherited=Object.create({get body(){reads++;throw Error('inherited body');}});await assert.rejects(b.window.fetch(url,inherited));assert.equal(reads,0);assert(dumps(b).at(-1).flags&4,'inherited bodies are unsupported without evaluating getters');
 const file=new File(['file bytes'], 'private-file.txt',{type:'text/plain'});b.window.navigator.sendBeacon(routes[2][1],file);const omitted=dumps(b).at(-1);assert(omitted.flags&4);assert.equal(omitted.reported,10);assert.equal(omitted.body.length,0);assert(!b.output.some(line=>line.includes('private-file.txt')));
 const blobBoot=boot({dump_telemetry:true},networkWindow().window);for(let i=0;i<9;i++)blobBoot.window.navigator.sendBeacon(routes[2][1],blob);
 assert.equal(dumps(blobBoot).length,1,'only eight Blob captures may be pending');assert(dumps(blobBoot)[0].flags&4);await new Promise(resolve=>setImmediate(resolve));assert.equal(dumps(blobBoot).length,9);
 const unsupported={get secret(){reads++;throw Error('do not inspect');}};b.window.navigator.sendBeacon(routes[2][1],unsupported);assert.equal(reads,0);assert(dumps(b).at(-1).flags&4);
 const cosmos={request:JSON.stringify({uri:'sp://logging/v3/events?secret=yes',method:'POST',body:payload,headers:{'content-type':'application/json'}}),persistent:false,onSuccess(){throw Error('success');},onFailure(){}};
 b.window.sendCosmosRequest(cosmos);const c=dumps(b).at(-1);assert.equal(c.hook,'cosmos');assert.equal(c.url,'sp://logging/v3/events');assert.equal(c.body.toString(),payload);
 for(let i=0;i<45;i++)b.window.navigator.sendBeacon(routes[2][1],'bounded');assert.equal(dumps(b).length,32,'per-frame bridge output count is capped while requests stay blocked');
 assert.equal(net.calls.filter(call=>['fetch','send','beacon','cosmos'].includes(call[0])).length,1,'only the explicit allowed request reached the native API');
 console.log('PASS: blocked renderer payload bridge, opt-in isolation, bytes/Blob capture, stream omissions and dump bounds');
}
async function headerPairDumps(){
 const net=networkWindow(),b=boot({dump_telemetry:true},net.window),payload='{"event":"media_playback_event_v2"}',url=routes[1][1];let reads=0;
 const credential=['Authorization'];Object.defineProperty(credential,'1',{get(){reads++;throw Error('credential value');}});
 const headers=[['Content-Type','application/json'],['CONTENT-ENCODING','identity'],credential];Object.defineProperty(headers,Symbol.iterator,{get(){reads++;throw Error('iterator');}});
 await assert.rejects(b.window.fetch(url,{method:'POST',body:payload,headers}));
 let record=dumps(b).at(-1);assert.equal(record.type,'application/json');assert.equal(record.encoding,'identity');assert.equal(record.flags,1);assert.equal(record.body.toString(),payload);assert.equal(reads,0);
 for(const headers of [{},{Authorization:'secret'},{get Authorization(){reads++;throw Error('credential getter');}}]){await assert.rejects(b.window.fetch(url,{method:'POST',body:payload,headers}));record=dumps(b).at(-1);assert.equal(record.flags,1,'missing type/encoding is valid metadata');assert.equal(record.body.toString(),payload);assert.equal(reads,0);}
 const invalid=[['Content-Type','application/json'],['missing-value']];await assert.rejects(b.window.fetch(url,{method:'POST',body:payload,headers:invalid}));
 record=dumps(b).at(-1);assert.equal(record.flags,9,'header errors mark metadata incomplete while the full body stays supported');assert.equal(record.body.toString(),payload);
 const accessorPair=['Content-Type'];Object.defineProperty(accessorPair,'1',{get(){reads++;throw Error('type getter');}});
 const arrayWithAccessor=[];Object.defineProperty(arrayWithAccessor,'0',{get(){reads++;throw Error('pair getter');}});
 for(const metadata of [{headers:[accessorPair]},{headers:arrayWithAccessor},{method:{}}]){
  await assert.rejects(b.window.fetch(url,{body:payload,...metadata}));assert.equal(dumps(b).at(-1).flags,9);assert.equal(dumps(b).at(-1).body.toString(),payload);
 }
 // Preserve the accessor itself instead of invoking it through object spread.
 const accessorInit={body:payload};Object.defineProperty(accessorInit,'headers',{get(){reads++;throw Error('headers getter');}});await assert.rejects(b.window.fetch(url,accessorInit));
 record=dumps(b).at(-1);assert.equal(record.flags,9);assert.equal(record.body.toString(),payload);assert.equal(reads,0);
 const capped=Array.from({length:128},()=>['unrelated','value']);Object.defineProperty(capped,'128',{get(){reads++;throw Error('beyond cap');}});
 await assert.rejects(b.window.fetch(url,{body:payload,headers:capped}));record=dumps(b).at(-1);assert.equal(record.flags,9);assert.equal(reads,0);assert.equal(net.calls.length,0);
 console.log('PASS: bounded header pairs preserve JSON metadata without iterators, credential reads or false body-unsupported flags');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
