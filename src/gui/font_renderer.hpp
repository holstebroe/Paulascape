#pragma once

#include "framebuffer.hpp"
#include <string>

namespace paulascape {

class FontRenderer {
public:
    static void drawChar(Framebuffer& fb, int x, int y, char c, uint32_t color, int scale = 1);
    static void drawString(Framebuffer& fb, int x, int y, const std::string& text, uint32_t color, int scale = 1);
};

} // namespace paulascape
