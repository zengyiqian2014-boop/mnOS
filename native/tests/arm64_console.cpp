/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-only QEMU integration test. No test runtime is shipped in the ISO.
 */
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
namespace fs = std::filesystem;
using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
static void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
static pid_t spawn(const std::vector<std::string> &args) {
    pid_t pid = fork();
    require(pid >= 0, "fork");
    if (!pid) {
        std::vector<char *> argv;
        for (const auto &s : args)
            argv.push_back(const_cast<char *>(s.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    return pid;
}
static void run(const std::vector<std::string> &args) {
    int status = 0;
    auto pid = spawn(args);
    require(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "command failed: " + args[0]);
}
static int connect_unix(const fs::path &path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    require(fd >= 0, "socket");
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    auto text = path.string();
    require(text.size() < sizeof addr.sun_path, "socket path too long");
    memcpy(addr.sun_path, text.c_str(), text.size() + 1);
    auto end = clock_type::now() + 15s;
    while (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        require(clock_type::now() < end, "connect " + text);
        std::this_thread::sleep_for(50ms);
    }
    return fd;
}
static void send_all(int fd, const std::string &text) {
    size_t at = 0;
    while (at < text.size()) {
        ssize_t n = send(fd, text.data() + at, text.size() - at, MSG_NOSIGNAL);
        require(n > 0, "socket write");
        at += n;
    }
}
struct VM {
    pid_t pid = -1;
    int serial = -1, qmp = -1;
    std::string output;
    fs::path log;
    VM(const fs::path &out, const fs::path &disk, const fs::path &work, int phase)
        : log(out / ("arm64-console-" + std::to_string(phase) + ".log")) {
        auto port = work / ("s" + std::to_string(phase)),
             control = work / ("q" + std::to_string(phase));
        pid = spawn({"qemu-system-aarch64",
                     "-machine",
                     "virt,gic-version=3,acpi=off",
                     "-cpu",
                     "max",
                     "-smp",
                     "2",
                     "-m",
                     "1024",
                     "-nic", "none", "-display",
                     "none",
                     "-monitor",
                     "none",
                     "-bios",
                     "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
                     "-cdrom",
                     (out / "native-arm64-demo.iso").string(),
                     "-global",
                     "virtio-mmio.force-legacy=false",
                     "-device",
                     "ramfb",
                     "-device",
                     "virtio-keyboard-device",
                     "-device",
                     "virtio-rng-device",
                     "-drive",
                     "file=" + disk.string() + ",format=raw,if=none,id=data,cache=writeback",
                     "-device",
                     "virtio-blk-device,drive=data",
                     "-serial",
                     "unix:" + port.string() + ",server=on,wait=on",
                     "-qmp",
                     "unix:" + control.string() + ",server=on,wait=off",
                     "-no-reboot"});
        serial = connect_unix(port);
        qmp = connect_unix(control);
        qline();
        call("qmp_capabilities");
    }
    ~VM() {
        if (serial >= 0)
            close(serial);
        if (qmp >= 0)
            close(qmp);
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
        std::ofstream(log) << output;
    }
    std::string qline() {
        std::string line;
        auto end = clock_type::now() + 20s;
        for (;;) {
            pollfd p{qmp, POLLIN, 0};
            require(clock_type::now() < end, "QMP timeout");
            if (poll(&p, 1, 100) <= 0)
                continue;
            char c;
            require(read(qmp, &c, 1) == 1, "QMP closed");
            line += c;
            if (c == '\n')
                return line;
        }
    }
    void call(const std::string &name, const std::string &arguments = "{}") {
        send_all(qmp, "{\"execute\":\"" + name + "\",\"arguments\":" + arguments + "}\n");
        for (;;) {
            auto line = qline();
            require(line.find("\"error\"") == std::string::npos, line);
            if (line.find("\"return\"") != std::string::npos)
                return;
        }
    }
    void wait(const std::string &text, size_t start = 0) {
        auto end = clock_type::now() + 180s;
        while (output.find(text, start) == std::string::npos) {
            require(clock_type::now() < end, "Missing " + text + "\n" + output);
            pollfd p{serial, POLLIN, 0};
            if (poll(&p, 1, 100) <= 0)
                continue;
            char data[65536];
            ssize_t n = recv(serial, data, sizeof data, 0);
            require(n > 0, "VM serial closed\n" + output);
            output.append(data, n);
            require(output.find("PANIC:") == std::string::npos, output);
        }
    }
    void command(const std::string &text, const std::string &expected) {
        size_t start = output.size();
        for (char c : text + '\n') {
            send_all(serial, std::string(1, c));
            std::this_thread::sleep_for(25ms);
        }
        wait(expected, start);
    }
    void terminal() {
        const size_t start = output.size();
        call("send-key", "{\"keys\":[{\"type\":\"qcode\",\"data\":\"f5\"}]}");
        wait("UI: page 4", start);
    }
    void quit() {
        call("quit");
        int status;
        require(waitpid(pid, &status, 0) == pid, "wait QEMU");
        pid = -1;
    }
};
int main(int argc, char **argv) {
    try {
        require(argc == 2, "usage: arm64-console BUILD/ARM64");
        auto out = fs::canonical(argv[1]);
        auto tool = (out.parent_path() / "mnfs").string();
        char name[] = "/tmp/mnos-arm-XXXXXX";
        require(mkdtemp(name) != nullptr, "mkdtemp");
        fs::path work = name, disk = work / "disk.img";
        run({tool, "format", disk.string(), "32"});
        for (int phase = 1; phase <= 2; ++phase) {
            VM vm(out, disk, work, phase);
            vm.wait("/disk validated read-only");
            vm.wait(phase == 1 ? "AUTH PROMPT: New username" : "AUTH PROMPT: Username:");
            if (phase == 1) {
                vm.command("demo", "New password (12+ characters)");
                vm.command("ordinarytest491", "Confirm new password");
                vm.command("ordinarytest491", "Set a separate password for root");
                vm.command("rootadmintest785", "Confirm new password");
                vm.command("rootadmintest785", "Accounts created. Signed in as the ordinary user.");
            } else {
                vm.command("demo", "AUTH PROMPT: Password:");
                vm.command("ordinarytest491", "Signed in as an ordinary user.");
            }
            vm.terminal();
            vm.command("kernel info", "mnOS native / aarch64");
            vm.command("help", "mnOS command guide (English)");
            vm.command("help -cn", "mnOS 中文帮助");
            vm.command("write /disk/outside-home denied", "Permission denied");
            vm.command("disk unlock", "Permission denied");
            if (phase == 1) {
                vm.command("write /disk/home/demo/message arm64-persisted", "written and flushed");
                vm.command("writeat /disk/home/demo/large 4294967294 cross4g",
                           "written and flushed");
                vm.command("su", "Root password:");
                vm.command("rootadmintest785", "Root session:");
                vm.terminal();
                vm.command("write /disk/root/secret root-only", "written and flushed");
                vm.command("exit", "ordinary user");
                vm.terminal();
            }
            vm.command("cat /disk/root/secret", "Permission denied");
            vm.command("cat /disk/system/accounts.bin", "Permission denied");
            vm.command("cat /disk/home/demo/message", "arm64-persisted");
            vm.command("cat /disk/home/demo/large 4294967294", "cross4g");
            vm.command("stat /disk/home/demo/large", "Size: 4294967301");
            vm.command("kernel wallpaper 1", "SETTINGS: wallpaper=1");
            auto picture = (out / ("arm64-console-" + std::to_string(phase) + ".ppm")).string();
            std::this_thread::sleep_for(1s);
            vm.call("screendump", "{\"filename\":\"" + picture + "\"}");
            std::ifstream shot(picture,std::ios::binary);std::string magic;unsigned w=0,h=0,max=0;
            shot>>magic>>w>>h>>max;shot.get();require(magic=="P6"&&w>=640&&h>=480&&max==255,"GOP screenshot format");
            size_t visible=0;for(size_t pixel=0;pixel<(size_t)w*h;++pixel){char rgb[3]{};shot.read(rgb,3);if(rgb[0]||rgb[1]||rgb[2])++visible;}
            require(shot.good()&&visible>(size_t)w*h/2,"GOP framebuffer is blank");
            vm.quit();
            require(vm.output.find("ordinarytest491") == std::string::npos &&
                        vm.output.find("rootadmintest785") == std::string::npos,
                    "password leaked");
            std::cout << "PASS ARM64 console phase " << phase
                      << ": accounts, root separation, keyboard, GOP, help and persisted files"
                      << std::endl;
        }
        run({tool, disk.string(), "check"});
        fs::remove_all(work);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
