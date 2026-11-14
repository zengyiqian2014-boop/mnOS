import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const base = process.env.MNOS_SITE_URL || 'http://127.0.0.1:4173/';
try {
  for (const width of [1440, 390, 320]) {
    const context = await browser.newContext({ viewport: { width, height: 900 }, reducedMotion: 'reduce', acceptDownloads: true });
    // Use the bundled catalog for a reproducible test; intercept ISO requests,
    // never download an actual multi-gigabyte image in a UI regression test.
    await context.route('https://raw.githubusercontent.com/**', route => route.abort());
    const requested = [];
    await context.route('https://github.com/**/releases/download/**', route => {
      requested.push(route.request().url());
      return route.fulfill({ status: 200, headers: { 'Content-Type': 'application/octet-stream', 'Content-Disposition': 'attachment; filename="test.iso"' }, body: 'fixture' });
    });
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    for (const [path, family] of [['', 'native'], ['linux.html', 'linux']]) {
      await page.goto(base + path, { waitUntil: 'networkidle' });
      await page.locator('#download-button:not(:disabled)').waitFor();
      for (const arch of ['amd64', 'arm64']) {
        await page.locator('#download-arch').selectOption(arch);
        const before = requested.length;
        await page.locator('#download-button').click();
        assert.equal(await page.locator('#download-consent').evaluate(el => el.open), true);
        assert.equal(await page.locator('#consent-agree').isChecked(), false);
        assert.equal(await page.locator('#consent-download').isDisabled(), true);
        assert.match(await page.locator('#consent-image').textContent(), new RegExp(`${family}.*${arch}\\.iso$`));
        assert.equal(await page.locator('#consent-cancel').evaluate(el => el === document.activeElement), true);
        await page.keyboard.press('Escape');
        assert.equal(await page.locator('#download-consent').evaluate(el => el.open), false);
        assert.equal(await page.locator('#download-button').evaluate(el => el === document.activeElement), true);
        assert.equal(requested.length, before);
        await page.locator('#download-button').click();
        await page.locator('#consent-agree').check();
        await page.locator('#consent-cancel').click();
        await page.locator('#download-button').click();
        assert.equal(await page.locator('#consent-agree').isChecked(), false, 'No consent retained after cancellation');
        assert.ok(await page.locator('#download-consent').evaluate(el => el.scrollWidth <= el.clientWidth), 'Dialog overflow');
        await page.locator('#consent-agree').check();
        if (width === 390 && arch === 'amd64') await page.screenshot({ path: `/workspace/mnos-consent-${family}.png` });
        const download = page.waitForEvent('download');
        await page.locator('#consent-download').click();
        await (await download).delete();
        assert.equal(requested.length, before + 1);
        assert.match(requested.at(-1), new RegExp(`${family}.*${arch}\\.iso$`));
      }
    }
    await page.goto(base + 'terms.html');
    assert.match(await page.locator('main').textContent(), /NO WARRANTY\. ABSOLUTELY NO WARRANTY\./);
    assert.ok(await page.locator('html').evaluate(el => el.scrollWidth <= innerWidth));
    assert.deepEqual(errors, []);
    await context.close();
  }
  console.log('PASS: both ISO families/architectures; no download before consent; cancellation/Escape/focus/reset; selected download; 1440/390/320px; terms and no browser errors.');
} finally { await browser.close(); }
