import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright-core');
const base=process.env.MNOS_SITE_URL||'http://127.0.0.1:4173/';
const browser=await chromium.launch({executablePath:process.env.CHROMIUM||'/usr/bin/chromium',args:['--no-sandbox','--enable-unsafe-swiftshader']});
try{
 for(const width of [1440,390,320]){
  const context=await browser.newContext({viewport:{width,height:width===320?640:900},reducedMotion:'no-preference'});
  await context.route('https://raw.githubusercontent.com/**',r=>r.abort());
  const page=await context.newPage(),errors=[],thirdParty=[];
  page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(/soundcloud|youtube|music\.163/.test(r.url()))thirdParty.push(r.url());});
  await page.goto(base);
  await page.waitForFunction(()=>document.querySelector('.scroll-film').dataset.playing==='true',null,{timeout:60000});
  await page.waitForFunction(()=>document.querySelector('.scroll-film').dataset.audio==='unconfigured');
  assert.equal(await page.locator('.film-sound').isVisible(),false);
  assert.equal(await page.locator('iframe').count(),0);
  const progress=()=>page.locator('.scroll-film').evaluate(e=>Number(e.dataset.progress));
  const before=await progress();await page.waitForTimeout(650);assert.ok(await progress()>before,'Introduction starts without a button');
  assert.equal(await page.evaluate(()=>scrollY),0,'Playback must not move the page');
  await page.evaluate(()=>scrollTo({top:innerHeight*1.5,behavior:'instant'}));
  await page.waitForFunction(()=>document.querySelector('.scroll-film').dataset.playing==='false');
  const stopped=await progress();await page.waitForTimeout(150);assert.equal(await progress(),stopped);
  await page.evaluate(()=>scrollTo({top:0,behavior:'instant'}));
  await page.waitForFunction(()=>document.querySelector('.scroll-film').dataset.playing==='true');
  await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'));});
  assert.equal(await page.locator('.film-play').getAttribute('aria-pressed'),'false','Tab visibility stops playback immediately, even with no next animation frame');
  await page.evaluate(()=>{delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));});
  await page.waitForFunction(()=>document.querySelector('.scroll-film').dataset.playing==='true');
  await page.keyboard.press('Escape');await page.waitForFunction(()=>document.querySelector('.scroll-film').dataset.playing==='false');
  await page.evaluate(()=>{scrollTo({top:1200,behavior:'instant'});});await page.waitForTimeout(80);await page.evaluate(()=>scrollTo({top:0,behavior:'instant'}));await page.waitForTimeout(150);
  assert.equal(await page.locator('.film-play').getAttribute('aria-pressed'),'false','An explicit pause must persist across scrolling');
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
  await page.emulateMedia({reducedMotion:'reduce'});
  await page.waitForFunction(()=>document.querySelector('.scroll-film').classList.contains('film-static'));
  assert.equal(await page.locator('.film-play').isVisible(),false);assert.equal(await page.locator('.film-scene[aria-hidden="true"]').count(),0);
  assert.deepEqual(thirdParty,[]);assert.deepEqual(errors,[]);
  await page.goto(base+'support.html');assert.equal(await page.locator('.support-email').getAttribute('href'),'mailto:report.mnos@proton.me?subject=mnOS%20feedback');
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
  await context.close();console.log(`PASS ${width}px: automatic in-place playback, leaving/resuming, explicit pause, reduced motion, no hidden third-party player`);
 }
 const context=await browser.newContext({javaScriptEnabled:false});const page=await context.newPage();await page.goto(base);
 assert.equal(await page.locator('.film-scene:visible').count(),3);assert.equal(await page.locator('.film-play').isVisible(),false);await context.close();
 console.log('PASS no JavaScript: all three chapters remain readable');
}finally{await browser.close();}
