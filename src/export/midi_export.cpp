#include "midi_export.hpp"
#include "song_unroll.hpp"
#include "core/fx_util.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace paulascape {

namespace {

constexpr int TPQN = 96;

void writeBE16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v));
}

void writeBE32(std::vector<uint8_t>& buf, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) buf.push_back(static_cast<uint8_t>(v >> s));
}

void writeVarLen(std::vector<uint8_t>& buf, uint32_t val) {
    uint8_t tmp[5];
    int n = 0;
    tmp[n++] = val & 0x7F;
    while ((val >>= 7) > 0) tmp[n++] = static_cast<uint8_t>(0x80 | (val & 0x7F));
    while (n > 0) buf.push_back(tmp[--n]);
}

int periodToMidiKey(uint16_t period) {
    if (period == 0) return -1;
    return 36 + fx::periodIndex(period); // C-1 = MIDI 36, C-3 = 60
}

// A track under construction: events with absolute ticks, encoded to deltas at the end.
struct Event {
    uint32_t tick;
    int priority; // at equal ticks: note-offs, then program/CC, then note-ons, then trailing CCs
    size_t seq;
    std::vector<uint8_t> bytes;
};

class Track {
public:
    void add(uint32_t tick, int priority, std::vector<uint8_t> bytes) {
        events.push_back({tick, priority, events.size(), std::move(bytes)});
    }
    void meta(uint32_t tick, uint8_t type, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> b{0xFF, type};
        writeVarLen(b, static_cast<uint32_t>(data.size()));
        b.insert(b.end(), data.begin(), data.end());
        add(tick, 1, std::move(b));
    }
    void finish(std::vector<uint8_t>& out, uint32_t endTick) {
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.seq < b.seq;
        });
        std::vector<uint8_t> body;
        uint32_t last = 0;
        for (const Event& e : events) {
            writeVarLen(body, e.tick - last);
            last = e.tick;
            body.insert(body.end(), e.bytes.begin(), e.bytes.end());
        }
        writeVarLen(body, std::max(endTick, last) - last);
        body.push_back(0xFF); body.push_back(0x2F); body.push_back(0x00);

        out.push_back('M'); out.push_back('T'); out.push_back('r'); out.push_back('k');
        writeBE32(out, static_cast<uint32_t>(body.size()));
        out.insert(out.end(), body.begin(), body.end());
    }
private:
    std::vector<Event> events;
};

void header(std::vector<uint8_t>& buf, uint16_t tracks) {
    buf.push_back('M'); buf.push_back('T'); buf.push_back('h'); buf.push_back('d');
    writeBE32(buf, 6);
    writeBE16(buf, 1);
    writeBE16(buf, tracks);
    writeBE16(buf, TPQN);
}

void tempoEvent(Track& t, uint32_t tick, uint32_t micros) {
    t.meta(tick, 0x51, {static_cast<uint8_t>(micros >> 16), static_cast<uint8_t>(micros >> 8), static_cast<uint8_t>(micros)});
}

void nameEvent(Track& t, const std::string& name) {
    t.meta(0, 0x03, std::vector<uint8_t>(name.begin(), name.end()));
}

bool writeFile(const std::vector<uint8_t>& buf, const std::string& path) {
    if (buf.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return out.good();
}

uint8_t velocityFor(int volume) {
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(volume * 127.0 / 64.0)), 1, 127));
}

} // namespace

std::vector<uint8_t> MidiExporter::generatePatternClip(const Module& mod, int patternBaseNote, int rowsPerBeat) {
    if (mod.songLength == 0 || mod.numPatterns == 0) return {};
    // The plugin scales MOD time by host tempo / starting tempo, so in host beats a pattern lasts as long as it
    // does at the MOD's own start tempo: tick lengths use the starting tempo and never change with later Fxx.
    const int startBpm = mod.initialBPM;
    Track t;
    nameEvent(t, mod.title.empty() ? "Paulascape" : mod.title);
    // musical tempo: BPM * 6 / speed at 4 rows per beat, scaled for other rows per beat
    const double musicalBpm = startBpm * 6.0 / std::max<int>(mod.initialSpeed, 1) * 4.0 / rowsPerBeat;
    tempoEvent(t, 0, static_cast<uint32_t>(std::lround(60000000.0 / musicalBpm)));

    uint32_t tick = 0;
    for (size_t o = 0; o < mod.songLength; ++o) {
        const int pat = mod.orderList[o];
        if (pat >= mod.numPatterns) continue;
        // Duration at native tempo, expressed in starting-tempo ticks: sum of (speed / bpm) scaled by the start bpm
        const auto rows = SongUnroller::unrollPattern(mod, pat);
        double ticks = 0;
        for (const auto& r : rows) {
            const double seconds = r.speed / (0.4 * r.bpm);
            const double startSeconds = mod.initialSpeed / (0.4 * mod.initialBPM);
            ticks += (TPQN / rowsPerBeat) * seconds / startSeconds; // a row at the start tempo is 1/rowsPerBeat of a beat
        }
        const uint32_t len = static_cast<uint32_t>(std::lround(ticks));
        const int key = patternBaseNote + pat;
        if (key > 127 || len == 0) continue;
        t.add(tick, 2, {0x90, static_cast<uint8_t>(key), 100});
        t.add(tick + len, 0, {0x80, static_cast<uint8_t>(key), 0});
        tick += len;
    }

    std::vector<uint8_t> buf;
    header(buf, 1);
    t.finish(buf, tick);
    return buf;
}

bool MidiExporter::exportPatternClip(const Module& mod, const std::string& outputPath, int patternBaseNote, int rowsPerBeat) {
    return writeFile(generatePatternClip(mod, patternBaseNote, rowsPerBeat), outputPath);
}

std::vector<uint8_t> MidiExporter::generateMidiBuffer(const Module& mod, int patternIndex, int rowsPerBeat) {
    const auto rows = patternIndex >= 0 ? SongUnroller::unrollPattern(mod, patternIndex) : SongUnroller::unrollSong(mod);
    if (rows.empty()) return {};

    Track conductor;
    nameEvent(conductor, mod.title.empty() ? "Paulascape" : mod.title);
    std::array<Track, 4> tracks;

    // Tempo map: only Fxx BPM changes matter, speed changes alter row lengths instead.
    int lastBpm = -1;
    uint32_t tick = 0;
    std::vector<uint32_t> rowStart(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        rowStart[i] = tick;
        if (rows[i].bpm != lastBpm) {
            tempoEvent(conductor, tick, SongUnroller::microsPerQuarter(rows[i].bpm, rowsPerBeat));
            lastBpm = rows[i].bpm;
        }
        tick += SongUnroller::rowTicks(rows[i].speed, rowsPerBeat);
    }
    const uint32_t endTick = tick;

    for (size_t ch = 0; ch < 4; ++ch) {
        Track& trk = tracks[ch];
        const uint8_t chan = static_cast<uint8_t>(ch);
        nameEvent(trk, "MOD channel " + std::to_string(ch + 1));

        int activeKey = -1;
        int sample = 0;      // sample currently selected on this track
        int volume = 64;     // current channel volume (Cxx and sample default)
        bool fxActive = false;

        auto cc = [&](uint32_t t, int prio, uint8_t num, uint8_t val) {
            trk.add(t, prio, {static_cast<uint8_t>(0xB0 | chan), num, val});
        };
        auto fxTriple = [&](uint32_t t, uint8_t number, uint8_t param) {
            cc(t, 1, 20, number);
            cc(t, 1, 21, (param >> 4) & 0x0F);
            cc(t, 1, 22, param & 0x0F);
        };

        for (size_t i = 0; i < rows.size(); ++i) {
            const UnrolledRow& ur = rows[i];
            if (ur.delayRepeat) continue; // time only
            const NoteCell& cell = mod.patterns[ur.pattern][ur.row][ch];
            const uint32_t T = rowStart[i];
            const uint32_t rowLen = SongUnroller::rowTicks(ur.speed, rowsPerBeat);
            const int x = cell.param >> 4, y = cell.param & 15;
            const bool ext = cell.effect == 0x0E;
            const bool hasNote = cell.period > 0;
            const bool tonePorta = cell.effect == 0x03 || cell.effect == 0x05;

            if (cell.sample > 0 && cell.sample <= 31) {
                if (cell.sample != sample) {
                    sample = cell.sample;
                    trk.add(T, 1, {static_cast<uint8_t>(0xC0 | chan), static_cast<uint8_t>(sample - 1)});
                }
                volume = mod.samples[sample].header.volume;
            }
            if (cell.effect == 0x0C) volume = std::min<int>(cell.param, 64);

            // What goes out as effect-command CCs
            enum class Fx { None, Clear, Send } fx = Fx::None;
            switch (cell.effect) {
                case 0x0B: case 0x0D: case 0x0F: break;                     // timeline only
                case 0x0C: if (!hasNote) fx = Fx::Send; break;              // velocity when a note is played
                case 0x0E:
                    if (x == 0x0) cc(T, 1, 74, y == 0 ? 127 : 0);            // E0x LED filter: 0 = on
                    else if (x == 0x6 || x == 0xE || x == 0xC || x == 0xD || x == 0x8 || x == 0xF) {}  // timeline or note timing
                    else fx = Fx::Send;
                    break;
                case 0x00: if (cell.param != 0) fx = Fx::Send; break;
                default: fx = Fx::Send; break;
            }
            if (fx == Fx::None && fxActive) fx = Fx::Clear;

            if (fx == Fx::Send) {
                const uint8_t number = ext ? static_cast<uint8_t>(16 + x) : cell.effect;
                const uint8_t param = ext ? static_cast<uint8_t>(y) : cell.param;
                // nibbles: for E effects the plugin rebuilds the parameter from the number and y
                fxTriple(T, number, ext ? static_cast<uint8_t>(y) : param);
                fxActive = true;
            } else if (fx == Fx::Clear) {
                fxTriple(T, 0, 0);
                fxActive = false;
            }

            if (hasNote) {
                const int key = periodToMidiKey(cell.period);
                uint32_t noteTick = T;
                if (ext && x == 0xD && y > 0 && y < ur.speed) noteTick = T + rowLen * y / ur.speed;
                const uint8_t vel = velocityFor(volume);
                if (tonePorta && activeKey != -1) {
                    // Overlapping note with legato on: the voice changes pitch instead of restarting
                    const int glide = cell.param ? std::clamp(128 - 2 * cell.param, 1, 127) : 64;
                    cc(T, 1, 68, 127);
                    cc(T, 1, 5, static_cast<uint8_t>(glide));
                    trk.add(T, 2, {static_cast<uint8_t>(0x90 | chan), static_cast<uint8_t>(key), vel});
                    cc(T, 3, 68, 0);
                } else {
                    if (activeKey != -1) trk.add(noteTick, 0, {static_cast<uint8_t>(0x80 | chan), static_cast<uint8_t>(activeKey), 0});
                    trk.add(noteTick, 2, {static_cast<uint8_t>(0x90 | chan), static_cast<uint8_t>(key), vel});
                }
                activeKey = key;
            }

            // ECx note cut
            if (ext && x == 0xC && activeKey != -1 && y < ur.speed) {
                trk.add(T + rowLen * y / ur.speed, 0, {static_cast<uint8_t>(0x80 | chan), static_cast<uint8_t>(activeKey), 0});
                activeKey = -1;
            }
        }
        if (activeKey != -1) trk.add(endTick, 0, {static_cast<uint8_t>(0x80 | chan), static_cast<uint8_t>(activeKey), 0});
    }

    std::vector<uint8_t> buf;
    header(buf, 5);
    conductor.finish(buf, endTick);
    for (auto& t : tracks) t.finish(buf, endTick);
    return buf;
}

bool MidiExporter::exportPatternToMidi(const Module& mod, int patternIndex, const std::string& outputPath) {
    return writeFile(generateMidiBuffer(mod, patternIndex), outputPath);
}

bool MidiExporter::exportSongToMidi(const Module& mod, const std::string& outputPath) {
    return writeFile(generateMidiBuffer(mod, -1), outputPath);
}

} // namespace paulascape
