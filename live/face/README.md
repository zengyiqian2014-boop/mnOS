# Camera preview assets

Pinned upstream: OpenCV Zoo commit
`48b10fa505aeb51fbd127fc60a5c660040e823b7`.

- `models/face_detection_yunet/face_detection_yunet_2023mar.onnx`: YuNet, MIT.
- `models/face_recognition_sface/face_recognition_sface_2021dec.onnx`: SFace,
  Apache-2.0. Model and license files are checked against `SOURCES.sha256`.
- OpenCV's packaged eye cascade retains its upstream license in `opencv-data`.

`../fetch-face-models.sh` downloads these files during the build. The installed
preview runs offline, with C++/OpenCV, and saves only an encrypted embedding.
These are third-party models, not mnOS-trained models. The mnOS challenge logic,
capture interface and encrypted-profile handling are original code.

The preview estimates head pose from 2D landmarks. It does not measure physical
depth, certify liveness, or authenticate system login. Camera performance and
spoof resistance require physical-device evaluation before enabling that use.
