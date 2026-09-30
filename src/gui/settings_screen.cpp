#include "settings_screen.hpp"
#include "font_renderer.hpp"

namespace paulascape {

SettingsScreen::SettingsScreen() = default;

void SettingsScreen::draw(Framebuffer& fb) {
    fb.clear(0xFF2B2B2B);

    // Header Bar
    fb.fillRect(0, 0, fb.getWidth(), 35, 0xFF3C3C3C);
    fb.drawRect(0, 0, fb.getWidth(), 35, 0xFF555555);
    FontRenderer::drawString(fb, 15, 10, "< Back to Front Page", 0xFF00FF00, 1);
    FontRenderer::drawString(fb, 260, 10, "Paulascape Settings", 0xFFFFFFFF, 1);

    // Settings Items
    int y = 55;
    auto drawSetting = [&](const std::string& label, const std::string& val) {
        fb.fillRect(20, y, fb.getWidth() - 40, 26, 0xFF222222);
        fb.drawRect(20, y, fb.getWidth() - 40, 26, 0xFF444444);
        FontRenderer::drawString(fb, 30, y + 8, label, 0xFFCCCCCC, 1);
        FontRenderer::drawString(fb, 380, y + 8, val, 0xFF00FFFF, 1);
        y += 32;
    };

    drawSetting("Output Layout", "Stereo (L R R L hard pan)");
    drawSetting("Resampler Mode", "Authentic (Paula + BLEP)");
    drawSetting("Filter Model", "A500 (4.9 kHz Low-Pass)");
    drawSetting("LED Filter", "Off");
    drawSetting("Stereo Separation", "20%");
    drawSetting("Clock Model", "PAL (7.09 MHz)");
    drawSetting("Pitch Mode", "Period Table (Snaps to PT2 notes)");
    drawSetting("Tempo Sync", "Follow Host (Scaled)");
    drawSetting("Rows Per Beat", "4");
    drawSetting("Pattern Base Note", "C1 (MIDI 24)");
}

} // namespace paulascape
