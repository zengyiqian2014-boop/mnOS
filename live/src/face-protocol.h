/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_FACE_PROTOCOL_H
#define MNOS_FACE_PROTOCOL_H
#include "login-protocol.h"
#define FACE_MAGIC 0x3243464dU
#ifndef MNOS_FACE_STORE
#define MNOS_FACE_STORE "/var/lib/mnos-login-process/faces"
#endif
#ifndef MNOS_FACE_HELPER
#define MNOS_FACE_HELPER "/usr/lib/mnos/face-helper"
#endif
#ifndef MNOS_FACE_CAMERA
#define MNOS_FACE_CAMERA "/usr/lib/mnos/face-camera"
#endif
/* The helper authenticates passwords independently. PAM verification may
 * only check status or verify the face; enrollment/removal always authenticate
 * through the password + TOTP broker. TOTP is independently required by PAM. */
struct face_request {
    uint32_t magic,operation,uid,password_bytes,totp_bytes,recovery_bytes;
    char display[32];
};
struct face_response {uint32_t result;char recovery[8][33];};
struct face_camera_request {uint32_t magic,enroll;float reference[128];};
struct face_camera_response {uint32_t magic,success;float reference[128];};
int face_child(uint32_t uid,int pam,int *pid);
int face_send(int fd,unsigned op,uint32_t uid,const char *password,const char *totp,const char *recovery,const char *display);
#endif
