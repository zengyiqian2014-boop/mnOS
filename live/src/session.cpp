// SPDX-License-Identifier: GPL-3.0-or-later
// Small C++ session supervisor for the Python-free edition.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
static volatile sig_atomic_t stopping=0;
static void stop(int) {stopping=1;}
int main() {
    if(getuid()==0) {fputs("Use an ordinary account for the desktop session.\n",stderr);return 1;}
    if(!getenv("DISPLAY"))return 1;
    setenv("XDG_CURRENT_DESKTOP","XFCE",1);
    setenv("XDG_SESSION_DESKTOP","mnos-lite",1);
    setenv("XDG_SESSION_TYPE","x11",1);
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
    std::vector<pid_t> children;
    auto launch=[&](const char *path,const char *option=nullptr) {
        pid_t p=fork();
        if(p==0) {
            signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
            setpgid(0,0);execl(path,path,option,static_cast<char *>(nullptr));_exit(127);
        }
        if(p>0) {setpgid(p,p);children.push_back(p);}return p;
    };
    pid_t dirs=launch("/usr/bin/xdg-user-dirs-update");
    if(dirs>0) {while(waitpid(dirs,nullptr,0)<0&&errno==EINTR){}children.pop_back();}
    pid_t wm=launch("/usr/bin/xfwm4","--compositor=off");
    if(wm<0)return 1;
    if(access("/usr/bin/xfsettingsd",X_OK)==0)launch("/usr/bin/xfsettingsd","--no-daemon");
    launch("/usr/bin/xfdesktop");launch("/usr/bin/xfce4-panel");
    launch("/usr/bin/nm-applet");launch("/usr/bin/xfce4-screensaver");
#if defined(__aarch64__)
    launch("/usr/lib/aarch64-linux-gnu/polkit-mate/polkit-mate-authentication-agent-1");
#else
    launch("/usr/lib/x86_64-linux-gnu/polkit-mate/polkit-mate-authentication-agent-1");
#endif
    if(access("/usr/bin/mnos-face-preview",X_OK)==0)launch("/usr/bin/mnos-face-preview","--first-run");
    // Only processes created by this session are terminated at logout.
    while(!stopping) {
        int status;pid_t p=waitpid(-1,&status,WNOHANG);
        if(p>0)for(auto &id:children)if(id==p)id=0;
        if(p==wm||(p<0&&errno==ECHILD))break;
        usleep(100000);
    }
    for(pid_t p:children)if(p>0)kill(-p,SIGTERM);
    for(unsigned i=0;i<20;++i) {
        pid_t p;while((p=waitpid(-1,nullptr,WNOHANG))>0)for(auto &id:children)if(id==p)id=0;
        usleep(50000);
    }
    for(pid_t p:children)if(p>0)kill(-p,SIGKILL);
    while(waitpid(-1,nullptr,0)>0||errno==EINTR){}
    return 0;
}
