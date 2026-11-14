# mnOS Pages

Original product site, published from this repository at
<https://zengyiqian2014-boop.github.io/mnOS/>. Buildless HTML, CSS and ES modules;
no frontend framework, external fonts, analytics or third-party scripts.

```sh
node --test website/scripts/catalog.test.mjs
node website/scripts/serve.mjs
# Open http://127.0.0.1:4173
node website/scripts/build.mjs
```

The page presents Native and Linux Edition separately. Screenshots in `assets/`
are unmodified copies of the repository's actual `native-linux-guest.png` and
`linux-edition-2.1.png`. The animated chip is a brand illustration, not a claim
of a physical mnOS processor or a benchmark.

Downloads are selected from the published resource catalog by edition, image
profile and CPU architecture. Unpublished combinations have no download link.
The page includes a fallback catalog and tries the live GitHub Raw JSON catalog.
Remote values are treated as data and only release URLs in this exact repository
are accepted. The browser does **not** perform OpenPGP verification; the deployment
workflow verifies the signed fallback and the operating-system updater verifies
its own signed catalog. The page links signatures and displays asset SHA-256.

Motion respects `prefers-reduced-motion`, offers a persistent pause control,
and stops the 30-fps, resolution-capped canvas outside the viewport or in hidden
tabs. Content and direct resource links remain available without JavaScript.

The Pages workflow has separate read-only build and Pages deployment permissions.
It runs for website changes and successful resource publications. The catalog
snapshot is verified against `live/keys/mnos-archive.gpg` before packaging.
No signing secret or GitHub credential is included in the website.
