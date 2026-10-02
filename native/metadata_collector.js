// Reads existing client objects only. Never starts a request or resolves a service.
(() => {
 'use strict';
 if(window.__floggfyMetadata)return;
 window.__floggfyMetadata=true;
 const prefix='FLOGGFY_METADATA_V1:',seenPayload=new Map(),records=[];
 let reported=false,player,root,queue=[],seen=new Set(),walked=0;
 const own=(o,k)=>{try{const d=Object.getOwnPropertyDescriptor(o,k);return d&&'value' in d?d.value:undefined;}catch{return undefined;}};
 const object=o=>o&&typeof o==='object';
 function remember(o){if(!records.includes(o)){if(records.length>=96)records.shift();records.push(o);}}
 function method(o,k){for(let i=0;object(o)&&i<4;i++,o=Object.getPrototypeOf(o)){const v=own(o,k);if(typeof v==='function')return v;}return undefined;}
 function scan(){
  const sp=own(window,'Spicetify'),p=own(sp,'Player');if(method(p,'getState'))player=p;
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
   if(own(o,'uri')||own(o,'trackUri')||own(o,'queryKey'))remember(o);
   try{let count=0;for(const v of Map.prototype.values.call(o)){if(++count>80||queue.length>=8192)break;if(object(v))queue.push(v);}}catch{}
   const priority=['memoizedProps','value','platform','props','dependencies','firstContext','memoizedValue','child','sibling'];
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
 function merge(data,source){
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
  const names=artists(own(source,'artists')||own(m,'artists'));
  putArtists(data,'ARTIST',names.length?names:indexed(m,'artist_name'));
  const albumNames=artists(own(album,'artists'));
  putArtists(data,'ALBUMARTIST',albumNames.length?albumNames:indexed(m,'album_artist_name'));
  const label=text(own(m,'album_label')||own(m,'label')||own(source,'label')||own(album,'label'));
  if(label&&label.length<=4096){data.LABEL=label;data.ORGANIZATION=label;}
  const genres=list(own(m,'genre')||own(m,'genres')||own(source,'genres')||own(album,'genres'));
  if(genres)data.GENRE=genres;
  for(const [tag,key] of [['DISCNUMBER','album_disc_number'],['DISCTOTAL','album_disc_count'],['TRACKTOTAL','album_track_count'],['ISRC','isrc'],['PUBLISHER','publisher'],['LANGUAGE','language'],['COPYRIGHT','copyright']]){
   const value=text(own(m,key)||own(source,key)||own(album,key));if(value&&value.length<=4096&&(!/NUMBER|TOTAL/.test(tag)||/^[1-9]\d{0,5}$/.test(value)))data[tag]=value;
  }
  const lyric=own(source,'lyrics')||own(m,'lyrics');
  let words=text(lyric);
  const lines=own(lyric,'lines');
  if(Array.isArray(lines)&&lines.length<=2000)words=entries(lines,2000).map(l=>text(own(l,'words'))||'').join('\n');
  if(words&&words.length<=20000)data.LYRICS=words;
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
  try{
   scan();if(!player)return;
   if(!reported){console.info('FLOGGFY_STATUS:cached player found');reported=true;}
   const item=own(method(player,'getState').call(player),'item'),m=own(item,'metadata')||{},uri=text(own(item,'uri'));
   if(!/^spotify:track:[A-Za-z0-9]{22}$/.test(uri||''))return;
   const data={v:1,title:text(own(m,'title')||own(item,'name')),artist:text(own(m,'artist_name')),
    album:text(own(m,'album_title')),uri,duration:Number(text(own(m,'duration')||own(own(item,'duration'),'milliseconds')))/1000,SPOTIFY_URI:uri};
   if(!data.title||!data.artist||!data.album||!Number.isFinite(data.duration)||data.duration<=0)return;
   for(const record of records)cached(data,record);
   merge(data,item); // Current playback snapshot wins over older cached data.
   const encode=()=>Object.entries(data).filter(([,v])=>v!==undefined&&v!==null&&v!=='').map(([k,v])=>k+'='+encodeURIComponent(String(v))).join('&');
   let payload=encode();if(payload.length>131072){delete data.LYRICS;payload=encode();}
   const last=seenPayload.get(uri),now=performance.now();
   if(payload.length>131072||(last&&last.payload===payload&&now-last.time<15000))return;
   seenPayload.set(uri,{payload,time:now});if(seenPayload.size>64)seenPayload.delete(seenPayload.keys().next().value);
   console.info(prefix+payload);
  }catch{/* Missing or changed caches leave ordinary tags intact. */}
 }
 window.__floggfyPoll=tick;
 console.info('FLOGGFY_STATUS:cache-only collector started');
 tick();
 if(!window.__floggfyNative)setInterval(tick,1500);
})();
