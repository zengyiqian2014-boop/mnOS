// SPDX-License-Identifier: GPL-3.0-or-later
#include "face-device.hpp"
#include <cassert>
#include <iostream>
using namespace mnos_face;
int main() {
    unsigned index=0;
    assert(video_index("video12",index)&&index==12);
    for(const char *name:{"video","video-1","video0.backup","video4294967296","../video0","camera0"})assert(!video_index(name,index));
    v4l2_capability cap{};cap.capabilities=V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_STREAMING;assert(capture_capable(cap));
    cap.capabilities|=V4L2_CAP_DEVICE_CAPS;cap.device_caps=V4L2_CAP_META_CAPTURE|V4L2_CAP_STREAMING;
    assert(!capture_capable(cap)); // USB camera metadata nodes inherit broad parent caps.
    cap.device_caps=V4L2_CAP_VIDEO_OUTPUT|V4L2_CAP_STREAMING;assert(!capture_capable(cap));
    cap.device_caps=V4L2_CAP_VIDEO_CAPTURE;assert(!capture_capable(cap));
    cap.device_caps|=V4L2_CAP_STREAMING;assert(capture_capable(cap));
    CameraInventory inventory;inventory.devices={{"/dev/video2","Busy camera",2},{"/dev/video8","USB camera",8}};
    unsigned attempts=0;
    auto *device=select_camera(inventory,[&](const CameraDevice &candidate){++attempts;return candidate.index==8;});
    assert(device&&device->path=="/dev/video8"&&attempts==2);
    assert(!select_camera(inventory,[](const auto &){return false;}));
    CameraInventory empty;assert(!select_camera(empty,[](const auto &){assert(false);return true;}));
    std::cout<<"PASS camera selection: metadata/output exclusion, device capability mask, numbered capture nodes and fallback after a failed first device. Device I/O still needs hardware validation.\n";
}
