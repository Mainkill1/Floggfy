// Reuses the current client request builder; never reads credentials or constructs services.
(() => {
 'use strict';
 if(window.__floggfyMetadata)return;
 window.__floggfyMetadata=true;
 const prefix='FLOGGFY_METADATA_V1:',seenPayload=new Map(),records=[];
 const serviceCache=new Map();
 let builder,activeRequests=0,connectDevices;
 let player,root,queue=[],seen=new Set(),walked=0,lastPlayback='',playbackTime=-Infinity;
 let timelineBinding;
 let timelineSeen=false,lastTimeline='',timelineTime=-Infinity,timelineBlocked='',timelineLocal='unknown';
 function playback(data){
  const payload=Object.entries(data).filter(([,v])=>v!==undefined&&v!==null&&v!=='').map(([k,v])=>k+'='+encodeURIComponent(String(v))).join('&');
  const now=performance.now();
  if(payload.length<=4096&&(payload!==lastPlayback||now-playbackTime>=1000)){
   console.info('FLOGGFY_PLAYBACK_V1:'+payload);lastPlayback=payload;playbackTime=now;
  }
 }
 function timeline(data){
  const payload=Object.entries(data).map(([k,v])=>k+'='+encodeURIComponent(String(v))).join('&'),now=performance.now();
  if(payload.length<=16384&&(payload!==lastTimeline||now-timelineTime>=500)){
   console.info('FLOGGFY_TIMELINE_V1:'+payload);lastTimeline=payload;timelineTime=now;
  }
 }
 function unavailableTimeline(reason){if(timelineSeen&&!timelineBlocked)timeline({v:1,unavailable:reason});}
 function guardTimeline(state){
  timelineLocal='unknown';
  try{const get=method(connectDevices,'getConnectState');const active=own(get&&get.call(connectDevices),'activeDevice');
   const flag=own(active,'isLocal');if(typeof flag==='boolean')timelineLocal=Number(flag);}catch{}
  timelineBlocked=timelineLocal===0?'remote-playback':own(state,'isBuffering')===true?'buffering':'';
  if(timelineBlocked){
   timelineSeen=true;const blocked={v:1,blocked:timelineBlocked};
   const uri=own(own(state,'item'),'uri'),stamp=own(state,'timestamp'),instance=own(state,'playbackId');
   if(timelineBlocked==='buffering'&&/^spotify:(?:track|episode):[A-Za-z0-9]{22}$/.test(uri||'')&&typeof stamp==='number'&&Number.isFinite(stamp)){
    blocked.uri=uri;blocked.instance=typeof instance==='string'&&instance.length<=128?instance:'';
    blocked.sampled_ms=Date.now();blocked.transition_ms=stamp;
    const position=own(state,'positionAsOfTimestamp');if(typeof position==='number'&&Number.isFinite(position)&&position>=0)blocked.position_ms=position;
   }
   timeline(blocked);
  }
 }
 function observeTimeline(state,data){
  if(timelineBlocked)return;
  const stamp=own(state,'timestamp'),position=own(state,'positionAsOfTimestamp'),duration=own(state,'duration');
  const paused=own(state,'isPaused'),buffering=own(state,'isBuffering'),speed=own(state,'speed');
  const sampled=Date.now(),elapsed=sampled-stamp;
  if(typeof stamp!=='number'||!Number.isFinite(stamp)||typeof position!=='number'||!Number.isFinite(position)||position<0||
     typeof duration!=='number'||!Number.isFinite(duration)||duration<=0||duration>86400000||position>duration||
     typeof paused!=='boolean'||typeof buffering!=='boolean'||elapsed< -250||elapsed>86400000||(speed!==undefined&&speed!==1)){
   unavailableTimeline('timeline-fields');return;
  }

  // Spicetify's Player.getProgress uses the same PlayerAPI clock/position.
  // Buffering is sent separately so native coverage cannot credit stalled audio.
  const seconds=Math.min(duration,position+(paused||buffering?0:Math.max(0,elapsed)))/1000;
  timelineSeen=true;
  timeline({v:1,title:data.title,artist:data.artist||'',album:data.album||'',uri:data.uri,
   duration:duration/1000,position:seconds,sampled_ms:sampled,instance:typeof own(state,'playbackId')==='string'?own(state,'playbackId'):'',paused:Number(paused),buffering:Number(buffering),local:timelineLocal});
 }
 function emitTimelineState(state){
  timelineBlocked='';
  const item=own(state,'item'),m=own(item,'metadata')||{},uri=text(own(item,'uri'));
  guardTimeline(state);
  if(!/^spotify:(?:track|episode):[A-Za-z0-9]{22}$/.test(uri||'')){unavailableTimeline('track-unavailable');return;}
  const show=own(item,'show')||{};
  const data={title:text(own(m,'title')||own(item,'name')),
   artist:text(own(m,'artist_name'))||artists(own(item,'artists'))[0]||text(own(m,'show.publisher')||own(m,'show_name')||own(show,'publisher')),
   album:text(own(m,'album_title')||own(show,'name')||own(m,'show_name')),uri};
  if(!data.title||(uri.startsWith('spotify:track:')&&(!data.artist||!data.album))){unavailableTimeline('identity-unavailable');return;}
  observeTimeline(state,data);
 }
 function bindTimeline(){
  if(timelineBinding?.player===player&&timelineBinding.active)return;
  if(timelineBinding){
   if(!timelineBinding.active)return;
   timelineBinding.active=false;
   try{timelineBinding.remove.call(timelineBinding.events,'update',timelineBinding.listener);timelineBinding=undefined;}catch{return;}
  }
  try{
   const get=method(player,'getEvents'),events=get&&get.call(player),add=method(events,'addListener'),remove=method(events,'removeListener');
   if(!add||!remove)return;
   const binding={player,events,remove,active:true};
   binding.listener=event=>{
    if(!binding.active)return;
    try{const state=own(event,'data');emitTimelineState(object(state)?state:method(binding.player,'getState').call(binding.player));}
    catch{unavailableTimeline('player-exception');}
   };
   timelineBinding=binding;
   try{add.call(events,'update',binding.listener);}catch{binding.active=false;}
  }catch{/* A changed event API leaves bounded native polling available. */}
 }
 const own=(o,k)=>{try{const d=Object.getOwnPropertyDescriptor(o,k);return d&&'value' in d?d.value:undefined;}catch{return undefined;}};
 const object=o=>o&&typeof o==='object';
 function remember(o){if(!records.includes(o)){if(records.length>=96)records.shift();records.push(o);}}
 function method(o,k){for(let i=0;object(o)&&i<4;i++,o=Object.getPrototypeOf(o)){const v=own(o,k);if(typeof v==='function')return v;}return undefined;}
 function options(){return own(window,'__floggfyConfig')||{};}
 // Debug observations are raw local flags, never a diagnosis of their cause.
 let debugPrevious;
 function debugObservation(state,item){
  if(own(options(),'debug')!==true||!object(state)||!object(item)){debugPrevious=undefined;return;}
  const boolean=v=>typeof v==='boolean'?Number(v):'?';
  const number=v=>Number.isSafeInteger(v)&&Math.abs(v)<=99999?v:'?';
  const identity=v=>typeof v==='string'&&v.length>0&&v.length<=256?v:undefined;
  const error=own(state,'error');
  const current={paused:boolean(own(state,'isPaused')),buffer:boolean(own(state,'isBuffering')),
   active:boolean(own(state,'isActiveDevice')),code:number(own(error,'code')??own(state,'errorCode')),
   status:number(own(error,'status')??own(state,'errorStatus')),
   instance:identity(own(state,'playbackId')),device:identity(own(own(state,'device'),'id'))};
  if(debugPrevious&&Object.keys(current).every(key=>Object.is(current[key],debugPrevious[key])))return;
  const changed=key=>current[key]===undefined?'?':debugPrevious&&debugPrevious[key]!==undefined?Number(current[key]!==debugPrevious[key]):0;
  const message='FLOGGFY_STATUS:client obs paused='+current.paused+' buf='+current.buffer+' active='+current.active+
   ' code='+current.code+' status='+current.status+' inst='+changed('instance')+' dev='+changed('device');
  debugPrevious=current;
  if(message.length<100)console.info(message);
 }
 // ConnectDevicesAPI supplies the playback target, not the sender of Pause.
 // Observe only already-created services. Never discover/transfer devices or
 // infer a remote command from the absence of a local client action.
 let connectPrevious,connectPaused,connectPausedTarget,controlBinding,controlQueue=[],controlOmitted=0;
 function connectEnabled(){return own(options(),'debug')===true&&own(options(),'connect')===true;}
 function connectStatus(message){const line='FLOGGFY_STATUS:'+message;if(line.length<100)console.info(line);}
 // Only our own enrichment operations: no transport monkey-patching, request
 // URLs, credentials, response bodies or arbitrary exception messages.
 let networkOperation=0;
 function networking(){return own(options(),'debug')===true&&own(options(),'networking')===true;}
 function networkStatus(op,kind,phase,status,started){
  if(!op||!networking())return;
  const code=Number.isSafeInteger(status)&&Math.abs(status)<=99999?status:'?';
  const elapsed=Math.max(0,Math.min(3600000,Math.round(performance.now()-started)));
  const line='FLOGGFY_NETWORK_V1:request op='+op+' kind='+kind+' phase='+phase+' code='+code+' elapsed_ms='+elapsed;
  if(line.length<160)try{console.info(line);}catch{/* Diagnostic failures must not affect requests. */}
 }
 function detachControls(){
  const binding=controlBinding;if(!binding)return true;
  if(!binding.active)return false; // Failed removal: retain at most one inert binding.
  binding.active=false;
  try{binding.remove.call(binding.events,'action',binding.listener);controlBinding=undefined;return true;}catch{return false;}
 }
 function syncControls(){
  if(controlBinding&&controlBinding.player===player&&controlBinding.active)return;
  if(!detachControls()||!player)return;
  try{
   const getEvents=method(player,'getEvents');if(!getEvents)return;
   const events=getEvents.call(player),add=method(events,'addListener'),remove=method(events,'removeListener');
   if(!add||!remove)return;
   const binding={player,events,remove,active:true};
   binding.listener=event=>{
    try{
     if(!binding.active||!connectEnabled())return;
     const action=own(own(event,'data'),'action');
     if(!['play','pause','resume','skipToNext','skipToPrevious'].includes(action))return;
     if(controlQueue.length<16)controlQueue.push(action);else controlOmitted=Math.min(1000000,controlOmitted+1);
    }catch{/* A diagnostic observer must never affect command dispatch. */}
   };
   controlBinding=binding; // Own cleanup even if addListener throws after attaching.
   try{add.call(events,'action',binding.listener);}catch{detachControls();}
  }catch{/* Changed event APIs leave commands untouched. */}
 }
 function connectDiagnostics(state){
  if(!connectEnabled()){
   detachControls();connectPrevious=undefined;connectPaused=undefined;connectPausedTarget=undefined;
   controlQueue=[];controlOmitted=0;return;
  }
  syncControls();
  let cached;
  try{const getter=method(connectDevices,'getConnectState');if(getter)cached=getter.call(connectDevices);}catch{}
  const raw=own(cached,'connectionStatus'),device=own(cached,'activeDevice');
  const status=typeof raw==='string'&&['NOT_CONNECTED','CONNECTING','CONNECTED','NO_DEVICES_AVAILABLE'].includes(raw)?
   (raw==='NO_DEVICES_AVAILABLE'?'no_devices':raw.toLowerCase()):'unknown';
  const local=own(device,'isLocal'),target=local===true?'local':local===false?'remote':'unknown';
  const value=own(device,'id'),id=typeof value==='string'&&value.length>0&&value.length<=256?value:undefined;
  const current={status,target,id};
  if(!connectPrevious||Object.keys(current).some(key=>current[key]!==connectPrevious[key])){
   const changed=id===undefined?'?':connectPrevious&&connectPrevious.id!==undefined?Number(id!==connectPrevious.id):0;
   connectStatus('connect state='+status+' target='+target+' device_changed='+changed+' controller=unknown');
  }
  connectPrevious=current;
  for(const action of controlQueue)connectStatus('control request='+action+' source=local-client');
  controlQueue=[];
  if(controlOmitted){connectStatus('control observations_omitted='+controlOmitted);controlOmitted=0;}
  const paused=own(state,'isPaused');
  if(typeof paused==='boolean'&&(paused!==connectPaused||target!==connectPausedTarget)){
   connectStatus('control state='+(paused?'paused':'playing')+' target='+target+' initiator=unknown');
  }
  connectPaused=typeof paused==='boolean'?paused:undefined;
  connectPausedTarget=target;
 }
 function requestState(uri){
  let entry=serviceCache.get(uri);
  if(!entry){entry={metadata:{attempts:0},album:{attempts:0},lyrics:{attempts:0}};serviceCache.set(uri,entry);}
  // Preserve in-flight entries so their bounds and cancellation remain observable.
  if(serviceCache.size>64)for(const [key,value] of serviceCache){if(key!==uri&&!value.metadata.pending&&!value.album.pending&&!value.lyrics.pending){serviceCache.delete(key);break;}}
  return entry;
 }
 function hexId(id){
  const alphabet='0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',bytes=Array(16).fill(0);
  for(const c of id){let carry=alphabet.indexOf(c);for(let i=15;i>=0;i--){carry+=bytes[i]*62;bytes[i]=carry&255;carry>>=8;}if(carry)return;}
  return bytes.map(x=>x.toString(16).padStart(2,'0')).join('');
 }
 function automatic(uri){
  const config=options(),now=performance.now();
  for(const entry of serviceCache.values())for(const kind of ['metadata','album','lyrics']){
   const r=entry[kind];
   if(r.pending&&(now-r.started>=12000||config.metadata!==true||(kind==='lyrics'&&config.lyrics===false))){
    // Expire only the logical operation. Abort may be unsupported or ignored;
    // its physical slot belongs to send() until that promise settles.
    networkStatus(r.operation,kind,'expired',undefined,r.started);
    r.pending=false;r.generation++;try{if(r.controller)r.controller.abort();}catch{}
    r.retryAt=now+5000;if(config.metadata!==true||(kind==='lyrics'&&config.lyrics===false))r.attempts=Math.max(0,r.attempts-1);
   }
  }
  if(config.metadata!==true||!builder||!uri)return;
  const entry=requestState(uri),id=uri.slice(14);
  for(const kind of ['metadata','album','lyrics']){
   const r=entry[kind];if(kind==='lyrics'&&config.lyrics===false)continue;
   if(r.data||r.done||r.pending||r.attempts>=2||now<(r.retryAt||0)||activeRequests>=4)continue;
   const albumId=text(own(own(entry.metadata.data,'album'),'gid'));if(kind==='album'&&!/^[a-f0-9]{32}$/.test(albumId||''))continue;
   const hex=kind==='metadata'&&hexId(id);if(kind==='metadata'&&!hex)continue;
   r.pending=true;r.started=now;r.attempts++;r.generation=(r.generation||0)+1;const generation=r.generation;activeRequests++;
   const operation=networking()?(networkOperation=networkOperation%1000000+1):0,started=now;
   r.operation=operation;
   const settled=()=>{
    activeRequests--;
    if(r.generation===generation){r.pending=false;r.controller=undefined;}
   };
   try{
    let request=method(builder,'build').call(builder);
    request=request.withHost('https://spclient.wg.spotify.com').withPath(kind==='metadata'?'/metadata/4/track/'+hex:kind==='album'?'/metadata/4/album/'+albumId:'/color-lyrics/v2/track/'+id).withQueryParameters({format:'json',vocalRemoval:false});
    if(typeof AbortController==='function'&&method(request,'withAbortSignal')){r.controller=new AbortController();request=request.withAbortSignal(r.controller.signal);}
    networkStatus(operation,kind,'send',undefined,started);
    Promise.resolve(request.send()).then(response=>{
     networkStatus(operation,kind,'response',own(response,'status'),started);
     if(r.generation!==generation)return;
     const data=own(response,'body');if(object(data)){r.data=kind==='album'?{album:data}:data;r.done=true;}else r.retryAt=performance.now()+5000;
    },error=>{const status=own(error,'status');networkStatus(operation,kind,'rejected',status,started);if(r.generation!==generation)return;r.done=status===400||status===401||status===403||status===404;r.retryAt=performance.now()+5000;}).then(settled,settled);
   }catch{networkStatus(operation,kind,'setup_error',undefined,started);r.pending=false;activeRequests--;r.retryAt=now+5000;}
  }
 }
 function scan(){
  const sp=own(window,'Spicetify'),wrapper=own(sp,'Player');
  const p=own(own(sp,'Platform'),'PlayerAPI')||own(wrapper,'origin')||wrapper;if(method(p,'getState'))player=p;
  const observeConnect=true; // Cached local-device identity is required even with diagnostics off.
  if(observeConnect)try{
   const candidate=own(own(sp,'Platform'),'ConnectDevicesAPI');
   if(method(candidate,'getConnectState'))connectDevices=candidate;
  }catch{}
  const main=document.querySelector('#main');
  const key=main&&Object.keys(main).find(k=>k.startsWith('__reactContainer$'));
  const current=key&&own(main,key);
  if(current!==root||!queue.length||walked>=8000){root=current;queue=current?[current]:[];seen=new Set();walked=0;}
  const clock=()=>performance.now(),deadline=clock()+4;
  while(queue.length&&walked<8000&&clock()<deadline){
   const o=queue.pop();if(!object(o)||seen.has(o))continue;
   seen.add(o);walked++;
   // getState is the PlayerAPI's local snapshot accessor. No other client
   // methods are called (including registry.resolve and query fetchers).
   const snapshot=method(o,'getState');
   if(snapshot&&method(o,'getEvents')){
    try{if(text(own(own(snapshot.call(o),'item'),'uri')))player=o;}catch{}
   }
   const services=own(o,'_map');
   try{let n=0;for(const [key,value] of Map.prototype.entries.call(services)){
    if(++n>160)break;
    if(String(key)==='Symbol(RequestBuilder)'){const candidate=own(value,'instance');if(method(candidate,'build'))builder=candidate;}
    if(String(key)==='Symbol(PlayerAPI)'){const candidate=own(value,'instance');if(method(candidate,'getState'))player=candidate;}
    if(observeConnect&&String(key)==='Symbol(ConnectDevicesAPI)'){const candidate=own(value,'instance');if(method(candidate,'getConnectState'))connectDevices=candidate;}
   }}catch{}
   if(own(o,'uri')||own(o,'trackUri')||own(o,'queryKey'))remember(o);
   try{let count=0;for(const v of Map.prototype.values.call(o)){if(++count>80||queue.length>=8192)break;if(object(v))queue.push(v);}}catch{}
   const priority=['memoizedProps','value','registry','platform','props','dependencies','firstContext','memoizedValue','child','sibling'];
   const keys=Object.keys(o).slice(0,80).sort((a,b)=>{
    const x=priority.indexOf(a),y=priority.indexOf(b);return (x<0?99:x)-(y<0?99:y);
   });
   for(const k of keys.reverse()){
    if(/token|credential|authorization|storage|session|user|cosmos|request|fetch/i.test(k))continue;
    const v=own(o,k);if(object(v)&&queue.length<8192)queue.push(v);
   }
  }
 }
 function text(v){return typeof v==='string'||typeof v==='number'?String(v):undefined;}
 function entries(v,limit){const out=[];if(Array.isArray(v))for(let i=0;i<Math.min(v.length,limit);i++)out.push(own(v,String(i)));return out;}
 function list(v){return Array.isArray(v)?entries(v,32).map(x=>text(x)||text(own(x,'name'))).filter(Boolean).join('; '):text(v);}
 function artists(v){return entries(Array.isArray(v)?v:own(v,'items'),32).map(x=>text(x)||text(own(x,'name'))||text(own(own(x,'profile'),'name'))).filter(Boolean);}
 function indexed(m,key){const names=[];const first=text(own(m,key));if(first)names.push(first);for(let i=1;i<32;i++){const n=text(own(m,key+':'+i)||own(m,key+'_'+i));if(n&&!names.includes(n))names.push(n);}return names;}
 function putArtists(data,tag,names){if(names.length&&(!data[tag]||names.length>=data[tag].split('; ').length))data[tag]=[...new Set(names)].join('; ');}
 function merge(data,source,currentItem=false){
  if(!object(source))return;
  const m=own(source,'metadata')||source,album=own(source,'album')||{},date=own(album,'date');
  let release=text(own(m,'release_date'))||text(own(m,'album_release_date'))||text(own(source,'release_date'))||text(own(own(source,'releaseDate'),'isoString'))||text(own(own(album,'releaseDate'),'isoString'));
  if(release&&/^\d{4}-\d{2}-\d{2}T/.test(release))release=release.slice(0,10);
  const year=text(own(date,'year')),month=text(own(date,'month')),day=text(own(date,'day'));
  if(!release&&/^\d{4}$/.test(year||'')){
   release=year;if(/^(?:[1-9]|1[0-2])$/.test(month||'')){release+='-'+month.padStart(2,'0');if(/^(?:[1-9]|[12][0-9]|3[01])$/.test(day||''))release+='-'+day.padStart(2,'0');}
  }
  if(!release)release=text(own(m,'year'));
  if(release&&/^\d{4}(?:-\d{2}(?:-\d{2})?)?$/.test(release)&&(!data.DATE||release.slice(0,4)!==data.DATE.slice(0,4)||release.length>=data.DATE.length)){data.DATE=release;data.YEAR=release.slice(0,4);}
  const names=artists(own(source,'artists')||own(m,'artists')||own(source,'artist'));
  putArtists(data,'ARTIST',names.length?names:indexed(m,'artist_name'));
  const albumNames=artists(own(album,'artists')||own(album,'artist'));
  const explicitAlbumNames=indexed(m,'album_artist_name');
  const albumCredits=currentItem&&explicitAlbumNames.length?explicitAlbumNames:albumNames.length?albumNames:explicitAlbumNames;
  // More track/guest credits do not make an album artist more accurate. The
  // current item's explicit album artist wins over older cached album lists.
  if(albumCredits.length&&(currentItem||!data.ALBUMARTIST))data.ALBUMARTIST=[...new Set(albumCredits)].join('; ');
  const label=text(own(m,'album_label')||own(m,'label')||own(source,'label')||own(album,'label'));
  if(label&&label.length<=4096){data.LABEL=label;data.ORGANIZATION=label;}
  const genres=list(own(m,'genre')||own(m,'genres')||own(source,'genres')||own(album,'genres')||own(album,'genre'));
  if(genres)data.GENRE=genres;
  for(const [tag,key] of [['TRACKNUMBER','number'],['TRACKNUMBER','album_track_number'],['DISCNUMBER','disc_number'],['DISCNUMBER','album_disc_number'],['DISCTOTAL','album_disc_count'],['TRACKTOTAL','album_track_count'],['ISRC','isrc'],['PUBLISHER','publisher'],['LANGUAGE','language'],['COPYRIGHT','copyright']]){
   const value=text(own(m,key)||own(source,key)||own(album,key));if(value&&value.length<=4096&&(!/NUMBER|TOTAL/.test(tag)||/^[1-9]\d{0,5}$/.test(value)))data[tag]=value;
  }
  const discs=entries(own(album,'disc'),100);if(discs.length){data.DISCTOTAL=String(discs.length);const total=discs.reduce((sum,disc)=>sum+entries(own(disc,'track'),1000).length,0);if(total)data.TRACKTOTAL=String(total);}
  const external=entries(own(source,'external_id'),32).find(x=>text(own(x,'type'))==='isrc');
  const isrc=text(own(external,'id'));if(isrc)data.ISRC=isrc;
  const language=list(own(source,'language_of_performance'));if(language)data.LANGUAGE=language;
  for(const copyright of entries(own(album,'copyright'),32)){
   const value=text(own(copyright,'text'));if(value&&value.length<=4096)data[own(copyright,'type')==='P'?'PHONOGRAMCOPYRIGHT':'COPYRIGHT']=value;
  }
  const urls=[];
  for(const key of ['image_xlarge_url','image_large_url','image_url','image_small_url']){const url=text(own(m,key));if(url)urls.push({url,size:key.includes('xlarge')?1000:key.includes('large')?640:0});}
  for(const image of [...entries(own(album,'images'),32),...entries(own(own(album,'cover_group'),'image'),32)]){
   const file=text(own(image,'file_id')),url=text(own(image,'url'))||(file&&/^[a-f0-9]{40}$/.test(file)?'https://i.scdn.co/image/'+file:undefined);
   if(url)urls.push({url,size:Number(own(image,'width'))||0});
  }
  const cover=urls.filter(x=>/^https:\/\/i\.scdn\.co\/image\/[a-f0-9]{40}$/.test(x.url)).sort((a,b)=>b.size-a.size)[0];
  if(cover&&(!data.artwork_url||cover.size>(data.__coverSize||0))){data.artwork_url=cover.url;Object.defineProperty(data,'__coverSize',{value:cover.size,writable:true,configurable:true});}
  if(options().lyrics===false)return;
  const lyric=own(source,'lyrics')||own(m,'lyrics');
  let words=text(lyric);
  const lines=Array.isArray(lyric)?lyric:own(lyric,'lines');
  let synced=false;
  if(Array.isArray(lines)&&lines.length<=2000){
   const values=entries(lines,2000),syncType=own(lyric,'syncType')||own(source,'syncType');
   const starts=values.map(l=>{const v=own(l,'startTimeMs')??own(l,'start');return typeof v==='number'?v:typeof v==='string'&&/^\d+$/.test(v)?Number(v):NaN;});
   synced=(syncType==='LINE_SYNCED'||syncType==='WORD_SYNCED')&&values.length>0&&starts.every((n,i)=>Number.isSafeInteger(n)&&n>=0&&n<=86400000&&(!i||n>=starts[i-1]))&&(starts.length===1||starts.at(-1)>0);
   words=values.map((l,i)=>{
    const line=text(own(l,'words'))||text(own(l,'text'))||'';
    if(!synced)return line;
    const ms=starts[i],stamp='['+String(Math.floor(ms/60000)).padStart(2,'0')+':'+String(Math.floor(ms/1000)%60).padStart(2,'0')+'.'+String(ms%1000).padStart(3,'0')+']';
    return line.split(/\r?\n/).map(part=>stamp+part).join('\n');
   }).join('\n');
  }
  // A plain cached response must not replace a synchronized response obtained
  // from another exact-track source during the same collection pass.
  if(words&&words.length<=20000&&(synced||!/^\[\d{2,}:\d{2}\.\d{3}\]/.test(data.LYRICS||'')))data.LYRICS=words;
 }
 function cached(data,o){
  const uri=own(o,'uri')||own(o,'trackUri');
  if(uri===data.uri){merge(data,o);const nested=own(o,'data');if(object(nested))merge(data,nested);return;}
  // React Query state.data has already been populated by the client. Read
  // it only when the existing key names this exact track; never fetch it.
  const key=own(o,'queryKey');
  if(Array.isArray(key)&&entries(key,16).some(x=>x===data.uri)){
   const value=own(own(o,'state'),'data');if(object(value)){
    const embedded=own(value,'uri')||own(value,'trackUri');
    if(!embedded||embedded===data.uri)merge(data,value);
   }
  }
 }
 function tick(){
  timelineBlocked='';
  try{
   automatic();scan();bindTimeline();if(!player){debugObservation();connectDiagnostics();playback({v:1});unavailableTimeline('player-unavailable');return;}
   const state=method(player,'getState').call(player),item=own(state,'item'),m=own(item,'metadata')||{},uri=text(own(item,'uri'));
   debugObservation(state,item);connectDiagnostics(state);emitTimelineState(state);
   if(!/^spotify:(?:track|episode):[A-Za-z0-9]{22}$/.test(uri||'')){playback({v:1});unavailableTimeline('track-unavailable');return;}
   const track=uri.startsWith('spotify:track:'),show=own(item,'show')||{};
   const data={v:1,title:text(own(m,'title')||own(item,'name')),
    artist:text(own(m,'artist_name'))||artists(own(item,'artists'))[0]||text(own(m,'show.publisher')||own(m,'show_name')||own(show,'publisher')),
    album:text(own(m,'album_title')||own(show,'name')||own(m,'show_name')),uri,
    duration:Number(text(own(m,'duration')||own(own(item,'duration'),'milliseconds')||own(state,'duration')))/1000,SPOTIFY_URI:uri};
   if(!data.title||(track&&(!data.artist||!data.album))||!Number.isFinite(data.duration)||data.duration<=0){playback({v:1});unavailableTimeline('identity-unavailable');return;}
   // Actual current playback quality, never targetBitrateLevel (the preference).
   const quality=own(state,'playbackQuality')||own(state,'playback_quality');
   const raw=own(quality,'bitrateLevel')||own(quality,'bitrate_level');
   // Spotify PlaybackQuality.BitrateLevel protobuf values (validated in
   // 1.2.92, 1.2.94, 1.3.0 and 1.3.3). Unknown additions stay unavailable.
   let level=typeof raw==='string'?raw.toLowerCase():
    Number.isInteger(raw)&&raw>=1&&raw<=6?['','low','normal','high','very_high','lossless','lossless_24'][raw]:undefined;
   if(level==='hifi24')level='lossless_24';
   if(['low','normal','high','very_high','lossless','lossless_24','hifi'].includes(level))data.playback_quality=level;
   playback({v:1,title:data.title,artist:data.artist,album:data.album,uri,duration:data.duration,playback_quality:data.playback_quality});
   if(!track)return; // Episode footer does not enter music-tag enrichment.
   automatic(uri);
   if(options().metadata===false)return;
   const fetched=serviceCache.get(uri);
   if(fetched){merge(data,fetched.metadata.data);merge(data,fetched.album.data);if(options().lyrics!==false)merge(data,fetched.lyrics.data);}
   for(const record of records)cached(data,record);
   merge(data,item,true); // Current playback snapshot wins over older cached data.
   if(options().lyrics===false)delete data.LYRICS;
   const encode=()=>Object.entries(data).filter(([,v])=>v!==undefined&&v!==null&&v!=='').map(([k,v])=>k+'='+encodeURIComponent(String(v))).join('&');
   let payload=encode();if(payload.length>131072){delete data.LYRICS;payload=encode();}
   const last=seenPayload.get(uri),now=performance.now();
   if(payload.length>131072||(last&&last.payload===payload&&now-last.time<15000))return;
   seenPayload.set(uri,{payload,time:now});if(seenPayload.size>64)seenPayload.delete(seenPayload.keys().next().value);
   console.info(prefix+payload);
  }catch{debugPrevious=undefined;unavailableTimeline('player-exception');/* Missing caches leave ordinary tags intact. */}
 }
 window.__floggfyPoll=tick;
 tick();
 // The native metadata worker owns the only heartbeat (and supplies settings).
})();
