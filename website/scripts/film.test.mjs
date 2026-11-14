import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const base = process.env.MNOS_SITE_URL || 'http://127.0.0.1:4173/';
try {
  for (const width of [1440, 390, 320]) {
    const context = await browser.newContext({ viewport: { width, height: width === 320 ? 640 : 900 }, reducedMotion: 'no-preference' });
    await context.route('https://raw.githubusercontent.com/**', r => r.abort());
    const page = await context.newPage(); const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.goto(base, { waitUntil: 'networkidle' });
    for (const chapter of [0, 1, 2, 1, 0]) {
      await page.locator(`button[data-chapter="${chapter}"]`).click();
      await page.waitForFunction(i => document.querySelector('.scroll-film').dataset.chapter === String(i), chapter);
      assert.ok(Number(await page.locator(`[data-scene="${chapter}"]`).evaluate(e => getComputedStyle(e).opacity)) > .99);
      assert.equal(await page.locator(`[data-scene="${chapter}"]`).getAttribute('aria-hidden'), 'false');
    }
    // Test actual scrollbar movement too; chapter navigation must not be the only path.
    await page.evaluate(() => {
      const s = document.querySelector('.scroll-film');
      scrollTo({ top: s.getBoundingClientRect().top + scrollY - 64 + .85 * (s.offsetHeight - innerHeight), behavior: 'instant' });
    });
    await page.waitForFunction(() => document.querySelector('.scroll-film').dataset.chapter === '2');
    await page.locator('button[data-chapter="0"]').click();
    const before = await page.evaluate(() => scrollY);
    await page.locator('.film-play').click(); await page.waitForTimeout(350);
    assert.equal(await page.locator('.film-play').getAttribute('aria-pressed'), 'true');
    assert.ok(await page.evaluate(() => scrollY) > before);
    await page.keyboard.press('Escape');
    assert.equal(await page.locator('.film-play').getAttribute('aria-pressed'), 'false');
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await page.waitForFunction(() => document.querySelector('.scroll-film').classList.contains('film-static'));
    assert.equal(await page.locator('.film-play').isVisible(), false);
    assert.equal(await page.locator('.film-scene[aria-hidden="true"]').count(), 0);
    assert.deepEqual(errors, []);
    await page.goto(base + 'support.html');
    assert.equal(await page.locator('.support-email').getAttribute('href'), 'mailto:report.mnos@proton.me?subject=mnOS%20feedback');
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    console.log(`PASS ${width}px: reversible scroll film, chapters, playback, pause, reduced motion and support`);
    await context.close();
  }
  const context = await browser.newContext({ javaScriptEnabled: false });
  const page = await context.newPage(); await page.goto(base);
  assert.equal(await page.locator('.film-scene:visible').count(), 3);
  assert.equal(await page.locator('.film-play').isVisible(), false);
  assert.equal(await page.locator('.support-strip .support-email').getAttribute('href'), 'mailto:report.mnos@proton.me');
  await context.close();
  console.log('PASS no JavaScript: all introduction chapters and support remain available');
} finally { await browser.close(); }
