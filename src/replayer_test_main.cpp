#include "core/replayer.hpp"
#include "core/module_io.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace paulascape;

namespace {

constexpr double SR = 44100.0;
constexpr int TICK = 882; // 125 BPM at 44.1 kHz: 44100 / 50 Hz

Module makeModule(int patterns = 1) {
    Module mod;
    mod.title = "Test";
    mod.initialSpeed = 6;
    mod.initialBPM = 125;
    mod.songLength = static_cast<uint8_t>(patterns);
    for (int i = 0; i < patterns; ++i) mod.orderList[i] = static_cast<uint8_t>(i);
    mod.numPatterns = static_cast<uint8_t>(patterns);
    mod.patterns.resize(patterns);
    auto& s = mod.samples[1];
    s.header.name = "Saw";
    s.header.length = 4000;
    s.header.volume = 64;
    s.header.loopEnabled = true;
    s.header.loopStart = 0;
    s.header.loopLength = 4000;
    s.pcmData.resize(4000);
    for (int i = 0; i < 4000; ++i) s.pcmData[i] = static_cast<int8_t>((i % 100) - 50);
    return mod;
}

struct Rig {
    Module mod;
    Replayer r;
    explicit Rig(Module m) : mod(std::move(m)) {
        r.setModule(&mod);
        r.setSampleRate(SR);
        r.setTempoSyncMode(TempoSyncMode::ModNative);
    }
    void run(int frames) {
        std::vector<float> l(frames), rr(frames);
        float* out[2] = {l.data(), rr.data()};
        r.processAudio(out, 2, frames);
    }
    // Start at tick 0 of the first row, then advance n more ticks.
    void start(uint8_t key = 24) { r.patternNoteOn(key); run(1); }
    void ticks(int n) { run(n * TICK); }
};

NoteCell cell(uint8_t sample, uint16_t period, uint8_t fx = 0, uint8_t param = 0) {
    NoteCell c;
    c.sample = sample; c.period = period; c.effect = fx; c.param = param;
    return c;
}

void testBasicAndSpeed() {
    Rig rig(makeModule());
    rig.mod.patterns[0][0][0] = cell(1, 214);
    rig.mod.patterns[0][0][1] = cell(0, 0, 0x0F, 3); // speed 3 (channel 1 effect applies to the whole song)
    rig.start();
    assert(rig.r.getCurrentRow() == 0);
    assert(rig.r.isChannelActive(0));
    rig.ticks(1);
    assert(rig.r.getCurrentRow() == 0);
    rig.ticks(1);
    assert(rig.r.getCurrentRow() == 1); // speed 3: the row advances after the third tick
    rig.ticks(3);
    assert(rig.r.getCurrentRow() == 2);
}

void testPortamento() {
    Rig rig(makeModule());
    rig.mod.patterns[0][0][0] = cell(1, 214, 0x01, 4); // slide up 4 per tick
    rig.start();
    assert(rig.r.getChannelPeriod(0) == 214);
    rig.ticks(2);
    assert(rig.r.getChannelPeriod(0) == 214 - 8);

    Rig down(makeModule());
    down.mod.patterns[0][0][0] = cell(1, 214, 0x02, 3);
    down.start();
    down.ticks(3);
    assert(down.r.getChannelPeriod(0) == 214 + 9);
}

void testTonePortamento() {
    Rig rig(makeModule());
    rig.mod.patterns[0][0][0] = cell(1, 428);                // C-2
    rig.mod.patterns[0][1][0] = cell(0, 214, 0x03, 0x40);    // slide to C-3 at speed 64
    rig.start();
    rig.ticks(6);
    assert(rig.r.getCurrentRow() == 1);
    rig.run(1);
    assert(rig.r.getChannelPeriod(0) == 428); // not retriggered
    rig.ticks(2);
    assert(rig.r.getChannelPeriod(0) == 428 - 2 * 64);
    rig.ticks(3);
    assert(rig.r.getChannelPeriod(0) == 214); // stops at the target
}

void testVolume() {
    Rig rig(makeModule());
    rig.mod.patterns[0][0][0] = cell(1, 214, 0x0C, 40);
    rig.mod.patterns[0][1][0] = cell(0, 0, 0x0A, 0x04);  // slide down 4/tick
    rig.mod.patterns[0][2][0] = cell(0, 0, 0x0E, 0xA3);  // fine volume up 3
    rig.start();
    assert(rig.r.getChannelVolume(0) == 40);
    rig.ticks(6);
    rig.run(1);
    rig.ticks(3);
    assert(rig.r.getChannelVolume(0) == 40 - 12);
}

void testArpeggioAndCut() {
    Rig rig(makeModule());
    rig.mod.patterns[0][0][0] = cell(1, 214, 0x00, 0x37); // C-3: +3, +7 semitones
    rig.start();
    assert(rig.r.getChannelPeriod(0) == 214);
    rig.ticks(1);
    assert(rig.r.getChannelPeriod(0) == 180); // D#3
    rig.ticks(1);
    assert(rig.r.getChannelPeriod(0) == 143); // G-3
    rig.ticks(1);
    assert(rig.r.getChannelPeriod(0) == 214);

    Rig cut(makeModule());
    cut.mod.patterns[0][0][0] = cell(1, 214, 0x0E, 0xC2);
    cut.start();
    assert(cut.r.getChannelVolume(0) == 64);
    cut.ticks(2);
    assert(cut.r.getChannelVolume(0) == 0);
}

void testVibratoTremolo() {
    Rig rig(makeModule());
    rig.mod.patterns[0][0][0] = cell(1, 214, 0x04, 0x4F);
    rig.start();
    bool moved = false;
    for (int i = 0; i < 6; ++i) {
        rig.ticks(1);
        if (rig.r.getChannelPeriod(0) != 214) moved = true;
        assert(rig.r.getChannelPeriod(0) >= 113 && rig.r.getChannelPeriod(0) <= 856);
    }
    assert(moved);

    Rig trem(makeModule());
    trem.mod.samples[1].header.volume = 32; // room to swing up as well as down
    trem.mod.patterns[0][0][0] = cell(1, 214, 0x07, 0x4F);
    trem.start();
    bool changed = false;
    for (int i = 0; i < 6; ++i) {
        trem.ticks(1);
        if (trem.r.getChannelVolume(0) != 32) changed = true;
    }
    assert(changed);
}

void testSampleOffsetAndRetrigger() {
    Rig rig(makeModule());
    rig.mod.patterns[0][0][0] = cell(1, 214, 0x09, 0x05); // offset 0x500 = 1280
    rig.start();
    assert(rig.r.isChannelActive(0));

    Rig beyond(makeModule());
    beyond.mod.patterns[0][0][0] = cell(1, 214, 0x09, 0x7F); // beyond the 4000 byte sample
    beyond.start();
    assert(!beyond.r.isChannelActive(0));

    Rig delay(makeModule());
    delay.mod.patterns[0][0][0] = cell(1, 214, 0x0E, 0xD3); // note delay 3 ticks
    delay.start();
    assert(!delay.r.isChannelActive(0));
    delay.ticks(3);
    assert(delay.r.isChannelActive(0));
}

void testFlowControl() {
    // Dxx in a looping pattern restarts it
    Rig brk(makeModule());
    brk.mod.patterns[0][1][0] = cell(0, 0, 0x0D, 0x00);
    brk.start();
    brk.ticks(6);
    assert(brk.r.getCurrentRow() == 1);
    brk.ticks(6);
    assert(brk.r.getCurrentRow() == 0);

    // Song mode: B jump and D break
    Module m = makeModule(3);
    m.patterns[0][0][0] = cell(0, 0, 0x0B, 2);      // jump to order 2
    m.patterns[2][0][0] = cell(0, 0, 0x0D, 0x05);   // break to order 3 (wraps to restart 0), row 5
    Rig song(std::move(m));
    song.start(12);
    assert(song.r.getCurrentPattern() == 0);
    song.ticks(6);
    assert(song.r.getCurrentPattern() == 2 && song.r.getCurrentRow() == 0);
    song.ticks(6);
    assert(song.r.getCurrentPattern() == 0 && song.r.getCurrentRow() == 5);

    // E6x loop: loop row 0..1 three times
    Rig loop(makeModule());
    loop.mod.patterns[0][0][0] = cell(0, 0, 0x0E, 0x60);
    loop.mod.patterns[0][1][0] = cell(0, 0, 0x0E, 0x62);
    loop.start();
    loop.ticks(6);
    assert(loop.r.getCurrentRow() == 1);
    loop.ticks(6);
    assert(loop.r.getCurrentRow() == 0); // looped once
    loop.ticks(12);
    assert(loop.r.getCurrentRow() == 0); // looped twice
    loop.ticks(12);
    assert(loop.r.getCurrentRow() == 2); // done, continues

    // EEx pattern delay repeats a row
    Rig pd(makeModule());
    pd.mod.patterns[0][0][0] = cell(0, 0, 0x0E, 0xE1);
    pd.start();
    pd.ticks(6);
    assert(pd.r.getCurrentRow() == 0);
    pd.ticks(6);
    assert(pd.r.getCurrentRow() == 1);
}

void testTempoScaling() {
    // Host 120 BPM, MOD starts at 125 BPM speed 6: factor 0.96, so a row lasts 6 ticks / (125 * 0.96 * 0.4 Hz)
    Module m = makeModule();
    Replayer r;
    r.setModule(&m);
    r.setSampleRate(SR);
    r.setHostTempo(120.0);
    r.patternNoteOn(24);
    std::vector<float> l(1), rr(1);
    float* out[2] = {l.data(), rr.data()};
    r.processAudio(out, 2, 1);
    const double rowSeconds = 6.0 / (125.0 * 0.96 * 0.4);
    // the row index moves when the sixth tick runs, i.e. five tick lengths after the first
    const int frames = static_cast<int>(rowSeconds / 6.0 * 5.0 * SR) - 10;
    std::vector<float> lb(frames), rb(frames);
    float* big[2] = {lb.data(), rb.data()};
    r.processAudio(big, 2, frames);
    assert(r.getCurrentRow() == 0);
    std::vector<float> l2(40), r2(40);
    float* small[2] = {l2.data(), r2.data()};
    r.processAudio(small, 2, 40);
    assert(r.getCurrentRow() == 1);
}

void testFinetune() {
    Rig rig(makeModule());
    rig.mod.samples[1].header.finetune = 4;
    rig.mod.patterns[0][0][0] = cell(1, 214);
    rig.start();
    assert(rig.r.getChannelPeriod(0) == 208); // +4 finetune C-3
    Rig neg(makeModule());
    neg.mod.samples[1].header.finetune = -8;
    neg.mod.patterns[0][0][0] = cell(1, 214);
    neg.start();
    assert(neg.r.getChannelPeriod(0) > 214);
}

void testRealSong() {
    Module mod;
    assert(ModLoader::loadFromFile(std::string(PAULASCAPE_TEST_DIR) + "/mods/BEDROCK.MOD", mod));
    Replayer r;
    r.setModule(&mod);
    r.setSampleRate(SR);
    r.setTempoSyncMode(TempoSyncMode::ModNative);
    r.patternNoteOn(12); // whole song
    assert(r.isPlaying());
    std::vector<float> l(4096), rr(4096);
    float* out[2] = {l.data(), rr.data()};
    float peak = 0;
    int nonFinite = 0;
    for (int block = 0; block < 600; ++block) { // ~55 s
        r.processAudio(out, 2, 4096);
        for (int i = 0; i < 4096; ++i) {
            peak = std::max(peak, std::fabs(l[i]));
            if (!std::isfinite(l[i]) || !std::isfinite(rr[i])) ++nonFinite;
        }
    }
    assert(nonFinite == 0);
    assert(peak > 0.05f);
    assert(peak < 8.0f);
    assert(r.isPlaying()); // song loops
}

} // namespace

int main() {
    std::cout << "Testing ProTracker Replayer engine..." << std::endl;
    testBasicAndSpeed();
    testPortamento();
    testTonePortamento();
    testVolume();
    testArpeggioAndCut();
    testVibratoTremolo();
    testSampleOffsetAndRetrigger();
    testFlowControl();
    testTempoScaling();
    testFinetune();
    testRealSong();
    std::cout << "Replayer Engine Test Passed Successfully!" << std::endl;
    return 0;
}
