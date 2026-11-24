# mnOS Pages

Original product site, published from this repository at
<https://zengyiqian2014-boop.github.io/mnOS/>. Buildless HTML, CSS and ES modules;
no frontend framework, external fonts, analytics or third-party player SDKs.
The visual introduction starts automatically in place; it never scrolls the page.

For a complete static deployment to `mnos.micronull.cn`, use the
[domain-administrator instructions](DEPLOY.zh.md). Build with
`MNOS_SITE_PUBLIC_URL=https://mnos.micronull.cn/ node website/scripts/build.mjs`
and upload the contents of `_site/` to Cloudflare Pages.
To preview that exact upload locally, run
`MNOS_SITE_DIR=_site node website/scripts/serve.mjs`.

The prepared Cloudflare gateway for `mnos.micronull.cn` is in
[`deploy/cloudflare`](../deploy/cloudflare/README.md), including the Worker,
domain configuration and domain-administrator handoff. It retains this Pages
address as its origin; do not enable a Pages custom-domain redirect for it.

```sh
node --test website/scripts/catalog.test.mjs website/scripts/film-unit.test.mjs
node website/scripts/serve.mjs
# Open http://127.0.0.1:4173
node website/scripts/build.mjs
```

The homepage presents the original mnOS kernel and its optional Linux simulation
environment. The independent Linux system has its own `linux.html` page and ISO
selector. Each page's selector is fixed to its system: switching architectures or
profiles cannot substitute the other system's installation image. The native page
describes the v0.10 Full image's optional RV64 software guest, including APT,
the C++ application center and Debian riscv64 Firefox ESR. It distinguishes these
from AMD64/ARM64 Linux DEBs, which require the independent Linux Edition.

Screenshots in `assets/` come from actual native and Linux desktops. The new
`native-home.png` and `native-editor.png` are PNG conversions of the ARM64 QEMU
GUI test captures. No operating-system interface is invented for the film.

The introduction uses a real WebGL perspective camera, original device geometry,
a hinged lid, metal materials and separated screen layers. Three.js 0.180.0 and
its two helpers are vendored under `vendor/`, including the MIT license. The
device and chip illustrate the software; they are not physical mnOS products.
The reference for lighting and camera framing was Apple's MacBook Pro product
presentation. No Apple models, images or animation files are included.

The 24-second introduction uses continuous camera curves, a moving hinge and
transmissive glass around the floating system screens. It plays once and holds
its final frame. Scrolling away or hiding the tab pauses it; returning resumes
unless the user explicitly paused it. A small corner button controls playback.
The ordinary page remains scrollable throughout. The introduction runs on its own
animation clock, with no audio element, music configuration or third-party player.

Downloads are selected from the published resource catalog by edition, image
profile and CPU architecture. Unpublished combinations have no download link.
The page includes a fallback catalog and tries the live GitHub Raw JSON catalog.
Remote values are treated as data and only release URLs in this exact repository
are accepted. The browser does **not** perform OpenPGP verification; the deployment
workflow verifies the signed fallback and the operating-system updater verifies
its own signed catalog. The page links signatures and displays asset SHA-256.

Motion respects `prefers-reduced-motion` and the persistent pause control. The
renderer caps pixel ratio at 1.5 on desktop and 1.25 on phones, reducing it to 1
when sustained slow frames are detected. Pin geometry uses instancing. No frame
loop runs while the film is off screen, paused, hidden, or finished. WebGL/image
failure keeps the readable fallback. Reduced motion and no JavaScript show all
three chapters in document order.

With Playwright and Chromium available, run these against the preview server:

```sh
node website/scripts/film.test.mjs
node website/scripts/cinema.test.mjs
node website/scripts/download-consent.test.mjs
```

`PLAYWRIGHT_MODULE`, `CHROMIUM` and `MNOS_SITE_URL` can override local paths.
The cinema test renders real WebGL at 1440, 390 and 320 pixels and verifies
automatic playback, completion/replay, pause/resume and image/context-loss fallback.
The film test covers off-screen suspension, visibility, reduced motion and the
absence of audio components and music requests. Download tests check both system
families, architectures, delivery modes and consent.

The Pages workflow has separate read-only build and Pages deployment permissions.
It runs for website changes and successful resource publications. The catalog
snapshot is verified against `live/keys/mnos-archive.gpg` before packaging.
No signing secret or GitHub credential is included in the website.
