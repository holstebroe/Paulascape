#include "front_page.hpp"
#include "font_renderer.hpp"
#include <iomanip>
#include <sstream>

namespace paulascape {

FrontPage::FrontPage() = default;

void FrontPage::draw(Framebuffer& fb, const Module& mod, PlaybackMode mainMode, PlaybackMode subMode, Scopes& scopes) {
    fb.clear(0xFF333333); // ProTracker grey background

    // Top Bar
    fb.fillRect(0, 0, fb.getWidth(), 30, 0xFF444444);
    fb.drawRect(0, 0, fb.getWidth(), 30, 0xFF666666);

    std::string titleStr = "Song: " + (mod.title.empty() ? "Untitled MOD" : mod.title);
    FontRenderer::drawString(fb, 10, 10, titleStr, 0xFFFFFFFF, 1);

    // Mode Switches
    std::string modeStr = (mainMode == PlaybackMode::Pattern) ? "[Pattern]" : "[Instrument]";
    FontRenderer::drawString(fb, 350, 10, modeStr, 0xFF00FF00, 1);

    FontRenderer::drawString(fb, 520, 10, "[MIDI]", 0xFFFFFF00, 1);
    FontRenderer::drawString(fb, 600, 10, "(*)", 0xFFFFFFFF, 1); // Gear icon for settings

    // Sub-mode Row
    fb.fillRect(0, 32, fb.getWidth(), 25, 0xFF3D3D3D);
    std::string subStr = "[Single] [Multi] [Drum]";
    FontRenderer::drawString(fb, 10, 38, subStr, 0xFFCCCCCC, 1);
    FontRenderer::drawString(fb, 260, 38, "Drop a WAV on a slot to replace it", 0xFF888888, 1);

    // Sample Matrix Header
    fb.fillRect(10, 65, fb.getWidth() - 20, 20, 0xFF222222);
    FontRenderer::drawString(fb, 15, 70, "#   Sample name          Loop   Vol   Fine        In key   Out key", 0xFFAAAAAA, 1);

    // Sample Rows (up to 12 visible)
    int rowY = 90;
    for (uint8_t i = 1; i <= 12; ++i) {
        const auto& smp = mod.samples[i];

        std::stringstream ss;
        ss << std::setw(2) << std::setfill('0') << (int)i << "  ";
        std::string name = smp.header.name.empty() ? "---" : smp.header.name;
        if (name.size() > 20) name = name.substr(0, 20);
        while (name.size() < 20) name += " ";

        ss << name << " ";
        ss << (smp.header.loopEnabled ? "on  " : "off ");
        ss << std::setw(3) << (int)smp.header.volume << "   ";
        ss << std::setw(2) << (int)smp.header.finetune << "          C-1      C-3";

        uint32_t color = (i == 1) ? 0xFF00FFFF : 0xFFDDDDDD;
        FontRenderer::drawString(fb, 15, rowY, ss.str(), color, 1);
        rowY += 16;
    }

    // Scopes Section
    scopes.draw(fb, 10, 300, fb.getWidth() - 20, 90);
}

} // namespace paulascape
