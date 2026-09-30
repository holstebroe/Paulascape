#pragma once

#include <cstdint>
#include <array>

namespace paulascape::pt2 {

// ProTracker period tables
extern const uint16_t periodTable[37];
extern const uint16_t periodTableFinetune[16][37];
extern const uint8_t sintab[31];

// Paula clock constants
constexpr double PAULA_PAL_CLK = 7093789.2;
constexpr double PAULA_NTSC_CLK = 7159090.5;
constexpr double PAULA_PAL_C3_FREQ = 16574.37; // PAL C-3 frequency (period 214)

// Period to frequency / rate helper
double periodToSampleRate(uint16_t period, bool isNTSC = false);
uint16_t noteToPeriod(int note, int8_t finetune = 0);

} // namespace paulascape::pt2
