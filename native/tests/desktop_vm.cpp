/* SPDX-License-Identifier: GPL-3.0-or-later
 * Install from ISO to a fresh disk, remove ISO, boot original GUI, save a file
 * and settings, reboot again. No guest Linux or Python is involved. */
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
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
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
    VM(const fs::path &build, const fs::path &work, const std::string &arch, unsigned phase)
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
                 "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
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
                fs::copy_file("/usr/share/OVMF/OVMF_VARS_4M.fd", vars);
            a = {"qemu-system-x86_64",
                 "-drive",
                 "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
                 "-drive",
                 "if=pflash,format=raw,file=" + vars.string(),
                 "-vga",
                 "std",
                 "-device",
                 "virtio-rng-pci",
                 "-drive",
                 "file=" + disk.string() + ",format=raw,if=ide,index=0"};
        }
        if (phase == 0) {
            a.push_back("-cdrom");
            a.push_back((build / ("mnOS-0.4-desktop-installer-" + arch + ".iso")).string());
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
                                                 "1024",
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
        auto end = Clock::now() + 150s, last = Clock::now();
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
    void qmp_command(const std::string &s) {
        std::string cmd = s + "\n", reply;
        require(::send(monitor, cmd.data(), cmd.size(), MSG_NOSIGNAL) == (ssize_t)cmd.size(),
                "QMP send");
        auto end = Clock::now() + 10s;
        while (reply.find("\"return\"") == std::string::npos) {
            require(Clock::now() < end, "QMP response timeout: " + reply);
            pollfd p{monitor, POLLIN, 0};
            if (poll(&p, 1, 100) <= 0)
                continue;
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
    void login(bool first) {
        wait(first ? "AUTH PROMPT: New username" : "AUTH PROMPT: Username:");
        command("demo", first ? "New password (12+ characters)" : "AUTH PROMPT: Password:");
        if (first) {
            command("ordinarytest491", "Confirm new password");
            command("ordinarytest491", "Set a separate password for root");
            command("rootadmintest785", "Confirm new password");
            command("rootadmintest785", "Accounts created. Signed in as the ordinary user.");
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
        fs::resize_file(work / "disk.img", 256ULL << 20);
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
                for (std::streamsize i = 0; i < empty.gcount(); i++)
                    require(block[i] == 0, "cancelled install modified disk");
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
            vm.send("Hello from the original mnOS desktop.\nSaved by the native editor.");
            m = vm.output.size();
            vm.key("ctrl-s");
            vm.wait("EDITOR: saved /disk/home/demo/hello.txt", m);
            vm.screenshot(build / (arch + "-editor.ppm"));
            m = vm.send(std::string(1, (char)0x87));
            vm.wait("UI: page 6", m);
            vm.send("w");
            m = vm.send("s");
            vm.wait("SETTINGS: saved", m);
            m = vm.send(std::string(1, (char)0x85));
            vm.wait("UI: page 4", m);
            vm.command("cat /disk/home/demo/hello.txt", "Saved by the native editor.");
            vm.command("cat /disk/home/demo/untitled.txt", "A first save must retain the untitled document.");
            vm.command("exec /ram/bin/hello",
                       "HELLO: native user ELF; initialized data and BSS OK");
            vm.send("\033");
            for (unsigned i = 0; i < 20; i++)
                vm.mouse_move(-100, -100);
            vm.mouse_move(60, 18);
            m = vm.output.size();
            vm.mouse_click();
            vm.wait("DESKTOP: launcher opened by pointer", m);
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
            vm.command("cat /disk/home/demo/untitled.txt", "A first save must retain the untitled document.");
            vm.command("write /disk/root/denied.txt blocked", "Permission denied");
            vm.screenshot(build / (arch + "-reboot.ppm"));
        }
        fs::remove_all(work);
        std::cout << "DESKTOP PASS " << arch
                  << ": ISO install, no-ISO disk boot, native GUI, account persistence, editor "
                     "save, settings restore, native ELF and access controls\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
