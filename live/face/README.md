# Camera verification assets

Pinned upstream: OpenCV Zoo commit
`48b10fa505aeb51fbd127fc60a5c660040e823b7`.

- `models/face_detection_yunet/face_detection_yunet_2023mar.onnx`: YuNet, MIT.
- `models/face_recognition_sface/face_recognition_sface_2021dec.onnx`: SFace,
  Apache-2.0. Model and license files are checked against `SOURCES.sha256`.
- OpenCV's packaged eye cascade retains its upstream license in `opencv-data`.

`../fetch-face-models.sh` downloads these files during the build. The installed
worker runs offline, with C++/OpenCV, and saves only an encrypted embedding.
These are third-party models, not mnOS-trained models. The mnOS challenge logic,
capture interface and encrypted-profile handling are original code.

The worker estimates pose from 2D landmarks; it does not measure trusted depth
or provide certified spoof detection. Linux Full can opt into it as an
additional PAM factor after password and any enabled TOTP, with separate
one-use recovery codes. See ../../AUTHENTICATION.md for the process boundary,
local X11 requirement and outstanding physical-camera/spoof evaluation.
