import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright-core');
const base=process.env.MNOS_SITE_URL||'http://127.0.0.1:4173/';
const browser=await chromium.launch({executablePath:process.env.CHROMIUM||'/usr/bin/chromium',args:['--no-sandbox','--enable-unsafe-swiftshader']});
try {
 const context=await browser.newContext({viewport:{width:390,height:844}});
 await context.route('https://raw.githubusercontent.com/**',r=>r.abort());
 const page=await context.newPage(),errors=[],mediaRequests=[];
 page.on('pageerror',e=>errors.push(e.message));
 page.on('request',r=>{if(r.resourceType()==='media')mediaRequests.push(r.url());});
 await page.goto(base,{waitUntil:'domcontentloaded'});
 const response=await context.request.get(new URL('media/music.json',base).href);
 assert.equal(response.status(),200);
 const config=await response.json();
 assert.equal(config.title,'3 Strikes');
 assert.equal(config.artist,'Terror Jr');
 assert.deepEqual(config.sources,[],'No verified original audio source is configured; do not silently substitute a different recording');
 await page.waitForFunction(()=>{
  const film=document.querySelector('.scroll-film');
  return film.dataset.audio==='unconfigured'&&Number(film.dataset.progress)>.02;
 });
 assert.equal(await page.locator('.film-sound').isVisible(),false,'Do not imply that a listening-page link supplies background audio');
 assert.equal(await page.locator('audio').evaluate(a=>a.currentSrc),'');
 const credit=page.locator('.music-credit');
 assert.ok((await credit.textContent()).includes('Terror Jr —《3 Strikes》'));
 assert.equal(await credit.locator('a').nth(0).getAttribute('href'),'https://www.kugou.com/song/cnygy2e.html');
 assert.equal(await credit.locator('a').nth(1).getAttribute('href'),'https://music.163.com/#/song?id=409647388');
 await page.keyboard.press('Escape');
 assert.equal(await page.locator('.scroll-film').getAttribute('data-playing'),'false');
 assert.deepEqual(mediaRequests,[]);assert.deepEqual(errors,[]);
 console.log('PASS original 3 Strikes selection, official listening links, no substitute audio and working silent introduction');
 await context.close();
} finally {await browser.close();}
