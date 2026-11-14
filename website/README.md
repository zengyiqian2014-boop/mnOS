# mnOS Pages

Original product site, published from this repository at
<https://zengyiqian2014-boop.github.io/mnOS/>. Buildless HTML, CSS and ES modules;
no frontend framework, external fonts or analytics. The optional music button
loads SoundCloud's visible official player only after a user click.

```sh
node --test website/scripts/catalog.test.mjs
node website/scripts/serve.mjs
# Open http://127.0.0.1:4173
node website/scripts/build.mjs
```

The homepage presents the original mnOS kernel and its optional Linux simulation
environment. The independent Linux system has its own `linux.html` page and ISO
selector. Each page's selector is fixed to its system: switching architectures or
profiles cannot substitute the other system's installation image. Linux desktop
applications and APT are described only on the independent Linux system page.

Screenshots in `assets/` come from actual native and Linux desktops. The new
`native-home.png` and `native-editor.png` are PNG conversions of the ARM64 QEMU
GUI test captures. No operating-system interface is invented for the film.

The introduction uses a real WebGL perspective camera, original device geometry,
a hinged lid, metal materials and separated screen layers. Three.js 0.180.0 and
its two helpers are vendored under `vendor/`, including the MIT license. The
device and chip illustrate the software; they are not physical mnOS products.
The reference for lighting and camera framing was Apple's MacBook Pro product
presentation. No Apple models, images or animation files are included.

Scroll scrubs the scene in either direction. The play button runs a 24-second
silent sequence. Music mode streams Terror Jr's artist upload of **3 Strikes**
through SoundCloud, beginning at approximately **00:12.06**. Eight 3-second bars
set the 80-BPM shot spacing. The SoundCloud position, rather than a separate
wall clock, drives playback; buffering cannot run the visuals ahead indefinitely.
No audio recording is stored or redistributed. Browser playback policies and
SoundCloud availability still apply; the visible player can be started manually.

Downloads are selected from the published resource catalog by edition, image
profile and CPU architecture. Unpublished combinations have no download link.
The page includes a fallback catalog and tries the live GitHub Raw JSON catalog.
Remote values are treated as data and only release URLs in this exact repository
are accepted. The browser does **not** perform OpenPGP verification; the deployment
workflow verifies the signed fallback and the operating-system updater verifies
its own signed catalog. The page links signatures and displays asset SHA-256.

Motion respects `prefers-reduced-motion` and the persistent pause control. The
3D scene renders only when its progress or size changes, caps the pixel ratio
at 1.7, and stops playback when the tab is hidden. Wheel, touch and Escape stop
automatic playback. WebGL/image failure falls back to the readable introduction;
music failure leaves silent playback available. Reduced motion and no JavaScript
show all three chapters in document order.

With Playwright and Chromium available, run these against the preview server:

```sh
node website/scripts/film.test.mjs
node website/scripts/cinema.test.mjs
node website/scripts/download-consent.test.mjs
```

`PLAYWRIGHT_MODULE`, `CHROMIUM` and `MNOS_SITE_URL` can override local paths.
The cinema test renders real WebGL at 1440, 390 and 320 pixels and uses a
deterministic player fixture to test seeking, buffering, pause and service
failure. Actual SoundCloud streaming is also checked manually before publishing.

The Pages workflow has separate read-only build and Pages deployment permissions.
It runs for website changes and successful resource publications. The catalog
snapshot is verified against `live/keys/mnos-archive.gpg` before packaging.
No signing secret or GitHub credential is included in the website.
