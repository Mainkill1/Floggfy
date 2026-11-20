const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('native/metadata_collector.js','utf8');
const track='spotify:track:0000000000000000000001',episode='spotify:episode:0000000000000000000002';
function fixture(item,config={metadata:true,lyrics:true}){
 const output=[],pending=[],state={item};
 const player={getState:()=>state,getEvents:()=>({})};
 const builder={build(){let path;return {withHost(){return this;},withPath(p){path=p;return this;},withQueryParameters(){return this;},send(){return new Promise(resolve=>pending.push({path,resolve}));}};}};
 const registry={_map:new Map([[Symbol('PlayerAPI'),{instance:player}],[Symbol('RequestBuilder'),{instance:builder}]])};
 const window={__floggfyConfig:config},main={__reactContainer$test:{memoizedProps:{registry}}};
 vm.runInNewContext(script,{window,document:{querySelector:()=>main},console:{info:x=>output.push(x)},performance:{now:()=>0},encodeURIComponent});
 return {window,state,pending,config,poll:()=>window.__floggfyPoll(),messages:()=>output.filter(x=>x.startsWith('FLOGGFY_METADATA_V1:')).map(x=>Object.fromEntries(new URLSearchParams(x.slice('FLOGGFY_METADATA_V1:'.length))))};
}
const song=()=>({uri:track,metadata:{title:'Original song',artist_name:'Original artist',album_title:'Original album',duration:'200000'}});
// Removing the episode merge must lose release precision, lyrics and exact artwork variants.
{
 const f=fixture({uri:episode,name:'Episode without music labels',duration:{milliseconds:900000},releaseDate:{isoString:'2024-05-06T00:00:00Z'},images:[{url:'spotify:image:'+ 'a'.repeat(40),width:640}],show:{images:[{url:'https://i.scdn.co/image/'+ 'b'.repeat(40),width:100}]},metadata:{image_xlarge_url:'spotify:image:'+ 'c'.repeat(40),image_url:'https://i.scdn.co.evil/image/'+ 'd'.repeat(40),lyrics:'Available episode transcript'}});
 const m=f.messages().at(-1);
 assert.equal(m.DATE,'2024-05-06');assert.equal(m.YEAR,'2024');
 assert.equal(m.title,'Episode without music labels');assert.equal(m.artist,undefined);assert.equal(m.album,undefined);
 assert.equal(m.LYRICS,'Available episode transcript');
 assert.equal(m.artwork_url,'https://i.scdn.co/image/'+ 'c'.repeat(40));
 assert.equal(m.artwork_urls,['c','a','b'].map(x=>'https://i.scdn.co/image/'+x.repeat(40)).join('\n'));
 assert.equal(f.pending.length,0);
 f.state.item.releaseDate='2023-02';f.poll();assert.equal(f.messages().at(-1).DATE,'2023-02');
 delete f.state.item.releaseDate;f.state.item.show.releaseDate={isoString:'2022'};f.poll();assert.equal(f.messages().at(-1).DATE,'2022');
 delete f.state.item.show.releaseDate;f.state.item.metadata.releaseDate={year:2021};f.poll();assert.equal(f.messages().at(-1).DATE,'2021');
 f.state.item.metadata.releaseDate={year:2021,day:5};f.poll();assert.equal(f.messages().at(-1).DATE,'2021');
}
// Candidate bounds must retain the song payload and the eight largest exact references.
{
 const item=song();item.images=Array.from({length:12},(_,i)=>({url:'spotify:image:'+String(i+1).padStart(40,'0'),width:(i+1)*100}));
 const f=fixture(item),m=f.messages().at(-1);
 assert.equal(m.title,'Original song');
 assert.equal(m.artwork_urls.split('\n').length,8);
 assert.equal(m.artwork_url,'https://i.scdn.co/image/'+ '12'.padStart(40,'0'));
 assert.equal(m.artwork_urls.split('\n').at(-1),'https://i.scdn.co/image/'+ '5'.padStart(40,'0'));
}
// Native album grants can use the locally observed album URI without a track request.
{
 const item=song();item.album={uri:'spotify:album:0000000000000000000002',images:[{url:'https://i.scdn.co/image/'+ 'E'.repeat(40),width:300}]};
 item.metadata.image_url='spotify:image:too-short';
 const f=fixture(item);
 assert.equal(f.messages().at(-1).artwork_url,'https://i.scdn.co/image/'+ 'e'.repeat(40));
 assert.equal(f.window.__floggfyEnrich('spotify:track:0000000000000000000009',{metadata:true}),false);
 assert.equal(f.pending.length,0,'an unobserved URI has no identity snapshot');
 assert.equal(f.window.__floggfyEnrich(track,{metadata:false,album:true,lyrics:false}),true);
 assert.equal(f.pending.length,1);assert.equal(f.pending[0].path,'/metadata/4/album/00000000000000000000000000000002');
}
async function flush(){for(let i=0;i<8;i++)await Promise.resolve();}
(async()=>{
 // Reintroducing per-tick requests must fail before the grant is supplied.
 const f=fixture(song());for(let i=0;i<5;i++)f.poll();assert.equal(f.pending.length,0,'local snapshots cannot authorize network requests');
 assert.equal(typeof f.window.__floggfyEnrich,'function');
 f.window.__floggfyEnrich(track,{metadata:false,album:false,lyrics:true});assert.equal(f.pending.length,1);assert(f.pending[0].path.includes('color-lyrics'));
 f.state.item={uri:episode,name:'New episode',duration:{milliseconds:60000}};f.poll();
 f.pending[0].resolve({body:{lyrics:'Late original lyrics'}});await flush();
 let m=f.messages().at(-1);assert.equal(m.uri,track);assert.equal(m.title,'Original song');assert.equal(m.artist,'Original artist');assert.equal(m.LYRICS,'Late original lyrics');
 f.poll();m=f.messages().at(-1);assert.equal(m.uri,track,'a late response does not force a duplicate current snapshot');
 f.window.__floggfyEnrich(track,{metadata:true,album:false,lyrics:false});assert.equal(f.pending.length,2);
 f.pending[1].resolve({body:{album:{gid:'a'.repeat(32),label:'Original label'}}});await flush();f.poll();assert.equal(f.pending.length,2,'album request requires its own grant');
 assert(f.messages().some(x=>x.uri===track&&x.LABEL==='Original label'&&x.title==='Original song'));
 f.window.__floggfyEnrich(track,{metadata:false,album:true,lyrics:false});assert.equal(f.pending.length,3);assert(f.pending[2].path.includes('/metadata/4/album/'));
 f.config.metadata=false;f.poll();f.pending[2].resolve({body:{label:'Disabled result'}});await flush();assert(!f.messages().some(x=>x.LABEL==='Disabled result'));
 f.window.__floggfyEnrich(episode,{metadata:true,album:true,lyrics:true});assert.equal(f.pending.length,3,'episodes do not call track services');
 // Late enrichment is not a fresh playback-quality observation for repeated URI playback.
 const quality=fixture(song());quality.state.playbackQuality={bitrateLevel:'LOW'};quality.poll();
 quality.window.__floggfyEnrich(track,{metadata:false,album:false,lyrics:true});
 quality.state.item={uri:episode,name:'Between plays',duration:{milliseconds:60000}};quality.poll();
 quality.state.item=song();quality.state.playbackQuality={bitrateLevel:'LOSSLESS'};quality.poll();
 quality.pending[0].resolve({body:{lyrics:'First playback late lyrics'}});await flush();
 assert.equal(quality.messages().at(-1).playback_quality,undefined,'async reply must not restamp captured old quality');
 quality.poll();assert.equal(quality.messages().at(-1).playback_quality,'lossless');
 // Grants add permission: a later lyrics request cannot cancel a pending metadata reply.
 const incremental=fixture(song());
 incremental.window.__floggfyEnrich(track,{metadata:true,album:false,lyrics:false});
 incremental.window.__floggfyEnrich(track,{metadata:false,album:false,lyrics:true});
 assert.equal(incremental.pending.length,2);
 incremental.pending[0].resolve({body:{album:{label:'Pending metadata survives'}}});await flush();
 assert(incremental.messages().some(x=>x.LABEL==='Pending metadata survives'));
 assert.equal(incremental.messages().at(-1).metadata_source,'online');
 incremental.poll();assert.equal(incremental.messages().at(-1).metadata_source,undefined,'ordinary snapshots are client observations');
 // Native local enrichment can discover album/artwork refs after the observed snapshot.
 const supplementedItem=song();supplementedItem.metadata.genre='Observed genre';
 const supplemented=fixture(supplementedItem);
 const nativeLocal={uri:track,title:'Original song',artist:'Original artist',album:'Original album',duration:200,
  album_uri:'spotify:album:0000000000000000000002',artwork_url:'https://i.scdn.co/image/'+ 'd'.repeat(40)};
 assert.equal(supplemented.window.__floggfyEnrich(track,{album:true},{...nativeLocal,title:'Wrong identity'}),true);
 assert.equal(supplemented.pending.length,0,'mismatching native labels cannot supply an album identity');
 supplemented.window.__floggfyEnrich(track,{album:true},nativeLocal);
 assert.equal(supplemented.pending.length,1,'native local album URI completes an observed snapshot');
 assert.equal(supplemented.pending[0].path,'/metadata/4/album/00000000000000000000000000000002');
 supplemented.pending[0].resolve({body:{label:'Local album lookup'}});await flush();
 const supplied=supplemented.messages().at(-1);
 assert.equal(supplied.GENRE,'Observed genre');assert.equal(supplied.album_uri,nativeLocal.album_uri);
 assert.equal(supplied.artwork_url,nativeLocal.artwork_url);assert.equal(supplied.metadata_source,'online');
 // A native-owned recording can outlive the collector's bounded recent history.
 const evicted=fixture(song());
 for(let i=3;i<75;i++){
  evicted.state.item={uri:'spotify:track:'+String(i).padStart(22,'0'),metadata:{title:'Later song '+i,artist_name:'Later artist',album_title:'Later album',duration:'100000'}};
  evicted.poll();
 }
 const owned={uri:track,title:'Original song',artist:'Original artist',album:'Original album',duration:200,
  album_uri:'spotify:album:0000000000000000000002',artwork_url:'https://i.scdn.co/image/'+ 'a'.repeat(40),
  artwork_urls:'https://i.scdn.co/image/'+ 'a'.repeat(40),SPOTIFY_URI:track};
 const needLyrics={metadata:false,album:false,lyrics:true};
 assert.equal(evicted.window.__floggfyEnrich(track,needLyrics),false,'old ungranted snapshot was evicted');
 for(const bad of [{...owned,uri:episode},{...owned,title:''},{...owned,artist:' '},{...owned,album:''},{...owned,duration:0},{...owned,duration:Infinity}]){
  assert.equal(evicted.window.__floggfyEnrich(track,needLyrics,bad),false,'invalid fallback identity must be rejected');
 }
 assert.equal(evicted.window.__floggfyEnrich(track,needLyrics,owned),true,'native record survives JS history eviction');
 assert.equal(evicted.pending.length,1);
 evicted.pending[0].resolve({body:{lyrics:'Late retained recording lyrics'}});await flush();
 const old=evicted.messages().at(-1);
 assert.equal(old.uri,track);assert.equal(old.title,'Original song');assert.equal(old.artist,'Original artist');assert.equal(old.album,'Original album');
 assert.equal(old.duration,'200');assert.equal(old.LYRICS,'Late retained recording lyrics');assert.equal(old.artwork_url,owned.artwork_url);
 assert.equal(old.album_uri,owned.album_uri);assert.equal(old.SPOTIFY_URI,track);
 evicted.window.__floggfyEnrich(track,{metadata:false,album:true,lyrics:false},owned);
 evicted.pending[1].resolve({body:{images:[{url:'https://i.scdn.co/image/'+ 'b'.repeat(40),width:640}]}});await flush();
 assert(evicted.messages().at(-1).artwork_urls.split('\n').includes(owned.artwork_url),'native artwork remains an available candidate after enrichment');
 // Prefer the richer observed snapshot when native only supplies identity fields.
 const retainedItem=song();retainedItem.metadata.genre='Observed genre';
 const retained=fixture(retainedItem);assert.equal(retained.window.__floggfyEnrich(track,needLyrics,owned),true);
 retained.pending[0].resolve({body:{lyrics:'Rich snapshot lyrics'}});await flush();assert.equal(retained.messages().at(-1).GENRE,'Observed genre');
 const disabled=fixture(song(),{metadata:true,lyrics:false});disabled.window.__floggfyEnrich(track,{metadata:false,album:false,lyrics:true});assert.equal(disabled.pending.length,0,'Lyrics=0 remains effective');
 console.log('PASS: shared episode snapshots, precise dates, ordered exact artwork, explicit grants and delayed URI-owned enrichment');
})().catch(error=>{console.error(error);process.exitCode=1;});
