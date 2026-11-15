# Native USB camera driver

mnOS now has an original xHCI host driver and UVC video receiver. The driver
runs inside the native AMD64/ARM64 kernel. It uses mnOS page allocation, MMIO,
DMA, timers and framebuffer drawing. It does not load Linux, a Linux driver,
V4L2, libusb or a Linux guest. The implementation is C; the test camera and VM
harness are C++. No external USB stack was copied into the kernel.

## Use

Connect a supported camera directly to an xHCI root port **before booting**.
Sign in and open Devices, then choose **Start camera**. The console provides:

```text
kernel usb
camera start
camera status
camera stop
help camera
help -cn camera
```

The camera is off at boot. Starting preview requires an authenticated local
session, not root. Stop clears the frame buffers; locking the session also
stops capture, as does closing the preview window or changing users. Unplugging
stops delivery and removes the preview. Reconnecting
currently requires rebooting; root-port hot-add and hub enumeration are not
implemented. No images are saved or sent over a network. A disconnected camera
cannot silently satisfy an authentication request: this driver is not connected
to face authentication at all.

## Current transport scope

- Firmware-assigned PCI xHCI MMIO BARs. AMD64 discovers PCI configuration space
  through CF8/CFC; ARM64 uses generic ECAM from FDT or ACPI MCFG. Firmware must
  have assigned BARs and enabled the upstream PCI bridge windows.
- Control endpoint transfers, bulk IN and high-speed isochronous IN, bounded
  command/event/transfer rings, controller timeouts and completion validation.
- Direct-root USB 2 high-speed UVC cameras exposing uncompressed YUY2 at an
  even width up to 640 pixels and height up to 480 pixels. The parser selects
  the largest supported advertised frame and negotiates PROBE/COMMIT. UVC 1.0,
  1.1 and 1.5 select the corresponding 26/34/48-byte probe structures.
- One camera stream; up to four controllers and 32 enumerated devices. The
  receiver uses 240 page-sized DMA buffers and two bounded YUY2 frame buffers.
  It validates header sizes, frame boundaries, residual lengths and transfer
  ownership. Incomplete/error frames are discarded; a ring-overrun notification
  does not consume a transfer descriptor. Doorbell updates are batched.

This is a polling driver. It does not yet provide xHCI interrupts, suspend/resume,
IOMMU isolation, hot-add, USB hubs, USB HID keyboard/mouse, SuperSpeed video,
MJPEG/H.264 decoding, audio or a user-process camera API. The existing PS/2 and
VirtIO input drivers remain necessary. Hardware DMA without an IOMMU is not a
security boundary against a malicious controller. Native desktop services still
run inside the kernel.

## Verification

The automated camera is deliberately synthetic. It advertises a 320×240 YUY2
stream with known red/blue pixels through QEMU's `usb-redir` device, attached to
`qemu-xhci`. Host-side libusbredir only implements the test peripheral; it is
not in the ISO. No Linux guest or camera shim receives frames for mnOS.

Tests install the actual ISO onto a fresh virtual disk, remove the ISO, boot the
original kernel, create accounts, enumerate USB, start capture and check pixels
in a framebuffer screenshot. They also stop/restart capture, lock/re-authenticate,
unplug and verify a subsequent start fails. Both bulk and isochronous cases run
on AMD64 and ARM64. Descriptor and frame parsing run under ASan/UBSan, including
truncation, invalid lengths, unsupported formats, oversized frames, error bits,
repeated EOF and recovery at a new frame boundary.

```sh
docker build -t mnos-native-builder -f native/Builder.Dockerfile .
docker run --rm --user "$(id -u):$(id -g)" --network none \
  -v "$PWD/native:/native" -w /native mnos-native-builder \
  bash tests/camera_ci.sh
```

Logs and screenshots are under `native/build/desktop/`. See the Native USB
camera workflow for the tested source revision and artifacts.

Physical xHCI controllers/cameras and ARM64 ACPI-only machines have not been
tested. Passing the synthetic transport test is not a physical compatibility
claim. Native face detection, identity matching and liveness verification are
still missing; this preview must not be described as working native face unlock.
Linux-edition camera/PAM code remains separate.
