#pragma once

#include <cstdint>

namespace paulascape {

// Band-limited step synthesis by aciddose, as used in pt2-clone (BSD-3-Clause, see THIRD_PARTY_LICENSES.md).
constexpr int BLEP_ZC = 16;  // zero crossings
constexpr int BLEP_OS = 16;  // oversampling in the table
constexpr int BLEP_SP = 16;  // step size per output sample
constexpr int BLEP_NS = BLEP_ZC * BLEP_OS / BLEP_SP; // samples of impulse inserted
constexpr int BLEP_RNS = 31; // lowest power of two above NS, minus one

class Blep {
public:
    Blep() { reset(); }
    void reset();
    // offset 0..1 within the output sample where the step happened; amplitude = old value - new value
    void add(float offset, float amplitude);
    // adds the pending BLEP tail to the input sample
    float run(float input);

    int samplesLeft = 0;
    float lastValue = 0.0f;

private:
    int index = 0;
    float buffer[BLEP_RNS + 1] = {};
};

} // namespace paulascape
