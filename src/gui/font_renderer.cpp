#include "font_renderer.hpp"
#include "core/pt2/pt2_font.hpp"

namespace paulascape {

void FontRenderer::drawChar(Framebuffer& fb, int x, int y, char c, uint32_t color, int scale) {
    uint8_t ch = static_cast<uint8_t>(c);
    if (ch >= 128) ch = '?';

    const uint8_t* rows = pt2::fontData[ch];
    for (int r = 0; r < 8; ++r) {
        uint8_t rowByte = rows[r];
        for (int col = 0; col < 8; ++col) {
            if (rowByte & (1 << (7 - col))) {
                if (scale == 1) {
                    fb.drawPixel(x + col, y + r, color);
                } else {
                    fb.fillRect(x + col * scale, y + r * scale, scale, scale, color);
                }
            }
        }
    }
}

void FontRenderer::drawString(Framebuffer& fb, int x, int y, const std::string& text, uint32_t color, int scale) {
    int curX = x;
    for (char c : text) {
        drawChar(fb, curX, y, c, color, scale);
        curX += 8 * scale;
    }
}

} // namespace paulascape
