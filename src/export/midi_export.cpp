#include "midi_export.hpp"
#include <fstream>
#include <algorithm>

namespace paulascape {

static void writeBE16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

static void writeBE32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

static void writeVarLen(std::vector<uint8_t>& buf, uint32_t val) {
    uint32_t buffer = val & 0x7F;
    while ((val >>= 7) > 0) {
        buffer <<= 8;
        buffer |= 0x80 | (val & 0x7F);
    }
    while (true) {
        buf.push_back(static_cast<uint8_t>(buffer & 0xFF));
        if (buffer & 0x80) {
            buffer >>= 8;
        } else {
            break;
        }
    }
}

static int periodToMidiKey(uint16_t period) {
    if (period == 0) return -1;
    // Standard ProTracker note periods
    static const uint16_t periods[36] = {
        856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, // C-1 .. B-1
        428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, // C-2 .. B-2
        214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113  // C-3 .. B-3
    };

    int bestIdx = 0;
    int minDiff = 999999;
    for (int i = 0; i < 36; ++i) {
        int diff = std::abs(static_cast<int>(periods[i]) - static_cast<int>(period));
        if (diff < minDiff) {
            minDiff = diff;
            bestIdx = i;
        }
    }
    return 36 + bestIdx; // C-1 = MIDI 36
}

bool MidiExporter::exportPatternToMidi(const Module& mod, int patternIndex, const std::string& outputPath) {
    auto buf = generateMidiBuffer(mod, patternIndex);
    if (buf.empty()) return false;
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    return true;
}

bool MidiExporter::exportSongToMidi(const Module& mod, const std::string& outputPath) {
    auto buf = generateMidiBuffer(mod, -1);
    if (buf.empty()) return false;
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    return true;
}

std::vector<uint8_t> MidiExporter::generateMidiBuffer(const Module& mod, int patternIndex) {
    std::vector<uint8_t> midiBuf;

    // Header Chunk
    midiBuf.push_back('M'); midiBuf.push_back('T'); midiBuf.push_back('h'); midiBuf.push_back('d');
    writeBE32(midiBuf, 6);
    writeBE16(midiBuf, 1); // Format 1 (multi-track)
    writeBE16(midiBuf, 5); // 5 tracks: 1 conductor + 4 channels
    writeBE16(midiBuf, 96); // 96 ticks per quarter note

    // Track 0: Conductor (Tempo)
    std::vector<uint8_t> track0;
    // Tempo Meta Event: 125 BPM -> 480,000 microseconds per quarter note
    writeVarLen(track0, 0);
    track0.push_back(0xFF); track0.push_back(0x51); track0.push_back(0x03);
    writeBE32(track0, 480000); // Only lower 3 bytes used
    track0.erase(track0.end() - 4); // Trim to 3 bytes
    // End of Track Meta Event
    writeVarLen(track0, 0);
    track0.push_back(0xFF); track0.push_back(0x2F); track0.push_back(0x00);

    midiBuf.push_back('M'); midiBuf.push_back('T'); midiBuf.push_back('r'); midiBuf.push_back('k');
    writeBE32(midiBuf, static_cast<uint32_t>(track0.size()));
    midiBuf.insert(midiBuf.end(), track0.begin(), track0.end());

    // Tracks 1..4: Channels 0..3
    for (size_t ch = 0; ch < 4; ++ch) {
        std::vector<uint8_t> trk;
        uint32_t deltaTicks = 0;
        int activeKey = -1;

        auto processRowEvent = [&](const NoteCell& cell) {
            if (cell.effect != 0 || cell.param != 0) {
                // Encode MOD effect command on CC 20, 21, 22
                writeVarLen(trk, deltaTicks);
                trk.push_back(0xB0 | static_cast<uint8_t>(ch));
                trk.push_back(20);
                trk.push_back(cell.effect & 0x7F);

                writeVarLen(trk, 0);
                trk.push_back(0xB0 | static_cast<uint8_t>(ch));
                trk.push_back(21);
                trk.push_back((cell.param >> 4) & 0x0F);

                writeVarLen(trk, 0);
                trk.push_back(0xB0 | static_cast<uint8_t>(ch));
                trk.push_back(22);
                trk.push_back(cell.param & 0x0F);

                deltaTicks = 0;
            }

            if (cell.period > 0) {
                if (activeKey != -1) {
                    // Note off
                    writeVarLen(trk, deltaTicks);
                    trk.push_back(0x80 | static_cast<uint8_t>(ch));
                    trk.push_back(static_cast<uint8_t>(activeKey));
                    trk.push_back(0);
                    deltaTicks = 0;
                }

                activeKey = periodToMidiKey(cell.period);
                if (activeKey != -1) {
                    writeVarLen(trk, deltaTicks);
                    trk.push_back(0x90 | static_cast<uint8_t>(ch));
                    trk.push_back(static_cast<uint8_t>(activeKey));
                    trk.push_back(100); // Default velocity
                    deltaTicks = 0;
                }
            }

            deltaTicks += 24; // Each row is 24 ticks (1/16th note at 96 TPQN)
        };

        if (patternIndex >= 0 && patternIndex < mod.numPatterns) {
            for (size_t r = 0; r < 64; ++r) {
                processRowEvent(mod.patterns[patternIndex][r][ch]);
            }
        } else {
            for (size_t o = 0; o < mod.songLength; ++o) {
                uint8_t pat = mod.orderList[o];
                if (pat < mod.numPatterns) {
                    for (size_t r = 0; r < 64; ++r) {
                        processRowEvent(mod.patterns[pat][r][ch]);
                    }
                }
            }
        }

        if (activeKey != -1) {
            writeVarLen(trk, deltaTicks);
            trk.push_back(0x80 | static_cast<uint8_t>(ch));
            trk.push_back(static_cast<uint8_t>(activeKey));
            trk.push_back(0);
            deltaTicks = 0;
        }

        // End of Track
        writeVarLen(trk, deltaTicks);
        trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0x00);

        midiBuf.push_back('M'); midiBuf.push_back('T'); midiBuf.push_back('r'); midiBuf.push_back('k');
        writeBE32(midiBuf, static_cast<uint32_t>(trk.size()));
        midiBuf.insert(midiBuf.end(), trk.begin(), trk.end());
    }

    return midiBuf;
}

} // namespace paulascape
