import { mkdir, cp, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { readCatalog } from '../catalog.mjs';

const source = fileURLToPath(new URL('../', import.meta.url));
const output = path.resolve(source, '../_site');
const catalogPath = process.env.MNOS_SITE_CATALOG || path.join(source, 'catalog.json');
readCatalog(JSON.parse(await readFile(catalogPath, 'utf8')));
await mkdir(output, { recursive: true });
for (const file of ['index.html', 'linux.html', 'styles.css', 'app.mjs', 'catalog.mjs', 'assets']) {
  await cp(path.join(source, file), path.join(output, file), { recursive: true });
}
await cp(catalogPath, path.join(output, 'catalog.json'));
await writeFile(path.join(output, '.nojekyll'), '');
console.log(`Static mnOS Pages files: ${output}`);
