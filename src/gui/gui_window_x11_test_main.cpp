// Smoke test for the X11 window: needs a display (run under Xvfb).
#include "gui/gui_window.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <filesystem>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

static const clap_host_t dummyHost = {
    .clap_version = CLAP_VERSION_INIT, .host_data = nullptr, .name = "t", .vendor = "t", .url = "", .version = "1",
    .get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    .request_restart = [](const clap_host_t*) {}, .request_process = [](const clap_host_t*) {},
    .request_callback = [](const clap_host_t*) {}};

static Window findWindow(Display* d, Window w, const char* name) {
    char* n = nullptr;
    if (XFetchName(d, w, &n) && n) {
        const bool match = std::string(n) == name;
        XFree(n);
        if (match) return w;
    }
    Window root, parent, *kids = nullptr;
    unsigned count = 0;
    if (!XQueryTree(d, w, &root, &parent, &kids, &count)) return 0;
    Window found = 0;
    for (unsigned i = 0; i < count && !found; ++i) found = findWindow(d, kids[i], name);
    if (kids) XFree(kids);
    return found;
}

int main() {
    paulascape::PaulascapePlugin plugin(&dummyHost);
    assert(plugin.init());
    assert(plugin.loadModFile(std::string(PAULASCAPE_TEST_DIR) + "/mods/BEDROCK.MOD"));
    paulascape::UiApp ui(plugin);
    paulascape::GuiWindow window(ui);
    assert(window.create(0));
    window.show();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    Display* d = XOpenDisplay(nullptr);
    assert(d);
    Window win = findWindow(d, DefaultRootWindow(d), "Paulascape");
    assert(win);

    // Pixels were painted (not all one colour)
    XImage* img = XGetImage(d, win, 0, 0, 640, 400, AllPlanes, ZPixmap);
    assert(img);
    const unsigned long first = XGetPixel(img, 0, 0);
    bool varied = false;
    for (int y = 0; y < 400 && !varied; y += 7)
        for (int x = 0; x < 640; x += 7)
            if (XGetPixel(img, x, y) != first) { varied = true; break; }
    XDestroyImage(img);
    assert(varied);

    // Click the "Pattern" button with a synthetic event
    XEvent ev{};
    ev.type = ButtonPress;
    ev.xbutton.window = win;
    ev.xbutton.x = 360;
    ev.xbutton.y = 12;
    ev.xbutton.button = Button1;
    ev.xbutton.same_screen = True;
    XSendEvent(d, win, True, ButtonPressMask, &ev);
    XFlush(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // the window thread renders (and so records hit regions) before it handles the click
    assert(plugin.snapshot().params[paulascape::PARAM_PLAYBACK_MODE] == 1.0);

    // ---- Drag the MIDI button onto another window that speaks XDND, as a DAW would ----
    {
        // leave pattern mode's click behind: back to instrument mode, front page
        Display* td = XOpenDisplay(nullptr);
        assert(td);
        Window target = XCreateSimpleWindow(td, DefaultRootWindow(td), 700, 0, 200, 200, 0, 0, 0);
        const Atom aware = XInternAtom(td, "XdndAware", False);
        const Atom enter = XInternAtom(td, "XdndEnter", False);
        const Atom position = XInternAtom(td, "XdndPosition", False);
        const Atom status = XInternAtom(td, "XdndStatus", False);
        const Atom drop = XInternAtom(td, "XdndDrop", False);
        const Atom finished = XInternAtom(td, "XdndFinished", False);
        const Atom selection = XInternAtom(td, "XdndSelection", False);
        const Atom copy = XInternAtom(td, "XdndActionCopy", False);
        const Atom uriList = XInternAtom(td, "text/uri-list", False);
        const Atom prop = XInternAtom(td, "PAULASCAPE_TEST_DROP", False);
        const long version = 5;
        XChangeProperty(td, target, aware, XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&version), 1);
        XMapWindow(td, target);
        XFlush(td);

        bool gotEnter = false, gotPosition = false, gotDrop = false;
        std::string uri;
        auto pump = [&](int ms, bool (*)(void) = nullptr) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            while (std::chrono::steady_clock::now() < end) {
                while (XPending(td)) {
                    XEvent ev;
                    XNextEvent(td, &ev);
                    if (ev.type == ClientMessage) {
                        const Atom t = ev.xclient.message_type;
                        const Window source = static_cast<Window>(ev.xclient.data.l[0]);
                        if (t == enter) {
                            gotEnter = true;
                        } else if (t == position) {
                            gotPosition = true;
                            XEvent r{};
                            r.xclient.type = ClientMessage;
                            r.xclient.window = source;
                            r.xclient.message_type = status;
                            r.xclient.format = 32;
                            r.xclient.data.l[0] = static_cast<long>(target);
                            r.xclient.data.l[1] = 1;
                            r.xclient.data.l[4] = static_cast<long>(copy);
                            XSendEvent(td, source, False, NoEventMask, &r);
                            XFlush(td);
                        } else if (t == drop) {
                            gotDrop = true;
                            XConvertSelection(td, selection, uriList, prop, target, CurrentTime);
                            XFlush(td);
                        }
                    } else if (ev.type == SelectionNotify && ev.xselection.property == prop) {
                        Atom type; int fmt; unsigned long n, after; unsigned char* data = nullptr;
                        XGetWindowProperty(td, target, prop, 0, 4096, True, AnyPropertyType, &type, &fmt, &n, &after, &data);
                        if (data) { uri.assign(reinterpret_cast<char*>(data), n); XFree(data); }
                        XEvent r{};
                        r.xclient.type = ClientMessage;
                        r.xclient.window = ev.xselection.requestor;
                        r.xclient.message_type = finished;
                        r.xclient.format = 32;
                        r.xclient.data.l[0] = static_cast<long>(target);
                        r.xclient.data.l[1] = 1;
                        r.xclient.data.l[2] = static_cast<long>(copy);
                        // the source window id travels in the XdndDrop message; it is the plugin window
                        XSendEvent(td, win, False, NoEventMask, &r);
                        XFlush(td);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };

        auto send = [&](int type, int x, int y, int xr, int yr, unsigned state) {
            XEvent e{};
            if (type == MotionNotify) {
                e.xmotion.type = MotionNotify;
                e.xmotion.window = win; e.xmotion.x = x; e.xmotion.y = y; e.xmotion.x_root = xr; e.xmotion.y_root = yr;
                e.xmotion.state = state; e.xmotion.same_screen = True;
                XSendEvent(d, win, True, PointerMotionMask, &e);
            } else {
                e.xbutton.type = type;
                e.xbutton.window = win; e.xbutton.x = x; e.xbutton.y = y; e.xbutton.x_root = xr; e.xbutton.y_root = yr;
                e.xbutton.button = Button1; e.xbutton.state = state; e.xbutton.same_screen = True;
                XSendEvent(d, win, True, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &e);
            }
            XFlush(d);
        };

        // switch back to the front page in instrument mode so the MIDI button is where we expect it
        plugin.setParamFromGui(paulascape::PARAM_PLAYBACK_MODE, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        send(ButtonPress, 520, 12, 520, 12, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        send(MotionNotify, 545, 20, 545, 20, Button1Mask);   // far enough: the drag starts
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        send(MotionNotify, 560, 30, 750, 50, Button1Mask);   // now over the target window
        pump(300);
        assert(gotEnter && gotPosition);
        send(ButtonRelease, 560, 30, 750, 50, Button1Mask);
        pump(2000);
        assert(gotDrop);
        assert(uri.rfind("file://", 0) == 0);
        while (!uri.empty() && (uri.back() == '\r' || uri.back() == '\n')) uri.pop_back();
        assert(uri.size() > 4 && uri.substr(uri.size() - 4) == ".mid");
        assert(std::filesystem::exists(uri.substr(7)));

        XDestroyWindow(td, target);
        XCloseDisplay(td);
    }

    XCloseDisplay(d);
    window.destroy();
    std::cout << "X11 window test passed" << std::endl;
    return 0;
}
