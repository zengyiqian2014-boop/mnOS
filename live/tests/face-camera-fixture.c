/* SPDX-License-Identifier: GPL-3.0-or-later
 * Installed ONLY inside a disposable test container at the production worker
 * path. Production builds never compile or include this deterministic fixture. */
#define _GNU_SOURCE
#include "face-protocol.h"
#include <assert.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
int main(void){
    struct passwd *p=getpwnam("login-camera");assert(p&&p->pw_uid==getuid()&&getuid()!=0&&geteuid()==getuid());
    assert(prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0)==1);
    assert(getenv("XAUTHORITY")&&!strcmp(getenv("XAUTHORITY"),"/proc/self/fd/4"));
    struct face_camera_request q;assert(!login_read(3,&q,sizeof q)&&q.magic==FACE_MAGIC);
    char mode[8]={0};int file=open("/run/mnos-test-camera-mode",O_RDONLY);assert(file>=0&&read(file,mode,7)>0);close(file);
    struct face_camera_response r={.magic=FACE_MAGIC,.success=!strncmp(mode,"pass",4)};
    if(q.enroll)for(unsigned i=0;i<128;++i)r.reference[i]=0.08838834765f;
    else for(unsigned i=0;i<128;++i)assert(q.reference[i]>0.08838f&&q.reference[i]<0.08840f);
    assert(!login_write(3,&r,sizeof r));return r.success?0:1;
}
