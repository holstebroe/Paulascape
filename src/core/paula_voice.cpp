#include "paula_voice.hpp"
#include "pt2/pt2_tables.hpp"
#include <cmath>
#include <algorithm>

namespace paulascape {

PaulaVoice::PaulaVoice() {
    reset();
}

void PaulaVoice::reset() {
    active = false;
    activeSample = nullptr;
    samplePos = 0.0;
    lastOutputVal = 0.0f;
    currentPeriod = 214;
    currentVolume = 64;
    blep.reset();
}

void PaulaVoice::trigger(const ModSample* sample, uint16_t period, uint8_t volume) {
    if (!sample || sample->pcmData.empty()) {
        stop();
        return;
    }

    activeSample = sample;
    currentPeriod = (period > 0) ? period : 214;
    currentVolume = std::min<uint8_t>(volume, 64);
    samplePos = 0.0;
    active = true;
    lastOutputVal = 0.0f;
    blep.reset();
}

void PaulaVoice::stop() {
    active = false;
    activeSample = nullptr;
    samplePos = 0.0;
    lastOutputVal = 0.0f;
    blep.reset();
}

void PaulaVoice::setPeriod(uint16_t period) {
    if (period > 0) {
        currentPeriod = period;
    }
}

void PaulaVoice::setVolume(uint8_t volume) {
    currentVolume = std::min<uint8_t>(volume, 64);
}

void PaulaVoice::setSampleRate(double sr) {
    if (sr > 0.0) outputSampleRate = sr;
}

void PaulaVoice::setResamplerMode(ResamplerMode mode) {
    resamplerMode = mode;
}

void PaulaVoice::setNTSC(bool ntsc) {
    isNTSCClock = ntsc;
}

float PaulaVoice::renderSample() {
    if (!active || !activeSample || activeSample->pcmData.empty()) {
        return 0.0f;
    }

    const auto& pcm = activeSample->pcmData;
    const auto& hdr = activeSample->header;

    double paulaRate = pt2::periodToSampleRate(currentPeriod, isNTSCClock);
    if (paulaRate <= 0.0) return 0.0f;

    double posStep = paulaRate / outputSampleRate;
    size_t length = hdr.length;

    if (samplePos >= length) {
        if (hdr.loopEnabled && hdr.loopLength > 2) {
            samplePos = hdr.loopStart + std::fmod(samplePos - hdr.loopStart, hdr.loopLength);
        } else {
            stop();
            return 0.0f;
        }
    }

    float rawSample = 0.0f;

    if (resamplerMode == ResamplerMode::Authentic) {
        // Step resampler with BLEP anti-aliasing
        size_t idx = static_cast<size_t>(samplePos);
        int8_t pcmVal = (idx < pcm.size()) ? pcm[idx] : 0;
        float currentVal = (pcmVal / 128.0f) * (currentVolume / 64.0f);

        float stepDelta = currentVal - lastOutputVal;
        if (std::abs(stepDelta) > 1e-4f) {
            double offset = samplePos - std::floor(samplePos);
            blep.addBlep(offset, stepDelta);
            lastOutputVal = currentVal;
        }

        rawSample = currentVal + blep.getSample();
    } else {
        // Clean linear interpolation resampler
        size_t idx1 = static_cast<size_t>(samplePos);
        size_t idx2 = idx1 + 1;

        if (idx2 >= length) {
            idx2 = hdr.loopEnabled ? hdr.loopStart : idx1;
        }

        float v1 = (idx1 < pcm.size()) ? (pcm[idx1] / 128.0f) : 0.0f;
        float v2 = (idx2 < pcm.size()) ? (pcm[idx2] / 128.0f) : 0.0f;

        float frac = static_cast<float>(samplePos - std::floor(samplePos));
        float interp = v1 + frac * (v2 - v1);
        rawSample = interp * (currentVolume / 64.0f);
    }

    // Advance position
    samplePos += posStep;
    return rawSample;
}

} // namespace paulascape
