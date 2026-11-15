import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');
const base = process.env.MNOS_SITE_URL || 'http://127.0.0.1:4173/';
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || '/usr/bin/chromium',
  args: ['--no-sandbox', '--enable-unsafe-swiftshader'] });

try {
  for (const width of [1440, 390, 320]) {
    const context = await browser.newContext({ viewport: { width, height: width === 320 ? 640 : 900 } });
    await context.route('https://raw.githubusercontent.com/**', route => route.abort());
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(base);
    await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.renderer === 'webgl');
    await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.playing === 'true');
    assert.equal(await page.locator('audio, iframe, [data-music-player]').count(), 0);
    const progress = () => page.locator('.scroll-film').evaluate(el => Number(el.dataset.progress));
    const start = await progress(), opening = await page.locator('.film-canvas').screenshot();
    await page.waitForFunction(value => Number(document.querySelector('.scroll-film').dataset.progress) > value + .04, start);
    await page.keyboard.press('Escape');
    assert.equal(await page.locator('.film-play').getAttribute('aria-pressed'), 'false');
    assert.notDeepEqual(opening, await page.locator('.film-canvas').screenshot(), 'Real WebGL scene must animate');
    const stopped = await progress();
    await page.waitForTimeout(150);
    assert.equal(await progress(), stopped);
    await page.locator('.film-play').click();
    await page.waitForFunction(value => Number(document.querySelector('.scroll-film').dataset.progress) > value, stopped);
    if (width === 1440) {
      // Let the real animation clock finish; no media element drives the scene.
      await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.progress === '1.00000', null, { timeout: 60000 });
      assert.equal(await page.locator('.film-play').getAttribute('aria-label'), '重新播放介绍');
      assert.equal(await page.locator('.scroll-film').getAttribute('data-playing'), 'false');
      await page.locator('.film-play').click();
      await page.waitForFunction(() => {
        const film = document.querySelector('.scroll-film');
        return film.dataset.playing === 'true' && Number(film.dataset.progress) > 0 && Number(film.dataset.progress) < .1;
      });
    }
    await page.locator('.film-canvas').evaluate(canvas =>
      canvas.dispatchEvent(new Event('webglcontextlost', { cancelable: true })));
    await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.renderer === 'fallback');
    assert.equal(await page.locator('.film-desktop .film-visual').isVisible(), true);
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    assert.deepEqual(errors, []);
    await context.close();
    console.log(`PASS ${width}px: real WebGL, independent animation clock, pause/resume, context fallback${width === 1440 ? ', completion and replay' : ''}`);
  }
  const context = await browser.newContext();
  await context.route('https://raw.githubusercontent.com/**', route => route.abort());
  await context.route('**/assets/native-home.png', route => route.abort());
  const page = await context.newPage();
  await page.goto(base);
  await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.renderer === 'fallback');
  await page.waitForFunction(() => Number(document.querySelector('.scroll-film').dataset.progress) > .01);
  assert.equal(await page.locator('.film-kernel .film-visual').isVisible(), true);
  assert.equal(await page.locator('audio, iframe').count(), 0);
  await context.close();
  console.log('PASS unavailable texture keeps the readable animated fallback');
} finally { await browser.close(); }
