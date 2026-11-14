export const REPOSITORY = 'https://github.com/zengyiqian2014-boop/mnOS';
export const RAW_CATALOG = 'https://raw.githubusercontent.com/zengyiqian2014-boop/mnOS/resources/catalog.json';

// A catalog can select a release asset, never arbitrary markup or an external
// executable URL. The system updater separately verifies the OpenPGP signature.
export function readCatalog(value) {
  if (!value || value.schema !== 1 || !Array.isArray(value.resources) || value.resources.length > 5000) throw new Error('Invalid catalog');
  const resources = value.resources.filter(resource => {
    if (!resource || typeof resource !== 'object') return false;
    const { name, url, release, version, bytes, sha256, edition, kind, architecture, delivery } = resource;
    return typeof name === 'string' && name.length < 300 &&
      typeof version === 'string' && /^[a-zA-Z0-9.+~_-]{1,100}$/.test(version) &&
      Number.isSafeInteger(bytes) && bytes > 0 && /^[0-9a-f]{64}$/.test(sha256) &&
      ['native', 'linux'].includes(edition) && ['full', 'lite', 'patch'].includes(kind) &&
      ['amd64', 'arm64', 'all'].includes(architecture) &&
      (delivery === undefined || ['offline', 'online'].includes(delivery)) &&
      (delivery === undefined || delivery === (name.includes('-online-') ? 'online' : 'offline')) &&
      typeof url === 'string' && url.startsWith(`${REPOSITORY}/releases/download/`) &&
      /^https:\/\/github\.com\/zengyiqian2014-boop\/mnOS\/releases\/download\/[a-zA-Z0-9.+~_-]+\/[a-zA-Z0-9.+~_-]+\.(iso|deb)$/.test(url) &&
      typeof release === 'string' && /^https:\/\/github\.com\/zengyiqian2014-boop\/mnOS\/releases\/tag\/[a-zA-Z0-9.+~_-]+$/.test(release);
  }).map(resource => ({ ...resource, delivery: resource.delivery ?? (resource.name.includes('-online-') ? 'online' : 'offline') }));
  if (!resources.length) throw new Error('Empty catalog');
  const updated = new Date(value.updated);
  return { resources, updated: Number.isNaN(updated.valueOf()) ? null : updated };
}

export function findImage(catalog, edition, architecture, kind, delivery = 'offline') {
  const versions = new Intl.Collator('en', { numeric: true, sensitivity: 'base' });
  return catalog?.resources.filter(resource => resource.edition === edition &&
    resource.architecture === architecture && resource.kind === kind && resource.delivery === delivery && resource.name.endsWith('.iso'))
    .sort((a, b) => versions.compare(b.version, a.version))[0] ?? null;
}

export function formatBytes(bytes) {
  return bytes >= 1024 ** 3 ? `${(bytes / 1024 ** 3).toFixed(2)} GiB` : `${(bytes / 1024 ** 2).toFixed(1)} MiB`;
}
