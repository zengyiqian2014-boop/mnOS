// mnOS website gateway. The origin is deliberately fixed: this is not an open proxy.
export const PUBLIC_ORIGIN = 'https://mnos.micronull.cn';
export const SOURCE_ORIGIN = 'https://zengyiqian2014-boop.github.io';
export const SOURCE_PREFIX = '/mnOS/';

const pages = new Set(['/', '/index.html', '/linux.html', '/support.html', '/terms.html']);
const files = new Set([
  '/styles.css', '/app.mjs', '/catalog.mjs', '/catalog.json', '/film.mjs',
  '/film-scene.mjs', '/film-timing.mjs',
]);
const assetPath = /^\/(?:assets|vendor)\/(?:[A-Za-z0-9_-]+\/)*[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(?:css|js|mjs|json|svg|png|jpg|jpeg|webp|avif|ico|woff2?|ttf|otf|txt)$/;

function headersFor(headers = {}) {
  const result = new Headers(headers);
  result.delete('set-cookie');
  result.set('X-Content-Type-Options', 'nosniff');
  result.set('Referrer-Policy', 'strict-origin-when-cross-origin');
  return result;
}

function message(status, text, head = false, extra = {}) {
  return new Response(head ? null : text + '\n', {
    status,
    headers: headersFor({ 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store', ...extra }),
  });
}

function redirect(url, status = 308) {
  return new Response(null, {
    status,
    headers: headersFor({ Location: url.toString(), 'Cache-Control': 'no-store' }),
  });
}

function isPublicPath(path) {
  return pages.has(path) || files.has(path) || assetPath.test(path);
}

// fetchOrigin injection is used only by the local tests; production always uses fetch.
export async function handleRequest(request, fetchOrigin = globalThis.fetch) {
  const url = new URL(request.url);
  const head = request.method === 'HEAD';
  if (url.hostname !== new URL(PUBLIC_ORIGIN).hostname || (url.port && url.port !== '443')) {
    return message(421, 'Unknown website host.', head);
  }
  if (request.method !== 'GET' && !head) {
    return message(405, 'Only GET and HEAD are supported.', head, { Allow: 'GET, HEAD' });
  }
  if (url.protocol !== 'https:' && url.protocol !== 'http:') {
    return message(400, 'Unsupported protocol.', head);
  }

  let path = url.pathname;
  // Bookmarks copied from the original project site also work on this domain.
  const oldPrefix = path === '/mnOS' || path.startsWith(SOURCE_PREFIX);
  if (oldPrefix) path = path === '/mnOS' ? '/' : path.slice(SOURCE_PREFIX.length - 1);
  if (!isPublicPath(path)) return message(404, 'Page not found.', head);
  if (oldPrefix || url.protocol === 'http:') {
    return redirect(PUBLIC_ORIGIN + path + url.search);
  }

  const source = new URL(SOURCE_PREFIX + path.slice(1), SOURCE_ORIGIN);
  source.search = url.search;
  const htmlPage = pages.has(path);
  const headers = new Headers();
  // Do not send visitor cookies, Authorization, IP headers or Referer to GitHub.
  for (const name of ['Accept', ...(htmlPage ? [] : ['Range', 'If-Range', 'If-None-Match', 'If-Modified-Since'])]) {
    const value = request.headers.get(name);
    if (value !== null) headers.set(name, value);
  }

  let response;
  try {
    response = await fetchOrigin(source.toString(), {
      method: request.method,
      headers,
      redirect: 'manual',
      signal: AbortSignal.timeout(10000),
      cf: {
        cacheEverything: true,
        cacheTtlByStatus: { 200: htmlPage ? 60 : 300, 206: -1, '300-599': -1 },
      },
    });
  } catch {
    return message(503, 'mnOS website is temporarily unavailable. Please retry shortly.', head, { 'Retry-After': '30' });
  }

  if ([301, 302, 303, 307, 308].includes(response.status)) {
    const location = response.headers.get('location');
    let target;
    try { target = location && new URL(location, source); } catch { /* Invalid redirect. */ }
    // Refuse cross-site redirects and a Pages custom-domain redirect loop.
    if (!target || target.origin !== SOURCE_ORIGIN || !target.pathname.startsWith(SOURCE_PREFIX)) {
      return message(502, 'Unexpected website origin redirect.', head);
    }
    const mapped = target.pathname.slice(SOURCE_PREFIX.length - 1);
    if (!isPublicPath(mapped) || (mapped === path && target.search === url.search)) {
      return message(502, 'Unexpected website origin redirect.', head);
    }
    return redirect(PUBLIC_ORIGIN + mapped + target.search + target.hash, response.status);
  }

  const outgoing = headersFor(response.headers);
  if (response.status >= 400) outgoing.set('Cache-Control', 'no-store');
  else if (response.status === 200 || response.status === 206) {
    outgoing.set('Cache-Control', `public, max-age=${htmlPage ? 60 : 300}`);
  }
  let body = head ? null : response.body;
  if (htmlPage && response.headers.get('content-type')?.includes('text/html')) {
    // The HTML changes, so its old encoding, byte count and validators are invalid.
    for (const name of ['Content-Length', 'Content-Encoding', 'ETag', 'Last-Modified', 'Accept-Ranges', 'Content-Range']) {
      outgoing.delete(name);
    }
    if (!head) {
      try {
        body = (await response.text()).replaceAll(SOURCE_ORIGIN + SOURCE_PREFIX, PUBLIC_ORIGIN + '/');
      } catch {
        return message(503, 'mnOS website is temporarily unavailable. Please retry shortly.', false, { 'Retry-After': '30' });
      }
    }
  }
  return new Response(body, { status: response.status, headers: outgoing });
}

export default { fetch: request => handleRequest(request) };
