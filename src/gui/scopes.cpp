#include "scopes.hpp"
#include "font_renderer.hpp"
#include <algorithm>

namespace paulascape {

Scopes::Scopes() {
    for (size_t c = 0; c < 4; ++c) {
        history[c].resize(120, 0.0f);
    }
}

void Scopes::updateSample(size_t ch, float sample) {
    ch &= 3;
    history[ch][writeIdx[ch]] = sample;
    writeIdx[ch] = (writeIdx[ch] + 1) % history[ch].size();
}

void Scopes::draw(Framebuffer& fb, int x, int y, int w, int h) {
    int boxW = (w - 15) / 4;
    uint32_t bg = 0xFF111111;
    uint32_t border = 0xFF666666;
    uint32_t traceColor = 0xFF00FF00; // Classic Amiga green scope

    for (size_t c = 0; c < 4; ++c) {
        int bx = x + static_cast<int>(c) * (boxW + 5);
        fb.fillRect(bx, y, boxW, h, bg);
        fb.drawRect(bx, y, boxW, h, border);

        std::string title = "Ch " + std::to_string(c + 1);
        FontRenderer::drawString(fb, bx + 4, y + 2, title, 0xFFAAAAAA, 1);

        int midY = y + h / 2 + 4;
        const auto& hist = history[c];
        size_t hSize = hist.size();

        for (int px = 0; px < boxW - 4; ++px) {
            size_t idx = (writeIdx[c] + px) % hSize;
            float val = hist[idx];
            int py = midY - static_cast<int>(val * (h / 3.0f));
            py = std::clamp(py, y + 12, y + h - 2);
            fb.drawPixel(bx + 2 + px, py, traceColor);
        }
    }
}

} // namespace paulascape
