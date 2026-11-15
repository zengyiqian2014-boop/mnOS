import { test } from 'node:test';
import assert from 'node:assert/strict';
import { handleRequest, PUBLIC_ORIGIN, SOURCE_ORIGIN, SOURCE_PREFIX } from './worker.mjs';

const origin = SOURCE_ORIGIN + SOURCE_PREFIX;
const request = (path = '/', options) => new Request(PUBLIC_ORIGIN + path, options);
const neverFetch = () => { throw new Error('Unexpected origin request'); };

test('root, Linux page and versioned module map to the original project path', async () => {
  for (const [path, expected] of [
    ['/', origin], ['/linux.html', origin + 'linux.html'],
    ['/film-scene.mjs?v=abc123', origin + 'film-scene.mjs?v=abc123'],
    ['/vendor/three.module.min.js', origin + 'vendor/three.module.min.js'],
    ['/assets/native-desktop.png', origin + 'assets/native-desktop.png'],
  ]) {
    let called = false;
    const response = await handleRequest(request(path), async (url, options) => {
      called = true;
      assert.equal(url, expected);
      assert.equal(options.redirect, 'manual');
      assert.ok(options.signal instanceof AbortSignal);
      return new Response('fixture');
    });
    assert.equal(response.status, 200);
    assert.equal(called, true);
  }
});

test('HTML canonical and Open Graph use the new origin; releases and relative links stay intact', async () => {
  const html = `<link rel="canonical" href="${origin}linux.html">
    <meta property="og:image" content="${origin}assets/linux-desktop.png">
    <script type="module" src="./app.mjs?v=123"></script>
    <a href="https://github.com/zengyiqian2014-boop/mnOS/releases">Downloads</a>`;
  const response = await handleRequest(request('/linux.html', {
    headers: { 'If-None-Match': 'old-html', Range: 'bytes=0-10' },
  }), async (_, options) => {
    assert.equal(options.headers.has('If-None-Match'), false);
    assert.equal(options.headers.has('Range'), false);
    return new Response(html, { headers: {
      'Content-Type': 'text/html; charset=utf-8', ETag: 'old-html',
      'Content-Length': '999', 'Content-Encoding': 'gzip',
      'Last-Modified': 'Wed, 11 Nov 2026 01:00:00 GMT', 'Accept-Ranges': 'bytes',
      'Set-Cookie': 'origin=private',
    } });
  });
  const body = await response.text();
  assert.ok(body.includes(`${PUBLIC_ORIGIN}/linux.html`));
  assert.ok(body.includes(`${PUBLIC_ORIGIN}/assets/linux-desktop.png`));
  assert.ok(body.includes('./app.mjs?v=123'));
  assert.ok(body.includes('https://github.com/zengyiqian2014-boop/mnOS/releases'));
  assert.ok(!body.includes(origin));
  for (const key of ['etag', 'last-modified', 'content-length', 'content-encoding', 'accept-ranges', 'set-cookie']) {
    assert.equal(response.headers.has(key), false, key);
  }
  assert.equal(response.headers.get('cache-control'), 'public, max-age=60');
  assert.equal(response.headers.get('x-content-type-options'), 'nosniff');
});

test('visitor credentials and identifying headers never reach the origin', async () => {
  await handleRequest(request('/catalog.json', { headers: {
    Cookie: 'session=private', Authorization: 'Bearer private',
    'CF-Connecting-IP': '192.0.2.1', 'X-Forwarded-For': '192.0.2.1',
    Referer: 'https://example.com/private', Accept: 'application/json',
  } }), async (_, options) => {
    assert.deepEqual([...options.headers.keys()], ['accept']);
    return new Response('{}');
  });
});

test('assets stream unchanged and preserve byte ranges and validators', async () => {
  const bytes = new Uint8Array([0, 255, 128, 37]);
  const response = await handleRequest(request('/assets/native-desktop.png', {
    headers: { Range: 'bytes=0-3', 'If-Range': 'image-v1' },
  }), async (_, options) => {
    assert.equal(options.headers.get('range'), 'bytes=0-3');
    assert.equal(options.headers.get('if-range'), 'image-v1');
    return new Response(bytes, { status: 206, headers: {
      'Content-Type': 'image/png', 'Content-Range': 'bytes 0-3/100',
      ETag: 'image-v1', 'Content-Length': '4', 'Accept-Ranges': 'bytes',
    } });
  });
  assert.equal(response.status, 206);
  assert.deepEqual(new Uint8Array(await response.arrayBuffer()), bytes);
  assert.equal(response.headers.get('content-range'), 'bytes 0-3/100');
  assert.equal(response.headers.get('etag'), 'image-v1');
  assert.equal(response.headers.get('content-length'), '4');
});

test('conditional asset requests retain 304 responses', async () => {
  const response = await handleRequest(request('/styles.css', {
    headers: { 'If-None-Match': 'style-v1' },
  }), async (_, options) => {
    assert.equal(options.headers.get('if-none-match'), 'style-v1');
    return new Response(null, { status: 304, headers: { ETag: 'style-v1' } });
  });
  assert.equal(response.status, 304);
  assert.equal(await response.text(), '');
});

test('HEAD stays bodyless and does not expose stale HTML validators', async () => {
  const response = await handleRequest(request('/', { method: 'HEAD' }), async (_, options) => {
    assert.equal(options.method, 'HEAD');
    return new Response(null, { headers: { 'Content-Type': 'text/html', 'Content-Length': '123', ETag: 'old' } });
  });
  assert.equal(await response.text(), '');
  assert.equal(response.headers.has('content-length'), false);
  assert.equal(response.headers.has('etag'), false);
});

test('old project-prefixed links and HTTP redirect without contacting origin', async () => {
  for (const [url, target] of [
    [PUBLIC_ORIGIN + '/mnOS', PUBLIC_ORIGIN + '/'],
    [PUBLIC_ORIGIN + '/mnOS/', PUBLIC_ORIGIN + '/'],
    [PUBLIC_ORIGIN + '/mnOS/linux.html?mode=1', PUBLIC_ORIGIN + '/linux.html?mode=1'],
    ['http://mnos.micronull.cn/support.html', PUBLIC_ORIGIN + '/support.html'],
  ]) {
    const response = await handleRequest(new Request(url), neverFetch);
    assert.equal(response.status, 308);
    assert.equal(response.headers.get('location'), target);
  }
});

test('unrelated hosts, unsupported methods and non-website paths are refused', async () => {
  assert.equal((await handleRequest(new Request('https://other.example/'), neverFetch)).status, 421);
  assert.equal((await handleRequest(new Request(PUBLIC_ORIGIN + ':8443/'), neverFetch)).status, 421);
  assert.equal((await handleRequest(request('/', { method: 'POST', body: 'data' }), neverFetch)).status, 405);
  for (const path of ['/other-project/', '/.git/config', '/assets/%2Fprivate.png', '/assets//a.png',
    '/media/.env', '/media/music.json', '/film-audio.mjs', '/music-player.mjs', '//other.example/', '/mnOS//other.example/', '/installer.iso']) {
    assert.equal((await handleRequest(request(path), neverFetch)).status, 404, path);
  }
});

test('query parameters cannot change the upstream host or project', async () => {
  await handleRequest(request('/?url=https://other.example/private'), async (url) => {
    assert.equal(new URL(url).origin, SOURCE_ORIGIN);
    assert.equal(new URL(url).pathname, SOURCE_PREFIX);
    return new Response('ok');
  });
});

test('origin redirects are rewritten only within this project', async () => {
  const response = await handleRequest(request('/index.html'), async () =>
    new Response(null, { status: 301, headers: { Location: SOURCE_PREFIX } }));
  assert.equal(response.status, 301);
  assert.equal(response.headers.get('location'), PUBLIC_ORIGIN + '/');
  for (const location of ['https://other.example/', PUBLIC_ORIGIN + '/', origin + 'index.html',
    SOURCE_ORIGIN + '/different-project/', 'http://[invalid', origin + '.git/config']) {
    const failed = await handleRequest(request('/index.html'), async () =>
      new Response(null, { status: 302, headers: { Location: location } }));
    assert.equal(failed.status, 502, location);
    assert.equal(failed.headers.get('cache-control'), 'no-store');
  }
});

test('origin network failures and errors are not cached or disguised as success', async () => {
  const failed = await handleRequest(request('/'), async () => { throw new Error('private diagnostic'); });
  assert.equal(failed.status, 503);
  assert.equal(failed.headers.get('retry-after'), '30');
  assert.equal(failed.headers.get('cache-control'), 'no-store');
  assert.ok(!(await failed.text()).includes('private diagnostic'));
  const missing = await handleRequest(request('/assets/missing.png'), async () =>
    new Response('not found', { status: 404, headers: { 'Cache-Control': 'public, max-age=600' } }));
  assert.equal(missing.status, 404);
  assert.equal(missing.headers.get('cache-control'), 'no-store');
});
