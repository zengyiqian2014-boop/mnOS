/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real native-kernel exec lifecycle tests. C++ host driver, no Python. */
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <regex>
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
static void require(bool v, const std::string &s) { if (!v) throw std::runtime_error(s); }
static pid_t launch(const std::vector<std::string> &args) {
    pid_t pid = fork(); require(pid >= 0, "fork");
    if (!pid) {
        std::vector<char *> a;
        for (auto &s: args) a.push_back(const_cast<char *>(s.c_str()));
        a.push_back(nullptr); execvp(a[0], a.data()); _exit(127);
    }
    return pid;
}
static int run(const std::vector<std::string> &args) {
    auto p = launch(args); int s;
    require(waitpid(p, &s, 0) == p, "waitpid");
    return WIFEXITED(s) ? WEXITSTATUS(s) : 255;
}
struct VM {
    pid_t pid = -1; int fd = -1; std::string output; fs::path log;
    VM(const fs::path &build, const fs::path &work, const fs::path &disk,
       const std::string &arch, unsigned phase): log(build / ("process-" + arch + "-" + std::to_string(phase) + ".log")) {
        auto port = work / ("serial-" + std::to_string(phase));
        std::vector<std::string> a;
        if (arch == "arm64") a = {"qemu-system-aarch64", "-machine", "virt,gic-version=3,acpi=off",
            "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd", "-device", "ramfb",
            "-global", "virtio-mmio.force-legacy=false", "-device", "virtio-rng-device",
            "-drive", "file=" + disk.string() + ",format=raw,if=none,id=data", "-device", "virtio-blk-device,drive=data",
            "-cdrom", (build / "arm64/native-arm64-demo.iso").string()};
        else a = {"qemu-system-x86_64", "-vga", "std", "-device", "virtio-rng-pci",
            "-drive", "file=" + disk.string() + ",format=raw,if=ide,index=0",
            "-cdrom", (build / "native-demo.iso").string(), "-boot", "order=d"};
        const std::vector<std::string> common = {"-accel", "tcg", "-cpu", "max", "-smp", "2", "-m", "512",
            "-display", "none", "-nic", "none", "-monitor", "none", "-no-reboot",
            "-serial", "unix:" + port.string() + ",server=on,wait=on"};
        a.insert(a.end(), common.begin(), common.end()); pid = launch(a);
        fd = socket(AF_UNIX, SOCK_STREAM, 0); require(fd >= 0, "socket");
        sockaddr_un addr{}; addr.sun_family = AF_UNIX;
        require(port.string().size() < sizeof addr.sun_path, "socket path");
        strcpy(addr.sun_path, port.c_str());
        auto end = Clock::now() + 15s;
        while (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr)) {
            require(Clock::now() < end, "connect QEMU"); std::this_thread::sleep_for(25ms);
        }
    }
    ~VM() {
        if (fd >= 0) close(fd);
        if (pid > 0) { kill(pid, SIGTERM); waitpid(pid, nullptr, 0); }
        std::ofstream(log) << output;
    }
    void wait(const std::string &s, size_t start = 0) {
        auto end = Clock::now() + 120s;
        while (output.find(s, start) == std::string::npos) {
            require(Clock::now() < end, "timeout: " + s + "\n" + output);
            pollfd p{fd, POLLIN, 0}; if (poll(&p, 1, 100) <= 0) continue;
            char b[4096]; auto n = read(fd, b, sizeof b);
            require(n > 0, "serial closed\n" + output); output.append(b, n);
            require(output.find("PANIC") == std::string::npos, output);
            require(output.find("EXTENDED STATE TRAP FAILED") == std::string::npos, output);
        }
    }
    size_t send(const std::string &s) {
        size_t mark = output.size();
        for (char c: s) {
            require(::send(fd, &c, 1, MSG_NOSIGNAL) == 1, "serial write");
            std::this_thread::sleep_for(25ms);
        }
        return mark;
    }
    size_t command(const std::string &s, const std::string &expected) {
        auto m = send(s + '\n'); wait(expected, m); return m;
    }
    void login(bool first) {
        wait(first ? "AUTH PROMPT: New username" : "AUTH PROMPT: Username:");
        command("demo", first ? "New password (12+ characters)" : "AUTH PROMPT: Password:");
        if (first) {
            command("ordinarytest491", "Confirm new password");
            command("ordinarytest491", "Set a separate password for root");
            command("rootadmintest785", "Confirm new password");
            command("rootadmintest785", "Accounts created. Signed in as the ordinary user.");
        } else command("ordinarytest491", "Signed in as an ordinary user.");
        auto m = send("t"); wait("UI: page 4", m);
    }
    void hello(const std::string &path) {
        auto m = command("kernel exec " + path, "HELLO: native user ELF; initialized data and BSS OK");
        auto id = launched(m);
        wait("PROCESS " + id + " exited status=7", m);
        wait("PROCESS reaped; pages restored=1 pid=" + id + "\n", m);
    }
    uint64_t pages() {
        auto m = command("mem", "CONSOLE: mem");
        std::smatch match; auto text = output.substr(m);
        require(std::regex_search(text, match, std::regex("Free physical pages: ([0-9]+)")), "free pages\n"+text);
        return std::stoull(match[1]);
    }
    std::string launched(size_t m) {
        std::smatch match; auto text = output.substr(m);
        require(std::regex_search(text, match, std::regex("PROCESS ([0-9]+) started")), "PID");
        return match[1];
    }
    std::string spin() {
        return launched(command("exec /ram/bin/spin", "SPIN: running without yielding"));
    }
    std::string isolate() {
        return launched(command("exec /disk/bin/isolate", "ISOLATE: private BSS initialized"));
    }
    void terminate(const std::string &id) {
        auto m = command("kernel kill " + id, "PROCESS " + id + " terminated status=130");
        wait("PROCESS reaped; pages restored=1 pid=" + id + "\n", m);
    }
    std::vector<uint64_t> progress(const std::vector<std::string> &ids) {
        auto m = command("ps", "CONSOLE: ps"); std::vector<uint64_t> counts;
        auto text = output.substr(m);
        for (const auto &id: ids) {
            std::smatch match;
            require(std::regex_search(text, match, std::regex("PID " + id + " UID 1000 TICKS ([0-9]+) SWITCHES ([0-9]+)")),
                    "missing live PID " + id + "\n" + text);
            require(std::stoull(match[2]) > 0, "process never scheduled: " + id);
            counts.push_back(std::stoull(match[1]));
        }
        return counts;
    }
};
int main(int argc, char **argv) {
    try {
        require(argc == 3, "usage: process-vm BUILD x86_64|arm64");
        auto build = fs::canonical(argv[1]); const std::string arch = argv[2];
        require(arch == "x86_64" || arch == "arm64", "architecture");
        char tmp[] = "/tmp/mnos-exec-XXXXXX"; require(mkdtemp(tmp), "mkdtemp");
        fs::path work = tmp, disk = work / "disk.img";
        const auto tool = (build / "mnfs").string();
        auto mnfs = [&](std::vector<std::string> args) {
            args.insert(args.begin(), disk.string()); args.insert(args.begin(), tool);
            require(run(args) == 0, "mnfs operation failed");
        };
        require(run({tool, "format", disk.string(), "32"}) == 0, "format");
        mnfs({"mkdir", "/bin"}); mnfs({"mkdir", "/root"});
        auto programs = build / (arch == "arm64" ? "arm64/user" : "user");
        for (auto name: {"hello", "fault", "badptr", "isolate", "fpstate", "simdstate", "tlsstate", "filetest"})
            mnfs({"import", std::string("/bin/") + name, (programs / (std::string(name)+".elf")).string()});
        mnfs({"import", "/root/hidden", (programs / "hello.elf").string()});
        require(run({tool, disk.string(), "import", "/bin/hello", (programs / "hello.elf").string()}) != 0,
                "import overwrote an existing file");
        std::ifstream source(programs / "hello.elf", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(source)), {});
        require(bytes.size() > 80, "ELF fixture");
        bytes[18] = 0; std::ofstream(work / "bad.elf", std::ios::binary).write(bytes.data(), bytes.size());
        mnfs({"import", "/bin/wrong-arch", (work / "bad.elf").string()});
        bytes[18] = arch == "arm64" ? static_cast<char>(183) : 62;
        bytes[68] = 7; std::ofstream(work / "bad.elf", std::ios::binary).write(bytes.data(), bytes.size());
        mnfs({"import", "/bin/writable-code", (work / "bad.elf").string()});
        for (unsigned phase = 1; phase <= 2; ++phase) {
            VM vm(build, work, disk, arch, phase); vm.login(phase == 1);
            vm.hello("/disk/bin/hello"); vm.hello("/ram/bin/hello");
            auto file_mark = vm.command("exec /disk/bin/filetest", "FILE: native user read/write, sparse offsets and permissions passed");
            auto file_pid = vm.launched(file_mark);
            vm.wait("PROCESS " + file_pid + " exited status=0", file_mark);
            vm.wait("PROCESS reaped; pages restored=1 pid=" + file_pid + "\n", file_mark);
            if (phase == 2) vm.wait("FILE: previous boot data present", file_mark);
            if (phase == 1) {
                auto free = vm.pages();
                for (unsigned i=0;i<12;i++) vm.hello("/disk/bin/hello");
                require(vm.pages() == free, "normal exit leaks pages");
                for (unsigned i=0;i<8;i++) {
                    vm.command("exec /disk/bin/wrong-arch", "invalid or unsupported ELF");
                    vm.command("exec /disk/bin/writable-code", "invalid or unsupported ELF");
                }
                require(vm.pages() == free, "invalid ELF leaks pages");
                vm.command("exec /disk/root/hidden", "Permission denied");
                vm.command("exec /disk/system/accounts.bin", "Permission denied");
                vm.command("exec /disk/bin/../root/hidden", "Permission denied");
                auto m = vm.command("exec /disk/bin/badptr", "BADPTR: boundaries and unknown syscalls rejected");
                vm.wait("exited status=0", m); vm.wait("PROCESS reaped; pages restored=1", m);
                m = vm.command("exec /disk/bin/fault", "fault status=");
                vm.wait("PROCESS reaped; pages restored=1", m);
                vm.hello("/disk/bin/hello");
                std::vector<std::string> pids;
                for (unsigned n = 0; n < 16; ++n) pids.push_back(vm.spin());
                require(vm.pages() == free - pids.size() * 8, "unexpected live process memory cost");
                auto before = vm.progress(pids);
                std::vector<std::string> sleeping;
                auto isolation_start = vm.output.size();
                for (unsigned n = 0; n < 4; ++n) sleeping.push_back(vm.isolate());
                vm.hello("/disk/bin/hello");
                m = vm.command("exec /disk/bin/fault", "fault status=");
                auto faulted = vm.launched(m);
                vm.wait("PROCESS reaped; pages restored=1 pid=" + faulted + "\n", m);
                m = vm.command("exec /disk/bin/badptr", "BADPTR: boundaries and unknown syscalls rejected");
                auto badptr = vm.launched(m);
                vm.wait("PROCESS reaped; pages restored=1 pid=" + badptr + "\n", m);
                for (auto probe: {"fpstate", "simdstate", "tlsstate"}) {
                    m = vm.command(std::string("exec /disk/bin/") + probe, "fault status=");
                    auto trapped = vm.launched(m);
                    vm.wait("PROCESS reaped; pages restored=1 pid=" + trapped + "\n", m);
                }
                vm.wait("ISOLATE: identity and private data survived 5 sleeps", isolation_start);
                auto after = vm.progress(pids);
                for (unsigned n = 0; n < pids.size(); ++n)
                    require(after[n] > before[n], "starved process " + pids[n]);
                vm.progress(sleeping);
                require(vm.pages() == free - (pids.size() + sleeping.size()) * 8, "concurrent fault/exit leaked pages");
                require(vm.output.find("exited status=9") == std::string::npos &&
                        vm.output.find("exited status=100") == std::string::npos, "private process state corrupted");
                vm.command("kill 0", "No such process");
                for (unsigned n = 0; n < pids.size(); n += 2) vm.terminate(pids[n]);
                for (unsigned n = 1; n < pids.size(); n += 2) vm.terminate(pids[n]);
                for (const auto &id: sleeping) vm.terminate(id);
                vm.command("kill " + pids[0], "No such process");
                require(vm.pages() == free, "kill/fault leaks pages");
                auto spin = vm.spin(), sleeper = vm.isolate();
                m = vm.command("logout", "Session locked.");
                for (const auto &id: {spin, sleeper}) {
                    vm.wait("PROCESS " + id + " session-ended status=125", m);
                    vm.wait("PROCESS reaped; pages restored=1 pid=" + id + "\n", m);
                }
                vm.command("demo", "AUTH PROMPT: Password:");
                vm.command("ordinarytest491", "Signed in as an ordinary user.");
                m = vm.send("t");
                vm.wait("UI: page 4", m);
                vm.hello("/disk/bin/hello");
                require(vm.pages() == free, "session change leaks pages");
            }
            require(vm.output.find("ordinarytest491") == std::string::npos &&
                    vm.output.find("rootadmintest785") == std::string::npos, "password leaked");
            std::cout << "PASS " << arch << " phase " << phase << ": MNFS exec, dynamic processes, preemption, fault isolation and reclamation\n";
        }
        mnfs({"check"}); fs::remove_all(work); return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
