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
for (const file of ['index.html', 'linux.html', 'styles.css', 'app.mjs', 'catalog.mjs', 'assets']) {
  await cp(path.join(source, file), path.join(output, file), { recursive: true });
}
// Keep both pages on the same UI revision after deployment. A cached old script
// must not select a native image on the new independent Linux download page.
const revision = createHash('sha256');
for (const file of ['styles.css', 'app.mjs', 'catalog.mjs']) {
  revision.update(await readFile(path.join(source, file)));
}
const version = revision.digest('hex').slice(0, 16);
for (const file of ['index.html', 'linux.html']) {
  const html = (await readFile(path.join(source, file), 'utf8'))
    .replace('./styles.css"', `./styles.css?v=${version}"`)
    .replace('./app.mjs"', `./app.mjs?v=${version}"`);
  await writeFile(path.join(output, file), html);
}
const script = (await readFile(path.join(source, 'app.mjs'), 'utf8'))
  .replace("from './catalog.mjs'", `from './catalog.mjs?v=${version}'`);
await writeFile(path.join(output, 'app.mjs'), script);
await cp(catalogPath, path.join(output, 'catalog.json'));
await writeFile(path.join(output, '.nojekyll'), '');
console.log(`Static mnOS Pages files: ${output}`);
