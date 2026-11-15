/* SPDX-License-Identifier: GPL-3.0-or-later
 * Install from ISO to a fresh disk, remove ISO, boot original GUI, save a file
 * and settings, reboot again. MNOS_TEST_LINUX_GUEST also tests bundled Linux
 * inside the original host kernel; the harness uses C++, not Python. */
#include <chrono>
#include <algorithm>
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
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static std::string firmware(const char *path) {
    const char *root = std::getenv("MNOS_FIRMWARE_ROOT");
    return std::string(root ? root : "") + path;
}
static void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
static void require_guest_frame(const fs::path &path) {
    std::ifstream image(path,std::ios::binary);std::string magic;unsigned w=0,h=0,max=0;
    image>>magic>>w>>h>>max;
    require(magic=="P6"&&w&&h&&w<=8192&&h<=8192&&max==255,"framebuffer PPM header");
    image.get();size_t green=0;
    for(size_t i=0;i<(size_t)w*h;i++) {
        unsigned char rgb[3];image.read((char *)rgb,3);require((bool)image,"truncated framebuffer");
        if(rgb[0]==0x24&&rgb[1]==0x8b&&rgb[2]==0x42)green++;
    }
    require(green>100,"Linux X11 background was not rendered in the native framebuffer");
}
static pid_t launch(const std::vector<std::string> &args) {
    pid_t p = fork();
    require(p >= 0, "fork");
    if (!p) {
        std::vector<char *> a;
        for (auto &s : args)
            a.push_back(const_cast<char *>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    return p;
}
static int connect_socket(const fs::path &path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    require(fd >= 0, "socket");
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    require(path.string().size() < sizeof a.sun_path, "path size");
    strcpy(a.sun_path, path.c_str());
    auto end = Clock::now() + 15s;
    while (connect(fd, (sockaddr *)&a, sizeof a)) {
        require(Clock::now() < end, "socket connect " + path.string());
        std::this_thread::sleep_for(30ms);
    }
    return fd;
}
struct VM {
    pid_t pid = -1;
    int serial = -1, monitor = -1;
    std::string output;
    fs::path log;
    VM(const fs::path &build, const fs::path &work, const std::string &arch, unsigned phase,
       const std::vector<std::string> &extra = {})
        : log(build / ("desktop-" + arch + "-" + std::to_string(phase) + ".log")) {
        std::ofstream(log).close();
        auto disk = work / "disk.img", vars = work / "vars.fd",
             socket = work / ("serial" + std::to_string(phase)),
             qmp = work / ("qmp" + std::to_string(phase));
        std::vector<std::string> a;
        if (arch == "arm64")
            a = {"qemu-system-aarch64",
                 "-machine",
                 "virt,gic-version=3,acpi=off",
                 "-bios",
                 firmware("/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"),
                 "-device",
                 "ramfb",
                 "-global",
                 "virtio-mmio.force-legacy=false",
                 "-device",
                 "virtio-rng-device",
                 "-device",
                 "virtio-keyboard-device",
                 "-device",
                 "virtio-mouse-device",
                 "-drive",
                 "file=" + disk.string() + ",format=raw,if=none,id=data",
                 "-device",
                 "virtio-blk-device,drive=data"};
        else {
            if (phase == 0)
                fs::copy_file(firmware("/usr/share/OVMF/OVMF_VARS_4M.fd"), vars);
            a = {"qemu-system-x86_64",
                 "-drive",
                 "if=pflash,format=raw,readonly=on,file=" + firmware("/usr/share/OVMF/OVMF_CODE_4M.fd"),
                 "-drive",
                 "if=pflash,format=raw,file=" + vars.string(),
                 "-vga",
                 "std",
                 "-device",
                 "virtio-rng-pci",
                 "-drive",
                 "file=" + disk.string() + ",format=raw,if=ide,index=0"};
        }
        if (const char *data = std::getenv("MNOS_QEMU_DATA")) {
            a.push_back("-L"); a.push_back(data);
        }
        if (phase == 0) {
            a.push_back("-cdrom");
            a.push_back((build / (std::string("mnOS-0.4-desktop-installer") + (std::getenv("MNOS_TEST_LITE")?"-lite-":"-") + arch + ".iso")).string());
            a.push_back("-boot");
            a.push_back("order=d");
        }
        const std::vector<std::string> common = {"-accel",
                                                 "tcg",
                                                 "-cpu",
                                                 "max",
                                                 "-smp",
                                                 "2",
                                                 "-m",
                                                 std::getenv("MNOS_TEST_LINUX_GUEST") ? "4096" : "1024",
                                                 "-display",
                                                 "none",
                                                 "-nic",
                                                 "none",
                                                 "-no-reboot",
                                                 "-monitor",
                                                 "none",
                                                 "-qmp",
                                                 "unix:" + qmp.string() + ",server=on,wait=off",
                                                 "-serial",
                                                 "unix:" + socket.string() + ",server=on,wait=on"};
        a.insert(a.end(), common.begin(), common.end());
        if (std::getenv("MNOS_TEST_LINUX_GUEST")) {
            a.insert(a.end(), {"-netdev", "user,id=guestnet", "-device",
                arch=="arm64" ? "virtio-net-device,netdev=guestnet" : "virtio-net-pci,disable-legacy=on,netdev=guestnet"});
        }
        a.insert(a.end(), extra.begin(), extra.end());
        pid = launch(a);
        serial = connect_socket(socket);
        monitor = connect_socket(qmp);
        qmp_command("{\"execute\":\"qmp_capabilities\"}");
    }
    ~VM() {
        if (serial >= 0)
            close(serial);
        if (monitor >= 0)
            close(monitor);
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
        std::ofstream(log) << output;
    }
    void poll_serial() {
        pollfd p{serial, POLLIN, 0};
        if (poll(&p, 1, 100) <= 0)
            return;
        char buf[8192];
        auto n = read(serial, buf, sizeof buf);
        require(n > 0, "serial closed\n" + output);
        output.append(buf, n);
        std::ofstream(log, std::ios::app).write(buf, n);
        require(output.find("PANIC") == std::string::npos, output);
    }
    void wait(const std::string &s, size_t mark = 0) {
        auto end = Clock::now() + (std::getenv("MNOS_TEST_LINUX_GUEST") ? 900s : 150s), last = Clock::now();
        while (output.find(s, mark) == std::string::npos) {
            if (Clock::now() - last > 5s) {
                qmp_command("{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"" +
                            log.string() + ".ppm\"}}");
                last = Clock::now();
            }
            require(Clock::now() < end, "timeout: " + s + "\n" + output);
            poll_serial();
        }
    }
    size_t send(const std::string &s) {
        auto m = output.size();
        for (char c : s) {
            require(::send(serial, &c, 1, MSG_NOSIGNAL) == 1, "send");
            std::this_thread::sleep_for(35ms);
        }
        return m;
    }
    void command(const std::string &s, const std::string &wanted) {
        auto m = send(s + "\n");
        wait(wanted, m);
    }
    void guest_command(const std::string &s,const std::string &wanted) {
        auto mark=send(s+"\n");wait(wanted,mark);
        wait("linux-guest# ",output.find(wanted,mark)+wanted.size());
    }
    void qmp_command(const std::string &s) {
        std::string cmd = s + "\n", reply;
        require(::send(monitor, cmd.data(), cmd.size(), MSG_NOSIGNAL) == (ssize_t)cmd.size(),
                "QMP send");
        auto end = Clock::now() + 10s;
        while (reply.find("\"return\"") == std::string::npos) {
            require(Clock::now() < end, "QMP response timeout: " + reply);
            /* Keep draining the guest UART while waiting for a screendump.
             * A burst of help output must not stall the emulated UART or QMP. */
            pollfd p[2]={{monitor, POLLIN, 0}, {serial, POLLIN, 0}};
            if (poll(p, 2, 100) <= 0)
                continue;
            if (p[1].revents & POLLIN) poll_serial();
            if (!(p[0].revents & POLLIN)) continue;
            char b[8192];
            auto n = read(monitor, b, sizeof b);
            require(n > 0, "QMP closed");
            reply.append(b, n);
            require(reply.find("\"error\"") == std::string::npos, "QMP error: " + reply);
        }
    }
    void screenshot(const fs::path &path) {
        std::this_thread::sleep_for(500ms);
        fs::remove(path);
        qmp_command("{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"" + path.string() +
                    "\"}}");
        require(fs::exists(path), "screenshot");
    }
    void key(const std::string &name) {
        qmp_command(
            "{\"execute\":\"human-monitor-command\",\"arguments\":{\"command-line\":\"sendkey " +
            name + "\"}}");
        std::this_thread::sleep_for(200ms);
    }
    void page_key(const std::string &name,unsigned page) {
        auto mark=output.size();key(name);wait("UI: page "+std::to_string(page),mark);
    }
    void mouse_move(int dx, int dy) {
        qmp_command("{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":"
                    "\"rel\",\"data\":{\"axis\":\"x\",\"value\":" +
                    std::to_string(dx) +
                    "}},{\"type\":\"rel\",\"data\":{\"axis\":\"y\",\"value\":" +
                    std::to_string(dy) + "}}]}}");
        std::this_thread::sleep_for(150ms);
    }
    void mouse_click() {
        for (bool down : {true, false}) {
            qmp_command("{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":"
                        "\"btn\",\"data\":{\"down\":" +
                        std::string(down ? "true" : "false") + ",\"button\":\"left\"}}]}}");
            std::this_thread::sleep_for(150ms);
        }
    }
    void click_at(int x,int y) {
        for(unsigned i=0;i<20;i++)mouse_move(-100,-100);
        // PS/2 packets cannot carry a whole-screen relative displacement.
        // Move in bounded steps; VirtIO also accepts these same events.
        while(x||y){int dx=x>100?100:x,dy=y>100?100:y;mouse_move(dx,dy);x-=dx;y-=dy;}
        mouse_click();
    }
    void login(bool first,bool configure_factor=false) {
        wait(first ? "AUTH PROMPT: New username" : "AUTH PROMPT: Username:");
        command("demo", first ? "New password (12+ characters)" : "AUTH PROMPT: Password:");
        if (first) {
            command("ordinarytest491", "Confirm new password");
            command("ordinarytest491", "Set a separate password for root");
            command("rootadmintest785", "Confirm new password");
            command("rootadmintest785", "Accounts created. Signed in as the ordinary user.");
            if(!std::getenv("MNOS_TEST_LITE")) {
                if(configure_factor) {command("y","Enter your password to bind an authenticator");return;}
                command("n", "Password-only setup complete.");
            }
        } else
            command("ordinarytest491", "Signed in as an ordinary user.");
        wait("DESKTOP READY: original native GUI");
    }
};
int main(int argc, char **argv) {
    try {
        require(argc == 3, "usage: desktop-vm BUILD/DESKTOP amd64|arm64");
        auto build = fs::canonical(argv[1]);
        std::string arch = argv[2];
        require(arch == "amd64" || arch == "arm64", "arch");
        char tmp[] = "/tmp/mnos-desktop-XXXXXX";
        require(mkdtemp(tmp), "mkdtemp");
        fs::path work = tmp;
        std::ofstream(work / "disk.img").close();
        fs::resize_file(work / "disk.img", std::getenv("MNOS_TEST_LINUX_GUEST") ? (16ULL << 30) : (256ULL << 20));
        {
            VM vm(build, work, arch, 0);
            vm.wait("INSTALLER READY");
            vm.screenshot(build / (arch + "-installer.ppm"));
            auto m = vm.send("\r");
            vm.wait("INSTALL CONFIRM", m);
            m = vm.send("no\r\033");
            vm.wait("INSTALL CANCELLED", m);
            std::ifstream empty(work / "disk.img", std::ios::binary);
            char block[65536];
            while (empty.read(block, sizeof block) || empty.gcount()) {
                require(std::all_of(block,block+empty.gcount(),[](char c){return c==0;}),
                        "cancelled install modified disk");
            }
            m = vm.send("\r");
            vm.wait("INSTALL CONFIRM", m);
            vm.send("ERASE\r");
            vm.wait("INSTALL PASS");
            vm.screenshot(build / (arch + "-installed.ppm"));
        }
        {
            VM vm(build, work, arch, 1);
            vm.wait("FILESYSTEM: installed GPT / MNFS partition found");
            vm.login(true);
            vm.screenshot(build / (arch + "-desktop.ppm"));
            auto m = vm.output.size();
            vm.key("f3");
            vm.wait("UI: page 2", m);
            vm.send("A first save must retain the untitled document.");
            vm.key("ctrl-s");
            m = vm.send("untitled.txt\n");
            vm.wait("EDITOR: saved /disk/home/demo/untitled.txt", m);
            m = vm.output.size();
            vm.key("f2");
            vm.wait("UI: page 1", m);
            vm.send("n");
            vm.send("hello.txt\n");
            vm.wait("UI: page 2", m);
            vm.send("Hello from the original mnOS desktop.\nSaved by the native editor.\n\n"
                    "Letters stay together. Spaces belong between words.\n"
                    "The terminal uses a monospace font.\n\nTask\tState\nFont\tReady\nIcons\tReady");
            /* Place the caret with the real pointer, then exercise real arrow
             * and backspace keys. Verify the resulting bytes below and on reboot. */
            std::ifstream screen(build / (arch + "-desktop.ppm"));
            std::string magic;
            int screen_width = 0, screen_height = 0;
            screen >> magic >> screen_width >> screen_height;
            require(magic == "P6" && screen_width >= 800 && screen_height >= 600, "screen size");
            int text_left = (screen_width > 1100 ? (screen_width - 900) / 2 : 144) + 24 + 32;
            for (unsigned i = 0; i < 20; i++) vm.mouse_move(-100, -100);
            vm.mouse_move(text_left + 1, 225);
            vm.mouse_click();
            /* Keep this edit on one ordered keyboard channel. Serial input
             * can still be queued when a PS/2 arrow arrives on the x86 host. */
            for(const char *key:{"shift-n","o","t","e","shift-semicolon","spc"})vm.key(key);
            vm.key("left");
            vm.key("backspace");
            vm.key("shift-semicolon");
            vm.key("right");
            m = vm.output.size();
            vm.key("ctrl-s");
            vm.wait("EDITOR: saved /disk/home/demo/hello.txt", m);
            vm.screenshot(build / (arch + "-editor.ppm"));
            m = vm.send(std::string(1, (char)0x87));
            vm.wait("UI: page 6", m);
            vm.send("w");
            m = vm.send("s");
            vm.wait("SETTINGS: saved", m);
            m = vm.send("h");
            vm.wait("UI: page 4", m);
            vm.wait("report.mnos@proton.me", m);
            vm.command("support -cn", "如遇 Bug 或硬件兼容性问题");
            vm.screenshot(build / (arch + "-support.ppm"));
            m = vm.send(std::string(1, (char)0x85));
            vm.wait("UI: page 4", m);
            vm.command("cat /disk/home/demo/hello.txt", "Saved by the native editor.");
            vm.wait("Note: Hello from the original mnOS desktop.");
            vm.command("cat /disk/home/demo/untitled.txt", "A first save must retain the untitled document.");
            vm.command("exec /ram/bin/hello",
                       "HELLO: native user ELF; initialized data and BSS OK");
            vm.command("help -cn", "/ram 临时；/disk 持久。F7 打开显示设置。");
            vm.screenshot(build / (arch + "-terminal.ppm"));
            vm.command("kernel help -cn display", "kernel display 1600x900");
            vm.command("kernel fonts", "Supported Unicode scalars: 49776");
            vm.command("kernel unicode", "Symbols:");
            vm.screenshot(build / (arch + "-unicode.ppm"));
            vm.command("kernel disk status", "/disk writable");
            vm.command("kernel disk lock", "Permission denied: root required");
            vm.command("kernel display invalid", "Invalid display object");
            vm.command("kernel wallpaper 9", "Unknown instruction or invalid object");
            vm.command("kernel snapshot save /disk/invalid", "Volume must be /ram or /disk");
            m=vm.send("ker\tsec\t\n");
            vm.wait("Session privilege: ordinary user",m);
            vm.command("kernel tools", "No native network stack, APT");
            if(std::getenv("MNOS_TEST_LINUX_GUEST")) {
                vm.command("linux status","Stopped / guest RAM cleared");
                require(vm.output.find("LINUX GUEST: start")==std::string::npos,"guest started without consent");
                vm.click_at(60,18);
                auto request=vm.send("linux\n");
                vm.wait("LINUX SIMULATION: confirmation requested",request);
                vm.screenshot(build/(arch+"-linux-guest-consent.ppm"));
                vm.click_at((screen_width-650)/2+460,(screen_height-280)/2+227);
                vm.wait("LINUX SIMULATION: cancelled",request);
                require(vm.output.find("LINUX GUEST: start")==std::string::npos,"cancel booted a guest");
                vm.page_key("f5",4);
                vm.command("linux memory 1024","Guest RAM limit set.");
                vm.command("linux open system","LINUX SIMULATION: confirmation requested");
                auto enabled=vm.output.size();
                vm.key("ret");
                vm.wait("LINUX GUEST: start",enabled);
                vm.wait("Linux Ethernet device ready",enabled);
                vm.wait("LINUX SIMULATION: app sent to guest shell",enabled);
                vm.wait("Swap:",enabled);
                vm.wait("linux-guest# ",vm.output.find("Swap:",enabled));
                vm.guest_command("uname -s", "\r\nLinux\r\n");
                vm.guest_command("dpkg --print-architecture", "\r\nriscv64\r\n");
                vm.guest_command("printf abc | sha256sum", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  -");
                vm.guest_command("test ! -e /disk && printf '%s%s\\n' NO_HOST_ DISK", "\r\nNO_HOST_DISK\r\n");
                if(const char *proxy=std::getenv("MNOS_TEST_PROXY")) {
                    std::string p=proxy;
                    require(p.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.-")==std::string::npos,"test proxy URL");
                    vm.command("export http_proxy="+p,"linux-guest# ");
                }
                vm.command("export DEBIAN_FRONTEND=noninteractive; apt-get -q -o APT::Update::Error-Mode=any update && echo MNOS_APT_UPDATE_PASS", "\r\nMNOS_APT_UPDATE_PASS\r\n");
                vm.wait("linux-guest# ", vm.output.rfind("MNOS_APT_UPDATE_PASS"));
                vm.command("apt-get -q install -y x11-utils && echo MNOS_APT_INSTALL_PASS", "\r\nMNOS_APT_INSTALL_PASS\r\n");
                vm.wait("linux-guest# ", vm.output.rfind("MNOS_APT_INSTALL_PASS"));
                vm.command("printf '%s\\n' PERSISTED > /root/native-test; echo saved", "\r\nsaved\r\n");
                vm.page_key("f5",4);
                auto desktop=vm.output.size();
                vm.command("linux open desktop","UI: page 7");
                vm.wait("LINUX SIMULATION: app sent to guest shell",desktop);
                vm.wait("linux-guest# ", vm.output.find("LINUX SIMULATION: app sent to guest shell",desktop));
                vm.page_key("f5",4);
                vm.command("linux send for i in $(seq 1 120); do DISPLAY=:0 xdpyinfo > /root/x11-info 2>/dev/null && { echo MNOS_X11_PASS; break; }; sleep 1; done","\r\nMNOS_X11_PASS\r\n");
                vm.command("linux send DISPLAY=:0 xsetroot -solid '#248b42'; echo MNOS_FRAME_READY", "\r\nMNOS_FRAME_READY\r\n");
                vm.command("linux resume","UI: page 7");
                // Move into the xterm client, then deliver real host keys through
                // mnOS -> SBI evdev -> Xorg -> the guest shell.
                int ww=screen_width>1100?900:screen_width-176;
                int wh=screen_height>740?590:screen_height-104;
                int area=wh-207,dw=ww-40,dh=dw*768/1024;
                if(dh>area){dh=area;dw=dh*1024/768;}
                int sx=(screen_width>1100?(screen_width-ww)/2:144)+12+20+(ww-40-dw)/2;
                int sy=74+52+115+(area-dh)/2;
                vm.click_at(sx+dw/2,sy+dh/3);
                for(const char *key:{"t","o","u","c","h","spc","slash","r","o","o","t","slash","g","u","i","i","n","p","u","t","ret"})vm.key(key);
                vm.screenshot(build/(arch+"-linux-guest-gui.ppm"));
                require_guest_frame(build/(arch+"-linux-guest-gui.ppm"));
                vm.page_key("f5",4);
                vm.command("linux send test -f /root/guiinput && echo MNOS_GUI_INPUT_PASS","\r\nMNOS_GUI_INPUT_PASS\r\n");
                vm.command("linux pause","Paused / guest RAM retained");
                vm.command("system memory","Free physical pages");
                vm.command("linux resume","UI: page 7");
                vm.page_key("f5",4);
                vm.command("linux send poweroff","LINUX GUEST: stopped");
                vm.command("linux start","LINUX SIMULATION: confirmation requested");
                vm.key("ret");
                auto reboot=vm.output.size();vm.wait("linux-guest# ",reboot);
                vm.guest_command("cat /root/native-test", "\r\nPERSISTED\r\n");
                vm.guest_command("test \"$(dpkg-query -W -f='${db:Status-Status}' x11-utils)\" = installed && echo MNOS_PACKAGE_PERSISTENCE_PASS", "\r\nMNOS_PACKAGE_PERSISTENCE_PASS\r\n");
                vm.command("test -f /root/guiinput && echo MNOS_PERSISTENCE_PASS","\r\nMNOS_PERSISTENCE_PASS\r\n");
                vm.command("poweroff","LINUX GUEST: stopped");
                vm.page_key("f5",4);
                std::cout << "NATIVE LINUX GUEST PASS " << arch << ": actual Linux/LP64D, signed APT update/install via native VirtIO Ethernet, X11 GUI, real keyboard/pointer, private disk persistence, consent and pause/resume\n";
            }
            vm.command("kernel su", "Root password:");
            vm.command("rootadmintest785", "Root session:");
            vm.send(std::string(1,(char)0x85));
            vm.command("kernel disk lock", "/disk read-only");
            vm.command("kernel disk unlock extra", "Invalid arguments");
            vm.command("kernel disk status", "/disk read-only");
            vm.command("kernel disk unlock", "/disk writes enabled");
            vm.command("kernel exit", "Returned to ordinary user");
            vm.send("\033");
            for (unsigned i = 0; i < 20; i++)
                vm.mouse_move(-100, -100);
            vm.mouse_move(60, 18);
            m = vm.output.size();
            vm.mouse_click();
            vm.wait("DESKTOP: launcher opened by pointer", m);
            vm.screenshot(build / (arch + "-applications.ppm"));
            m = vm.send("files\n");
            vm.wait("UI: page 1", m);
            vm.screenshot(build / (arch + "-files.ppm"));
        }
        {
            VM vm(build, work, arch, 2);
            vm.login(false);
            vm.wait("SETTINGS: restored wallpaper=1");
            auto m = vm.send("t");
            vm.wait("UI: page 4", m);
            vm.command("cat /disk/home/demo/hello.txt", "Saved by the native editor.");
            vm.wait("Note: Hello from the original mnOS desktop.");
            vm.command("cat /disk/home/demo/untitled.txt", "A first save must retain the untitled document.");
            vm.command("write /disk/root/denied.txt blocked", "Permission denied");
            if(std::getenv("MNOS_TEST_LINUX_GUEST")) {
                vm.command("linux status","Stopped / guest RAM cleared");
                require(vm.output.find("LINUX GUEST: start")==std::string::npos,"guest enabled after reboot");
            }
            vm.screenshot(build / (arch + "-reboot.ppm"));
        }
        fs::remove_all(work);
        std::cout << "DESKTOP PASS " << arch
                  << ": ISO install, no-ISO disk boot, native GUI, account persistence, editor "
                     "save, pointer caret, arrow/backspace edits, settings restore, native ELF and access controls\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
