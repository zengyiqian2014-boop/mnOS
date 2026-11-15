// Local-only integration harness. Deploy worker.mjs, not this Node.js server.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { handleRequest, PUBLIC_ORIGIN, SOURCE_ORIGIN, SOURCE_PREFIX } from './worker.mjs';

const root = fileURLToPath(new URL('../../_site/', import.meta.url));
const types = {
  '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript',
  '.mjs': 'text/javascript', '.json': 'application/json', '.svg': 'image/svg+xml',
  '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp',
  '.mp3': 'audio/mpeg', '.m4a': 'audio/mp4', '.ogg': 'audio/ogg', '.wav': 'audio/wav',
  '.mp4': 'video/mp4', '.webm': 'video/webm', '.txt': 'text/plain',
};

async function localOrigin(input, options) {
  const url = new URL(input);
  if (url.origin !== SOURCE_ORIGIN || !url.pathname.startsWith(SOURCE_PREFIX)) {
    throw new Error('Unexpected upstream');
  }
  const relative = url.pathname.slice(SOURCE_PREFIX.length) || 'index.html';
  const file = resolve(root, relative);
  if (!file.startsWith(root.endsWith(sep) ? root : root + sep)) throw new Error('Unexpected path');
  try {
    const bytes = await readFile(file);
    return new Response(options.method === 'HEAD' ? null : bytes, {
      headers: { 'Content-Type': types[extname(file)] || 'application/octet-stream', 'Content-Length': String(bytes.length) },
    });
  } catch (error) {
    if (error.code === 'ENOENT') return new Response('Not found', { status: 404 });
    throw error;
  }
}

const server = createServer(async (incoming, outgoing) => {
  try {
    const url = new URL(incoming.url, PUBLIC_ORIGIN);
    const request = new Request(url, { method: incoming.method, headers: incoming.headers });
    const response = await handleRequest(request, localOrigin);
    outgoing.writeHead(response.status, Object.fromEntries(response.headers));
    outgoing.end(Buffer.from(await response.arrayBuffer()));
  } catch {
    outgoing.writeHead(500, { 'Content-Type': 'text/plain' });
    outgoing.end('Local preview error');
  }
});
server.listen(4174, '127.0.0.1', () => console.log('mnOS gateway preview: http://127.0.0.1:4174/'));
