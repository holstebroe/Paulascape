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
