import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');
const base = process.env.MNOS_SITE_URL || 'http://127.0.0.1:4173/';
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || '/usr/bin/chromium', headless: true,
  args: ['--no-sandbox', '--enable-unsafe-swiftshader'] });

// Deterministic boundary test for the external player. A separate manual smoke
// test uses the real artist upload; CI must not require that service to be up.
const playerFixture = `(() => {
  const handlers = {};
  const emit = (name, data) => handlers[name]?.(data);
  const state = window.testPlayer = { position: 0, pauses: 0, seeks: [],
    advance(ms) { this.position = ms; emit('progress', {currentPosition: ms}); } };
  const widget = { bind(name, fn) { handlers[name] = fn; if (name === 'ready') setTimeout(fn, 0); },
    seekTo(ms) { state.seeks.push(ms); state.advance(ms); },
    play() { emit('play'); }, pause() { state.pauses++; emit('pause'); } };
  const Widget = () => widget;
  Widget.Events = { READY:'ready', ERROR:'error', PLAY_PROGRESS:'progress', PLAY:'play', PAUSE:'pause', FINISH:'finish' };
  window.SC = { Widget };
})();`;
try {
  for (const width of [1440, 390, 320]) {
    const context = await browser.newContext({ viewport: {width, height: width === 320 ? 640 : 900}, reducedMotion: 'no-preference' });
    await context.route('https://raw.githubusercontent.com/**', route => route.abort());
    let audioRequests = 0;
    await context.route('https://w.soundcloud.com/**', route => {
      audioRequests++;
      return route.fulfill(route.request().url().endsWith('/api.js')
        ? {contentType:'text/javascript', body:playerFixture} : {contentType:'text/html', body:'<p>Player test fixture</p>'});
    });
    const page = await context.newPage(); const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(base, {waitUntil:'networkidle'});
    await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.renderer === 'webgl');
    assert.equal(audioRequests, 0, 'No music service or iframe before a user click');
    await page.locator('button[data-chapter="0"]').click();
    const first = await page.locator('.film-canvas').screenshot();
    await page.locator('button[data-chapter="2"]').click();
    const second = await page.locator('.film-canvas').screenshot();
    assert.notDeepEqual(first, second, 'The actual WebGL scene must change with the chapter');
    await page.locator('button[data-chapter="1"]').click();
    assert.ok(await page.locator('.film-desktop .film-copy').evaluate(el => Number(getComputedStyle(el).opacity)) < .01,
      'The screen close-up must not have a heading over its interface');
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);

    await page.locator('.film-music').click();
    await page.waitForFunction(() => window.testPlayer?.seeks.length === 1);
    assert.equal(await page.evaluate(() => window.testPlayer.seeks[0]), 12060, 'Start at the selected vocal hook');
    const progress = () => page.locator('.scroll-film').evaluate(el => Number(el.dataset.progress));
    await page.evaluate(() => window.testPlayer.advance(24060));
    await page.waitForFunction(() => Number(document.querySelector('.scroll-film').dataset.progress) >= .5);
    await page.waitForTimeout(600);
    assert.ok(Math.abs(await progress() - .5) < .006, 'Buffering audio must hold the film at the media time');
    await page.evaluate(() => window.testPlayer.advance(18060));
    await page.waitForFunction(() => Number(document.querySelector('.scroll-film').dataset.progress) < .26);
    assert.ok(Math.abs(await progress() - .25) < .006, 'Seeking backwards must rewind the scene');
    await page.keyboard.press('Escape');
    const paused = await progress();
    await page.evaluate(() => window.testPlayer.advance(30060));
    await page.waitForTimeout(200);
    assert.ok(Math.abs(await progress() - paused) < .002, 'Escape stops both sound and film');
    assert.ok(await page.evaluate(() => window.testPlayer.pauses) > 0);
    await page.locator('.film-audio-close').click();
    assert.equal(await page.locator('.film-audio').isVisible(), false);
    assert.equal(await page.locator('.film-music').evaluate(el => el === document.activeElement), true);

    await page.evaluate(() => document.querySelector('.film-canvas').dispatchEvent(new Event('webglcontextlost', {cancelable:true})));
    await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.renderer === 'fallback');
    await page.locator('button[data-chapter="1"]').click();
    assert.equal(await page.locator('.film-desktop .film-visual').isVisible(), true);
    assert.deepEqual(errors, []);
    await context.close();
    console.log(`PASS ${width}px: real WebGL, changing camera, opt-in music, audio seek/buffering/pause, context-loss fallback`);
  }
  {
    const context = await browser.newContext();
    await context.route('https://raw.githubusercontent.com/**', route => route.abort());
    await context.route('https://w.soundcloud.com/**', route => route.abort());
    await context.route('**/assets/native-home.png', route => route.abort());
    const page = await context.newPage();
    await page.goto(base, {waitUntil:'networkidle'});
    await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.renderer === 'fallback');
    await page.locator('.film-music').click();
    await page.waitForFunction(() => document.querySelector('.film-audio-status').textContent.includes('无法连接'));
    await page.locator('.film-audio-close').click();
    await page.locator('.film-play').click();
    await page.waitForTimeout(250);
    assert.equal(await page.locator('.film-play').getAttribute('aria-pressed'), 'true');
    await context.close();
    console.log('PASS: failed image/music services preserve the silent, readable introduction');
  }
} finally { await browser.close(); }
