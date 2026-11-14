import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright-core');
const base=process.env.MNOS_SITE_URL||'http://127.0.0.1:4173/';
const browser=await chromium.launch({executablePath:process.env.CHROMIUM||'/usr/bin/chromium',args:['--no-sandbox','--enable-unsafe-swiftshader','--autoplay-policy=document-user-activation-required']});
try{
 const context=await browser.newContext({viewport:{width:390,height:844}});
 await context.route('https://raw.githubusercontent.com/**',r=>r.abort());
 const p=await context.newPage(),errors=[],musicServices=[];
 p.on('pageerror',e=>errors.push(e.message));p.on('request',r=>{if(/soundcloud|music\.163|y\.qq\.com/.test(r.url()))musicServices.push(r.url());});
 await p.goto(base,{waitUntil:'domcontentloaded'});
 // Do not grant activation through Playwright evaluation before checking autoplay.
 const cdp=await context.newCDPSession(p);let state;
 for(let i=0;i<80;i++){
  const result=await cdp.send('Runtime.evaluate',{expression:'JSON.stringify({...document.querySelector(".scroll-film").dataset})',returnByValue:true,userGesture:false});
  state=JSON.parse(result.result.value);if(state.audio==='blocked')break;
  await p.waitForTimeout(250);
 }
 assert.equal(state.audio,'blocked',JSON.stringify(state));await cdp.detach();
 await p.locator('.film-sound').click();
 await p.waitForFunction(()=>{const a=document.querySelector('audio');return!a.paused&&a.readyState>=3&&a.currentTime>.1;});
 await p.keyboard.press('Escape');assert.equal(await p.locator('audio').evaluate(a=>a.paused),true);
 const decoded=await p.evaluate(async()=>{
  const a=document.querySelector('audio'),response=await fetch(a.currentSrc);
  const context=new OfflineAudioContext(2,44100,44100),decoded=await context.decodeAudioData(await response.arrayBuffer());
  let peak=0,sum=0;const channel=decoded.getChannelData(0);
  for(const v of channel){peak=Math.max(peak,Math.abs(v));sum+=v*v;}
  const range=await fetch(a.currentSrc,{headers:{Range:'bytes=0-63'}});
  return{duration:decoded.duration,peak,rms:Math.sqrt(sum/channel.length),source:a.currentSrc,mime:response.headers.get('Content-Type'),range:range.status,rangeBytes:(await range.arrayBuffer()).byteLength};
 });
 assert.ok(Math.abs(decoded.duration-24)<.1);assert.ok(decoded.peak>.1&&decoded.rms>.01,'The production recording must contain actual music');
 assert.equal(new URL(decoded.source).origin,new URL(base).origin);assert.ok(decoded.mime.includes('audio/mpeg'));
 assert.equal(decoded.range,206);assert.equal(decoded.rangeBytes,64);
 await p.locator('audio').evaluate(a=>{a.currentTime=8;});
 await p.waitForFunction(()=>{const a=document.querySelector('audio');return!a.seeking&&Math.abs(a.currentTime-8)<.1;});
 await p.locator('.film-play').click();await p.waitForFunction(()=>!document.querySelector('audio').paused);
 await p.locator('.film-sound').click();assert.equal(await p.locator('audio').evaluate(a=>a.paused),true);
 assert.equal(await p.locator('.film-play').getAttribute('aria-pressed'),'true','Muting must leave the film running');
 assert.ok((await p.locator('.music-credit').textContent()).includes('Kevin MacLeod'));assert.deepEqual(errors,[]);assert.deepEqual(musicServices,[]);
 console.log('PASS bundled Cipher MP3: real decoding, audible samples, 24-second duration, same-origin byte ranges, autoplay unlock, seeking, pause/mute and credit');
 console.log(JSON.stringify(decoded));await context.close();
}finally{await browser.close();}
