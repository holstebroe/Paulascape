#include "framebuffer.hpp"
#include <algorithm>
#include <cmath>

namespace paulascape {

Framebuffer::Framebuffer(uint32_t width, uint32_t height) {
    resize(width, height);
}

void Framebuffer::resize(uint32_t w, uint32_t h) {
    width = w;
    height = h;
    pixels.assign(width * height, 0xFF222222);
}

void Framebuffer::clear(uint32_t color) {
    std::fill(pixels.begin(), pixels.end(), color);
}

void Framebuffer::drawPixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= static_cast<int>(width) || y < 0 || y >= static_cast<int>(height)) return;
    pixels[y * width + x] = color;
}

void Framebuffer::drawRect(int x, int y, int w, int h, uint32_t color) {
    for (int i = x; i < x + w; ++i) {
        drawPixel(i, y, color);
        drawPixel(i, y + h - 1, color);
    }
    for (int j = y; j < y + h; ++j) {
        drawPixel(x, j, color);
        drawPixel(x + w - 1, j, color);
    }
}

void Framebuffer::fillRect(int x, int y, int w, int h, uint32_t color) {
    int x1 = std::max(0, x);
    int y1 = std::max(0, y);
    int x2 = std::min(static_cast<int>(width), x + w);
    int y2 = std::min(static_cast<int>(height), y + h);

    for (int j = y1; j < y2; ++j) {
        for (int i = x1; i < x2; ++i) {
            pixels[j * width + i] = color;
        }
    }
}

void Framebuffer::drawLine(int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        drawPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

} // namespace paulascape
