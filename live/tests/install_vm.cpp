// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only test: the published ISO, real GUI input, a new blank disk, then no ISO.
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
namespace fs = std::filesystem;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
static void check(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
static pid_t spawn(const std::vector<std::string> &args) {
    pid_t pid = fork(); check(pid >= 0, "fork");
    if (!pid) {
        std::vector<char *> argv;
        for (const auto &s : args) argv.push_back(const_cast<char *>(s.c_str()));
        argv.push_back(nullptr); execvp(argv[0], argv.data()); _exit(127);
    }
    return pid;
}
static void run(const std::vector<std::string> &args) {
    int status = 0; auto pid = spawn(args);
    check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0, "command: " + args[0]);
}
static int connect_socket(const fs::path &path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); check(fd >= 0, "socket");
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    auto s = path.string(); check(s.size() < sizeof addr.sun_path, "socket name");
    memcpy(addr.sun_path, s.c_str(), s.size() + 1);
    auto deadline = Clock::now() + 30s;
    while (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr)) {
        check(Clock::now() < deadline, "connect " + s); std::this_thread::sleep_for(50ms);
    }
    return fd;
}
static void send_all(int fd, const std::string &text) {
    size_t at = 0;
    while (at < text.size()) {
        ssize_t n = send(fd, text.data() + at, text.size() - at, MSG_NOSIGNAL);
        check(n > 0, "socket write"); at += size_t(n);
    }
}
static bool image_active(const fs::path &path) {
    std::ifstream f(path, std::ios::binary); std::string type; int w=0,h=0, max=0;
    f >> type >> w >> h >> max; f.get();
    if (!f || type != "P6" || w < 640 || h < 480 || max != 255 || w > 8192 || h > 8192) return false;
    size_t colored = 0; std::set<unsigned> colors;
    for (int i = 0; i < w*h; ++i) {
        unsigned char rgb[3]; f.read(reinterpret_cast<char *>(rgb), 3); if (!f) return false;
        if (rgb[0] != rgb[1] || rgb[1] != rgb[2]) ++colored;
        if (colors.size() < 1024) colors.insert((unsigned(rgb[0]) << 16) | (unsigned(rgb[1]) << 8) | rgb[2]);
    }
    return colored > size_t(w*h)/5 && colors.size() > 256;
}
struct VM {
    pid_t pid=-1; int serial=-1, qmp=-1; std::string output; fs::path log;
    VM(const std::string &arch, const fs::path &iso, const fs::path &disk, const fs::path &out,
       const fs::path &work, const std::string &phase, bool nvme=false, bool bios=false) : log(out/(phase+".log")) {
        auto sock=work/(phase+".serial"), q=work/(phase+".qmp"), vars=work/(phase+".fd");
        bool arm = arch == "arm64";
        std::vector<std::string> args{arm ? "qemu-system-aarch64" : "qemu-system-x86_64",
            "-accel", "tcg", "-cpu", "max", "-smp", "2", "-m", "4096", "-machine",
            arm ? "virt,gic-version=3" : "q35", "-nic", "none", "-display", "none", "-monitor", "none",
            "-device", arm ? "virtio-gpu-pci,xres=1280,yres=720" : "virtio-vga,xres=1280,yres=720",
            "-device", "virtio-rng-pci", "-serial", "unix:"+sock.string()+",server=on,wait=on",
            "-qmp", "unix:"+q.string()+",server=on,wait=off", "-no-reboot",
            "-drive", "file="+disk.string()+",if=none,id=target,format=qcow2,discard=unmap",
            "-device", nvme ? "nvme,drive=target,serial=MNOSTEST01" : "virtio-blk-pci,drive=target"};
        args.insert(args.end(), {"-device","qemu-xhci","-device","usb-kbd","-device","usb-tablet"});
        if (!bios) {
            std::string base = arm ? "/usr/share/AAVMF/AAVMF_" : "/usr/share/OVMF/OVMF_";
            std::string end = arm ? ".fd" : "_4M.fd";
            fs::copy_file(base+"VARS"+end, vars, fs::copy_options::overwrite_existing);
            args.insert(args.end(), {"-drive","if=pflash,format=raw,readonly=on,file="+base+"CODE"+end,
                                     "-drive","if=pflash,format=raw,file="+vars.string()});
        }
        if (!iso.empty()) {
            if (arm) args.insert(args.end(), {"-device","virtio-scsi-pci,id=scsi","-drive",
                "file="+iso.string()+",media=cdrom,if=none,id=cd,format=raw,readonly=on","-device","scsi-cd,drive=cd,bootindex=0"});
            else args.insert(args.end(), {"-cdrom",iso.string()});
            args.insert(args.end(), {"-boot","order=d"});
        } else args.insert(args.end(), {"-boot","order=c"});
        pid=spawn(args); serial=connect_socket(sock); qmp=connect_socket(q);
        qline(); call("qmp_capabilities");
    }
    ~VM() {
        if (serial>=0) close(serial);
        if (qmp>=0) close(qmp);
        if (pid>0) { kill(pid,SIGTERM); waitpid(pid,nullptr,0); }
        std::ofstream(log) << output;
    }
    std::string qline() {
        std::string s; auto end=Clock::now()+30s;
        for (;;) {
            pollfd p{qmp,POLLIN,0}; int n=poll(&p,1,500);
            check(Clock::now()<end,"QMP timeout"); if (n<=0) continue;
            char c; check(recv(qmp,&c,1,0)==1,"QMP closed"); s+=c; if(c=='\n') return s;
        }
    }
    void call(const std::string &cmd,const std::string &args="{}") {
        send_all(qmp,"{\"execute\":\""+cmd+"\",\"arguments\":"+args+"}\n");
        for (;;) { auto r=qline(); check(r.find("\"error\"")==std::string::npos,r);
            if(r.find("\"return\"")!=std::string::npos) return; }
    }
    void pump(int ms=100) {
        pollfd p{serial,POLLIN,0}; if(poll(&p,1,ms)>0) {
            char b[16384]; ssize_t n=recv(serial,b,sizeof b,MSG_DONTWAIT);
            if(n>0) { output.append(b,size_t(n)); std::ofstream(log)<<output; }
        }
    }
    void wait(const std::string &text,int seconds=480,size_t after=0) {
        auto end=Clock::now()+std::chrono::seconds(seconds);
        auto capture_at=Clock::now()+20s;
        while(output.find(text,after)==std::string::npos) {
            check(Clock::now()<end,"Timeout waiting for "+text+"; see "+log.string()); pump(500);
            if (Clock::now() >= capture_at) {
                capture(log.parent_path()/(log.stem().string()+"-current.ppm"));
                capture_at=Clock::now()+20s;
            }
            check(output.find("Kernel panic")==std::string::npos,"Kernel panic");
            check(output.find("SQUASHFS error")==std::string::npos,"SquashFS failure");
        }
    }
    void pause(int seconds) { auto end=Clock::now()+std::chrono::seconds(seconds); while(Clock::now()<end) pump(); }
    void key(const std::string &keys) {
        std::string args="{\"keys\":["; size_t at=0;
        while(at<keys.size()) {
            size_t end=keys.find('-',at); if(end==std::string::npos) end=keys.size();
            if(at) args+=",";
            args+="{\"type\":\"qcode\",\"data\":\""+keys.substr(at,end-at)+"\"}"; at=end+1;
        }
        call("send-key",args+"],\"hold-time\":60}"); std::this_thread::sleep_for(90ms); pump(0);
    }
    void type(const std::string &text) {
        for(char c:text) {
            if(c>='A'&&c<='Z') key("shift-"+std::string(1,char(c-'A'+'a')));
            else if(c=='/') key("slash"); else if(c=='.') key("dot");
            else if(c=='-') key("minus"); else if(c==' ') key("spc");
            else key(std::string(1,c));
        }
    }
    void click(int x, int y) {
        call("input-send-event", "{\"events\":[{\"type\":\"abs\",\"data\":{\"axis\":\"x\",\"value\":"+
            std::to_string(x*32767/1279)+"}},{\"type\":\"abs\",\"data\":{\"axis\":\"y\",\"value\":"+
            std::to_string(y*32767/719)+"}},{\"type\":\"btn\",\"data\":{\"button\":\"left\",\"down\":true}}]}");
        call("input-send-event", "{\"events\":[{\"type\":\"btn\",\"data\":{\"button\":\"left\",\"down\":false}}]}");
        pause(1);
    }
    void capture(const fs::path &path) { call("screendump","{\"filename\":\""+path.string()+"\"}"); }
    void screen(const fs::path &path) {
        for(int i=0;i<60;++i) { capture(path); if(image_active(path)) return; pause(2); }
        throw std::runtime_error("No active graphical output: "+path.string());
    }
    void shutdown() {
        auto end=Clock::now()+90s; int status=0;
        while(waitpid(pid,&status,WNOHANG)==0) { check(Clock::now()<end,"Guest did not power off"); pump(); }
        pid=-1; check(WIFEXITED(status)&&WEXITSTATUS(status)==0,"QEMU failed");
    }
};
int main(int argc,char **argv) {
    try {
        check(argc==4,"Usage: install-vm amd64|arm64 ISO OUTPUT");
        std::string arch=argv[1]; check(arch=="amd64"||arch=="arm64","architecture");
        fs::path iso=fs::absolute(argv[2]), out=fs::absolute(argv[3]); fs::create_directories(out);
        char temp[]="/tmp/mnos-install-test.XXXXXX"; check(mkdtemp(temp),"mkdtemp"); fs::path work=temp;
        fs::path disk=work/"installed.qcow2"; run({"qemu-img","create","-f","qcow2",disk.string(),"40G"});
        const std::string password="CedarOrbit72Quartz";
        {
            VM vm(arch,iso,disk,out,work,"install");
            vm.wait("MNOS INSTALLER GUI READY"); vm.screen(out/"installer.ppm");
            std::cout << "Installer visible; entering account through USB keyboard and pointer" << std::endl;
            vm.click(640,310);
            vm.key("alt-u"); vm.type("mnosqa"); vm.key("tab"); vm.type(password);
            vm.key("tab"); vm.type(password); vm.key("tab"); vm.key("spc");
            vm.key("alt-i"); vm.pause(3); vm.type("/dev/vda"); vm.key("ret");
            vm.wait("MNOS INSTALL PASS",1800); vm.pause(8); vm.capture(out/"installed.ppm");
            std::cout << "Disk installation complete; shutting down the ISO session" << std::endl;
            vm.key("alt-s"); vm.shutdown();
        }
        for(bool nvme : {false,true}) {
            std::string phase=nvme?"disk-nvme":"disk-virtio";
            VM vm(arch,{},disk,out,work,phase,nvme);
            vm.wait("mnOS login:"); vm.pause(12); vm.screen(out/(phase+"-login.ppm"));
            // Use real credentials on the graphical greeter, then validate its desktop.
            vm.click(640,349); vm.type(password); vm.key("ret"); vm.pause(25);
            send_all(vm.serial,"mnosqa\n"); vm.wait("Password:");
            send_all(vm.serial,password+"\n"); vm.pause(3);
            size_t start=vm.output.size();
            send_all(vm.serial,"test $(id -u) -ge 1000 && ! grep -qw boot=live /proc/cmdline && findmnt -n -o FSTYPE / | grep -qx ext4 && test -e /var/lib/mnos/installed-architecture && command -v code firefox-esr nano nmap && getcap /usr/lib/mnos/net-admin | grep -q cap_net_admin,cap_net_raw=ep && timeout 120 sh -c 'until pgrep -u $(id -u) xfce4-session; do sleep 1; done' && echo MNOS_DISK_DESKTOP_PASS\n");
            vm.wait("\r\nMNOS_DISK_DESKTOP_PASS\r\n",180,start);
            vm.screen(out/(phase+"-desktop.ppm"));
            send_all(vm.serial,"sudo -S -p 'QA_AUTH:' /usr/bin/systemctl poweroff\n"); vm.wait("QA_AUTH:",30);
            send_all(vm.serial,password+"\n"); vm.shutdown();
        }
        if(arch=="amd64") {
            VM vm(arch,{},disk,out,work,"disk-bios",false,true);
            vm.wait("mnOS login:"); vm.screen(out/"disk-bios-login.ppm");
        }
        fs::remove_all(work);
        std::cout<<"PASS: real GUI installation; no-ISO UEFI boot; VirtIO and NVMe disks; ordinary-user desktop and applications"<<std::endl;
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl; return 1; }
}
