// SPDX-License-Identifier: GPL-3.0-or-later
// Launched by the broker as a dedicated, unprivileged UID. This process has
// only the encrypted-record's decrypted template, never a password/TOTP seed.
#include "face-capture.hpp"
extern "C" {
#include "face-protocol.h"
}
int main(int argc,char **argv) {
    auto *user=getpwnam("login-camera");if(!user||getuid()!=user->pw_uid||geteuid()!=user->pw_uid||!getuid())return 1;
    rlimit core{0,0};if(setrlimit(RLIMIT_CORE,&core)||prctl(PR_SET_DUMPABLE,0)||prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return 1;
    face_camera_request request{};face_camera_response response{};response.magic=FACE_MAGIC;
    if(mlock(&request,sizeof request)||login_read(3,&request,sizeof request)||request.magic!=FACE_MAGIC||request.enroll>1)return 1;
    try {
        if(!gtk_init_check(&argc,&argv))throw std::runtime_error("Display unavailable");
        bool enroll=request.enroll==1;
        Sensitive sensitive;memcpy(sensitive.reference.data(),request.reference,512);
        auth_wipe(&request,sizeof request);munlock(&request,sizeof request);
        bool accepted=false;
        {Capture capture(sensitive,enroll);
            {View view(capture);gtk_main();accepted=view.accepted;}
            capture.stop=true;if(capture.worker.joinable())capture.worker.join();
            accepted=accepted&&capture.done&&capture.success;
        }
        response.success=accepted?1:0;
        if(accepted&&enroll)memcpy(response.reference,sensitive.reference.data(),512);
        bool sent=!login_write(3,&response,sizeof response);auth_wipe(&response,sizeof response);
        return accepted&&sent?0:1;
    } catch(...) {auth_wipe(&request,sizeof request);munlock(&request,sizeof request);(void)login_write(3,&response,sizeof response);return 1;}
}
