#pragma once

#include "pt2/pt2_tables.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Helpers shared by the pattern replayer and the MIDI-driven voice effects.
namespace paulascape::fx {

constexpr int PERIOD_MIN = 113;
constexpr int PERIOD_MAX = 856;

inline const uint8_t VIBRATO_TABLE[32] = {
    0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24};

// Index of a base (finetune 0) period in the note table, nearest match.
inline int periodIndex(uint16_t period) {
    int best = 0;
    int bestDiff = 1 << 30;
    for (int i = 0; i < 36; ++i) {
        const int d = std::abs(static_cast<int>(pt2::periodTableFinetune[0][i]) - static_cast<int>(period));
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

// Finetune -8..7 selects table row 8..15 for the negative values.
inline const uint16_t* fineRow(int8_t finetune) { return pt2::periodTableFinetune[finetune & 15]; }

// Index of the nearest note at or below `period` in a finetune row (ProTracker's scan)
inline int findInRow(const uint16_t* row, uint16_t period) {
    int i = 0;
    while (i < 36 && row[i] > period) ++i;
    return i;
}

// Vibrato/tremolo waveform value 0..255 at a position 0..63 (bit 5 = negative half)
inline int waveValue(uint8_t wave, uint8_t pos) {
    const uint8_t p = pos & 0x1F;
    switch (wave & 3) {
        case 0: return VIBRATO_TABLE[p];
        case 1: {
            int v = p * 8;
            if (pos & 0x20) v = 255 - v;
            return v;
        }
        default: return 255;
    }
}

} // namespace paulascape::fx
