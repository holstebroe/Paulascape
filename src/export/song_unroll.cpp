#include "song_unroll.hpp"
#include <algorithm>
#include <array>
#include <bitset>

namespace paulascape {

namespace {

struct Walker {
    const Module& mod;
    bool songMode;
    std::vector<UnrolledRow> rows;
    int speed;
    int bpm;
    std::array<std::bitset<64>, 128> visited;
    std::array<uint8_t, 4> loopRow{};
    std::array<uint8_t, 4> loopCount{};

    Walker(const Module& m, bool song) : mod(m), songMode(song), speed(m.initialSpeed), bpm(m.initialBPM) {}

    void run(int order, int pattern) {
        int row = 0;
        int guard = 0;
        while (guard++ < 200000) {
            if (pattern >= mod.numPatterns) return;
            if (songMode) {
                if (visited[order][row]) return; // the song loops back here
                visited[order][row] = true;
            }

            const PatternRow& cells = mod.patterns[pattern][row];
            int jumpOrder = -1, breakRow = -1, loopJump = -1, delay = 0;
            for (size_t ch = 0; ch < 4; ++ch) {
                const NoteCell& c = cells[ch];
                const int x = c.param >> 4, y = c.param & 15;
                switch (c.effect) {
                    case 0x0B: jumpOrder = c.param; break;
                    case 0x0D: breakRow = (x * 10 + y) > 63 ? 0 : x * 10 + y; break;
                    case 0x0F:
                        if (c.param > 0) {
                            if (c.param < 32) speed = c.param; else bpm = c.param;
                        }
                        break;
                    case 0x0E:
                        if (x == 0x6) {
                            if (y == 0) {
                                loopRow[ch] = static_cast<uint8_t>(row);
                            } else {
                                if (loopCount[ch] == 0) loopCount[ch] = static_cast<uint8_t>(y); else --loopCount[ch];
                                if (loopCount[ch] != 0) loopJump = loopRow[ch];
                            }
                        } else if (x == 0xE && delay == 0) {
                            delay = y;
                        }
                        break;
                    default: break;
                }
            }

            UnrolledRow r;
            r.order = order; r.pattern = pattern; r.row = row; r.speed = speed; r.bpm = bpm;
            rows.push_back(r);
            for (int d = 0; d < delay; ++d) {
                UnrolledRow rep = r;
                rep.delayRepeat = true;
                rows.push_back(rep);
            }

            int nextRow = row + 1;
            int nextOrder = order;
            bool newPattern = false;
            if (jumpOrder >= 0 || breakRow >= 0) {
                if (songMode) {
                    nextOrder = jumpOrder >= 0 ? jumpOrder : order + 1;
                    nextRow = breakRow >= 0 ? breakRow : 0;
                    newPattern = true;
                } else {
                    if (breakRow >= 0) return; // Dxx shortens the looping pattern
                }
            } else if (loopJump >= 0) {
                // forget the rows of the loop body so the repeat is not taken for a song loop
                for (int rr = loopJump; rr <= row; ++rr) visited[order][rr] = false;
                nextRow = loopJump;
            }

            if (nextRow >= 64 && !newPattern) {
                if (!songMode) return;
                nextRow = 0;
                nextOrder = order + 1;
                newPattern = true;
            }
            if (newPattern) {
                if (nextOrder >= mod.songLength) return; // end of the song
                order = nextOrder;
                pattern = mod.orderList[order];
            }
            row = nextRow;
        }
    }
};

} // namespace

std::vector<UnrolledRow> SongUnroller::unrollSong(const Module& mod) {
    Walker w(mod, true);
    if (mod.songLength > 0 && mod.numPatterns > 0) w.run(0, mod.orderList[0]);
    return w.rows;
}

std::vector<UnrolledRow> SongUnroller::unrollPattern(const Module& mod, int pattern) {
    Walker w(mod, false);
    if (pattern >= 0 && pattern < mod.numPatterns) w.run(0, pattern);
    return w.rows;
}

} // namespace paulascape
