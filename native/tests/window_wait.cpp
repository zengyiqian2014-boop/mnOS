/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace std::chrono_literals;
static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static pid_t start(const char *program, const char *title, const char *seconds) {
    pid_t child = fork(); require(child >= 0, "fork");
    if (!child) { execl(program, program, title, seconds, static_cast<char *>(nullptr)); _exit(127); }
    return child;
}
static void finish(pid_t child, int expected) {
    auto deadline = std::chrono::steady_clock::now() + 6s; int status = 0;
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        require(result == 0, "waitpid");
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL); waitpid(child, &status, 0); throw std::runtime_error("window waiter did not terminate");
        }
        std::this_thread::sleep_for(20ms);
    }
    require(WIFEXITED(status) && WEXITSTATUS(status) == expected, "wrong waiter result");
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "window-wait-test HELPER");
        Display *display = XOpenDisplay(nullptr); require(display, "Xvfb display");
        Window root = DefaultRootWindow(display);
        Window hidden = XCreateSimpleWindow(display, root, 0, 0, 160, 80, 0, 0, 0);
        XStoreName(display, hidden, "mnOS invisible fixture"); XSync(display, False);
        finish(start(argv[1], "invisible fixture", "1"), 1);
        finish(start(argv[1], "fixture", "not-a-number"), 2);
        // A mapped frame alone must not satisfy a child window's hidden title.
        Window frame = XCreateSimpleWindow(display, root, 5, 5, 180, 100, 0, 0, 0);
        Window child_window = XCreateSimpleWindow(display, frame, 0, 0, 160, 80, 0, 0, 0);
        XMapWindow(display, frame); XStoreName(display, child_window, "mnOS delayed fixture"); XSync(display, False);
        pid_t waiter = start(argv[1], "delayed fixture", "4");
        std::this_thread::sleep_for(250ms);
        int status = 0; require(waitpid(waiter, &status, WNOHANG) == 0, "matched an unmapped window");
        XMapWindow(display, child_window); XSync(display, False); finish(waiter, 0);
        // GTK's UTF-8 title and renaming after mapping must also work.
        XDeleteProperty(display, child_window, XA_WM_NAME);
        XSync(display, False); waiter = start(argv[1], "UTF8 fixture", "4");
        Atom property = XInternAtom(display, "_NET_WM_NAME", False), utf8 = XInternAtom(display, "UTF8_STRING", False);
        const char title[] = "mnOS UTF8 fixture";
        XChangeProperty(display, child_window, property, utf8, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(title), sizeof(title) - 1);
        // Exercise disappearing windows while the independent helper walks.
        for (unsigned i = 0; i < 100; ++i) {
            Window transient = XCreateSimpleWindow(display, root, 0, 0, 10, 10, 0, 0, 0);
            XMapWindow(display, transient); XDestroyWindow(display, transient);
        }
        XSync(display, False); finish(waiter, 0);
        XDestroyWindow(display, frame); XDestroyWindow(display, hidden); XCloseDisplay(display);
        std::cout << "WINDOW WAIT PASS: hidden/mapped/reparented/renamed windows, UTF-8 title, timeout and disappearing windows\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
