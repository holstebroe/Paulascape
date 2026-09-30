#pragma once

#include "ui_app.hpp"
#include <memory>

namespace paulascape {

// Native window hosting a UiApp: X11 on Linux, Win32 on Windows. Other platforms
// report no support, so the plugin then offers no GUI.
class GuiWindow {
public:
    explicit GuiWindow(UiApp& app);
    ~GuiWindow();

    static bool isSupported();
    static const char* clapApi(); // CLAP_WINDOW_API_* for this platform, or nullptr

    // parent: X11 Window id or HWND of the host window; 0 creates a top-level window.
    bool create(uintptr_t parent);
    void destroy();
    void show();
    void hide();
    void setScale(int scale);
    void pixelSize(uint32_t& w, uint32_t& h) const;

    struct Impl; // public so the platform callbacks can name it

private:
    std::unique_ptr<Impl> impl;
    UiApp& app;
};

} // namespace paulascape
