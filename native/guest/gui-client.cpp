// SPDX-License-Identifier: GPL-3.0-or-later
// Guest-side desktop control. This program never executes on the mnOS host.
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
static std::string directory() {
    const char *value=getenv("MNOS_GUI_DIRECTORY");
    return value?value:"/run/mnos-gui";
}
static bool private_directory(const std::string &path) {
    if(mkdir(path.c_str(),0700)&&errno!=EEXIST)return false;
    struct stat st{};
    return !lstat(path.c_str(),&st)&&S_ISDIR(st.st_mode)&&st.st_uid==getuid()&&!(st.st_mode&0077);
}
static int connection(const std::string &path) {
    sockaddr_un address{};address.sun_family=AF_UNIX;
    if(path.size()>=sizeof address.sun_path)return -1;
    memcpy(address.sun_path,path.c_str(),path.size()+1);
    const int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(fd<0)return -1;
    if(connect(fd,reinterpret_cast<sockaddr *>(&address),sizeof address)){close(fd);return -1;}
    timeval timeout{10,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
    return fd;
}
static bool start_session(const std::string &path) {
    if(getenv("MNOS_GUI_NO_START"))return false;
    const auto file=path+"/session.lock";
    int lock=open(file.c_str(),O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0)return false;
    if(flock(lock,LOCK_EX|LOCK_NB)) {
        const bool other=errno==EWOULDBLOCK;close(lock);return other;
    }
    const pid_t daemon=fork();
    if(daemon<0){close(lock);return false;}
    if(daemon){close(lock);return true;}
    if(setsid()<0)_exit(1);
    const int log=open((path+"/session.log").c_str(),O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);
    const int input=open("/dev/null",O_RDONLY|O_CLOEXEC);
    if(log<0||input<0||dup2(log,STDOUT_FILENO)<0||dup2(log,STDERR_FILENO)<0||dup2(input,STDIN_FILENO)<0)_exit(1);
    close(log);close(input);
    const auto child=fork();
    if(child<0)_exit(1);
    if(!child) {
        // A fixed argv starts the included session. App names never become
        // shell code or arguments to startx.
        execl("/usr/bin/startx","startx","/usr/local/lib/mnos/desktop-session","--",":0",static_cast<char *>(nullptr));
        perror("startx");_exit(127);
    }
    int status=0;while(waitpid(child,&status,0)<0&&errno==EINTR){}
    close(lock);_exit(WIFEXITED(status)?WEXITSTATUS(status):1);
}
static std::string quoted(const std::string &word) {
    std::string out="'";
    for(char c:word){if(c=='\'')out+="'\\''";else out+=c;}
    return out+"'";
}
int main(int argc,char **argv) {
    if(argc==1||(argc==2&&!strcmp(argv[1],"--help"))) {
        std::cout<<"mnos-gui show | desktop | hide | list | open DESKTOP_ID | run PROGRAM [ARG...] | status\n"
                    "Starts one real X11 session inside Linux; installed desktop entries are discovered automatically.\n"
                    "Use riscv64/all packages in the native mnOS guest. No architecture conversion occurs.\n";return 0;
    }
    std::string request;
    const std::string action=argv[1];
    if((action=="show"||action=="hide"||action=="list"||action=="status")&&argc==2)request=action;
    else if(action=="desktop"&&argc==2)request="hide";
    else if(action=="open"&&argc==3)request="open "+std::string(argv[2]);
    else if(action=="run"&&argc>=3) {
        request="run";for(int i=2;i<argc;++i)request+=" "+quoted(argv[i]);
    } else {std::cerr<<"Invalid arguments. Use mnos-gui --help.\n";return 2;}
    if(request.size()>500||request.find_first_of("\r\n")!=std::string::npos) {
        std::cerr<<"Application request is too long or contains a line break.\n";return 2;
    }
    const auto path=directory();
    if(!private_directory(path)){std::cerr<<"Cannot open the private Linux desktop directory.\n";return 1;}
    int fd=connection(path+"/socket");
    if(fd<0&&(action=="status"||action=="hide")){std::cerr<<"Linux graphical session is not running.\n";return 1;}
    if(fd<0) {
        if(!start_session(path)){std::cerr<<"Cannot start the Linux graphical session.\n";return 1;}
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(300);
        std::cerr<<"Starting Linux graphical session...\n";
        while(fd<0&&std::chrono::steady_clock::now()<end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));fd=connection(path+"/socket");
        }
    }
    if(fd<0){std::cerr<<"Linux desktop did not become ready. Details: "<<path<<"/session.log\n";return 1;}
    request+='\n';size_t sent=0;
    while(sent<request.size()) {
        const auto n=send(fd,request.data()+sent,request.size()-sent,MSG_NOSIGNAL);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){close(fd);std::cerr<<"Cannot send desktop request.\n";return 1;}sent+=size_t(n);
    }
    shutdown(fd,SHUT_WR);std::string reply;
    for(;;) {
        char buffer[4096];const auto n=read(fd,buffer,sizeof buffer);
        if(n<0&&errno==EINTR)continue;
        if(n<0||reply.size()>1024*1024){close(fd);std::cerr<<"Incomplete desktop response.\n";return 1;}
        if(!n)break;
        reply.append(buffer,size_t(n));
    }
    close(fd);
    if(reply.compare(0,3,"OK\n")) {std::cerr<<(reply.empty()?"Empty desktop response.\n":reply);return 1;}
    std::cout<<reply.substr(3);
    return 0;
}
