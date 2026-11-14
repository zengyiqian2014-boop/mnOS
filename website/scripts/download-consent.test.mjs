import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFile } from 'node:fs/promises';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const base = process.env.MNOS_SITE_URL || 'http://127.0.0.1:4173/';
const fixture = JSON.parse(await readFile(new URL('../catalog.json', import.meta.url)));
fixture.resources = fixture.resources.filter(r => r.delivery !== 'online' && !r.name.includes('-online-'));
try {
  for (const width of [1440, 390, 320]) {
    const context = await browser.newContext({ viewport: { width, height: 900 }, reducedMotion: 'reduce', acceptDownloads: true });
    // Use the bundled catalog for a reproducible test; intercept ISO requests,
    // never download an actual multi-gigabyte image in a UI regression test.
    await context.route('https://raw.githubusercontent.com/**', route => route.abort());
    await context.route(base + 'catalog.json', route => route.fulfill({ json: fixture }));
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
        const chosen = fixture.resources.filter(r => r.edition === family && r.architecture === arch && r.kind === 'full')
          .sort((a,b) => new Intl.Collator('en',{numeric:true}).compare(b.version,a.version))[0];
        assert.ok((await page.locator('#result-size-detail').textContent()).includes(chosen.bytes.toLocaleString('en-US')));
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
      const beforeOnline = requested.length;
      await page.locator('#download-delivery').selectOption('online');
      assert.equal(await page.locator('#download-button').isDisabled(), true, 'Unpublished online media must not fall back to an offline download');
      assert.match(await page.locator('#download-guidance').textContent(), /在线安装包需要网络/);
      assert.equal(requested.length, beforeOnline);
      await page.locator('#download-delivery').selectOption('offline');
      assert.equal(await page.locator('#download-button').isDisabled(), false);
      assert.match(await page.locator('#download-version').textContent(), /离线安装/);
    }
    const online = structuredClone(fixture);
    for (const family of ['native', 'linux']) for (const architecture of ['amd64', 'arm64']) {
      const template = fixture.resources.find(r => r.edition === family && r.architecture === architecture && r.kind === 'full');
      const name = `mnOS-${family}-online-${architecture}.iso`;
      online.resources.push({ ...template, delivery: 'online', name, bytes: 1024 ** 2, version: 'v9.0-preview.1',
        url: `https://github.com/zengyiqian2014-boop/mnOS/releases/download/v9.0-preview.1/${name}`,
        release: 'https://github.com/zengyiqian2014-boop/mnOS/releases/tag/v9.0-preview.1' });
    }
    await page.route(base + 'catalog.json', route => route.fulfill({ json: online }));
    for (const [path, family] of [['', 'native'], ['linux.html', 'linux']]) {
      await page.goto(base + path, { waitUntil: 'networkidle' });
      for (const architecture of ['amd64', 'arm64']) {
        await page.locator('#download-arch').selectOption(architecture);
        await page.locator('#download-delivery').selectOption('online');
        await page.locator('#download-button:not(:disabled)').waitFor();
        assert.match(await page.locator('#download-version').textContent(), /在线安装/);
        await page.locator('#download-button').click();
        assert.equal(await page.locator('#consent-download').isDisabled(), true);
        assert.match(await page.locator('#consent-image').textContent(), new RegExp(`${family}-online-${architecture}\\.iso$`));
        await page.locator('#consent-agree').check();
        const download = page.waitForEvent('download');
        await page.locator('#consent-download').click();
        await (await download).delete();
        assert.match(requested.at(-1), new RegExp(`${family}-online-${architecture}\\.iso$`));
      }
    }
    await page.goto(base + 'terms.html');
    assert.match(await page.locator('main').textContent(), /NO WARRANTY\. ABSOLUTELY NO WARRANTY\./);
    assert.ok(await page.locator('html').evaluate(el => el.scrollWidth <= innerWidth));
    assert.deepEqual(errors, []);
    await context.close();
  }
  console.log('PASS: both ISO families/architectures, online/offline selection and missing-mode behavior; no download before consent; cancellation/Escape/focus/reset; 1440/390/320px; terms and no browser errors.');
} finally { await browser.close(); }
