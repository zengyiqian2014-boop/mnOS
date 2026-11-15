/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_USB_H
#define MNOS_USB_H
#include "uvc.h"
void usb_init(void);
void usb_poll(void);
unsigned usb_controller_count(void);
unsigned usb_device_count(void);
void usb_list(void (*)(const char *));
bool camera_start(void);
bool camera_active(void);
void camera_stop(void);
const char *camera_status(void);
const struct uvc_mode *camera_mode(void);
const struct uvc_frames *camera_frames(void);
#endif
