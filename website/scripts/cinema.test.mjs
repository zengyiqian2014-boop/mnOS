import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright-core');
const base=process.env.MNOS_SITE_URL||'http://127.0.0.1:4173/';
const browser=await chromium.launch({executablePath:process.env.CHROMIUM||'/usr/bin/chromium',args:['--no-sandbox','--enable-unsafe-swiftshader','--autoplay-policy=document-user-activation-required']});
// Generated silent PCM tests the real browser decoder/clock, not a music SDK mock.
// This is a test fixture, never a replacement for the requested track.
const rate=8000,length=45*rate*2,wav=Buffer.alloc(44+length);
wav.write('RIFF');wav.writeUInt32LE(36+length,4);wav.write('WAVEfmt ',8);wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);wav.writeUInt32LE(rate,24);wav.writeUInt32LE(rate*2,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);wav.write('data',36);wav.writeUInt32LE(length,40);
async function audioFixture(context,sources=['./test-audio.wav']){
 await context.route('**/media/music.json*',r=>r.fulfill({contentType:'application/json',body:JSON.stringify({start:12.06,duration:24,sources})}));
 await context.route('**/test-audio.wav',r=>{
  const range=/^bytes=(\d+)-(\d*)$/.exec(r.request().headers().range||'');
  const start=range?Number(range[1]):0,end=range&&range[2]?Number(range[2]):wav.length-1;
  return r.fulfill({status:range?206:200,contentType:'audio/wav',headers:{'Accept-Ranges':'bytes','Content-Length':String(end-start+1),...(range?{'Content-Range':`bytes ${start}-${end}/${wav.length}`}:{})},body:wav.subarray(start,end+1)});
 });
}
try{
 for(const width of [1440,390,320]){
  const context=await browser.newContext({viewport:{width,height:width===320?640:900}});
  await context.route('https://raw.githubusercontent.com/**',r=>r.abort());await audioFixture(context);
  const p=await context.newPage(),errors=[];p.on('pageerror',e=>errors.push(e.message));await p.goto(base);
  // CDP without userGesture is deliberate: browser automation helpers can
  // otherwise grant activation and invalidate an audible-autoplay policy test.
  const cdp=await context.newCDPSession(p);let state;
  for(let attempt=0;attempt<60;attempt++){
   const result=await cdp.send('Runtime.evaluate',{expression:'JSON.stringify({...document.querySelector(".scroll-film").dataset})',returnByValue:true,userGesture:false});
   state=JSON.parse(result.result.value);if(state.audio==='blocked')break;
   await p.waitForTimeout(250);
  }
  assert.equal(state.audio,'blocked',JSON.stringify(state));await cdp.detach();
  assert.equal(await p.locator('.film-play').getAttribute('aria-pressed'),'true','Blocked audio must not block the film');
  const opening=await p.locator('.film-canvas').screenshot();
  await p.locator('.film-sound').click();
  await p.waitForFunction(()=>!document.querySelector('audio').paused&&document.querySelector('audio').currentTime>12);
  await p.evaluate(()=>{document.querySelector('audio').currentTime=26;});
  await p.waitForFunction(()=>Number(document.querySelector('.scroll-film').dataset.progress)>.55);
  await p.keyboard.press('Escape');
  const layers=await p.locator('.film-canvas').screenshot();assert.notDeepEqual(opening,layers);
  assert.equal(await p.locator('audio').evaluate(a=>a.paused),true);
  const stopped=await p.locator('.scroll-film').getAttribute('data-progress');await p.waitForTimeout(180);
  assert.equal(await p.locator('.scroll-film').getAttribute('data-progress'),stopped);
  await p.locator('.film-play').click();await p.waitForFunction(()=>!document.querySelector('audio').paused&&document.querySelector('audio').readyState>=3&&!document.querySelector('audio').seeking);
  const position=await p.locator('audio').evaluate(a=>a.currentTime);assert.ok(position>25,'Resume keeps the chosen song position');
  // A real media error must leave the independently timed visual sequence running.
  await p.evaluate(()=>{const a=document.querySelector('audio');a.pause();a.dispatchEvent(new Event('error'));});
  const before=Number(await p.locator('.scroll-film').getAttribute('data-progress'));
  await p.waitForFunction(value=>{const progress=Number(document.querySelector('.scroll-film').dataset.progress);return progress>value||progress===1;},before);
  await p.evaluate(()=>document.querySelector('.film-canvas').dispatchEvent(new Event('webglcontextlost',{cancelable:true})));
  await p.waitForFunction(()=>document.querySelector('.scroll-film').dataset.renderer==='fallback');assert.equal(await p.locator('.film-desktop .film-visual').isVisible(),true);
  assert.equal(await p.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);assert.deepEqual(errors,[]);
  await context.close();console.log(`PASS ${width}px: real WebGL and native audio, autoplay policy, media seeking, pause/resume, service/context failure`);
 }
 {
  const context=await browser.newContext();await context.route('https://raw.githubusercontent.com/**',r=>r.abort());
  await context.route('**/assets/native-home.png',r=>r.abort());await audioFixture(context,['https://unavailable.invalid/intro.mp3','./test-audio.wav']);
  await context.route('https://unavailable.invalid/**',r=>r.abort());const p=await context.newPage();await p.goto(base);
  await p.waitForFunction(()=>document.querySelector('.scroll-film').dataset.renderer==='fallback');
  await p.waitForFunction(()=>['playing','blocked'].includes(document.querySelector('.scroll-film').dataset.audio));
  if(await p.locator('.scroll-film').getAttribute('data-audio')==='blocked')await p.locator('.film-sound').click();
  await p.waitForFunction(()=>!document.querySelector('audio').paused&&document.querySelector('audio').currentTime>=12.06);
  assert.ok(await p.locator('audio').evaluate(a=>a.currentSrc.endsWith('/test-audio.wav')));
  await context.close();console.log('PASS failed primary source uses configured audio mirror; image failure retains the introduction');
 }
}finally{await browser.close();}
