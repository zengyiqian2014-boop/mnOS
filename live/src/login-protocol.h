/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_LOGIN_PROTOCOL_H
#define MNOS_LOGIN_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#ifndef MNOS_LOGIN_STORE
#define MNOS_LOGIN_STORE "/var/lib/mnos-login-process"
#endif
#ifndef MNOS_LOGIN_SOCKET
#define MNOS_LOGIN_SOCKET "/run/mnos-login-process.sock"
#endif
#define LOGIN_MAGIC 0x3141464dU
enum login_op {LOGIN_STATUS=1,LOGIN_ENROLL,LOGIN_VERIFY,LOGIN_DISABLE};
enum login_result {LOGIN_OK=0,LOGIN_ABSENT=1,LOGIN_DENIED=2,LOGIN_ERROR=3,LOGIN_SETUP=4,LOGIN_RETRY=5};
struct login_request {uint32_t magic,operation,uid,password_bytes,code_bytes;};
struct login_response {uint32_t result;uint8_t secret[32];char recovery[8][33];};
int login_write(int,const void *,size_t);
int login_read(int,void *,size_t);
int login_connect(void);
int login_send(int,unsigned,uint32_t,const char *,const char *);
/* A missing record is the only fail-open case for accounts without 2FA. */
int login_configured(uint32_t);
void login_serve(int);
int login_child(uint32_t, int install, int *child_pid);
#endif
