#pragma once

#include <cstdint>
#include <vector>

namespace paulascape {

class Framebuffer {
public:
    Framebuffer(uint32_t width = 640, uint32_t height = 400);

    void resize(uint32_t width, uint32_t height);
    void clear(uint32_t color = 0xFF222222);

    void drawPixel(int x, int y, uint32_t color);
    void drawRect(int x, int y, int w, int h, uint32_t color);
    void fillRect(int x, int y, int w, int h, uint32_t color);
    void drawLine(int x1, int y1, int x2, int y2, uint32_t color);

    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    const uint32_t* getPixels() const { return pixels.data(); }
    uint32_t* getPixels() { return pixels.data(); }

private:
    uint32_t width = 640;
    uint32_t height = 400;
    std::vector<uint32_t> pixels;
};

} // namespace paulascape
