// Round trip: a MOD exported as note-by-note MIDI and played by the voice pool in multi-channel mode
// must do what the pattern replayer does with the same patterns. Both are sampled once per MOD tick
// and compared channel by channel: sounding or silent, sample, period, volume, retriggers and sample offset.

#include "export/midi_export.hpp"
#include "export/midi_file_reader.hpp"
#include "export/song_unroll.hpp"
#include "core/fx_util.hpp"
#include "core/replayer.hpp"
#include "core/voice_pool.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace paulascape;
using namespace paulascape::testmidi;

namespace {

constexpr double SR = 44100.0;

// What one MOD channel sounds like at one moment
struct Snap {
    bool sounding = false;
    const ModSample* sample = nullptr;
    uint16_t period = 0;
    uint8_t volume = 0;
    uint32_t triggers = 0;   // trigger count of the voice
    const void* voiceId = nullptr;
    uint32_t offset = 0;
};

Snap snap(const PaulaVoice* v) {
    Snap s;
    if (!v || !v->isActive()) return s;
    s.sounding = v->getVolume() > 0;
    s.sample = v->getSample();
    s.period = v->getPeriod();
    s.volume = v->getVolume();
    s.triggers = v->getTriggerCount();
    s.voiceId = v;
    s.offset = v->getStartOffset();
    return s;
}

// A moment to compare: tick `tick` of unrolled row `row`, 0.6 into the tick (row setup events come 1/4 tick
// ahead of the row, and the voice pool runs its ticks up to half a tick after the replayer)
struct Probe {
    size_t row;
    int tick;
    uint64_t frame;
};

std::vector<Probe> probes(const std::vector<UnrolledRow>& rows) {
    std::vector<Probe> out;
    double t = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const double tickSecs = 2.5 / rows[i].bpm;
        for (int k = 0; k < rows[i].speed; ++k) out.push_back({i, k, static_cast<uint64_t>(std::llround((t + (k + 0.6) * tickSecs) * SR))});
        t += rows[i].speed * tickSecs;
    }
    return out;
}

std::vector<std::array<Snap, 4>> playReplayer(const Module& mod, const std::vector<Probe>& at, int patternKey) {
    Replayer rep;
    rep.setModule(&mod);
    rep.setSampleRate(SR);
    rep.setTempoSyncMode(TempoSyncMode::ModNative);
    rep.patternNoteOn(static_cast<uint8_t>(patternKey));
    std::vector<float> l(4096), r(4096);
    std::vector<std::array<Snap, 4>> out;
    uint64_t pos = 0;
    for (const auto& p : at) {
        while (pos < p.frame) {
            const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(4096, p.frame - pos));
            float* ch[2] = {l.data(), r.data()};
            rep.processAudio(ch, 2, n);
            pos += n;
        }
        std::array<Snap, 4> s;
        for (size_t c = 0; c < 4; ++c) s[c] = snap(&rep.channelVoice(c));
        out.push_back(s);
    }
    return out;
}

// How a host might order events that share a MIDI tick
enum class SameTickOrder {
    File,       // as written in the file
    NotesFirst  // note-offs, then note-ons, then controllers (what many DAWs do with clip contents)
};

std::vector<std::array<Snap, 4>> playMidi(const Module& mod, const Midi& midi, const std::vector<Probe>& at, SameTickOrder order) {
    // Tempo map from the conductor track
    std::vector<std::pair<uint32_t, uint32_t>> tempos;
    for (const auto& e : midi.events)
        if (e.data[0] == 0xFF && e.data[1] == 0x51) tempos.push_back({e.tick, static_cast<uint32_t>((e.data[2] << 16) | (e.data[3] << 8) | e.data[4])});
    std::sort(tempos.begin(), tempos.end());
    auto frameOf = [&](uint32_t tick) {
        double secs = 0;
        uint32_t last = 0, micros = 500000;
        for (const auto& t : tempos) {
            if (t.first >= tick) break;
            secs += static_cast<double>(t.first - last) * micros / 1e6 / midi.division;
            last = t.first;
            micros = t.second;
        }
        secs += static_cast<double>(tick - last) * micros / 1e6 / midi.division;
        return static_cast<uint64_t>(std::llround(secs * SR));
    };
    auto rank = [&](const Ev& e) {
        if (order == SameTickOrder::File) return 0;
        const uint8_t st = e.data[0] & 0xF0;
        if (e.data[0] == 0xFF) return 0;
        if (st == 0x80 || (st == 0x90 && e.data[2] == 0)) return 1;
        if (st == 0x90) return 2;
        return 3;
    };
    std::vector<const Ev*> evs;
    for (const auto& e : midi.events) evs.push_back(&e);
    std::stable_sort(evs.begin(), evs.end(), [&](const Ev* a, const Ev* b) {
        if (a->tick != b->tick) return a->tick < b->tick;
        return rank(*a) < rank(*b);
    });

    VoicePool pool;
    pool.setModule(&mod);
    pool.setSampleRate(SR);
    pool.setPlaybackMode(PlaybackMode::MultiChannel);
    pool.setHostTempo(mod.initialBPM);

    std::vector<float> l(4096), r(4096);
    uint64_t pos = 0;
    auto render = [&](uint64_t upto) {
        while (pos < upto) {
            const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(4096, upto - pos));
            float* ch[2] = {l.data(), r.data()};
            pool.processAudio(ch, 2, n);
            pos += n;
        }
    };
    std::vector<std::array<Snap, 4>> out;
    size_t ei = 0;
    for (const auto& p : at) {
        while (ei < evs.size() && frameOf(evs[ei]->tick) <= p.frame) {
            const Ev& e = *evs[ei++];
            render(frameOf(e.tick));
            const auto& d = e.data;
            if (d[0] == 0xFF) {
                if (d[1] == 0x51) pool.setHostTempo(60000000.0 / ((d[2] << 16) | (d[3] << 8) | d[4]));
                continue;
            }
            const uint8_t ch = d[0] & 15;
            switch (d[0] & 0xF0) {
                case 0x90: pool.noteOn(ch, d[1], d[2]); break;
                case 0x80: pool.noteOff(ch, d[1]); break;
                case 0xB0: pool.controlChange(ch, d[1], d[2]); break;
                case 0xC0: pool.programChange(ch, d[1]); break;
                case 0xE0: pool.setPitchBendValue(ch, d[1] | (d[2] << 7)); break;
                default: break;
            }
        }
        render(p.frame);
        std::array<Snap, 4> s;
        for (size_t c = 0; c < 4; ++c) s[c] = snap(pool.channelVoice(static_cast<uint8_t>(c)));
        out.push_back(s);
    }
    return out;
}

std::string cellText(const NoteCell& c) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%4u %02u %X%02X", c.period, c.sample, c.effect, c.param);
    return buf;
}

std::string snapText(const Snap& s) {
    if (!s.sounding) return "silent";
    char buf[64];
    std::snprintf(buf, sizeof buf, "per %u vol %u trig %u off %u", s.period, s.volume, s.triggers, s.offset);
    return buf;
}

// Compares and reports; returns the number of mismatching channel ticks.
size_t compare(const Module& mod, const std::vector<UnrolledRow>& rows, const std::vector<Probe>& at,
               const std::vector<std::array<Snap, 4>>& want, const std::vector<std::array<Snap, 4>>& got,
               const char* label, size_t maxReport) {
    size_t bad = 0, total = 0, sounding = 0, endJitters = 0;
    std::map<std::string, size_t> byEffect;
    std::array<Snap, 4> prevWant{}, prevGot{};
    for (size_t i = 0; i < at.size(); ++i) {
        const UnrolledRow& ur = rows[at[i].row];
        for (size_t c = 0; c < 4; ++c) {
            const Snap& w = want[i][c];
            const Snap& g = got[i][c];
            // a new voice or a higher trigger count since the last probe means the sample was (re)started
            const bool wTrig = w.sounding && (w.voiceId != prevWant[c].voiceId || w.triggers != prevWant[c].triggers);
            const bool gTrig = g.sounding && (g.voiceId != prevGot[c].voiceId || g.triggers != prevGot[c].triggers);
            // A one-shot sample running out: the voice pool's ticks trail the replayer's by up to half a tick, so
            // with pitch effects the end can fall one probe apart. Allowed once, when both played the same sample.
            const Snap& still = w.sounding ? w : g;
            const bool endJitter = w.sounding != g.sounding && still.sample && !still.sample->header.loopEnabled &&
                                   prevWant[c].sounding && prevGot[c].sounding && prevWant[c].sample == prevGot[c].sample &&
                                   i + 1 < at.size() && !want[i + 1][c].sounding && !got[i + 1][c].sounding;
            prevWant[c] = w;
            prevGot[c] = g;
            ++total;
            sounding += w.sounding ? 1 : 0;
            std::string why;
            if (endJitter) { ++endJitters; continue; }
            if (w.sounding != g.sounding) why = "sounding";
            else if (w.sounding) {
                if (w.sample != g.sample) why = "sample";
                else if (w.period != g.period) why = "period";
                else if (w.volume != g.volume) why = "volume";
                else if (wTrig != gTrig) why = "trigger";
                else if (wTrig && w.offset != g.offset) why = "offset";
            }
            if (why.empty()) continue;
            ++bad;
            // attribute the mismatch to the effect last set on the channel (this row or earlier)
            const NoteCell& cell = mod.patterns[ur.pattern][ur.row][c];
            char fx[16];
            if (cell.effect == 0x0E) std::snprintf(fx, sizeof fx, "E%X", cell.param >> 4);
            else std::snprintf(fx, sizeof fx, "%X", cell.effect);
            ++byEffect[std::string(fx) + "/" + why];
            if (bad <= maxReport) {
                std::cout << "  [" << label << "] order " << ur.order << " pat " << ur.pattern << " row " << ur.row << (ur.delayRepeat ? "(delay)" : "")
                          << " tick " << at[i].tick << " ch " << c << " cell [" << cellText(cell) << "] " << why
                          << ": replayer " << snapText(w) << " | midi " << snapText(g) << "\n";
            }
        }
    }
    std::cout << label << ": " << bad << " of " << total << " channel ticks differ (" << sounding << " sounding";
    if (endJitters) std::cout << ", " << endJitters << " sample end one probe apart";
    std::cout << ")";
    if (!byEffect.empty()) {
        std::cout << " [";
        bool first = true;
        for (const auto& [k, n] : byEffect) { std::cout << (first ? "" : ", ") << k << " x" << n; first = false; }
        std::cout << "]";
    }
    std::cout << std::endl;
    return bad;
}

// Each MOD channel is monophonic: its track must never hold two keys at once, or a host plays a chord where
// the MOD slides. Returns the number of overlaps; counts pitch bends per track in `bends`.
size_t overlaps(const Midi& midi, const char* name, std::array<size_t, 4>& bends) {
    std::vector<const Ev*> evs;
    for (const auto& e : midi.events) evs.push_back(&e);
    std::stable_sort(evs.begin(), evs.end(), [](const Ev* a, const Ev* b) {
        if (a->tick != b->tick) return a->tick < b->tick;
        const bool offA = (a->data[0] & 0xF0) == 0x80, offB = (b->data[0] & 0xF0) == 0x80;
        return offA && !offB; // offs first at a shared tick, as hosts play them
    });
    size_t n = 0;
    std::array<int, 4> held{};
    bends.fill(0);
    for (const Ev* e : evs) {
        if (e->track < 1 || e->track > 4 || e->data[0] == 0xFF) continue;
        const size_t t = static_cast<size_t>(e->track - 1);
        const uint8_t st = e->data[0] & 0xF0;
        if (st == 0x90 && e->data[2] > 0) {
            if (held[t] > 0) {
                ++n;
                std::cout << "  [" << name << "] track " << t + 1 << " holds two keys at MIDI tick " << e->tick << "\n";
            }
            ++held[t];
        } else if (st == 0x80 || st == 0x90) {
            held[t] = std::max(held[t] - 1, 0);
        } else if (st == 0xE0) {
            ++bends[t];
        }
    }
    return n;
}

size_t roundTrip(const Module& mod, int patternIndex, const char* name, size_t maxReport = 40, std::array<size_t, 4>* bendCount = nullptr) {
    const auto rows = patternIndex >= 0 ? SongUnroller::unrollPattern(mod, patternIndex) : SongUnroller::unrollSong(mod);
    assert(!rows.empty());
    const auto at = probes(rows);
    const Midi midi = parse(MidiExporter::generateMidiBuffer(mod, patternIndex));
    const auto want = playReplayer(mod, at, patternIndex >= 0 ? 24 + patternIndex : 12);
    std::array<size_t, 4> bends{};
    size_t bad = overlaps(midi, name, bends);
    if (bendCount) *bendCount = bends;
    for (SameTickOrder o : {SameTickOrder::File, SameTickOrder::NotesFirst}) {
        const std::string label = std::string(name) + (o == SameTickOrder::File ? " (file order)" : " (notes first)");
        bad += compare(mod, rows, at, want, playMidi(mod, midi, at, o), label.c_str(), maxReport);
    }
    return bad;
}

// ---------------------------------------------------------------------------------------------------------------
// A module that exercises every effect the export supports

uint16_t note(const char* n) {
    static const char* names[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    for (int s = 0; s < 12; ++s) {
        if (n[0] == names[s][0] && n[1] == names[s][1]) return pt2::periodTableFinetune[0][(n[2] - '1') * 12 + s];
    }
    assert(false);
    return 0;
}

struct Writer {
    Module& mod;
    int pat;
    int ch;
    void operator()(int row, const char* n, int sample, int fx = 0, int param = 0) {
        NoteCell& c = mod.patterns[pat][row][ch];
        c.period = n ? note(n) : 0;
        c.sample = static_cast<uint8_t>(sample);
        c.effect = static_cast<uint8_t>(fx);
        c.param = static_cast<uint8_t>(param);
    }
};

Module effectModule() {
    Module mod;
    mod.title = "Effects";
    mod.initialSpeed = 6;
    mod.initialBPM = 125;
    mod.numPatterns = 3;
    mod.patterns.resize(3);
    mod.songLength = 3;
    mod.orderList[0] = 0;
    mod.orderList[1] = 1;
    mod.orderList[2] = 2;
    // Looped samples so notes keep sounding; sample 3 has a finetune
    const int vols[4] = {0, 64, 40, 50};
    const int8_t fine[4] = {0, 0, 0, -3};
    for (int i = 1; i <= 3; ++i) {
        auto& s = mod.samples[i];
        s.header.length = 8192;
        s.header.volume = static_cast<uint8_t>(vols[i]);
        s.header.finetune = fine[i];
        s.header.loopStart = 0;
        s.header.loopLength = 8192;
        s.header.loopEnabled = true;
        s.pcmData.resize(8192);
        for (size_t k = 0; k < s.pcmData.size(); ++k) s.pcmData[k] = static_cast<int8_t>((k * (i + 1)) & 0x7F) - 64;
    }

    // Pattern 0, channel 0: pitch effects
    Writer a{mod, 0, 0};
    a(0, "C-2", 1);
    a(1, nullptr, 0, 0x1, 0x03);         // portamento up
    a(2, nullptr, 0, 0x1, 0x02);
    a(3, nullptr, 0, 0x2, 0x04);         // portamento down
    a(5, nullptr, 0, 0x0, 0x37);         // arpeggio
    a(6, nullptr, 0, 0x0, 0x47);
    a(8, "E-2", 0, 0x3, 0x08);           // tone portamento
    a(9, nullptr, 0, 0x3, 0x00);         // ... continued from memory
    a(10, nullptr, 0, 0x3, 0x00);
    a(11, "G-2", 0, 0x3, 0x00);          // new target, speed from memory
    a(12, nullptr, 0, 0x3, 0x00);
    a(13, nullptr, 0, 0x4, 0x48);        // vibrato
    a(14, nullptr, 0, 0x4, 0x00);        // ... continued
    a(15, nullptr, 0, 0x6, 0x01);        // vibrato + volume slide
    a(16, nullptr, 0, 0xE, 0x41);        // vibrato waveform: ramp
    a(17, "C-2", 1, 0x4, 0x37);          // new note, ramp vibrato
    a(18, nullptr, 0, 0x4, 0x00);
    a(19, nullptr, 0, 0xE, 0x40);
    a(20, nullptr, 0, 0xE, 0x13);        // fine portamento up
    a(21, nullptr, 0, 0xE, 0x25);        // fine portamento down
    a(22, nullptr, 0, 0xE, 0x31);        // glissando on
    a(23, "C-3", 0, 0x3, 0x06);          // glissando tone portamento
    a(24, nullptr, 0, 0x3, 0x00);
    a(25, nullptr, 0, 0x3, 0x00);
    a(26, nullptr, 0, 0xE, 0x30);
    a(27, "A-1", 0, 0x3, 0xFF);          // very fast tone portamento
    a(28, "C-2", 2, 0x3, 0x10);          // tone portamento with a sample number: volume reset
    a(29, nullptr, 0, 0x5, 0x02);        // tone portamento + volume slide
    a(30, "D-2", 0, 0x5, 0x20);          // with a new target
    a(31, nullptr, 0, 0x5, 0x00);
    a(32, "E-2", 3, 0xE, 0x52);          // finetune with a note
    a(33, nullptr, 0, 0x0, 0xC3);        // arpeggio with finetune
    a(34, "F-2", 1, 0x0, 0x47);          // note with arpeggio
    a(36, "G-2", 1, 0x6, 0x20);          // note with vibrato (from memory) + volume slide up
    a(37, "A-2", 1, 0x4, 0x00);          // note with vibrato from memory
    a(38, "C-2", 1, 0x2, 0x08);          // note with portamento down
    a(39, "C-2", 1, 0x7, 0x00);          // note with tremolo from memory (set on channel 1)
    a(40, "D-2", 1, 0xE, 0x12);          // note with fine slide

    // Pattern 0, channel 1: volume effects, sample handling, note timing
    Writer b{mod, 0, 1};
    b(0, "C-2", 2);
    b(1, nullptr, 0, 0xA, 0x02);         // volume slide down
    b(2, nullptr, 0, 0xA, 0x20);         // up
    b(3, nullptr, 0, 0xA, 0x0F);
    b(4, nullptr, 0, 0xC, 0x30);         // set volume
    b(5, nullptr, 0, 0x7, 0x48);         // tremolo
    b(6, nullptr, 0, 0x7, 0x00);
    b(7, nullptr, 0, 0xE, 0x71);         // tremolo waveform: ramp
    b(8, "D-2", 0, 0x7, 0x00);           // note, tremolo from memory
    b(9, nullptr, 0, 0xE, 0xA3);         // fine volume up
    b(10, nullptr, 0, 0xE, 0xB8);        // fine volume down
    b(11, nullptr, 2);                   // sample number alone: volume back to the sample's
    b(12, "C-2", 2, 0xC, 0x00);          // note at volume 0
    b(13, nullptr, 0, 0xC, 0x20);        // ... and back
    b(14, "G-2", 2, 0xC, 0x10);          // note with a volume
    b(15, nullptr, 0, 0xA, 0x04);
    b(16, "E-2", 0);                     // note without sample: keeps the slid volume
    b(17, nullptr, 0, 0xE, 0xC2);        // note cut on tick 2
    b(18, "C-2", 2, 0xE, 0xC0);          // note cut at once
    b(19, nullptr, 0, 0xC, 0x30);        // volume 0 is not the end of a note
    b(20, "E-2", 2, 0xE, 0xD3);          // note delay
    b(21, "F-2", 0, 0xE, 0xD9);          // delay longer than the row: no note
    b(22, "E-2", 2, 0xE, 0x92);          // retrigger every 2 ticks
    b(23, nullptr, 0, 0xE, 0x93);        // retrigger without a note
    b(24, "G-2", 1);                     // other sample
    b(25, "G-2", 1, 0x9, 0x04);          // sample offset
    b(26, "C-2", 0, 0x9, 0x00);          // ... from memory
    b(27, nullptr, 0, 0x9, 0x10);        // remembered for the next note
    b(28, "D-2", 0, 0x9, 0x00);
    b(29, "D-2", 2, 0x7, 0x24);          // tremolo on channel 1, memory for channel 0 row 39 is per channel

    // Pattern 0, channel 2: time effects
    Writer c{mod, 0, 2};
    c(0, "C-1", 3);
    c(4, nullptr, 0, 0xF, 0x03);         // speed 3
    c(5, nullptr, 0, 0xA, 0x01);
    c(8, nullptr, 0, 0xF, 0x06);
    c(10, nullptr, 0, 0xE, 0x60);        // loop start
    c(11, "D-1", 3, 0xA, 0x01);
    c(12, nullptr, 0, 0xE, 0x62);        // loop twice
    c(14, nullptr, 0, 0xE, 0xE2);        // pattern delay: row 14 lasts 3 rows
    c(16, nullptr, 0, 0xF, 0x8C);        // 140 BPM
    c(17, nullptr, 0, 0xA, 0x02);
    c(20, nullptr, 0, 0xF, 0x7D);        // back to 125
    c(24, "G-1", 3, 0xF, 0x04);
    c(26, nullptr, 0, 0xF, 0x06);
    c(41, nullptr, 0, 0xD, 0x05);        // break to row 5 of the next pattern

    // Pattern 0, channel 3: effects while other channels play pattern delay and loops
    Writer d{mod, 0, 3};
    d(0, "C-3", 1, 0xC, 0x20);
    d(12, nullptr, 0, 0xA, 0x01);        // inside the loop
    d(14, nullptr, 0, 0x4, 0x34);        // vibrato through the pattern delay
    d(15, nullptr, 0, 0x1, 0x01);

    // Pattern 1: rows 5.. a melody with mixed effects on all channels, then Bxx to order 2
    Writer e{mod, 1, 0}, f{mod, 1, 1}, g{mod, 1, 2}, h{mod, 1, 3};
    const char* mel[] = {"C-2", "E-2", "G-2", "C-3", "G-2", "E-2", "D-2", "B-1"};
    for (int r = 5; r < 37; ++r) {
        const char* n = mel[(r - 5) % 8];
        if (r % 2 == 1) e(r, n, 1, 0x0, r % 4 == 1 ? 0x37 : 0x00);
        else e(r, nullptr, 0, 0x1, 0x01);
        if (r % 4 == 1) f(r, n, 2, 0xC, 0x28 + r);
        else if (r % 4 == 3) f(r, n, 0, 0x3, 0x04);
        else f(r, nullptr, 0, 0xA, 0x01);
        if (r % 8 == 5) g(r, "C-1", 3, 0x9, 0x02);
        else if (r % 8 == 1) g(r, "C-1", 3, 0xE, 0x93);
        if (r % 3 == 0) h(r, n, 1, 0x4, 0x26);
        else h(r, nullptr, 0, 0x6, 0x01);
    }
    h(37, nullptr, 0, 0xB, 0x02);        // jump to order 2

    // Pattern 2: short, ends the song with Dxx at the last order
    Writer i{mod, 2, 0}, j{mod, 2, 1};
    i(0, "C-2", 1, 0x0, 0x47);
    i(1, nullptr, 0, 0x0, 0x47);
    j(0, "E-2", 2, 0x1, 0x02);
    j(2, nullptr, 0, 0xE, 0xC1);
    i(4, nullptr, 0, 0xD, 0x00);
    return mod;
}

size_t testEffectModule() {
    const Module mod = effectModule();
    size_t bad = roundTrip(mod, -1, "effects song");
    for (int p = 0; p < mod.numPatterns; ++p) {
        const std::string name = "effects pattern " + std::to_string(p);
        bad += roundTrip(mod, p, name.c_str(), 10);
    }
    return bad;
}

size_t testRealModule(const char* file, int pattern = -1, std::array<size_t, 4>* bends = nullptr) {
    Module mod;
    const bool loaded = ModLoader::loadFromFile(std::string(PAULASCAPE_TEST_DIR) + "/mods/" + file, mod);
    assert(loaded);
    (void)loaded;
    const std::string name = std::string(file) + (pattern >= 0 ? " pattern " + std::to_string(pattern) : "");
    return roundTrip(mod, pattern, name.c_str(), 20, bends);
}

} // namespace

int main() {
    std::cout << "Testing MIDI export round trip..." << std::endl;
    // everything runs before the check, so one report shows all differences
    size_t bad = testEffectModule();
    bad += testRealModule("BEDROCK.MOD");
    bad += testRealModule("JULEMAND.MOD");
    // Tone portamento on channel 3 (sample 8): one held key per slide, the slide itself in pitch bends
    std::array<size_t, 4> bends{};
    bad += testRealModule("JULEMAND.MOD", 5, &bends);
    if (bends[2] < 10) {
        std::cout << "JULEMAND.MOD pattern 5: only " << bends[2] << " pitch bends on channel 3" << std::endl;
        ++bad;
    }
    if (bad != 0) {
        std::cout << "MIDI round trip FAILED: " << bad << " differences" << std::endl;
        return 1;
    }
    std::cout << "MIDI round trip test passed" << std::endl;
    return 0;
}
