import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright-core');
const base=process.env.MNOS_SITE_URL||'http://127.0.0.1:4173/';
const browser=await chromium.launch({executablePath:process.env.CHROMIUM||'/usr/bin/chromium',args:['--no-sandbox','--enable-unsafe-swiftshader','--autoplay-policy=document-user-activation-required']});
try {
 for (const width of [390,320]) {
  const context=await browser.newContext({viewport:{width,height:844}});
  await context.route('https://raw.githubusercontent.com/**',r=>r.abort());
  const page=await context.newPage(),errors=[],musicRequests=[];
  page.on('pageerror',e=>errors.push(e.message));
  page.on('request',r=>{if(/(?:soundcloud|sndcdn|music\.apple|itunes\.apple|musickit)/.test(r.url()))musicRequests.push(r.url());});
  await page.goto(base,{waitUntil:'domcontentloaded'});
  await page.waitForFunction(()=>document.querySelector('.scroll-film').dataset.audio==='unconfigured');
  assert.equal(musicRequests.length,0,'No third-party music requests before opening the official player');
  assert.equal(await page.locator('audio').evaluate(a=>a.currentSrc),'','No substituted background recording');
  const trigger=page.locator('.film-track');await trigger.click();
  const dialog=page.locator('#music-player');
  assert.equal(await dialog.evaluate(d=>d.open),true);
  assert.equal(new URL(await dialog.locator('iframe').getAttribute('src')).hostname,'w.soundcloud.com');
  if(width===320){
   const previous=await dialog.locator('iframe').elementHandle();
   await page.locator('#music-provider').selectOption('apple');
   assert.equal(await previous.evaluate(frame=>frame.isConnected),false,'Switching providers stops the previous player');
  }
  const source=new URL(await dialog.locator('iframe').getAttribute('src'));
  let playback;
  const player=page.frameLocator('#music-player iframe');
  // Use the real provider and browser decoder, not a media fixture or a mocked Play event.
  if(width===390){
   assert.equal(source.searchParams.get('url'),'https://api.soundcloud.com/tracks/256591463');
   await player.getByTitle('Play',{exact:true}).click({timeout:30000});
   await page.addScriptTag({url:'https://w.soundcloud.com/player/api.js'});
   playback=await page.evaluate(()=>new Promise((resolve,reject)=>{
    const timeout=setTimeout(()=>reject(new Error('SoundCloud original did not play')),20000);
    const widget=window.SC.Widget(document.querySelector('#music-player iframe'));
    const get=method=>new Promise(done=>widget[method](done));
    // The player is already loaded when Play is clicked; READY is not replayed
    // to a late subscriber. Read its live clock directly through the public API.
    (async()=>{
     for(let i=0;i<50;i++){
      const [sound,time,duration,paused,volume]=await Promise.all(['getCurrentSound','getPosition','getDuration','isPaused','getVolume'].map(get));
      if(time>1000&&!paused){clearTimeout(timeout);resolve({track:sound.id,title:sound.title,artist:sound.user.username,time:time/1000,duration:duration/1000,muted:volume===0,volume:volume/100});return;}
      await new Promise(done=>setTimeout(done,200));
     }
     clearTimeout(timeout);reject(new Error('SoundCloud clock did not advance'));
    })().catch(error=>{clearTimeout(timeout);reject(error);});
   }));
   assert.equal(playback.track,256591463);assert.equal(playback.title,'3 Strikes');assert.equal(playback.artist,'Terror Jr');
   assert.ok(playback.duration>170&&playback.duration<180);
  }else{
  assert.equal(source.origin,'https://embed.music.apple.com');
  assert.equal(source.searchParams.get('i'),'1165457668','Original Terror Jr recording');
  await player.getByRole('button',{name:'Play',exact:true}).click({timeout:30000});
  const media=player.locator('audio');await media.waitFor({state:'attached',timeout:15000});
  playback=await media.evaluate(a=>new Promise((resolve,reject)=>{
   const timeout=setTimeout(()=>{clearInterval(poll);reject(new Error('Official preview did not actually play'));},15000);
   const poll=setInterval(()=>{
    if(a.readyState>=3&&!a.paused&&a.currentTime>1){clearTimeout(timeout);clearInterval(poll);resolve({time:a.currentTime,duration:a.duration,source:a.currentSrc,muted:a.muted,volume:a.volume});}
   },100);
  }));
  assert.equal(new URL(playback.source).hostname,'audio-ssl.itunes.apple.com');
  }
  assert.ok(playback.duration>=30&&playback.time>1);
  assert.equal(playback.muted,false);assert.ok(playback.volume>0);
  assert.equal(await dialog.evaluate(d=>d.scrollWidth>d.clientWidth),false);
  await page.screenshot({path:`/tmp/mnos-original-player-${width}.png`});
  await page.getByRole('button',{name:'关闭试听',exact:true}).click();
  await dialog.locator('iframe').waitFor({state:'detached'});
  assert.equal(await dialog.evaluate(d=>d.open),false);
  assert.equal(await dialog.locator('iframe').count(),0,'Closing must destroy the playing media context');
  assert.equal(await trigger.evaluate(e=>e===document.activeElement),true);
  await trigger.click();await page.keyboard.press('Escape');
  await dialog.locator('iframe').waitFor({state:'detached'});
  assert.equal(await dialog.locator('iframe').count(),0,'Escape also stops the player');
  assert.deepEqual(errors,[]);
  console.log(`PASS ${width}px: ${width===390?'SoundCloud original':'Apple fallback'} actually playing, unmuted, ${playback.duration.toFixed(2)}s; dialog fits, close/Escape remove audio context and restore focus`);
  await context.close();
 }
 {
  const context=await browser.newContext({reducedMotion:'reduce'});
  await context.route('https://raw.githubusercontent.com/**',r=>r.abort());
  await context.route('https://embed.music.apple.com/**',r=>r.abort());
  await context.route('https://w.soundcloud.com/**',r=>r.abort());
  const page=await context.newPage();await page.goto(base,{waitUntil:'domcontentloaded'});
  const footer=page.locator('.music-credit [data-music-player]');await footer.click();
  assert.equal(await page.locator('#music-player').evaluate(d=>d.open),true,'Music remains available with reduced motion');
  assert.equal(await page.locator('#music-player a').getAttribute('href'),'https://soundcloud.com/terrorjr/3-strikes');
  const firstFrame=await page.locator('#music-player iframe').elementHandle();
  await page.getByRole('button',{name:'重新加载',exact:true}).click();
  assert.equal(await firstFrame.evaluate(f=>f.isConnected),false,'Retry replaces the failed player');
  await page.getByRole('button',{name:'关闭试听',exact:true}).click();
  await page.locator('#music-player iframe').waitFor({state:'detached'});
  assert.equal(await page.locator('#music-player iframe').count(),0);
  await context.close();console.log('PASS reduced-motion access, blocked-provider retry and explicit original-song fallback');
 }
} finally {await browser.close();}
