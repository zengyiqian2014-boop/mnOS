/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
bool query_error;
int xerror(Display *, XErrorEvent *error) {
    // A window can disappear between QueryTree and GetWindowAttributes.
    if (error->error_code != BadWindow && error->error_code != BadDrawable)
        query_error = true;
    return 0;
}
bool title_matches(Display *display, Window window, Atom property, const std::string &wanted) {
    Atom type = None; int format = 0; unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    int status = XGetWindowProperty(display, window, property, 0, 1024, False,
                                   AnyPropertyType, &type, &format, &count, &remaining, &data);
    bool found = status == Success && format == 8 && data &&
                 std::string(reinterpret_cast<char *>(data), count).find(wanted) != std::string::npos;
    if (data) XFree(data);
    return found;
}
Window find_window(Display *display, Atom utf8_name, const std::string &wanted) {
    struct Pending { Window window; unsigned depth; };
    std::vector<Pending> pending{{DefaultRootWindow(display), 0}};
    unsigned visited = 0;
    while (!pending.empty() && visited++ < 4096) {
        auto item = pending.back(); pending.pop_back();
        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display, item.window, &attributes) || attributes.map_state != IsViewable) continue;
        if (item.depth && (title_matches(display, item.window, utf8_name, wanted) ||
                           title_matches(display, item.window, XA_WM_NAME, wanted))) return item.window;
        if (item.depth >= 16) continue;
        Window root = None, parent = None, *children = nullptr; unsigned count = 0;
        if (XQueryTree(display, item.window, &root, &parent, &children, &count)) {
            count = std::min(count, static_cast<unsigned>(4096 - pending.size()));
            for (unsigned i = 0; i < count; ++i) pending.push_back({children[i], item.depth + 1});
        }
        if (children) XFree(children);
    }
    return None;
}
}

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) {
        std::cout << "mnos-window-wait TITLE SECONDS\nWait for a visible X11 window whose title contains TITLE.\n"
                     "Exit status: 0 visible, 1 timeout, 2 invalid arguments/display error.\n";
        return 0;
    }
    unsigned seconds = 0;
    if (argc != 3 || !*argv[1] || std::strlen(argv[1]) > 256) return 2;
    const char *end = argv[2] + std::strlen(argv[2]);
    auto parsed = std::from_chars(argv[2], end, seconds);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !seconds || seconds > 86400) return 2;
    Display *display = XOpenDisplay(nullptr);
    if (!display) { std::cerr << "Cannot connect to the X11 display.\n"; return 2; }
    XSetErrorHandler(xerror);
    Atom utf8_name = XInternAtom(display, "_NET_WM_NAME", False);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    int status = 1;
    for (;;) {
        Window window = find_window(display, utf8_name, argv[1]);
        if (query_error) { std::cerr << "X11 window query failed.\n"; status = 2; break; }
        if (window != None) { std::cout << "WINDOW READY: " << window << '\n'; status = 0; break; }
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) { std::cerr << "Timed out waiting for visible window: " << argv[1] << '\n'; break; }
        // One process and one connection avoid repeatedly loading an ELF/X11
        // client while an interpreted Linux application is trying to start.
        std::this_thread::sleep_until(std::min(deadline, now + std::chrono::seconds(1)));
    }
    XCloseDisplay(display);
    return status;
}
