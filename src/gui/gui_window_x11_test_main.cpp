// Smoke test for the X11 window: needs a display (run under Xvfb).
#include "gui/gui_window.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
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

    XCloseDisplay(d);
    window.destroy();
    std::cout << "X11 window test passed" << std::endl;
    return 0;
}
