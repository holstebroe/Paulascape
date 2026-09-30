#pragma once

#include "core/mod_loader.hpp"
#include <vector>

namespace paulascape {

// One played row of a song, in playing order, after following Bxx, Dxx, E6x loops and EEx delays.
struct UnrolledRow {
    int order = 0;
    int pattern = 0;
    int row = 0;
    int speed = 6;           // ticks in this row
    int bpm = 125;           // tempo in effect while the row plays
    bool delayRepeat = false;// repeat caused by EEx: time passes, no new notes
};

class SongUnroller {
public:
    // The whole song from order 0, up to its end or the first point it loops back to.
    static std::vector<UnrolledRow> unrollSong(const Module& mod);
    // One pattern played once from row 0, honouring Dxx and E6x/EEx inside it (Bxx is ignored, as in pattern mode).
    static std::vector<UnrolledRow> unrollPattern(const Module& mod, int pattern);

    // Length of a row in MIDI ticks at 96 ticks per quarter note.
    static int rowTicks(int speed, int rowsPerBeat) { return 16 * speed / rowsPerBeat; }
    // MIDI tempo (microseconds per quarter note) that makes those rows last as long as ProTracker plays them.
    static uint32_t microsPerQuarter(int bpm, int rowsPerBeat) { return static_cast<uint32_t>(15000000.0 * rowsPerBeat / bpm); }
};

} // namespace paulascape
