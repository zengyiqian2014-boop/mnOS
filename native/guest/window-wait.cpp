/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <set>
#include <string>
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
Window find_window(Display *display, Atom utf8_name, const std::string &wanted,
                   std::set<Window> &watched) {
    struct Pending { Window window; unsigned depth; };
    std::vector<Pending> pending{{DefaultRootWindow(display), 0}};
    unsigned visited = 0;
    while (!pending.empty() && visited++ < 4096) {
        auto item = pending.back(); pending.pop_back();
        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display, item.window, &attributes)) continue;
        if (watched.size() < 4096 && watched.insert(item.window).second)
            XSelectInput(display, item.window, SubstructureNotifyMask | PropertyChangeMask);
        if (item.depth && attributes.map_state == IsViewable &&
                          (title_matches(display, item.window, utf8_name, wanted) ||
                           title_matches(display, item.window, XA_WM_NAME, wanted))) return item.window;
        // Watch hidden descendants too: mapping an ancestor can make a titled
        // child viewable without changing that child's own map state.
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
    std::set<Window> watched;
    bool rescan = true;
    int status = 1;
    for (;;) {
        if (rescan) {
            Window window = find_window(display, utf8_name, argv[1], watched);
            if (query_error) { std::cerr << "X11 window query failed.\n"; status = 2; break; }
            if (window != None) { std::cout << "WINDOW READY: " << window << '\n'; status = 0; break; }
            rescan = false;
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) { std::cerr << "Timed out waiting for visible window: " << argv[1] << '\n'; break; }
        // Sleep on the X connection instead of walking every window once a
        // second. On a software CPU, those synchronous X11 round trips take
        // execution time away from the application being started.
        if (!XPending(display)) {
            XFlush(display);
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            pollfd socket{ConnectionNumber(display), POLLIN, 0};
            int ready = poll(&socket, 1, static_cast<int>(std::min<int64_t>(remaining + 1, 2147483647)));
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (socket.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                std::cerr << "X11 connection failed.\n"; status = 2; break;
            }
            if (!ready) continue;
        }
        // Coalesce a burst of startup events; bound the batch so a busy
        // client cannot prevent the deadline from being checked.
        for (unsigned count = 0; count < 4096 && XPending(display); ++count) {
            XEvent event{}; XNextEvent(display, &event);
            if (event.type == DestroyNotify) watched.erase(event.xdestroywindow.window);
            if (event.type == CreateNotify || event.type == MapNotify || event.type == ReparentNotify ||
                (event.type == PropertyNotify && (event.xproperty.atom == utf8_name || event.xproperty.atom == XA_WM_NAME)))
                rescan = true;
        }
    }
    XCloseDisplay(display);
    return status;
}
