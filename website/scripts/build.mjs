import { mkdir, cp, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { readCatalog } from '../catalog.mjs';

const source = fileURLToPath(new URL('../', import.meta.url));
const output = path.resolve(source, '../_site');
const catalogPath = process.env.MNOS_SITE_CATALOG || path.join(source, 'catalog.json');
readCatalog(JSON.parse(await readFile(catalogPath, 'utf8')));
await mkdir(output, { recursive: true });
for (const file of ['index.html', 'linux.html', 'terms.html', 'support.html', 'styles.css', 'app.mjs', 'film.mjs', 'film-scene.mjs', 'film-audio.mjs', 'film-timing.mjs', 'catalog.mjs', 'assets', 'vendor', 'media']) {
  await cp(path.join(source, file), path.join(output, file), { recursive: true });
}
// Keep both pages on the same UI revision after deployment. A cached old script
// must not select a native image on the new independent Linux download page.
const revision = createHash('sha256');
for (const file of ['styles.css', 'app.mjs', 'film.mjs', 'film-scene.mjs', 'film-audio.mjs', 'film-timing.mjs', 'media/music.json', 'catalog.mjs']) {
  revision.update(await readFile(path.join(source, file)));
}
const version = revision.digest('hex').slice(0, 16);
for (const file of ['index.html', 'linux.html', 'terms.html', 'support.html']) {
  const html = (await readFile(path.join(source, file), 'utf8'))
    .replace('./styles.css"', `./styles.css?v=${version}"`)
    .replace('./app.mjs"', `./app.mjs?v=${version}"`);
  await writeFile(path.join(output, file), html);
}
const script = (await readFile(path.join(source, 'app.mjs'), 'utf8'))
  .replace("from './catalog.mjs'", `from './catalog.mjs?v=${version}'`)
  .replace("from './film.mjs'", `from './film.mjs?v=${version}'`);
await writeFile(path.join(output, 'app.mjs'), script);
const film = (await readFile(path.join(source, 'film.mjs'), 'utf8'))
  .replace("'./film-scene.mjs'", `'./film-scene.mjs?v=${version}'`)
  .replace("'./film-audio.mjs'", `'./film-audio.mjs?v=${version}'`)
  .replace("'./film-timing.mjs'", `'./film-timing.mjs?v=${version}'`);
await writeFile(path.join(output, 'film.mjs'), film);
for (const module of ['film-scene.mjs', 'film-audio.mjs']) {
  const contents = (await readFile(path.join(source, module), 'utf8'))
    .replace("'./film-timing.mjs'", `'./film-timing.mjs?v=${version}'`)
    .replace("'./media/music.json'", `'./media/music.json?v=${version}'`);
  await writeFile(path.join(output, module), contents);
}
await cp(catalogPath, path.join(output, 'catalog.json'));
await writeFile(path.join(output, '.nojekyll'), '');
console.log(`Static mnOS Pages files: ${output}`);
