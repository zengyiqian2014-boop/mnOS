// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual GTK service and client under Xvfb, including real windows.
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
namespace fs=std::filesystem;
static std::string base,client;
static void check(bool ok,const char *reason){if(!ok)throw std::runtime_error(reason);}
static std::string read(const std::string &path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
static bool run(std::vector<std::string> args,bool success=true) {
    const auto child=fork();check(child>=0,"fork client");
    if(!child) {
        const auto log=base+"/reply";
        if(!freopen(log.c_str(),"w",stdout)||!freopen((base+"/error").c_str(),"w",stderr))_exit(127);
        args.insert(args.begin(),client);std::vector<char *> pointers;
        for(auto &word:args)pointers.push_back(word.data());
        pointers.push_back(nullptr);execv(client.c_str(),pointers.data());_exit(127);
    }
    int status=0;check(waitpid(child,&status,0)==child,"wait client");
    const bool ok=WIFEXITED(status)&&WEXITSTATUS(status)==0;
    check(ok==success,("client request failed: "+read(base+"/error")).c_str());return ok;
}
static bool mapped(Display *display,Window parent,const std::string &name) {
    Window root,up,*children=nullptr;unsigned count=0;
    if(!XQueryTree(display,parent,&root,&up,&children,&count))return false;
    bool found=false;
    for(unsigned i=0;i<count&&!found;i++) {
        char *title=nullptr;XWindowAttributes attr{};
        if(XFetchName(display,children[i],&title)&&title&&name==title&&
           XGetWindowAttributes(display,children[i],&attr)&&attr.map_state==IsViewable)found=true;
        if(title)XFree(title);
        if(!found)found=mapped(display,children[i],name);
    }
    if(children)XFree(children);
    return found;
}
static void wait_window(Display *display,const std::string &name,bool visible) {
    for(unsigned i=0;i<100;i++) {
        XSync(display,False);
        if(mapped(display,DefaultRootWindow(display),name)==visible)return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check(false,"expected real X11 window visibility");
}
int main(int argc,char **argv) {
    if(argc==4&&!strcmp(argv[1],"--probe")) {
        std::ofstream(argv[2])<<argv[3];std::ofstream(std::string(argv[2])+".pid")<<getpid();
        gtk_init(&argc,&argv);auto *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window),"mnOS test application");gtk_window_set_default_size(GTK_WINDOW(window),420,200);
        gtk_container_add(GTK_CONTAINER(window),gtk_label_new("Actual Linux GUI application"));gtk_widget_show_all(window);
        g_timeout_add_seconds(15,[](gpointer)->gboolean{gtk_main_quit();return G_SOURCE_REMOVE;},nullptr);
        gtk_main();return 0;
    }
    try {
        check(argc==3,"Usage: gui-apps-test WORK CLIENT");base=argv[1];client=argv[2];
        const std::string binary=fs::canonical(argv[0]);
        const auto appdir=std::string(getenv("XDG_DATA_HOME"))+"/applications";fs::create_directories(appdir);
        auto desktop=[&](const char *id,bool hidden){std::ofstream(appdir+"/"+id)<<"[Desktop Entry]\nType=Application\nName=mnOS test application\nExec="<<binary<<" --probe "<<base<<"/opened from-desktop\nIcon=utilities-terminal\nHidden="<<(hidden?"true":"false")<<"\n";};
        desktop("mnos-test.desktop",false);desktop("mnos-hidden.desktop",true);
        for(unsigned i=0;i<40;i++) {
            run({"list"});if(read(base+"/reply").find("mnos-test.desktop")!=std::string::npos)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        check(read(base+"/reply").find("mnos-test.desktop")!=std::string::npos,"discover installed desktop entry");
        check(read(base+"/reply").find("mnos-hidden.desktop")==std::string::npos,"exclude hidden desktop entry");
        auto *display=XOpenDisplay(nullptr);check(display,"X11 display");
        run({"show"});wait_window(display,"mnOS Linux applications",true);
        run({"hide"});wait_window(display,"mnOS Linux applications",false);
        run({"open","mnos-test.desktop"});wait_window(display,"mnOS test application",true);
        check(read(base+"/opened")=="from-desktop","desktop Exec launches real application");
        run({"open","mnos-hidden.desktop"},false);run({"open","../bad.desktop"},false);
        const std::string literal="space ' quote ; $(touch /tmp/mnos-unexpected-shell) $HOME";
        run({"run",binary,"--probe",base+"/arguments",literal});
        for(unsigned i=0;i<100&&!fs::exists(base+"/arguments");i++)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(read(base+"/arguments")==literal,"argv must not undergo shell substitution");
        run({"run","/missing-mnos-program"},false);run({"run",std::string(501,'x')},false);
        run({"hide"});run({"run","/bin/false"});
        wait_window(display,"mnOS Linux applications",true);
        // A connected but silent local client must not block the GTK loop.
        const int idle=socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un address{};address.sun_family=AF_UNIX;
        const auto socket=std::string(getenv("MNOS_GUI_DIRECTORY"))+"/socket";
        check(socket.size()<sizeof address.sun_path,"socket length");strcpy(address.sun_path,socket.c_str());
        check(idle>=0&&!connect(idle,reinterpret_cast<sockaddr *>(&address),sizeof address),"idle client");
        const auto start=std::chrono::steady_clock::now();run({"status"});
        check(std::chrono::steady_clock::now()-start<std::chrono::seconds(2),"idle client blocked desktop");close(idle);
        chmod(getenv("MNOS_GUI_DIRECTORY"),0777);run({"status"},false);chmod(getenv("MNOS_GUI_DIRECTORY"),0700);
        run({"show"});wait_window(display,"mnOS Linux applications",true);XCloseDisplay(display);
        for(const char *name:{"opened.pid","arguments.pid"}) {
            std::ifstream pidfile(base+"/"+name);int pid=0;pidfile>>pid;if(pid>1)kill(pid,SIGTERM);
        }
        // A session can die before its service socket exists. Observe a real
        // process-held startup lock, then release it like an exiting supervisor;
        // the client must fail promptly instead of waiting the full deadline.
        const auto startup=base+"/failed-startup";fs::create_directory(startup);chmod(startup.c_str(),0700);
        const int lock=open((startup+"/session.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
        check(lock>=0&&!flock(lock,LOCK_EX|LOCK_NB),"startup fixture lock");
        const auto waiting=fork();check(waiting>=0,"startup client fork");
        if(!waiting) {
            unsetenv("MNOS_GUI_NO_START");setenv("MNOS_GUI_DIRECTORY",startup.c_str(),1);
            if(!freopen((startup+"/error").c_str(),"w",stderr))_exit(127);
            execl(client.c_str(),client.c_str(),"show",static_cast<char *>(nullptr));_exit(127);
        }
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(read(startup+"/error").find("Starting Linux graphical session")==std::string::npos&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        close(lock);
        int result=0;pid_t done=0;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!(done=waitpid(waiting,&result,WNOHANG))&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(!done){kill(waiting,SIGKILL);waitpid(waiting,&result,0);}
        check(done==waiting&&WIFEXITED(result)&&WEXITSTATUS(result)!=0&&read(startup+"/error").find("exited during startup")!=std::string::npos,"failed session must not leave a stale startup wait");
        std::cout<<"PASS Linux GUI: desktop discovery, visible GTK windows, launch, hidden entries, literal argv, nonblocking private IPC\n";
        std::cout<<"PASS Linux GUI startup: supervisor exit detected before the startup deadline\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
