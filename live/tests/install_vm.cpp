// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only test: the published ISO, real GUI input, a new blank disk, then no ISO.
#include <algorithm>
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
static bool panel_visible(const fs::path &path) {
    std::ifstream f(path,std::ios::binary); std::string type; int w=0,h=0,max=0;
    f >> type >> w >> h >> max; f.get();
    if (!f || type!="P6" || w<640 || w>8192 || h<480 || max!=255) return false;
    size_t dark=0, launcher_color=0;
    for (int y=0;y<25;++y) for (int x=0;x<w;++x) {
        unsigned char rgb[3]; f.read(reinterpret_cast<char *>(rgb),3); if (!f) return false;
        if (y>=5 && rgb[0]<55 && rgb[1]<55 && rgb[2]<55) ++dark;
        // A black compositor placeholder also looks like a dark panel. Require
        // painted launcher icons at the left, not just a dark horizontal band.
        if (y>=5 && x<180 &&
            std::max({rgb[0],rgb[1],rgb[2]}) > 100 &&
            std::max({rgb[0],rgb[1],rgb[2]}) - std::min({rgb[0],rgb[1],rgb[2]}) > 40)
            ++launcher_color;
    }
    return dark>size_t(w)*20*3/4 && launcher_color>60;
}
static bool grub_menu_visible(const fs::path &path) {
    std::ifstream f(path,std::ios::binary); std::string type; int w=0,h=0,max=0;
    f >> type >> w >> h >> max; f.get();
    if (!f || type!="P6" || w<640 || h<480 || w>8192 || h>8192 || max!=255) return false;
    size_t cyan=0, black=0;
    for (int i=0;i<w*h;++i) {
        unsigned char p[3]; f.read(reinterpret_cast<char *>(p),3); if (!f) return false;
        if (p[0]<160 && p[1]>150 && p[2]>150 && p[1]>p[0]*1.3) ++cyan;
        if (p[0]<20 && p[1]<20 && p[2]<20) ++black;
    }
    return cyan>size_t(w)*10 && black>size_t(w)*h*3/4;
}
struct VM {
    pid_t pid=-1; int serial=-1, qmp=-1; std::string output; fs::path log;
    VM(const std::string &arch, const fs::path &iso, const fs::path &disk, const fs::path &out,
       const fs::path &work, const std::string &phase, bool nvme=false, bool bios=false) : log(out/(phase+".log")) {
        auto sock=work/(phase+".serial"), q=work/(phase+".qmp"), vars=work/(phase+".fd");
        bool arm = arch == "arm64";
#ifdef __aarch64__
        bool matching = arm;
#else
        bool matching = !arm;
#endif
        bool kvm = matching && access("/dev/kvm", R_OK | W_OK) == 0;
        std::ofstream(out/"hypervisor.txt", std::ios::app) << phase << ": "
            << (kvm ? "KVM / host CPU" : "TCG / max CPU") << '\n';
        std::vector<std::string> args{arm ? "qemu-system-aarch64" : "qemu-system-x86_64",
            "-accel", kvm ? "kvm" : "tcg", "-cpu", kvm ? "host" : "max", "-smp", "2", "-m", "4096", "-machine",
            arm ? "virt-9.1,gic-version=3" : "q35", "-nic", "none", "-display", "none", "-monitor", "none",
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
        if (pid>0) {
            kill(pid,SIGTERM);
            bool reaped=false;
            for (int i=0;i<100;++i) {
                if (waitpid(pid,nullptr,WNOHANG)==pid) { reaped=true; break; }
                std::this_thread::sleep_for(50ms);
            }
            if (!reaped) { kill(pid,SIGKILL); waitpid(pid,nullptr,0); }
        }
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
    void wait(const std::string &text,int seconds=480,size_t after=0,bool keep_display=false) {
        std::cout << log.stem().string() << ": waiting for " << text << std::endl;
        auto end=Clock::now()+std::chrono::seconds(seconds);
        auto capture_at=Clock::now()+20s;
        size_t progress_at=output.size();
        while(output.find(text,after)==std::string::npos) {
            check(Clock::now()<end,"Timeout waiting for "+text+"; see "+log.string()); pump(500);
            auto stage=output.find("STAGE ",progress_at);
            if (stage!=std::string::npos) {
                auto newline=output.find('\n',stage);
                if (newline!=std::string::npos) {
                    std::cout << output.substr(stage,newline-stage) << std::endl;
                    progress_at=newline+1;
                }
            }
            if (Clock::now() >= capture_at) {
                auto picture=log.parent_path()/(log.stem().string()+"-current.ppm");
                capture(picture);
                if (keep_display) check(image_active(picture),"Installer display became inactive: "+picture.string());
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
    void select_entry(unsigned index) {
        auto picture=log.parent_path()/(log.stem().string()+"-menu.ppm");
        auto deadline=Clock::now()+90s;
        for (;;) {
            capture(picture);
            if (grub_menu_visible(picture)) break;
            check(Clock::now()<deadline,"GRUB menu did not appear");
            check(output.find("Booting Linux Kernel")==std::string::npos,"Missed GRUB menu");
            pump(200);
        }
        key("home");
        for (unsigned i=0;i<index;++i) key("down");
        key("ret");
    }
    void screen(const fs::path &path) {
        for(int i=0;i<60;++i) { capture(path); if(image_active(path)) return; pause(2); }
        throw std::runtime_error("No active graphical output: "+path.string());
    }
    void desktop_screen(const fs::path &path) {
        // A running xfce4-panel process can still be initializing on TCG.
        // Require its actual top panel, rather than the greeter's wallpaper.
        for (int i=0;i<60;++i) {
            capture(path);
            if (image_active(path) && panel_visible(path)) return;
            pause(2);
        }
        throw std::runtime_error("Desktop panel did not render: "+path.string());
    }
    void shutdown() {
        auto end=Clock::now()+90s; int status=0;
        while(waitpid(pid,&status,WNOHANG)==0) { check(Clock::now()<end,"Guest did not power off"); pump(); }
        pid=-1; check(WIFEXITED(status)&&WEXITSTATUS(status)==0,"QEMU failed");
    }
    void finish_installation(const fs::path &picture) {
        // Backend success precedes its unmount/flush cleanup. The GUI exposes
        // its shutdown button only after the child has actually exited.
        auto end=Clock::now()+180s, press_at=Clock::now();
        bool qmp_closed=false;
        for (;;) {
            int status=0;
            if(waitpid(pid,&status,WNOHANG)==pid) {
                pid=-1; check(WIFEXITED(status)&&WEXITSTATUS(status)==0,"QEMU failed"); return;
            }
            check(Clock::now()<end,"Installer did not finish cleanup and power off");
            if(!qmp_closed && Clock::now()>=press_at) {
                try { capture(picture); key("alt-s"); }
                catch(const std::exception &) { qmp_closed=true; }
                press_at=Clock::now()+3s;
            }
            pump(100);
        }
    }
};
int main(int argc,char **argv) {
    try {
        check(argc==4,"Usage: install-vm amd64|arm64 ISO OUTPUT");
        std::string arch=argv[1]; check(arch=="amd64"||arch=="arm64","architecture");
        fs::path iso=fs::absolute(argv[2]), out=fs::absolute(argv[3]); fs::create_directories(out);
        char temp[]="/tmp/mnos-install-test.XXXXXX"; check(mkdtemp(temp),"mkdtemp"); fs::path work=temp;
        fs::path disk=work/"installed.qcow2"; run({"qemu-img","create","-f","qcow2",disk.string(),"40G"});
        // Exercise names longer than the old 31-character policy and a short
        // user-selected password, including real graphical/PAM login later.
        const std::string account="mnosqa" + std::string(58, 'a');
        const std::string password="q7";
        {
            VM vm(arch,iso,disk,out,work,"compatibility");
            vm.select_entry(1);
            vm.wait("MNOS GRAPHICS: compat",600);
            check(vm.output.find("mnos.graphics=compat")!=std::string::npos,"Compatibility entry lacks its own kernel argument");
            vm.wait("MNOS INSTALLER GUI READY",240);
            vm.screen(out/"installer-compat.ppm");
            std::cout << "PASS: second menu entry starts a visible installer with Xorg acceleration disabled" << std::endl;
        }
        {
            VM vm(arch,iso,disk,out,work,"install");
            vm.wait("MNOS INSTALLER GUI READY"); vm.screen(out/"installer.ppm");
            std::cout << "Installer visible; entering account through USB keyboard and pointer" << std::endl;
            vm.click(640,310);
            vm.key("alt-u"); vm.type(account); vm.key("tab"); vm.type(password);
            vm.key("tab"); vm.type(password); vm.key("tab"); vm.key("spc");
            vm.key("alt-i"); vm.pause(3); vm.type("/dev/vda"); vm.key("ret");
            vm.wait("STAGE 5 ",120,0,true);
            vm.wait("MNOS INSTALL PASS",1800,0,true); vm.pause(8); vm.screen(out/"installed.ppm");
            std::cout << "Disk installation complete; shutting down the ISO session" << std::endl;
            vm.finish_installation(out/"installed.ppm");
        }
        for(bool nvme : {false,true}) {
            std::string phase=nvme?"disk-nvme":"disk-virtio";
            VM vm(arch,{},disk,out,work,phase,nvme);
            vm.wait("mnOS login:");
            // Authenticate promptly: login/PAM has a real login timeout, unlike
            // agetty's old pre-authentication username prompt.
            send_all(vm.serial,account+"\n"); vm.wait("Password:",60);
            send_all(vm.serial,password+"\n"); vm.pause(3);
            vm.pause(12); vm.screen(out/(phase+"-login.ppm"));
            // Use real credentials on the graphical greeter, then validate its desktop.
            vm.click(640,349); vm.type(password); vm.key("ret"); vm.pause(25);
            size_t start=vm.output.size();
            send_all(vm.serial,"test $(id -u) -ge 1000 && ! grep -qw boot=live /proc/cmdline && findmnt -n -o FSTYPE / | grep -qx ext4 && test -e /var/lib/mnos/installed-architecture && command -v code firefox-esr nano nmap mnos-pkg && /usr/sbin/getcap /usr/lib/mnos/net-admin | grep -q cap_net_admin,cap_net_raw=ep && timeout 120 sh -c 'until pgrep -u $(id -u) xfce4-session && pgrep -u $(id -u) xfce4-panel && pgrep -u $(id -u) xfwm4; do sleep 1; done' && echo MNOS_DISK_DESKTOP_PASS\n");
            vm.wait("\r\nMNOS_DISK_DESKTOP_PASS\r\n",180,start);
            vm.desktop_screen(out/(phase+"-desktop.ppm"));
            if(!nvme) {
                start=vm.output.size();
                send_all(vm.serial,"code --version && firefox-esr --version && mnos-help -cn code && ! mnos-pkg install nano && timeout 90 firefox-esr --headless --screenshot /tmp/mnos-browser.png 'data:text/html,<h1>mnOS offline browser check</h1>' && test -s /tmp/mnos-browser.png && printf '#include <stdio.h>\\nint main(void){puts(\"BUILD_OK\");}\\n' > /tmp/mnos-app.c && cc -Wall -Werror /tmp/mnos-app.c -o /tmp/mnos-app && /tmp/mnos-app && echo MNOS_APPLICATIONS_PASS\n");
                vm.wait("\r\nMNOS_APPLICATIONS_PASS\r\n",180,start);
                start=vm.output.size();
                send_all(vm.serial,"DISPLAY=:0 code --disable-extensions --new-window /tmp/mnos-app.c >/tmp/mnos-code.log 2>&1; timeout 90 sh -c 'until DISPLAY=:0 xdotool search --onlyvisible --class Code; do sleep 1; done' && echo MNOS_CODE_WINDOW_PASS\n");
                vm.wait("\r\nMNOS_CODE_WINDOW_PASS\r\n",120,start);
                vm.screen(out/"vscode-window.ppm");
            }
            // Keep the echoed command distinct from the actual sudo prompt.
            send_all(vm.serial,"sudo -S -p 'QA_'\"AUTH:\" /usr/bin/systemctl poweroff\n"); vm.wait("QA_AUTH:",30);
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
