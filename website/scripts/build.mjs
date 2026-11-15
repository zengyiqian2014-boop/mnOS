import { mkdir, rm, cp, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { readCatalog } from '../catalog.mjs';

const source = fileURLToPath(new URL('../', import.meta.url));
const output = path.resolve(source, '../_site');
const catalogPath = process.env.MNOS_SITE_CATALOG || path.join(source, 'catalog.json');
const catalogText = await readFile(catalogPath, 'utf8');
readCatalog(JSON.parse(catalogText));
const originalBase = 'https://zengyiqian2014-boop.github.io/mnOS/';
const publicBase = new URL(process.env.MNOS_SITE_PUBLIC_URL || originalBase);
if (publicBase.protocol !== 'https:' || publicBase.username || publicBase.password ||
    publicBase.search || publicBase.hash || /["'<>&]/.test(publicBase.href)) {
  throw new Error('MNOS_SITE_PUBLIC_URL must be a public HTTPS base URL without credentials, query or fragment');
}
if (!publicBase.pathname.endsWith('/')) publicBase.pathname += '/';
// Always rebuild this generated directory so removed modules cannot ship again.
await rm(output, { recursive: true, force: true });
await mkdir(output, { recursive: true });
for (const file of ['index.html', 'linux.html', 'terms.html', 'support.html', 'styles.css', 'app.mjs', 'film.mjs', 'film-scene.mjs', 'film-timing.mjs', 'catalog.mjs', 'assets', 'vendor']) {
  await cp(path.join(source, file), path.join(output, file), { recursive: true });
}
// Keep both pages on the same UI revision after deployment. A cached old script
// must not select a native image on the new independent Linux download page.
const revision = createHash('sha256');
for (const file of ['styles.css', 'app.mjs', 'film.mjs', 'film-scene.mjs', 'film-timing.mjs', 'catalog.mjs']) {
  revision.update(await readFile(path.join(source, file)));
}
const version = revision.digest('hex').slice(0, 16);
for (const file of ['index.html', 'linux.html', 'terms.html', 'support.html']) {
  const html = (await readFile(path.join(source, file), 'utf8'))
    .replace('./styles.css"', `./styles.css?v=${version}"`)
    .replace('./app.mjs"', `./app.mjs?v=${version}"`)
    .replaceAll(originalBase, publicBase.href);
  await writeFile(path.join(output, file), html);
}
const script = (await readFile(path.join(source, 'app.mjs'), 'utf8'))
  .replace("from './catalog.mjs'", `from './catalog.mjs?v=${version}'`)
  .replace("from './film.mjs'", `from './film.mjs?v=${version}'`);
await writeFile(path.join(output, 'app.mjs'), script);
const film = (await readFile(path.join(source, 'film.mjs'), 'utf8'))
  .replace("'./film-scene.mjs'", `'./film-scene.mjs?v=${version}'`)
  .replace("'./film-timing.mjs'", `'./film-timing.mjs?v=${version}'`);
await writeFile(path.join(output, 'film.mjs'), film);
const scene = (await readFile(path.join(source, 'film-scene.mjs'), 'utf8'))
  .replace("'./film-timing.mjs'", `'./film-timing.mjs?v=${version}'`);
await writeFile(path.join(output, 'film-scene.mjs'), scene);
await writeFile(path.join(output, 'catalog.json'), catalogText);
await writeFile(path.join(output, '.nojekyll'), '');
console.log(`Static mnOS Pages files: ${output}`);
