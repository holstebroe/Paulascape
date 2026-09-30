#pragma once

#include "framebuffer.hpp"
#include "scopes.hpp"
#include "clap/paulascape_plugin.hpp"
#include <functional>
#include <string>
#include <vector>

namespace paulascape {

// Platform independent ProTracker-style UI. The native window feeds it input and
// blits the framebuffer it renders into.
class UiApp {
public:
    static constexpr int WIDTH = 640;
    static constexpr int HEIGHT = 400;

    explicit UiApp(PaulascapePlugin& plugin);

    void render(Framebuffer& fb);

    // Coordinates are in UI pixels (window pixels divided by the scale).
    void onMouseDown(int x, int y, int button, bool shift); // button: 1 left, 3 right
    void onWheel(int x, int y, int delta);                  // delta > 0 scrolls up
    void onFileDropped(int x, int y, const std::string& path);

    int scale() const { return uiScale; }
    void setScale(int s) { uiScale = s < 1 ? 1 : (s > 3 ? 3 : s); }
    std::function<void(int)> onScaleChanged;

private:
    enum class Screen { Front, Settings, MidiMap };

    struct Hit {
        int x, y, w, h;
        std::function<void(int button, bool shift)> action;
        std::function<void(int delta)> wheel;
    };

    PaulascapePlugin& plugin;
    Scopes scopes;
    Screen screen = Screen::Front;
    int scroll = 0;
    int uiScale = 1;
    std::string status;
    UiSnapshot snap;
    std::vector<Hit> hits;

    void drawFront(Framebuffer& fb);
    void drawSettings(Framebuffer& fb);
    void drawMidiMap(Framebuffer& fb);
    void drawSampleMatrix(Framebuffer& fb);
    void drawPatternList(Framebuffer& fb);

    void button(Framebuffer& fb, int x, int y, int w, int h, const std::string& label, bool active,
                std::function<void(int, bool)> action);
    void addHit(int x, int y, int w, int h, std::function<void(int, bool)> action, std::function<void(int)> wheel = {});
    int visibleRows() const { return 16; }
    int totalRows() const;
    void clampScroll();
    void loadModDialog();
    void importWavDialog();
    void exportMidiDialog();
    void cycleParam(clap_id id, int button, bool shift);
};

} // namespace paulascape
