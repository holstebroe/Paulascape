#include "gui/framebuffer.hpp"
#include "gui/font_renderer.hpp"
#include "gui/scopes.hpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Testing Framebuffer, FontRenderer, and Scopes..." << std::endl;

    paulascape::Framebuffer fb(640, 400);
    assert(fb.getWidth() == 640);
    assert(fb.getHeight() == 400);

    fb.clear(0xFF000000);
    fb.fillRect(10, 10, 50, 50, 0xFFFFFFFF);

    paulascape::FontRenderer::drawString(fb, 20, 20, "Paulascape", 0xFF00FF00, 1);

    paulascape::Scopes scopes;
    scopes.updateSample(0, 0.5f);
    scopes.draw(fb, 10, 300, 620, 80);

    assert(fb.getPixels()[0] == 0xFF000000);

    std::cout << "GUI Render Engine Test Passed Successfully!" << std::endl;
    return 0;
}
