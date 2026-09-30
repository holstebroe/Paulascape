#include "gui_window.hpp"
#include <clap/clap.h>
#include "framebuffer.hpp"

#if defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <poll.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace paulascape {

struct GuiWindow::Impl {
    std::thread thread;
    std::atomic<bool> quit{false};
    std::atomic<bool> wantVisible{false};
    std::atomic<int> scale{1};
    std::mutex m;
    std::condition_variable cv;
    bool started = false;
    bool ok = false;
    uintptr_t parent = 0;
};

namespace {

void runLoop(GuiWindow::Impl* impl, UiApp* app) {
    Display* dpy = XOpenDisplay(nullptr);
    auto signalStart = [&](bool ok) {
        std::lock_guard<std::mutex> lock(impl->m);
        impl->ok = ok;
        impl->started = true;
        impl->cv.notify_all();
    };
    if (!dpy) { signalStart(false); return; }

    const int screen = DefaultScreen(dpy);
    int scale = impl->scale.load();
    const ::Window root = RootWindow(dpy, screen);
    ::Window parent = impl->parent ? static_cast<::Window>(impl->parent) : root;
    ::Window win = XCreateSimpleWindow(dpy, parent, 0, 0, UiApp::WIDTH * scale, UiApp::HEIGHT * scale, 0, 0, 0);
    if (!win) { XCloseDisplay(dpy); signalStart(false); return; }

    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | StructureNotifyMask);
    XStoreName(dpy, win, "Paulascape");
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    if (!impl->parent) XSetWMProtocols(dpy, win, &wmDelete, 1);
    GC gc = XCreateGC(dpy, win, 0, nullptr);
    Visual* visual = DefaultVisual(dpy, screen);
    const int depth = DefaultDepth(dpy, screen);

    Framebuffer fb(UiApp::WIDTH, UiApp::HEIGHT);
    std::vector<uint32_t> scaled;
    bool mapped = false;
    signalStart(true);

    auto paint = [&]() {
        app->render(fb);
        scale = impl->scale.load();
        const int W = UiApp::WIDTH * scale, H = UiApp::HEIGHT * scale;
        scaled.resize(static_cast<size_t>(W) * H);
        const uint32_t* src = fb.getPixels();
        for (int y = 0; y < H; ++y) {
            const uint32_t* row = src + (y / scale) * UiApp::WIDTH;
            uint32_t* dst = scaled.data() + static_cast<size_t>(y) * W;
            for (int x = 0; x < W; ++x) dst[x] = row[x / scale];
        }
        XImage* img = XCreateImage(dpy, visual, depth, ZPixmap, 0, reinterpret_cast<char*>(scaled.data()), W, H, 32, 0);
        if (img) {
            XPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H);
            img->data = nullptr; // buffer is ours
            XDestroyImage(img);
        }
        XFlush(dpy);
    };

    int lastScale = scale;
    while (!impl->quit) {
        const bool wantVisible = impl->wantVisible.load();
        if (wantVisible && !mapped) { XMapWindow(dpy, win); mapped = true; }
        if (!wantVisible && mapped) { XUnmapWindow(dpy, win); mapped = false; }
        if (impl->scale.load() != lastScale) {
            lastScale = impl->scale.load();
            XResizeWindow(dpy, win, UiApp::WIDTH * lastScale, UiApp::HEIGHT * lastScale);
        }

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            const int s = impl->scale.load();
            if (ev.type == ButtonPress) {
                const int x = ev.xbutton.x / s, y = ev.xbutton.y / s;
                const unsigned b = ev.xbutton.button;
                if (b == 1 || b == 3) app->onMouseDown(x, y, static_cast<int>(b), (ev.xbutton.state & ShiftMask) != 0);
                else if (b == 4) app->onWheel(x, y, 1);
                else if (b == 5) app->onWheel(x, y, -1);
            } else if (ev.type == ClientMessage && static_cast<Atom>(ev.xclient.data.l[0]) == wmDelete) {
                impl->quit = true;
            }
        }
        if (mapped) paint();

        pollfd pfd{ConnectionNumber(dpy), POLLIN, 0};
        poll(&pfd, 1, 33);
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

} // namespace

GuiWindow::GuiWindow(UiApp& app) : app(app) {}
GuiWindow::~GuiWindow() { destroy(); }

bool GuiWindow::isSupported() { return true; }
const char* GuiWindow::clapApi() { return CLAP_WINDOW_API_X11; }

bool GuiWindow::create(uintptr_t parent) {
    if (impl) return true;
    impl = std::make_unique<Impl>();
    impl->parent = parent;
    impl->scale = app.scale();
    impl->thread = std::thread(runLoop, impl.get(), &app);
    std::unique_lock<std::mutex> lock(impl->m);
    impl->cv.wait(lock, [&] { return impl->started; });
    if (!impl->ok) {
        lock.unlock();
        impl->thread.join();
        impl.reset();
        return false;
    }
    return true;
}

void GuiWindow::destroy() {
    if (!impl) return;
    impl->quit = true;
    if (impl->thread.joinable()) impl->thread.join();
    impl.reset();
}

void GuiWindow::show() { if (impl) impl->wantVisible = true; }
void GuiWindow::hide() { if (impl) impl->wantVisible = false; }
void GuiWindow::setScale(int scale) { if (impl) impl->scale = scale; }
void GuiWindow::pixelSize(uint32_t& w, uint32_t& h) const {
    const int s = impl ? impl->scale.load() : app.scale();
    w = UiApp::WIDTH * s;
    h = UiApp::HEIGHT * s;
}

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>
#include <string>

namespace paulascape {

struct GuiWindow::Impl {
    HWND hwnd = nullptr;
    int scale = 1;
    Framebuffer fb{UiApp::WIDTH, UiApp::HEIGHT};
    UiApp* app = nullptr;
};

namespace {

const wchar_t* CLASS_NAME = L"PaulascapeWindow";
constexpr UINT_PTR TIMER_ID = 1;

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* impl = reinterpret_cast<GuiWindow::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!impl) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_TIMER:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            impl->app->render(impl->fb);
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = UiApp::WIDTH;
            bmi.bmiHeader.biHeight = -UiApp::HEIGHT;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, 0, 0, UiApp::WIDTH * impl->scale, UiApp::HEIGHT * impl->scale, 0, 0, UiApp::WIDTH,
                          UiApp::HEIGHT, impl->fb.getPixels(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            SetFocus(hwnd);
            impl->app->onMouseDown(GET_X_LPARAM(lp) / impl->scale, GET_Y_LPARAM(lp) / impl->scale,
                                   msg == WM_LBUTTONDOWN ? 1 : 3, (wp & MK_SHIFT) != 0);
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            impl->app->onWheel(p.x / impl->scale, p.y / impl->scale, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            POINT p{};
            DragQueryPoint(drop, &p);
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(drop, 0, path, MAX_PATH)) {
                char utf8[MAX_PATH * 3];
                WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
                impl->app->onFileDropped(p.x / impl->scale, p.y / impl->scale, utf8);
            }
            DragFinish(drop);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

GuiWindow::GuiWindow(UiApp& app) : app(app) {}
GuiWindow::~GuiWindow() { destroy(); }

bool GuiWindow::isSupported() { return true; }
const char* GuiWindow::clapApi() { return CLAP_WINDOW_API_WIN32; }

bool GuiWindow::create(uintptr_t parent) {
    if (impl) return true;
    HINSTANCE inst = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&wndProc), &inst);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc); // fails harmlessly if already registered

    impl = std::make_unique<Impl>();
    impl->app = &app;
    impl->scale = app.scale();
    const DWORD style = parent ? (WS_CHILD | WS_CLIPSIBLINGS) : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU);
    RECT r{0, 0, UiApp::WIDTH * impl->scale, UiApp::HEIGHT * impl->scale};
    if (!parent) AdjustWindowRect(&r, style, FALSE);
    impl->hwnd = CreateWindowExW(0, CLASS_NAME, L"Paulascape", style, 0, 0, r.right - r.left, r.bottom - r.top,
                                 reinterpret_cast<HWND>(parent), nullptr, inst, nullptr);
    if (!impl->hwnd) {
        impl.reset();
        return false;
    }
    SetWindowLongPtrW(impl->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl.get()));
    DragAcceptFiles(impl->hwnd, TRUE);
    SetTimer(impl->hwnd, TIMER_ID, 33, nullptr);
    return true;
}

void GuiWindow::destroy() {
    if (!impl) return;
    KillTimer(impl->hwnd, TIMER_ID);
    SetWindowLongPtrW(impl->hwnd, GWLP_USERDATA, 0);
    DestroyWindow(impl->hwnd);
    impl.reset();
}

void GuiWindow::show() { if (impl) ShowWindow(impl->hwnd, SW_SHOW); }
void GuiWindow::hide() { if (impl) ShowWindow(impl->hwnd, SW_HIDE); }
void GuiWindow::setScale(int scale) {
    if (!impl) return;
    impl->scale = scale;
    SetWindowPos(impl->hwnd, nullptr, 0, 0, UiApp::WIDTH * scale, UiApp::HEIGHT * scale, SWP_NOMOVE | SWP_NOZORDER);
}
void GuiWindow::pixelSize(uint32_t& w, uint32_t& h) const {
    const int s = impl ? impl->scale : app.scale();
    w = UiApp::WIDTH * s;
    h = UiApp::HEIGHT * s;
}

#else // macOS and others: Cocoa window not implemented yet

namespace paulascape {

struct GuiWindow::Impl {};
GuiWindow::GuiWindow(UiApp& app) : app(app) {}
GuiWindow::~GuiWindow() = default;
bool GuiWindow::isSupported() { return false; }
const char* GuiWindow::clapApi() { return nullptr; }
bool GuiWindow::create(uintptr_t) { return false; }
void GuiWindow::destroy() {}
void GuiWindow::show() {}
void GuiWindow::hide() {}
void GuiWindow::setScale(int) {}
void GuiWindow::pixelSize(uint32_t& w, uint32_t& h) const { w = UiApp::WIDTH; h = UiApp::HEIGHT; }

#endif

} // namespace paulascape
