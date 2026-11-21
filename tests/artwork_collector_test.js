const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/artwork_collector.js','utf8');
const uri='spotify:track:0000000000000000000001';
const image='https://i.scdn.co/image/'+'a'.repeat(40),nativeImage='spotify:image:'+'a'.repeat(40);
const varint=n=>{const out=[];do{let b=Number(n&127n);n>>=7n;out.push(b|(n?128:0));}while(n);return out;};
const bytes=(field,b)=>[...varint(BigInt(field*8+2)),...varint(BigInt(b.length)),...b];
const string=(field,s)=>bytes(field,Buffer.from(s));
const number=(field,n)=>[...varint(BigInt(field*8)),...varint(BigInt(n))];
const status=(key=image)=>Uint8Array.from(bytes(1,[...string(1,key),...number(2,1)]));
const pixels=Uint8Array.of(82,73,70,70,1,2,3,255);
const imageReply=(b=pixels,source=2)=>Uint8Array.from([...number(2,source),...bytes(3,b)]);
const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
function harness(){
 const output=[],timers=new Map();let sequence=0,now=0;
 const window={__floggfyConfig:{metadata:true}};
 const context={window,console:{info:x=>output.push(x)},Uint8Array,ArrayBuffer,TextDecoder,performance:{now:()=>now},
  btoa:x=>Buffer.from(x,'binary').toString('base64'),encodeURIComponent,
  setTimeout:(fn,ms)=>{const id=++sequence;timers.set(id,{fn,at:now+ms});return id;},clearTimeout:id=>timers.delete(id)};
 vm.runInNewContext(script,context);
 const messages=()=>output.map(x=>{assert(x.startsWith('FLOGGFY_ARTWORK_V1:id='));return Object.fromEntries(new URLSearchParams(x.split(':').slice(1).join(':')));});
 return {window,output,messages,advance(ms){now+=ms;for(const [id,t] of [...timers])if(t.at<=now){timers.delete(id);t.fn();}}};
}
function transport(fn){return {callSingle:fn,callStream(){throw Error('stream calls forbidden');}};}
async function main(){
 {
  const h=harness(),calls=[];
  h.window.__floggfyArtworkObserve({_esperantoTransport:transport(request=>{calls.push(request);return Promise.resolve(request.method==='GetCacheStatus'?status():imageReply());})});
  assert(h.window.__floggfyArtwork('1',uri,[image]));await flush();
  const result=h.messages()[0];assert.equal(result.status,'ready');assert.equal(result.uri,uri);assert.equal(result.reference,image);assert.equal(result.source,'2');
  assert.deepEqual(Buffer.from(result.data,'base64url'),Buffer.from(pixels));assert(!/[+/=]/.test(result.data));
  assert.equal(calls.length,2);assert(calls.every(x=>x.service==='spotify.image.esperanto.proto.Image'));
  assert(Buffer.from(calls[1].payload).includes(Buffer.from(image)));
 }
 {
  const h=harness(),calls=[];h.window.__floggfyArtwork('2',uri,[image]);
  h.window.__floggfyArtworkObserve({transport:transport(request=>{calls.push(request.method);return Promise.resolve(request.method==='GetCacheStatus'?status(nativeImage):imageReply());})});
  await flush();assert.equal(h.messages()[0].status,'ready');assert.equal(h.messages()[0].reference,image);assert.deepEqual(calls,['GetCacheStatus','GetImage']);
 }
 {
  const h=harness(),calls=[];h.window.__floggfyArtworkObserve(transport(r=>{calls.push(r.method);return Promise.resolve(new Uint8Array());}));
  h.window.__floggfyArtwork('3',uri,[image]);await flush();assert.equal(h.messages()[0].reason,'cache-miss');assert.deepEqual(calls,['GetCacheStatus']);
 }
 {
  const h=harness();let invoked=0;const trap={};Object.defineProperty(trap,'_esperantoTransport',{get(){invoked++;throw Error('getter');}});
  h.window.__floggfyArtworkObserve(trap);h.window.__floggfyArtwork('4',uri,[image]);h.advance(8000);await flush();
  assert.equal(invoked,0);assert.equal(h.messages()[0].reason,'service-unavailable');
  assert.equal(h.window.__floggfyArtwork('5','spotify:track:bad',[image]),false);
  assert.equal(h.window.__floggfyArtwork('5',uri,['https://wrong.invalid/image/'+'a'.repeat(40)]),false);
 }
 {
  const h=harness();let resolve,calls=0;h.window.__floggfyArtworkObserve(transport(()=>{calls++;return new Promise(r=>resolve=r);}));
  h.window.__floggfyArtwork('6',uri,[image]);h.advance(8000);await flush();assert.equal(h.messages()[0].reason,'timeout');
  h.window.__floggfyArtwork('7',uri,[image]);await flush();assert.equal(h.messages()[1].reason,'busy');assert.equal(calls,1);
  resolve(status());await flush();assert.equal(calls,1);assert.equal(h.messages().length,2);
 }
 {
  const h=harness();let resolve,calls=0;h.window.__floggfyArtworkObserve(transport(r=>{calls++;return r.method==='GetCacheStatus'?Promise.resolve(status()):new Promise(ok=>resolve=ok);}));
  h.window.__floggfyArtwork('8',uri,[image]);await flush();h.window.__floggfyConfig.metadata=false;resolve(imageReply());await flush();
  assert.equal(h.messages()[0].reason,'disabled');assert(!h.messages()[0].data);assert.equal(calls,2);
 }
 {
  const h=harness(),calls=[];let resolveStatus;
  const config=h.window.__floggfyConfig;
  config.fetchMissingArtwork=false;
  h.window.__floggfyArtworkObserve(transport(request=>{
   calls.push(request.method);
   return request.method==='GetCacheStatus'?new Promise(resolve=>resolveStatus=resolve):Promise.resolve(imageReply());
  }));
  h.window.__floggfyArtwork('11',uri,[image]);await flush();
  assert.deepEqual(calls,[]);assert.equal(h.messages()[0].reason,'disabled');
  config.fetchMissingArtwork=true;
  h.window.__floggfyArtwork('12',uri,[image]);
  config.fetchMissingArtwork=false;resolveStatus(status());await flush();
  assert.deepEqual(calls,['GetCacheStatus']);assert.equal(h.messages()[1].reason,'disabled');
  config.fetchMissingArtwork=true;
  h.window.__floggfyArtwork('13',uri,[image]);resolveStatus(status());await flush();
  assert.deepEqual(calls,['GetCacheStatus','GetCacheStatus','GetImage']);assert.equal(h.messages()[2].status,'ready');
 }
 {
  const h=harness(),calls=[];let resolveImage;
  h.window.__floggfyArtworkObserve(transport(request=>{
   calls.push(request.method);
   return request.method==='GetCacheStatus'?Promise.resolve(status()):new Promise(resolve=>resolveImage=resolve);
  }));
  h.window.__floggfyArtwork('14',uri,[image]);await flush();
  h.window.__floggfyConfig.fetchMissingArtwork=false;resolveImage(imageReply());await flush();
  assert.deepEqual(calls,['GetCacheStatus','GetImage']);
  assert.equal(h.messages()[0].status,'ready');
 }
 for(const body of [Uint8Array.of(26,255),new Uint8Array(4*1024*1024+65537),Uint8Array.from([...number(2,2),...bytes(3,pixels),...bytes(3,pixels)]),imageReply(pixels,4294967298)]){
  const h=harness();h.window.__floggfyArtworkObserve(transport(r=>Promise.resolve(r.method==='GetCacheStatus'?status():body)));
  h.window.__floggfyArtwork('9',uri,[image]);await flush();assert.equal(h.messages()[0].status,'unavailable');assert.equal(h.messages()[0].reason,'invalid-response');assert(!h.messages()[0].data);
 }
 {
  const h=harness();h.window.__floggfyArtworkObserve(transport(r=>Promise.resolve(r.method==='GetCacheStatus'?status():imageReply(pixels,3))));
  h.window.__floggfyArtwork('10','spotify:episode:0000000000000000000002',[image]);await flush();
  assert.equal(h.messages()[0].source,'3');assert.equal(h.messages()[0].uri,'spotify:episode:0000000000000000000002');
 }
 console.log('PASS: native artwork cache lookup, identity, delayed discovery, physical request bound, disabled/late results, malformed payloads and source reporting');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
