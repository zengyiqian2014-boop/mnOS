// SPDX-License-Identifier: GPL-3.0-or-later
// Real virtual-machine enrollment with CPU RNG deliberately absent/present.
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
namespace fs=std::filesystem;
using namespace std::chrono_literals;
static void check(bool ok,const std::string &s) {if(!ok)throw std::runtime_error(s);}
struct Guest {
    pid_t pid=-1; int serial=-1; std::string output; fs::path log, work;
    Guest(std::vector<std::string> args,const fs::path &result):log(result) {
        char temp[]="/tmp/mnos-rng.XXXXXX";check(mkdtemp(temp),"mkdtemp");work=temp;
        std::string path=(work/"serial").string();
        args.insert(args.end(),{"-serial","unix:"+path+",server=on,wait=on","-nic","none",
            "-display","none","-monitor","none","-accel","tcg","-m","512","-smp","2","-no-reboot"});
        pid=fork();check(pid>=0,"fork");
        if(!pid) {std::vector<char*> a;for(auto &s:args)a.push_back(s.data());a.push_back(nullptr);execvp(a[0],a.data());_exit(127);}
        serial=socket(AF_UNIX,SOCK_STREAM,0);check(serial>=0,"socket");
        sockaddr_un address{};address.sun_family=AF_UNIX;std::strcpy(address.sun_path,path.c_str());
        auto until=std::chrono::steady_clock::now()+15s;
        while(connect(serial,reinterpret_cast<sockaddr*>(&address),sizeof address)) {
            check(std::chrono::steady_clock::now()<until,"connect serial");std::this_thread::sleep_for(50ms);
        }
    }
    ~Guest() {
        if(serial>=0)close(serial);
        if(pid>0){kill(pid,SIGTERM);waitpid(pid,nullptr,0);}
        std::ofstream(log)<<output;fs::remove_all(work);
    }
    void wait(const std::string &text,size_t start=0) {
        auto until=std::chrono::steady_clock::now()+180s;
        while(output.find(text,start)==std::string::npos) {
            check(std::chrono::steady_clock::now()<until,"Missing "+text+"; see "+log.string());
            pollfd p{serial,POLLIN,0};if(poll(&p,1,100)<=0)continue;
            char bytes[16384];ssize_t n=recv(serial,bytes,sizeof bytes,0);check(n>0,"Serial closed");
            output.append(bytes,size_t(n));std::ofstream(log)<<output;
            check(output.find("PANIC:")==std::string::npos,"Kernel panic");
        }
    }
    void enter(const std::string &line,const std::string &expected) {
        size_t start=output.size();for(char c:line+'\n') {
            check(send(serial,&c,1,MSG_NOSIGNAL)==1,"send serial");std::this_thread::sleep_for(30ms);
        }
        wait(expected,start);
    }
    void enrollment(bool succeeds) {
        wait("AUTH PROMPT: New username");enter("demo","New password (12+ characters)");
        enter("ordinarytest491","Confirm new password");
        enter("ordinarytest491",succeeds?"Set a separate password for root":"No secure RNG");
        if(succeeds) {
            enter("rootadmintest785","Confirm new password");
            enter("rootadmintest785","Accounts created. Signed in as the ordinary user.");
        } else check(output.find("Accounts created")==std::string::npos,"Enrollment bypassed missing entropy");
        check(output.find("ordinarytest491")==std::string::npos && output.find("rootadmintest785")==std::string::npos,"Password leaked");
    }
};
int main(int argc,char **argv) {
    try {
        check(argc==3,"Usage: random-vm NATIVE_BUILD OUTPUT");
        auto build=fs::absolute(argv[1]),out=fs::absolute(argv[2]);fs::create_directories(out);
        for(const std::string mode:{"legacy","modern","missing"}) {
            std::vector<std::string> args{"qemu-system-x86_64","-machine",mode=="legacy"?"pc":"q35",
                "-cpu","max,rdrand=off","-vga","std","-cdrom",(build/"native-demo.iso").string()};
            if(mode!="missing")args.insert(args.end(),{"-device",mode=="legacy"?"virtio-rng-pci,disable-modern=on":"virtio-rng-pci,disable-legacy=on"});
            Guest vm(args,out/("rng-x86-"+mode+".log"));vm.enrollment(mode!="missing");
            if(mode!="missing")check(vm.output.find("RNG: VirtIO PCI")!=std::string::npos,"Device not initialized");
            std::cout<<"PASS x86 "<<mode<<": RDRAND disabled, enrollment "<<(mode=="missing"?"refused":"completed")<<std::endl;
        }
        for(bool rng:{true,false}) {
            Guest vm({"qemu-system-aarch64","-machine","virt,gic-version=3","-cpu",rng?"max":"cortex-a53",
                "-kernel",(build/"arm64/Image").string()},out/(rng?"rng-arm-cpu.log":"rng-arm-missing.log"));
            vm.enrollment(rng);
            std::cout<<"PASS ARM "<<(rng?"RNDR":"no RNG")<<": no VirtIO/UEFI entropy, enrollment "<<(rng?"completed":"refused")<<std::endl;
        }
        return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
}
