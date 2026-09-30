#pragma once

#include "framebuffer.hpp"
#include <array>

namespace paulascape {

class Scopes {
public:
    Scopes();
    void updateSample(size_t ch, float sample);
    void draw(Framebuffer& fb, int x, int y, int w, int h);

private:
    std::array<std::vector<float>, 4> history;
    std::array<size_t, 4> writeIdx{0, 0, 0, 0};
};

} // namespace paulascape
