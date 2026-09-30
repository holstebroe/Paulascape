#pragma once

#include <cstdint>

namespace paulascape {

constexpr int BLEP_SIZE = 4096;

class Blep {
public:
    Blep();
    void reset();
    void addBlep(double offset, float delta);
    float getSample();

private:
    float buffer[BLEP_SIZE] = {0.0f};
    int bufferIndex = 0;
};

} // namespace paulascape
