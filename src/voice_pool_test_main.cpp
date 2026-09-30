#include "core/voice_pool.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace paulascape;

namespace {

constexpr double SR = 44100.0;

Module makeModule() {
    Module mod;
    for (int i = 1; i <= 4; ++i) {
        auto& s = mod.samples[i];
        s.header.name = "S" + std::to_string(i);
        s.header.length = 2000;
        s.header.volume = 64;
        s.header.loopEnabled = true;
        s.header.loopStart = 0;
        s.header.loopLength = 2000;
        s.header.inKey = static_cast<uint8_t>(36 + i);
        s.header.outKey = 60;
        s.pcmData.resize(2000);
        for (int k = 0; k < 2000; ++k) s.pcmData[k] = static_cast<int8_t>(((k % 50) - 25) * (i == 2 ? -1 : 1));
    }
    return mod;
}

struct Rig {
    Module mod = makeModule();
    VoicePool pool;
    Rig() {
        pool.setModule(&mod);
        pool.setSampleRate(SR);
        pool.setPlaybackMode(PlaybackMode::Single);
        pool.setPitchMode(PitchMode::PeriodTable);
    }
    void run(int frames, float* left = nullptr) {
        std::vector<float> l(frames), r(frames);
        float* out[2] = {l.data(), r.data()};
        pool.processAudio(out, 2, frames);
        if (left) for (int i = 0; i < frames; ++i) left[i] = l[i];
    }
    float energy(int frames = 512) {
        std::vector<float> l(frames);
        run(frames, l.data());
        float e = 0;
        for (float v : l) e += std::fabs(v);
        return e;
    }
    int firstVoice() {
        for (size_t i = 0; i < MAX_VOICES; ++i) if (pool.voiceActive(i)) return static_cast<int>(i);
        return -1;
    }
};

void testModes() {
    Rig rig;
    rig.pool.setSelectedSlot(3);
    rig.pool.noteOn(0, 60, 127);
    assert(rig.firstVoice() >= 0);
    rig.run(256);
    rig.pool.allNotesOff();
    assert(rig.pool.activeVoiceCount() == 0);

    // Multi-channel: channel n plays slot n+1
    rig.pool.setPlaybackMode(PlaybackMode::MultiChannel);
    rig.pool.noteOn(1, 60, 127); // slot 2 (inverted waveform)
    std::vector<float> l(256);
    rig.run(256, l.data());
    float sum = 0;
    for (float v : l) sum += v;
    assert(rig.pool.activeVoiceCount() == 1);
    rig.pool.allNotesOff();
    rig.pool.noteOn(9, 60, 127); // slot 10 is empty
    assert(rig.pool.activeVoiceCount() == 0);

    // Drum: in key picks the sample, out key sets the pitch; unmapped keys are silent
    rig.pool.setPlaybackMode(PlaybackMode::Drum);
    rig.mod.samples[2].header.outKey = 72; // one octave up
    rig.pool.noteOn(0, 38, 127);           // in key of slot 2
    assert(rig.pool.activeVoiceCount() == 1);
    assert(rig.pool.voicePeriod(rig.firstVoice()) == 107);
    rig.pool.noteOn(0, 100, 127);
    assert(rig.pool.activeVoiceCount() == 1);
    (void)sum;
}

void testRoundRobinAndFourMono() {
    Rig rig;
    rig.pool.setOutputLayout(OutputLayout::FourMono);
    rig.pool.setFilterModel(FilterModel::Off);
    std::vector<float> o[4];
    float* out[4];
    for (int c = 0; c < 4; ++c) { o[c].assign(64, 0.f); out[c] = o[c].data(); }
    // Four notes, one at a time, each lands on the next output
    for (int n = 0; n < 4; ++n) {
        rig.pool.allNotesOff();
        // the allocator keeps counting across notes: note n goes to output n
        for (int k = 0; k < 4; ++k) { o[k].assign(64, 0.f); }
        rig.pool.noteOn(0, 60, 127);
        rig.pool.processAudio(out, 4, 64);
        int loud = -1;
        for (int c = 0; c < 4; ++c) {
            float e = 0;
            for (float v : o[c]) e += std::fabs(v);
            if (e > 0.01f) { assert(loud == -1); loud = c; }
        }
        assert(loud == n);
    }
}

void testNoteOffAndFade() {
    Rig rig;
    rig.pool.setResamplerMode(ResamplerMode::Authentic);
    rig.pool.noteOn(0, 60, 127);
    rig.run(100);
    rig.pool.noteOff(0, 60);
    assert(rig.pool.activeVoiceCount() == 0); // Paula cuts hard

    rig.pool.setResamplerMode(ResamplerMode::Clean);
    rig.pool.noteOn(0, 60, 127);
    rig.run(100);
    rig.pool.noteOff(0, 60);
    assert(rig.pool.activeVoiceCount() == 1); // fading
    rig.run(static_cast<int>(SR * 0.0015));
    assert(rig.pool.activeVoiceCount() == 1);
    rig.run(static_cast<int>(SR * 0.001));
    assert(rig.pool.activeVoiceCount() == 0); // gone after about 2 ms
}

void testPolyphonyAndStealing() {
    Rig rig;
    for (int k = 0; k < 40; ++k) rig.pool.noteOn(0, static_cast<uint8_t>(40 + k), 100);
    assert(rig.pool.activeVoiceCount() == MAX_VOICES);
}

void testPitch() {
    Rig rig;
    rig.pool.setPitchMode(PitchMode::Free);
    rig.pool.noteOn(0, 60, 127);
    assert(rig.pool.voicePeriod(rig.firstVoice()) == 214);
    rig.pool.allNotesOff();
    rig.pool.noteOn(0, 72, 127);
    assert(rig.pool.voicePeriod(rig.firstVoice()) == 107);

    // Free mode: bend glides immediately
    rig.pool.setPitchBendValue(0, 16383);
    const int bent = rig.pool.voicePeriod(rig.firstVoice());
    assert(bent < 107 && bent > 88); // about +2 semitones

    // Table mode: bend takes effect on the next tick
    Rig t;
    t.pool.noteOn(0, 60, 127);
    const int base = t.pool.voicePeriod(t.firstVoice());
    assert(base == 214);
    t.pool.setPitchBendValue(0, 16383);
    assert(t.pool.voicePeriod(t.firstVoice()) == 214);
    t.run(1000);
    assert(t.pool.voicePeriod(t.firstVoice()) < 200);

    // Notes outside the table extend by octaves
    Rig e;
    e.pool.noteOn(0, 84, 127);
    assert(e.pool.voicePeriod(e.firstVoice()) == 214 / 4 || e.pool.voicePeriod(e.firstVoice()) == 53 || e.pool.voicePeriod(e.firstVoice()) == 54);
}

void testLegato() {
    Rig rig;
    rig.pool.controlChange(0, 68, 127); // legato on
    rig.pool.noteOn(0, 60, 127);
    rig.run(100);
    rig.pool.noteOn(0, 67, 127);
    assert(rig.pool.activeVoiceCount() == 1);
    assert(rig.pool.voicePeriod(rig.firstVoice()) == 143); // G-3, pitch changed without a new voice

    // Glide with CC 5
    Rig g;
    g.pool.controlChange(0, 68, 127);
    g.pool.controlChange(0, 5, 100);
    g.pool.noteOn(0, 60, 127);
    g.run(100);
    g.pool.noteOn(0, 72, 127);
    assert(g.pool.voicePeriod(g.firstVoice()) == 214); // starts gliding from the old pitch
    g.run(static_cast<int>(918.75 * 3) + 10);
    const int mid = g.pool.voicePeriod(g.firstVoice());
    assert(mid < 214 && mid > 107);
    g.run(static_cast<int>(918.75 * 60));
    assert(g.pool.voicePeriod(g.firstVoice()) == 107);

    // CC 68 off: a second note is a new voice
    Rig p;
    p.pool.noteOn(0, 60, 127);
    p.pool.noteOn(0, 67, 127);
    assert(p.pool.activeVoiceCount() == 2);
}

void testControllers() {
    Rig rig;
    rig.pool.controlChange(0, 1, 127);  // mod wheel: full vibrato depth
    rig.pool.controlChange(0, 76, 127); // fast
    rig.pool.noteOn(0, 60, 127);
    int lo = 9999, hi = 0;
    for (int i = 0; i < 40; ++i) {
        rig.run(919);
        const int p = rig.pool.voicePeriod(rig.firstVoice());
        lo = std::min(lo, p);
        hi = std::max(hi, p);
    }
    assert(hi - lo >= 4);

    // CC 72: note cut after N ticks (Paula-style hard cut in authentic mode)
    Rig cut;
    cut.pool.controlChange(0, 72, 3);
    cut.pool.noteOn(0, 60, 127);
    cut.run(919 * 2);
    assert(cut.pool.activeVoiceCount() == 1);
    cut.run(919 * 2);
    assert(cut.pool.activeVoiceCount() == 0);

    // Effect command CC 20-22 before the note: 0C 20 -> set volume 0x20
    Rig fxr;
    fxr.pool.controlChange(0, 20, 0x0C);
    fxr.pool.controlChange(0, 21, 2);
    fxr.pool.controlChange(0, 22, 0);
    fxr.pool.noteOn(0, 60, 127);
    fxr.run(2000);
    const int v = fxr.firstVoice();
    assert(v >= 0);

    // Volume slide effect command applied to a sounding voice: A0 4 (down by 4 per tick)
    Rig slide;
    slide.pool.noteOn(0, 60, 127);
    slide.pool.controlChange(0, 20, 0x0A);
    slide.pool.controlChange(0, 21, 0);
    slide.pool.controlChange(0, 22, 4);
    const float before = slide.energy(256);
    slide.run(919 * 10);
    const float after = slide.energy(256);
    assert(after < before * 0.6f);

    // All notes off
    slide.pool.controlChange(0, 123, 0);
    assert(slide.pool.activeVoiceCount() == 0);
}

void testSampleOffset() {
    Rig rig;
    // CC 70 = 10 -> 9xx param 20 -> start 20*256 = 5120 bytes: beyond the 2000 byte sample, so ignored (plays from 0)
    rig.pool.controlChange(0, 70, 10);
    rig.pool.noteOn(0, 60, 127);
    assert(rig.pool.activeVoiceCount() == 1);
    // Effect command 9xx through the CC triple: offset 0x02 -> 512 bytes
    Rig e;
    e.pool.controlChange(0, 20, 0x09);
    e.pool.controlChange(0, 21, 0);
    e.pool.controlChange(0, 22, 2);
    e.pool.noteOn(0, 60, 127);
    assert(e.pool.activeVoiceCount() == 1);
}

} // namespace

int main() {
    std::cout << "Testing VoicePool..." << std::endl;
    testModes();
    testRoundRobinAndFourMono();
    testNoteOffAndFade();
    testPolyphonyAndStealing();
    testPitch();
    testLegato();
    testControllers();
    testSampleOffset();
    std::cout << "VoicePool Test Passed Successfully!" << std::endl;
    return 0;
}
