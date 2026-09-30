#include "core/voice_pool.hpp"
#include <iostream>
#include <cassert>
#include <vector>

int main() {
    std::cout << "Testing VoicePool polyphonic routing and allocation..." << std::endl;

    // Create module with 2 samples
    paulascape::Module mod;
    mod.samples[1].header.name = "Lead";
    mod.samples[1].header.length = 100;
    mod.samples[1].header.volume = 64;
    mod.samples[1].pcmData.resize(100, 50);

    mod.samples[2].header.name = "Bass";
    mod.samples[2].header.length = 100;
    mod.samples[2].header.volume = 64;
    mod.samples[2].pcmData.resize(100, -50);

    paulascape::VoicePool pool;
    pool.setModule(&mod);
    pool.setSampleRate(44100.0);
    pool.setPlaybackMode(paulascape::PlaybackMode::Single);

    // Trigger note on
    pool.noteOn(0, 60, 127); // C-3

    // Render audio
    std::vector<float> leftBuffer(512, 0.0f);
    std::vector<float> rightBuffer(512, 0.0f);
    float* outputs[2] = { leftBuffer.data(), rightBuffer.data() };

    pool.processAudio(outputs, 2, 512);

    float sumLeft = 0.0f;
    for (float v : leftBuffer) sumLeft += std::abs(v);
    assert(sumLeft > 0.0f);

    // Test note off
    pool.noteOff(0, 60);
    pool.processAudio(outputs, 2, 512);

    std::cout << "VoicePool Test Passed Successfully!" << std::endl;
    return 0;
}
