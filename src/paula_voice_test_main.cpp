#include "core/paula_voice.hpp"
#include "core/rc_filters.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "Testing PaulaVoice, BLEP and RC filters..." << std::endl;

    // Create a dummy sample (square wave)
    paulascape::ModSample sample;
    sample.header.name = "Test Square";
    sample.header.length = 64;
    sample.header.volume = 64;
    sample.header.loopStart = 0;
    sample.header.loopLength = 64;
    sample.header.loopEnabled = true;

    sample.pcmData.resize(64);
    for (size_t i = 0; i < 64; ++i) {
        sample.pcmData[i] = (i < 32) ? 100 : -100;
    }

    // Test Paula Voice
    paulascape::PaulaVoice voice;
    voice.setSampleRate(44100.0);
    voice.setResamplerMode(paulascape::ResamplerMode::Authentic);
    voice.trigger(&sample, 214, 64);

    assert(voice.isActive());
    assert(voice.getPeriod() == 214);
    assert(voice.getVolume() == 64);

    float sumSq = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float s = voice.renderSample();
        sumSq += s * s;
    }
    assert(sumSq > 0.0f);

    // Pitch: halving the period doubles the frequency, also below the 113 limit of the BLEP engine
    auto crossings = [&](uint16_t period) {
        paulascape::PaulaVoice v;
        v.setSampleRate(44100.0);
        v.setResamplerMode(paulascape::ResamplerMode::Authentic);
        v.trigger(&sample, period, 64);
        int count = 0;
        float prev = 0;
        for (int i = 0; i < 44100; ++i) {
            const float x = v.renderSample();
            if ((x > 0.05f && prev < -0.05f) || (x < -0.05f && prev > 0.05f)) { ++count; prev = x; }
            else if (std::fabs(x) > 0.05f) prev = x;
        }
        return count;
    };
    const int c214 = crossings(214), c107 = crossings(107), c53 = crossings(53);
    assert(c214 > 100);
    assert(std::abs(c107 - 2 * c214) < c214 / 10);
    assert(std::abs(c53 - 4 * c214) < c214 / 5);

    // A one-shot sample ends: the voice frees itself
    paulascape::ModSample shot;
    shot.header.length = 64; shot.header.volume = 64;
    shot.pcmData.assign(64, 90);
    paulascape::PaulaVoice once;
    once.setSampleRate(44100.0);
    once.trigger(&shot, 428, 64);
    for (int i = 0; i < 4000; ++i) once.renderSample();
    assert(!once.isActive());
    assert(!once.hasOutput());

    // Test RC Filters
    paulascape::RcFilters filter;
    filter.setSampleRate(44100.0);
    filter.setFilterModel(paulascape::FilterModel::A500);
    filter.setLedFilter(true);

    float filteredOut = filter.processSample(0.8f);
    assert(std::abs(filteredOut) > 0.0f);

    std::cout << "Paula Voice and RC Filters Test Passed Successfully!" << std::endl;
    return 0;
}
