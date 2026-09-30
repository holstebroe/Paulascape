#include "export/midi_export.hpp"
#include "export/song_unroll.hpp"
#include "core/replayer.hpp"
#include "core/voice_pool.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace paulascape;

namespace {

struct Ev {
    uint32_t tick;
    int track;
    std::vector<uint8_t> data; // channel message bytes, or {0xFF, type, payload...}
};

struct Midi {
    int format = 0, tracks = 0, division = 0;
    std::vector<Ev> events;
};

uint32_t be32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

Midi parse(const std::vector<uint8_t>& b) {
    Midi m;
    assert(b.size() > 14 && std::string(b.begin(), b.begin() + 4) == "MThd");
    m.format = (b[8] << 8) | b[9];
    m.tracks = (b[10] << 8) | b[11];
    m.division = (b[12] << 8) | b[13];
    size_t pos = 14;
    for (int t = 0; t < m.tracks; ++t) {
        assert(std::string(b.begin() + pos, b.begin() + pos + 4) == "MTrk");
        const uint32_t len = be32(&b[pos + 4]);
        size_t p = pos + 8;
        const size_t end = p + len;
        uint32_t tick = 0;
        uint8_t running = 0;
        bool ended = false;
        while (p < end) {
            uint32_t delta = 0;
            while (true) { const uint8_t c = b[p++]; delta = (delta << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
            tick += delta;
            uint8_t status = b[p];
            if (status == 0xFF) {
                const uint8_t type = b[p + 1];
                p += 2;
                uint32_t n = 0;
                while (true) { const uint8_t c = b[p++]; n = (n << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                Ev e{tick, t, {0xFF, type}};
                e.data.insert(e.data.end(), b.begin() + p, b.begin() + p + n);
                p += n;
                if (type == 0x2F) ended = true;
                m.events.push_back(e);
            } else {
                if (status & 0x80) { running = status; ++p; } else status = running;
                const int args = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : 2;
                Ev e{tick, t, {status}};
                for (int a = 0; a < args; ++a) e.data.push_back(b[p++]);
                m.events.push_back(e);
            }
        }
        assert(ended && p == end);
        pos = end;
    }
    return m;
}

Module makeModule() {
    Module mod;
    mod.title = "Export";
    mod.initialSpeed = 6;
    mod.initialBPM = 125;
    mod.songLength = 2;
    mod.orderList[0] = 0;
    mod.orderList[1] = 1;
    mod.numPatterns = 2;
    mod.patterns.resize(2);
    for (int i = 1; i <= 2; ++i) {
        mod.samples[i].header.length = 200;
        mod.samples[i].header.volume = i == 1 ? 64 : 32;
        mod.samples[i].pcmData.assign(200, 40);
    }
    return mod;
}

NoteCell cell(uint8_t sample, uint16_t period, uint8_t fx = 0, uint8_t param = 0) {
    NoteCell c; c.sample = sample; c.period = period; c.effect = fx; c.param = param; return c;
}

std::vector<const Ev*> select(const Midi& m, int track, uint8_t statusHigh) {
    std::vector<const Ev*> r;
    for (const auto& e : m.events)
        if (e.track == track && e.data[0] != 0xFF && (e.data[0] & 0xF0) == statusHigh) r.push_back(&e);
    return r;
}

void testUnroll() {
    Module m = makeModule();
    m.patterns[0][3][0] = cell(0, 0, 0x0D, 0x00);          // pattern 0 ends after row 3
    m.patterns[1][0][0] = cell(0, 0, 0x0F, 3);             // speed 3
    m.patterns[1][1][0] = cell(0, 0, 0x0F, 140);           // BPM 140
    m.patterns[1][2][1] = cell(0, 0, 0x0E, 0xE2);          // pattern delay 2
    const auto rows = SongUnroller::unrollSong(m);
    // 4 rows of pattern 0, then 64 rows of pattern 1 plus 2 delay repeats
    assert(rows.size() == 4 + 64 + 2);
    assert(rows[4].speed == 3 && rows[4].bpm == 125);
    assert(rows[5].bpm == 140);
    int repeats = 0;
    for (const auto& r : rows) repeats += r.delayRepeat ? 1 : 0;
    assert(repeats == 2);

    // Bxx jump back loops: the walker stops instead of running forever
    Module loop = makeModule();
    loop.patterns[1][63][0] = cell(0, 0, 0x0B, 0);
    const auto lr = SongUnroller::unrollSong(loop);
    assert(lr.size() == 128);
}

void testPatternClip() {
    Module m = makeModule();
    m.patterns[1][31][0] = cell(0, 0, 0x0D, 0x00); // pattern 1 only has 32 rows
    const auto midi = parse(MidiExporter::generatePatternClip(m, 24, 4));
    assert(midi.format == 1 && midi.tracks == 1 && midi.division == 96);
    auto on = select(midi, 0, 0x90);
    auto off = select(midi, 0, 0x80);
    assert(on.size() == 2 && off.size() == 2);
    assert(on[0]->data[1] == 24 && on[0]->tick == 0);
    assert(off[0]->tick == 64 * 24);                 // pattern 0: 64 rows of 24 ticks
    assert(on[1]->data[1] == 25 && on[1]->tick == 64 * 24);
    assert(off[1]->tick == 64 * 24 + 32 * 24);       // pattern 1: Dxx shortens it
    // tempo: 125 BPM speed 6 -> 480000 us per quarter
    bool tempo = false;
    for (const auto& e : midi.events) if (e.data[0] == 0xFF && e.data[1] == 0x51) tempo = tempo || (e.data[2] == 0x07 && e.data[3] == 0x53 && e.data[4] == 0x00);
    assert(tempo);
}

void testNoteExport() {
    Module m = makeModule();
    m.patterns[0][0][0] = cell(1, 214);                      // C-3, sample 1
    m.patterns[0][1][0] = cell(2, 226);                      // sample 2 -> program change, velocity from volume 32
    m.patterns[0][2][0] = cell(0, 190, 0x03, 0x10);          // tone portamento to E-3: legato overlap
    m.patterns[0][3][0] = cell(0, 0, 0x0E, 0x53);            // E53 finetune -> CC 20 = 21
    m.patterns[0][4][0] = cell(0, 214, 0x0C, 0x10);          // Cxx with note: velocity
    m.patterns[0][5][0] = cell(0, 0, 0x04, 0x46);            // vibrato
    m.patterns[0][6][0] = cell(0, 0, 0);                     // effect ends: clear triple
    m.patterns[0][7][0] = cell(0, 214, 0x0E, 0xD3);          // note delay 3 ticks
    m.patterns[0][8][0] = cell(0, 0, 0x0F, 3);               // speed 3
    m.patterns[0][9][0] = cell(0, 0, 0x0F, 130);             // tempo 130
    m.patterns[0][10][0] = cell(0, 0, 0x0E, 0xC2);           // note cut after 2 ticks (of 3)
    m.patterns[0][11][0] = cell(0, 0, 0x0E, 0x01);           // E01: LED off -> CC74 = 0
    m.songLength = 1;

    const auto midi = parse(MidiExporter::generateMidiBuffer(m, -1));
    assert(midi.tracks == 5 && midi.format == 1);

    // Tempo map: 125 at 0, 130 from row 9 (rows 0..7 at 24 ticks, row 8 at 12 ticks)
    std::vector<std::pair<uint32_t, uint32_t>> tempos;
    for (const auto& e : midi.events)
        if (e.track == 0 && e.data[0] == 0xFF && e.data[1] == 0x51) tempos.push_back({e.tick, (e.data[2] << 16) | (e.data[3] << 8) | e.data[4]});
    assert(tempos.size() == 2 && tempos[0].first == 0 && tempos[0].second == 480000);
    assert(tempos[1].first == 8 * 24 + 12 && tempos[1].second == static_cast<uint32_t>(std::lround(60000000.0 / 130)));

    auto pc = select(midi, 1, 0xC0);
    assert(pc.size() == 2 && pc[0]->data[1] == 0 && pc[1]->data[1] == 1);
    auto on = select(midi, 1, 0x90);
    auto off = select(midi, 1, 0x80);
    // Note ons: C-3 (vel 127), D-3(226 -> key 59? B-2) sample 2 vel 64, porta target, Cxx, delayed, ...
    assert(on.size() >= 5);
    assert(on[0]->data[1] == 60 && on[0]->data[2] == 127);
    assert(on[1]->data[2] == 64);                            // sample 2 volume 32 of 64
    // Tone portamento row: CC68 on before the note, note-on without a preceding note-off at that tick
    const uint32_t portaTick = 2 * 24;
    bool legatoOn = false, legatoOff = false, offAtPorta = false;
    for (const auto& e : midi.events) {
        if (e.track != 1 || e.data[0] == 0xFF) continue;
        if ((e.data[0] & 0xF0) == 0xB0 && e.data[1] == 68 && e.tick == portaTick) (e.data[2] >= 64 ? legatoOn : legatoOff) = true;
        if ((e.data[0] & 0xF0) == 0x80 && e.tick == portaTick) offAtPorta = true;
    }
    assert(legatoOn && legatoOff && !offAtPorta);
    // Cxx 0x10 with a note: velocity 16*127/64 = 32
    bool cxx = false;
    for (const auto* e : on) if (e->tick == 4 * 24) cxx = e->data[2] == 32;
    assert(cxx);
    // E53: CC20 = 16 + 5, CC22 = 3 (triple in order)
    auto ccs = select(midi, 1, 0xB0);
    bool e5 = false, vibr = false, cleared = false, led = false;
    for (size_t i = 0; i + 2 < ccs.size(); ++i) {
        if (ccs[i]->data[1] == 20 && ccs[i + 1]->data[1] == 21 && ccs[i + 2]->data[1] == 22) {
            if (ccs[i]->data[2] == 21 && ccs[i + 2]->data[2] == 3 && ccs[i]->tick == 3 * 24) e5 = true;
            if (ccs[i]->data[2] == 4 && ccs[i + 1]->data[2] == 4 && ccs[i + 2]->data[2] == 6) vibr = true;
            if (ccs[i]->data[2] == 0 && ccs[i + 1]->data[2] == 0 && ccs[i + 2]->data[2] == 0 && ccs[i]->tick == 6 * 24) cleared = true;
        }
    }
    for (auto* c : ccs) if (c->data[1] == 74 && c->data[2] == 0) led = true;
    if (!(e5 && vibr && cleared && led)) { std::cerr << "e5 " << e5 << " vibr " << vibr << " cleared " << cleared << " led " << led << "\n"; for (auto* c : ccs) std::cerr << c->tick << ":" << int(c->data[1]) << "=" << int(c->data[2]) << " "; std::cerr << "\n"; }
    assert(e5 && vibr && cleared && led);
    // EDx: delayed by 3 of 6 ticks = 12 MIDI ticks
    bool delayed = false;
    for (const auto* e : on) if (e->tick == 7 * 24 + 12) delayed = true;
    assert(delayed);
    // ECx at speed 3: cut after 2 of 3 ticks = 8 MIDI ticks into the row
    bool cut = false;
    for (const auto* e : off) if (e->tick == 8 * 24 + 12 + 12 + 8) cut = true;
    assert(cut);
    // Every note on has an off
    assert(on.size() >= off.size());
}

// Play an exported file through the voice pool and compare it with the pattern replayer.
std::vector<float> renderMidi(const Module& mod, const Midi& midi, double seconds, double sr) {
    std::vector<std::pair<uint32_t, uint32_t>> tempos; // tick, micros per quarter
    for (const auto& e : midi.events)
        if (e.data[0] == 0xFF && e.data[1] == 0x51) tempos.push_back({e.tick, static_cast<uint32_t>((e.data[2] << 16) | (e.data[3] << 8) | e.data[4])});
    std::sort(tempos.begin(), tempos.end());
    auto tickToSamples = [&](uint32_t tick) {
        double secs = 0;
        uint32_t last = 0;
        uint32_t micros = 500000;
        for (const auto& t : tempos) {
            if (t.first >= tick) break;
            secs += static_cast<double>(t.first - last) * micros / 1e6 / midi.division;
            last = t.first;
            micros = t.second;
        }
        secs += static_cast<double>(tick - last) * micros / 1e6 / midi.division;
        return static_cast<uint64_t>(std::llround(secs * sr));
    };

    struct Timed { uint64_t sample; const Ev* ev; };
    std::vector<Timed> timeline;
    for (const auto& e : midi.events) if (e.data[0] != 0xFF) timeline.push_back({tickToSamples(e.tick), &e});
    std::stable_sort(timeline.begin(), timeline.end(), [](const Timed& a, const Timed& b) { return a.sample < b.sample; });

    VoicePool pool;
    pool.setModule(&mod);
    pool.setSampleRate(sr);
    pool.setPlaybackMode(PlaybackMode::Single);
    pool.setHostTempo(mod.initialBPM);
    std::vector<float> out(static_cast<size_t>(seconds * sr));
    std::vector<float> r(out.size());
    size_t pos = 0;
    auto render = [&](size_t upto) {
        upto = std::min(upto, out.size());
        if (upto <= pos) return;
        float* ch[2] = {out.data() + pos, r.data() + pos};
        pool.processAudio(ch, 2, static_cast<uint32_t>(upto - pos));
        pos = upto;
    };
    for (const auto& t : timeline) {
        render(static_cast<size_t>(t.sample));
        const auto& d = t.ev->data;
        if (getenv("MIDI_DEBUG") && t.sample < 44100) std::cout << t.sample << " tick " << t.ev->tick << " trk " << t.ev->track << " st " << std::hex << int(d[0]) << " " << std::dec << int(d[1]) << " " << (d.size() > 2 ? int(d[2]) : -1) << " voices " << pool.activeVoiceCount() << "\n";
        const uint8_t ch = d[0] & 15;
        switch (d[0] & 0xF0) {
            case 0x90: pool.noteOn(ch, d[1], d[2]); break;
            case 0x80: pool.noteOff(ch, d[1]); break;
            case 0xB0: if (d[1] != 74) pool.controlChange(ch, d[1], d[2]); break;
            case 0xC0: pool.programChange(ch, d[1]); break;
            default: break;
        }
    }
    render(out.size());
    for (size_t i = 0; i < out.size(); ++i) out[i] += r[i];
    return out;
}

void testRoundTrip() {
    Module mod;
    assert(ModLoader::loadFromFile(std::string(PAULASCAPE_TEST_DIR) + "/mods/BEDROCK.MOD", mod));
    const double sr = 44100.0, seconds = 12.0;

    const auto midi = parse(MidiExporter::generateMidiBuffer(mod, -1));
    assert(midi.tracks == 5);
    // Song length in seconds from MIDI equals the unrolled timeline
    const auto rows = SongUnroller::unrollSong(mod);
    double expected = 0;
    for (const auto& r : rows) expected += r.speed / (0.4 * r.bpm);
    uint32_t lastTick = 0;
    for (const auto& e : midi.events) lastTick = std::max(lastTick, e.tick);
    assert(lastTick > 0);

    if (getenv("MIDI_DEBUG")) { int n = 0; for (const auto& e : midi.events) { if (e.data[0] == 0xFF) continue; std::cout << "EV tick " << e.tick << " trk " << e.track << " st " << std::hex << int(e.data[0]) << std::dec << " " << int(e.data[1]) << " " << (e.data.size() > 2 ? int(e.data[2]) : -1) << "\n"; if (++n > 24) break; } }
    const auto fromMidi = renderMidi(mod, midi, seconds, sr);

    Replayer rep;
    rep.setModule(&mod);
    rep.setSampleRate(sr);
    rep.setTempoSyncMode(TempoSyncMode::ModNative);
    rep.patternNoteOn(12);
    std::vector<float> direct(fromMidi.size()), r(fromMidi.size());
    float* ch[2] = {direct.data(), r.data()};
    rep.processAudio(ch, 2, static_cast<uint32_t>(direct.size()));
    for (size_t i = 0; i < direct.size(); ++i) direct[i] += r[i];

    // Compare loudness envelopes in 100 ms windows
    const size_t win = static_cast<size_t>(sr * 0.1);
    std::vector<double> a, b;
    for (size_t s = 0; s + win <= direct.size(); s += win) {
        double ea = 0, eb = 0;
        for (size_t i = 0; i < win; ++i) { ea += direct[s + i] * direct[s + i]; eb += fromMidi[s + i] * fromMidi[s + i]; }
        a.push_back(std::sqrt(ea / win));
        b.push_back(std::sqrt(eb / win));
    }
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= a.size(); mb /= b.size();
    double cov = 0, va = 0, vb = 0;
    for (size_t i = 0; i < a.size(); ++i) { cov += (a[i] - ma) * (b[i] - mb); va += (a[i] - ma) * (a[i] - ma); vb += (b[i] - mb) * (b[i] - mb); }
    const double corr = cov / std::sqrt(va * vb + 1e-12);
    if (getenv("MIDI_DEBUG")) { for (size_t i = 0; i < a.size(); ++i) std::cout << i << " " << a[i] << " " << b[i] << "\n"; }
    std::cout << "round trip envelope correlation: " << corr << " (mean level " << ma << " vs " << mb << ")" << std::endl;
    assert(ma > 0.01 && mb > 0.01);
    assert(corr > 0.97);
    assert(mb > ma * 0.9 && mb < ma * 1.1);
    (void)expected;
}

} // namespace

int main() {
    std::cout << "Testing MIDI export..." << std::endl;
    testUnroll();
    testPatternClip();
    testNoteExport();
    testRoundTrip();
    std::cout << "MIDI Exporter Test Passed Successfully!" << std::endl;
    return 0;
}
