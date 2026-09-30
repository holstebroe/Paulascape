#include "core/replayer.hpp"
#include <iostream>
#include <cassert>
#include <vector>

int main() {
    std::cout << "Testing ProTracker Replayer engine..." << std::endl;

    paulascape::Module mod;
    mod.title = "Test Song";
    mod.initialSpeed = 6;
    mod.initialBPM = 125;
    mod.songLength = 1;
    mod.orderList[0] = 0;
    mod.numPatterns = 1;
    mod.patterns.resize(1);

    mod.samples[1].header.name = "Pluck";
    mod.samples[1].header.length = 64;
    mod.samples[1].header.volume = 64;
    mod.samples[1].pcmData.resize(64, 80);

    // Row 0 Ch 0: Sample 1, Note C-3 (period 214)
    mod.patterns[0][0][0].sample = 1;
    mod.patterns[0][0][0].period = 214;

    paulascape::Replayer replayer;
    replayer.setModule(&mod);
    replayer.setSampleRate(44100.0);

    // Trigger Pattern 0 (key C1 = 24)
    replayer.patternNoteOn(24);
    assert(replayer.isPlaying());

    std::vector<float> leftBuffer(1024, 0.0f);
    std::vector<float> rightBuffer(1024, 0.0f);
    float* outputs[2] = { leftBuffer.data(), rightBuffer.data() };

    replayer.processAudio(outputs, 2, 1024);

    float sumLeft = 0.0f;
    for (float v : leftBuffer) sumLeft += std::abs(v);
    assert(sumLeft > 0.0f);

    replayer.patternNoteOff(24);
    assert(!replayer.isPlaying());

    std::cout << "Replayer Engine Test Passed Successfully!" << std::endl;
    return 0;
}
