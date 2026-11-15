// SPDX-License-Identifier: GPL-3.0-or-later
// Discover real V4L2 capture nodes; metadata/output nodes are not cameras.
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
namespace mnos_face {
struct CameraDevice {std::string path,name;unsigned index=0;};
struct CameraInventory {std::vector<CameraDevice> devices;bool denied=false,busy=false;};
inline bool video_index(const char *name,unsigned &index) {
    if(strncmp(name,"video",5)||!name[5])return false;
    unsigned number=0;
    for(const char *p=name+5;*p;++p) {
        if(*p<'0'||*p>'9'||number>(UINT32_MAX-unsigned(*p-'0'))/10)return false;
        number=number*10+unsigned(*p-'0');
    }
    index=number;return true;
}
inline bool capture_capable(const v4l2_capability &cap) {
    const uint32_t flags=(cap.capabilities&V4L2_CAP_DEVICE_CAPS)?cap.device_caps:cap.capabilities;
    // OpenCV's V4L2 backend consumes single-plane capture through streaming.
    return (flags&V4L2_CAP_VIDEO_CAPTURE)&&(flags&V4L2_CAP_STREAMING);
}
inline CameraInventory camera_inventory() {
    CameraInventory result;DIR *directory=opendir("/dev");if(!directory)return result;
    while(auto *entry=readdir(directory)) {
        unsigned index=0;if(!video_index(entry->d_name,index))continue;
        const std::string path=std::string("/dev/")+entry->d_name;
        int fd=open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0){result.denied|=errno==EACCES||errno==EPERM;result.busy|=errno==EBUSY;continue;}
        struct stat st{};v4l2_capability cap{};int queried=-1;
        if(!fstat(fd,&st)&&S_ISCHR(st.st_mode))do{queried=ioctl(fd,VIDIOC_QUERYCAP,&cap);}while(queried<0&&errno==EINTR);
        close(fd);
        if(queried<0||!capture_capable(cap))continue;
        result.devices.push_back({path,std::string(reinterpret_cast<const char *>(cap.card),strnlen(reinterpret_cast<const char *>(cap.card),sizeof cap.card)),index});
    }
    closedir(directory);
    std::sort(result.devices.begin(),result.devices.end(),[](const auto &a,const auto &b){return a.index<b.index;});
    return result;
}
// The failed first camera must not prevent a second usable camera from opening.
template<class TryOpen> inline const CameraDevice *select_camera(const CameraInventory &inventory,TryOpen open) {
    for(const auto &device:inventory.devices)if(open(device))return &device;
    return nullptr;
}
}
