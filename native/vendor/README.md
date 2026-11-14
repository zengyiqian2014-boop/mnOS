# Cryptography and QR encoding dependencies

Upstream sources are preserved without local modifications. These libraries
provide standard primitives; mnOS's authentication flow, storage integration,
account policy and GUI are original C/C++ code.

| Component | Version | Upstream | License |
| --- | --- | --- | --- |
| Monocypher | 4.0.2 | https://github.com/LoupVaillant/Monocypher/tree/4.0.2/src | Dual BSD-2-Clause / CC0; see `monocypher/LICENCE.md` |
| QR Code Generator | 1.8.0 | https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/c | MIT, embedded in source/header |

SHA-256 of the upstream source files:

```text
02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d  monocypher/monocypher.c
fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3  monocypher/monocypher.h
a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0  monocypher/LICENCE.md
300eff07ee25baaa7578f20284411638154716379437391e7e689c0e6ce81403  qrcodegen/qrcodegen.c
e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589  qrcodegen/qrcodegen.h
```
