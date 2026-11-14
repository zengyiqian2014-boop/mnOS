import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { readCatalog, findImage, formatBytes } from '../catalog.mjs';

const value = JSON.parse(await readFile(new URL('../catalog.json', import.meta.url)));
const catalog = readCatalog(value);
test('architecture and edition never fall through to a different image', () => {
  for (const edition of ['native', 'linux']) for (const arch of ['amd64', 'arm64']) {
    const image = findImage(catalog, edition, arch, 'full');
    assert.ok(image, `${edition} ${arch} must have a published Full image`);
    assert.equal(image.architecture, arch);
    assert.equal(image.edition, edition);
  }
  assert.equal(findImage(catalog, 'native', 'riscv64', 'full'), null);
  assert.equal(findImage({ resources: [] }, 'linux', 'amd64', 'lite'), null);
});
test('numeric preview order selects preview 10 after preview 2', () => {
  const template = findImage(catalog, 'native', 'amd64', 'full');
  const candidates = { resources: [2, 10, 1].map(n => ({ ...template, version: `v0.7-native-preview.${n}` })) };
  assert.equal(findImage(candidates, 'native', 'amd64', 'full').version, 'v0.7-native-preview.10');
});
test('untrusted URLs, invalid checksums and missing metadata are not downloads', () => {
  const template = catalog.resources[0];
  for (const patch of [
    { url: 'javascript:alert(1)' },
    { url: 'https://github.com.evil.test/zengyiqian2014-boop/mnOS/releases/download/x/y.iso' },
    { url: 'https://github.com/someone/another/releases/download/x/y.iso' },
    { release: 'https://example.com/' },
    { sha256: 'unverified' },
    { architecture: 'unknown' },
    { bytes: -1 },
    { version: '<script>' },
  ]) assert.throws(() => readCatalog({ ...value, resources: [{ ...template, ...patch }] }));
});
test('public fallback catalog is usable and uses binary units honestly', () => {
  assert.ok(catalog.updated instanceof Date);
  assert.equal(formatBytes(1024 ** 3), '1.00 GiB');
  assert.equal(formatBytes(54 * 1024 ** 2), '54.0 MiB');
});
