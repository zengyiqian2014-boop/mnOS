# mnOS Pages

Original product site, published from this repository at
<https://zengyiqian2014-boop.github.io/mnOS/>. Buildless HTML, CSS and ES modules;
no frontend framework, external fonts, analytics or third-party player SDKs.
The visual introduction starts automatically in place; it never scrolls the page.

```sh
node --test website/scripts/catalog.test.mjs website/scripts/film-unit.test.mjs
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

The 24-second introduction uses continuous camera curves, a moving hinge and
transmissive glass around the floating system screens. It plays once and holds
its final frame. Scrolling away or hiding the tab pauses it; returning resumes
unless the user explicitly paused it. Small corner buttons control playback and
sound. The ordinary page remains scrollable throughout.

Audio uses a hidden native HTML audio element. Its clock synchronizes the camera
while playing; blocked or unavailable audio cannot stop the visual introduction.
Audible autoplay still follows browser policy: when blocked, a genuine tap or key
press can start it. Reduced motion does not request audio or construct WebGL.

The selected recording is **3 Strikes — Terror Jr**, the original 2016
recording. The previously substituted Cipher excerpt has been removed.
Official song pages: [Kugou](https://www.kugou.com/song/cnygy2e.html) and
[NetEase Music](https://music.163.com/#/song?id=409647388).
The music-note button in the introduction and “站内试听” in the footer open an
actual Apple Music embedded player for the original recording (song ID
1165457668). Visitors press Play inside the official player; the tested signed-out
player plays a 90-second preview. Its controls, attribution and Apple Music link
remain visible. Closing the dialog removes the iframe and stops playback. No
music platform requests are made before the visitor opens the player. The
Kugou page requests login. Mainland-China network performance has not been tested.

The official player is separate from the film clock. Apple's preview terms do
not permit using its preview files as advertising soundtracks or synchronizing
them with video: https://performance-partners.apple.com/search-api . Do not copy
preview audio into this repository or silently substitute another track.
The native background-audio interface remains available for an authorized
recording; until one is configured, the introduction itself is silent.

`media/music.json` selects the recording, its offset, the film duration and
up to three authorized local/HTTPS audio sources in preference order:

```json
{
  "title": "3 Strikes",
  "artist": "Terror Jr",
  "start": 12.06,
  "duration": 24,
  "sources": []
}
```

Relative paths resolve from the website root. The recording must include the
whole configured excerpt. Serve a correct audio MIME type and support byte
ranges for seeking. Each unavailable source has a five-second timeout before
trying the next mirror. Audio files under `media/` are included by the build;
the configuration is versioned with the scripts. This setting alone does not
make a GitHub-hosted file fast in China; verify the chosen CDN from that region.

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
node website/scripts/music.test.mjs
node website/scripts/download-consent.test.mjs
```

`PLAYWRIGHT_MODULE`, `CHROMIUM` and `MNOS_SITE_URL` can override local paths.
The cinema test renders real WebGL at 1440, 390 and 320 pixels and uses a
generated WAV to exercise Chromium's real decoder, audio clock and autoplay
policy. It verifies seeking, pause/resume, failed-source failover and image or
context-loss fallback. The fixture is not the production recording. `music.test.mjs` separately
checks the bundled MP3, actual decoding, seeking, autoplay and pause. No test
from this build machine proves reachability from all mainland Chinese networks. Download tests separately check
both system families, architectures, delivery modes and consent.

The Pages workflow has separate read-only build and Pages deployment permissions.
It runs for website changes and successful resource publications. The catalog
snapshot is verified against `live/keys/mnos-archive.gpg` before packaging.
No signing secret or GitHub credential is included in the website.
