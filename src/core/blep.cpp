#include "blep.hpp"
#include <cmath>
#include <algorithm>

namespace paulascape {

Blep::Blep() {
    reset();
}

void Blep::reset() {
    std::fill(buffer, buffer + BLEP_SIZE, 0.0f);
    bufferIndex = 0;
}

void Blep::addBlep(double offset, float delta) {
    if (std::abs(delta) < 1e-6f) return;

    for (int i = 0; i < 32; ++i) {
        int idx = (bufferIndex + i) % BLEP_SIZE;
        double t = (i + (1.0 - offset)) / 32.0;
        if (t < 1.0) {
            double sinc = std::sin(3.14159265358979323846 * t) / (3.14159265358979323846 * t + 1e-9);
            double window = 0.5 * (1.0 + std::cos(3.14159265358979323846 * t));
            float val = delta * static_cast<float>(sinc * window - 1.0);
            buffer[idx] += val;
        }
    }
}

float Blep::getSample() {
    float val = buffer[bufferIndex];
    buffer[bufferIndex] = 0.0f;
    bufferIndex = (bufferIndex + 1) % BLEP_SIZE;
    return val;
}

} // namespace paulascape
