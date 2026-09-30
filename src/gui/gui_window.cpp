#include "gui_window.hpp"
#include <clap/clap.h>
#include "framebuffer.hpp"

#if defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <chrono>
#include <string>
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

    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
    XStoreName(dpy, win, "Paulascape");
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    if (!impl->parent) XSetWMProtocols(dpy, win, &wmDelete, 1);
    GC gc = XCreateGC(dpy, win, 0, nullptr);
    Visual* visual = DefaultVisual(dpy, screen);
    const int depth = DefaultDepth(dpy, screen);


    // ---- XDND drag source: lets the user drag a file (the exported MIDI) onto a DAW ----
    const Atom aAware = XInternAtom(dpy, "XdndAware", False);
    const Atom aEnter = XInternAtom(dpy, "XdndEnter", False);
    const Atom aPosition = XInternAtom(dpy, "XdndPosition", False);
    const Atom aStatus = XInternAtom(dpy, "XdndStatus", False);
    const Atom aLeave = XInternAtom(dpy, "XdndLeave", False);
    const Atom aDrop = XInternAtom(dpy, "XdndDrop", False);
    const Atom aFinished = XInternAtom(dpy, "XdndFinished", False);
    const Atom aSelection = XInternAtom(dpy, "XdndSelection", False);
    const Atom aCopy = XInternAtom(dpy, "XdndActionCopy", False);
    const Atom aUriList = XInternAtom(dpy, "text/uri-list", False);
    const Atom aTargets = XInternAtom(dpy, "TARGETS", False);

    struct DragState {
        bool active = false;
        std::string uri;
        ::Window target = 0;
        long targetVersion = 0;
        bool accepted = false;
        bool dropSent = false;
        std::chrono::steady_clock::time_point dropTime;
    } drag;

    auto sendClient = [&](::Window to, Atom type, long d0, long d1, long d2, long d3, long d4) {
        XEvent ev{};
        ev.xclient.type = ClientMessage;
        ev.xclient.window = to;
        ev.xclient.message_type = type;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = d0;
        ev.xclient.data.l[1] = d1;
        ev.xclient.data.l[2] = d2;
        ev.xclient.data.l[3] = d3;
        ev.xclient.data.l[4] = d4;
        XSendEvent(dpy, to, False, NoEventMask, &ev);
    };

    // topmost XDND-aware window under a root-window point
    auto findTarget = [&](int rx, int ry, long& version) -> ::Window {
        ::Window w = root;
        for (int depth = 0; depth < 32; ++depth) {
            int dx, dy;
            ::Window child = 0;
            if (!XTranslateCoordinates(dpy, root, w, rx, ry, &dx, &dy, &child)) return 0;
            if (w != root && w != win) {
                Atom type; int fmt; unsigned long n, after; unsigned char* data = nullptr;
                if (XGetWindowProperty(dpy, w, aAware, 0, 1, False, AnyPropertyType, &type, &fmt, &n, &after, &data) == Success && data) {
                    const bool aware = type != None && n == 1;
                    if (aware) version = static_cast<long>(*reinterpret_cast<long*>(data));
                    XFree(data);
                    if (aware) return w;
                }
            }
            if (!child) return 0;
            w = child;
        }
        return 0;
    };

    auto percentEncode = [](const std::string& path) {
        std::string out = "file://";
        static const char* hex = "0123456789ABCDEF";
        for (unsigned char c : path) {
            if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
            else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
        }
        return out;
    };

    auto endDrag = [&]() {
        if (drag.active && !drag.dropSent && drag.target) sendClient(drag.target, aLeave, win, 0, 0, 0, 0);
        XUngrabPointer(dpy, CurrentTime);
        drag.active = false;
        drag.target = 0;
    };

    app->onDragFile = [&](const std::string& path) {
        drag = DragState{};
        drag.uri = percentEncode(path);
        XSetSelectionOwner(dpy, aSelection, win, CurrentTime);
        if (XGrabPointer(dpy, win, False, PointerMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None,
                         CurrentTime) != GrabSuccess) {
            return false;
        }
        drag.active = true;
        return true;
    };

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
            if (ev.type == SelectionRequest) {
                const XSelectionRequestEvent& rq = ev.xselectionrequest;
                XSelectionEvent note{};
                note.type = SelectionNotify;
                note.display = rq.display;
                note.requestor = rq.requestor;
                note.selection = rq.selection;
                note.target = rq.target;
                note.time = rq.time;
                note.property = None;
                if (rq.selection == aSelection && drag.active) {
                    if (rq.target == aTargets) {
                        const Atom list[2] = {aTargets, aUriList};
                        XChangeProperty(dpy, rq.requestor, rq.property, XA_ATOM, 32, PropModeReplace,
                                        reinterpret_cast<const unsigned char*>(list), 2);
                        note.property = rq.property;
                    } else if (rq.target == aUriList) {
                        const std::string data = drag.uri + "\r\n";
                        XChangeProperty(dpy, rq.requestor, rq.property, aUriList, 8, PropModeReplace,
                                        reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()));
                        note.property = rq.property;
                    }
                }
                XSendEvent(dpy, rq.requestor, False, NoEventMask, reinterpret_cast<XEvent*>(&note));
            } else if (drag.active) {
                if (ev.type == MotionNotify && !drag.dropSent) {
                    long version = 0;
                    const ::Window t = findTarget(ev.xmotion.x_root, ev.xmotion.y_root, version);
                    if (t != drag.target) {
                        if (drag.target) sendClient(drag.target, aLeave, win, 0, 0, 0, 0);
                        drag.target = t;
                        drag.targetVersion = std::min<long>(version, 5);
                        drag.accepted = false;
                        if (t) sendClient(t, aEnter, win, (drag.targetVersion << 24), aUriList, 0, 0);
                    }
                    if (drag.target) {
                        sendClient(drag.target, aPosition, win, 0, (static_cast<long>(ev.xmotion.x_root) << 16) | ev.xmotion.y_root,
                                   CurrentTime, aCopy);
                    }
                } else if (ev.type == ClientMessage && ev.xclient.message_type == aStatus) {
                    drag.accepted = (ev.xclient.data.l[1] & 1) != 0;
                } else if (ev.type == ClientMessage && ev.xclient.message_type == aFinished) {
                    endDrag();
                } else if (ev.type == ButtonRelease && !drag.dropSent) {
                    XUngrabPointer(dpy, CurrentTime);
                    if (drag.target && drag.accepted) {
                        sendClient(drag.target, aDrop, win, 0, CurrentTime, 0, 0);
                        drag.dropSent = true;
                        drag.dropTime = std::chrono::steady_clock::now();
                    } else {
                        endDrag();
                    }
                }
            } else if (ev.type == ButtonPress) {
                const int x = ev.xbutton.x / s, y = ev.xbutton.y / s;
                const unsigned b = ev.xbutton.button;
                if (b == 1 || b == 3) app->onMouseDown(x, y, static_cast<int>(b), (ev.xbutton.state & ShiftMask) != 0);
                else if (b == 4) app->onWheel(x, y, 1);
                else if (b == 5) app->onWheel(x, y, -1);
            } else if (ev.type == ButtonRelease) {
                app->onMouseUp(ev.xbutton.x / s, ev.xbutton.y / s, static_cast<int>(ev.xbutton.button));
            } else if (ev.type == MotionNotify && (ev.xmotion.state & (Button1Mask | Button3Mask))) {
                app->onMouseMove(ev.xmotion.x / s, ev.xmotion.y / s);
            } else if (ev.type == ClientMessage && static_cast<Atom>(ev.xclient.data.l[0]) == wmDelete) {
                impl->quit = true;
            }
        }
        if (drag.active && drag.dropSent &&
            std::chrono::steady_clock::now() - drag.dropTime > std::chrono::seconds(5)) {
            endDrag(); // target never answered with XdndFinished
        }
        if (mapped) paint();

        pollfd pfd{ConnectionNumber(dpy), POLLIN, 0};
        poll(&pfd, 1, 33);
    }

    app->onDragFile = nullptr;
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
#include <shlobj.h>
#include <objidl.h>
#include <ole2.h>
#include <windowsx.h>
#include <cstring>
#include <string>

namespace paulascape {

struct GuiWindow::Impl {
    HWND hwnd = nullptr;
    int scale = 1;
    Framebuffer fb{UiApp::WIDTH, UiApp::HEIGHT};
    UiApp* app = nullptr;
};

namespace {


// ---- OLE drag source: drag a file (the exported MIDI) onto a DAW ----
class FileDropSource : public IDropSource {
public:
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropSource) { *out = this; AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { const ULONG r = --refs; if (!r) delete this; return r; }
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    ULONG refs = 1;
};

class FileDataObject : public IDataObject {
public:
    explicit FileDataObject(const std::wstring& path) {
        const size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
        data = GlobalAlloc(GHND, bytes);
        auto* df = static_cast<DROPFILES*>(GlobalLock(data));
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        std::memcpy(reinterpret_cast<char*>(df) + sizeof(DROPFILES), path.c_str(), path.size() * sizeof(wchar_t));
        GlobalUnlock(data);
    }
    ~FileDataObject() { GlobalFree(data); }
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDataObject) { *out = this; AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { const ULONG r = --refs; if (!r) delete this; return r; }
    STDMETHODIMP GetData(FORMATETC* f, STGMEDIUM* m) override {
        if (f->cfFormat != CF_HDROP || !(f->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
        const SIZE_T size = GlobalSize(data);
        HGLOBAL copy = GlobalAlloc(GHND, size);
        std::memcpy(GlobalLock(copy), GlobalLock(data), size);
        GlobalUnlock(copy);
        GlobalUnlock(data);
        m->tymed = TYMED_HGLOBAL;
        m->hGlobal = copy;
        m->pUnkForRelease = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* f) override {
        return (f->cfFormat == CF_HDROP && (f->tymed & TYMED_HGLOBAL)) ? S_OK : DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override { out->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        return SHCreateStdEnumFmtEtc(1, &fmt, out);
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
private:
    ULONG refs = 1;
    HGLOBAL data = nullptr;
};

bool startFileDrag(const std::string& utf8Path) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (n <= 0) return false;
    std::wstring path(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, path.data(), n);
    auto* data = new FileDataObject(path);
    auto* source = new FileDropSource();
    DWORD effect = 0;
    ReleaseCapture();
    const HRESULT hr = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
    data->Release();
    source->Release();
    return hr == DRAGDROP_S_DROP || hr == DRAGDROP_S_CANCEL;
}

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
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            ReleaseCapture();
            impl->app->onMouseUp(GET_X_LPARAM(lp) / impl->scale, GET_Y_LPARAM(lp) / impl->scale, msg == WM_LBUTTONUP ? 1 : 3);
            return 0;
        case WM_MOUSEMOVE:
            if (wp & (MK_LBUTTON | MK_RBUTTON)) impl->app->onMouseMove(GET_X_LPARAM(lp) / impl->scale, GET_Y_LPARAM(lp) / impl->scale);
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            SetFocus(hwnd);
            SetCapture(hwnd); // keep receiving the release (and moves) outside the window
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
    OleInitialize(nullptr);
    app.onDragFile = [](const std::string& path) { return startFileDrag(path); };
    DragAcceptFiles(impl->hwnd, TRUE);
    SetTimer(impl->hwnd, TIMER_ID, 33, nullptr);
    return true;
}

void GuiWindow::destroy() {
    if (!impl) return;
    app.onDragFile = nullptr;
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
