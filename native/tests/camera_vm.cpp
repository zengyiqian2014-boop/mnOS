/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual ISO install and native xHCI/UVC DMA with a synthetic USB camera.
 * This verifies the transport/preview, not physical cameras or face identity. */
#define main desktop_regression_main
#include "desktop_vm.cpp"
#undef main
struct CameraServer {
    pid_t pid = -1;
    CameraServer(const fs::path &program, const fs::path &socket, const std::string &mode) {
        pid = launch({program.string(), socket.string(), mode});
        auto end = Clock::now() + 5s;
        while (!fs::exists(socket)) {
            require(Clock::now() < end, "camera server startup");
            std::this_thread::sleep_for(10ms);
        }
    }
    ~CameraServer() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    }
};
static void check_pattern(const fs::path &path, bool wanted) {
    std::ifstream f(path, std::ios::binary);
    std::string kind;
    unsigned w, h, max;
    f >> kind >> w >> h >> max;
    f.get();
    require(kind == "P6" && max == 255 && w && h, "PPM header");
    unsigned red = 0, blue = 0;
    for (unsigned i = 0; i < w * h; ++i) {
        unsigned char rgb[3]{};
        f.read((char *)rgb, 3);
        require(bool(f), "PPM payload");
        red += rgb[0] > 240 && rgb[1] < 10 && rgb[2] < 10;
        blue += rgb[2] > 240 && rgb[0] < 10 && rgb[1] < 10;
    }
    require(wanted ? (red > 5000 && blue > 5000) : (red < 100 && blue < 100),
            "camera frame pixels red=" + std::to_string(red) + " blue=" + std::to_string(blue));
}
int main(int argc, char **argv) {
    try {
        require(argc == 4, "usage: camera-vm BUILD/DESKTOP amd64|arm64 bulk|iso");
        auto build = fs::canonical(argv[1]);
        std::string arch = argv[2], mode = argv[3];
        require((arch == "amd64" || arch == "arm64") && (mode == "bulk" || mode == "iso"),
                "arguments");
        char tmp[] = "/tmp/mnos-camera-XXXXXX";
        require(mkdtemp(tmp), "mkdtemp");
        fs::path work = tmp;
        std::ofstream(work / "disk.img").close();
        fs::resize_file(work / "disk.img", 256ULL << 20);
        {
            VM vm(build, work, arch, 0);
            vm.wait("INSTALLER READY");
            vm.send("\r");
            vm.wait("INSTALL CONFIRM");
            vm.send("ERASE\r");
            vm.wait("INSTALL PASS");
        }
        auto socket = work / "camera.sock";
        CameraServer server(build.parent_path() / "usb-camera", socket, mode);
        {
            VM vm(build, work, arch, 1,
                  {"-device", "qemu-xhci,id=usb", "-chardev",
                   "socket,id=camera,path=" + socket.string(), "-device",
                   "usb-redir,id=webcam,chardev=camera,bus=usb.0"});
            vm.wait("XHCI READY");
            vm.wait("USB DEVICE: ");
            vm.wait("UVC YUY2");
            vm.login(true);
            vm.page_key("f5", 4);
            vm.command("kernel usb", "cafe:4001 UVC YUY2 camera");
            vm.command("camera start", "UVC STREAM STARTED");
            vm.wait("UVC FRAME READY");
            auto shot = build / (arch + "-camera-" + mode + ".ppm");
            vm.screenshot(shot);
            check_pattern(shot, true);
            vm.page_key("f5", 4);
            vm.command("camera stop", "Camera off");
            vm.page_key("f6", 5);
            shot = build / (arch + "-camera-stopped-" + mode + ".ppm");
            vm.screenshot(shot);
            check_pattern(shot, false);
            vm.page_key("f5", 4);
            auto mark = vm.output.size();
            vm.command("camera start", "UVC STREAM STARTED");
            vm.wait("UVC FRAME READY", mark);
            vm.page_key("f5", 4);
            vm.command("lock", "AUTH PROMPT: Username:");
            vm.login(false);
            vm.page_key("f5", 4);
            vm.command("camera status", "Camera off");
            mark = vm.output.size();
            vm.command("camera start", "UVC STREAM STARTED");
            vm.wait("UVC FRAME READY", mark);
            // Close the actual preview window with the emulated mouse.
            vm.click_at(arch == "amd64" ? 1090 : 992, 108);
            vm.page_key("f5", 4);
            vm.command("camera status", "Camera off");
            mark = vm.output.size();
            vm.command("camera start", "UVC STREAM STARTED");
            vm.wait("UVC FRAME READY", mark);
            vm.qmp_command("{\"execute\":\"device_del\",\"arguments\":{\"id\":\"webcam\"}}");
            std::this_thread::sleep_for(1s);
            vm.page_key("f5", 4);
            vm.command("camera status", "Camera disconnected");
            vm.command("camera start", "No supported UVC YUY2 camera");
        }
        fs::copy_file(build / ("desktop-" + arch + "-1.log"),
                      build / ("camera-" + arch + "-" + mode + ".log"),
                      fs::copy_options::overwrite_existing);
        fs::remove_all(work);
        std::cout << "Native camera " << arch << " " << mode
                  << ": install, enumeration, frame pixels, stop/restart, lock, close and unplug PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
