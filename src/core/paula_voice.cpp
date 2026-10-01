#include "paula_voice.hpp"
#include "pt2/pt2_tables.hpp"
#include <algorithm>
#include <cmath>

namespace paulascape {

namespace {

// Two zero bytes: what Paula plays after a non-looping sample has ended.
const int8_t NULL_SAMPLE[2] = {0, 0};

constexpr int MAX_OVERSAMPLE = 8;

} // namespace

PaulaVoice::PaulaVoice() { reset(); }

void PaulaVoice::reset() {
    active = false;
    activeSample = nullptr;
    location = storedLocation = nullptr;
    currentPeriod = 214;
    currentVolume = 64;
    samplePos = 0.0;
    sample = 0.0f;
    delta = phase = blepDelta = blepPhase = storedDelta = 0.0f;
    storedVol = 0.0f;
    oversample = storedOversample = 1;
    sampleCounter = 0;
    nextSampleStage = sampleJustStarted = false;
    blep.reset();
}

void PaulaVoice::setSampleRate(double sr) {
    if (sr > 0.0) outputSampleRate = sr;
}

void PaulaVoice::setResamplerMode(ResamplerMode mode) { resamplerMode = mode; }
void PaulaVoice::setNTSC(bool ntsc) { isNTSCClock = ntsc; }

void PaulaVoice::setVolume(uint8_t volume) {
    currentVolume = std::min<uint8_t>(volume, 64);
    storedVol = static_cast<float>(currentVolume) * (1.0f / (128.0f * 64.0f));
}

void PaulaVoice::setPeriod(uint16_t period) {
    if (period == 0) return;
    currentPeriod = period;
    // Where pt2-clone clamps to 113 because BLEP needs at most one sample step per output sample, we
    // render at a higher internal rate for shorter periods and average down.
    const double clock = isNTSCClock ? pt2::PAULA_NTSC_CLK : pt2::PAULA_PAL_CLK;
    const double perOutput = clock / (2.0 * outputSampleRate * std::max<int>(period, 20)); // clock is 2x the CCK
    storedOversample = std::clamp(static_cast<int>(std::ceil(perOutput - 1e-9)), 1, MAX_OVERSAMPLE);
    storedDelta = static_cast<float>(perOutput / storedOversample);
    if (blepDelta == 0.0f) blepDelta = delta; // BLEP edge case, as in pt2-clone
}

void PaulaVoice::startDma(uint32_t startOffset) {
    const ModSample& s = *activeSample;
    const uint32_t bytes = static_cast<uint32_t>(std::min<size_t>(s.header.length, s.pcmData.size()));
    const uint32_t offset = std::min(startOffset & ~1u, bytes);
    const uint32_t words = (bytes - offset) / 2;
    if (words == 0) {
        active = false;
        return;
    }
    location = s.pcmData.data() + offset;
    lengthCounter = words;

    // What Paula reloads when the first pass ends: the loop, or silence
    const bool looped = s.header.loopEnabled && s.header.loopLength > 2 && s.header.loopStart < bytes;
    if (looped) {
        const uint32_t loopStart = s.header.loopStart & ~1u;
        const uint32_t loopEnd = std::min<uint32_t>(s.header.loopStart + s.header.loopLength, bytes);
        storedLocation = s.pcmData.data() + loopStart;
        storedLength = std::max<uint32_t>((loopEnd - loopStart) / 2, 1);
    } else {
        storedLocation = NULL_SAMPLE;
        storedLength = 1;
    }

    sampleCounter = 0;
    sampleJustStarted = true;
    refetchPeriod();
    phase = 0.0f; // must be cleared after refetchPeriod()
    active = true;
}

void PaulaVoice::trigger(const ModSample* smp, uint16_t period, uint8_t volume, uint32_t startOffset) {
    if (!smp || smp->pcmData.empty()) {
        stop();
        return;
    }
    activeSample = smp;
    ++triggerCount;
    lastStartOffset = startOffset;
    setVolume(volume);
    setPeriod(period > 0 ? period : 214);
    samplePos = static_cast<double>(startOffset);
    if (resamplerMode == ResamplerMode::Authentic) {
        startDma(startOffset);
    } else {
        active = true;
    }
}

void PaulaVoice::stop() {
    if (active && resamplerMode == ResamplerMode::Authentic && blep.lastValue != 0.0f) {
        // the output steps to zero: band-limit that step instead of clicking
        blep.add(0.0f, blep.lastValue);
        blep.lastValue = 0.0f;
    }
    active = false;
    sample = 0.0f;
    samplePos = 0.0;
}

void PaulaVoice::refetchPeriod() {
    blepPhase = phase;
    blepDelta = delta;
    // Paula only updates its period at this stage
    delta = storedDelta;
    oversample = storedOversample;
    nextSampleStage = true;
}

void PaulaVoice::nextSample() {
    if (sampleCounter == 0) {
        // time to fetch from DMA
        if (!sampleJustStarted) {
            if (--lengthCounter == 0) {
                lengthCounter = storedLength;
                location = storedLocation;
            }
        }
        sampleJustStarted = false;
        audDat[0] = *location++;
        audDat[1] = *location++;
        sampleCounter = 2;
    }

    sample = static_cast<float>(audDat[0]) * storedVol;

    if (sample != blep.lastValue) {
        if (blepDelta > blepPhase) {
            blep.add(blepPhase / blepDelta, blep.lastValue - sample);
        }
        blep.lastValue = sample;
    }

    audDat[0] = audDat[1];
    --sampleCounter;

    // A finished one-shot has reloaded the silent buffer: once its step to zero is rendered, free the voice
    if (location == NULL_SAMPLE + 2 && lengthCounter == storedLength && storedLocation == NULL_SAMPLE && sample == 0.0f) {
        active = false;
    }
}

float PaulaVoice::stepAuthentic() {
    float out = 0.0f;
    if (active) {
        if (nextSampleStage) {
            nextSampleStage = false;
            nextSample();
        }
        out = sample;
    }
    if (blep.samplesLeft > 0) out = blep.run(out);
    if (active) {
        phase += delta;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            refetchPeriod();
        }
    }
    return out;
}

float PaulaVoice::renderClean() {
    if (!active || !activeSample) return 0.0f;
    const auto& pcm = activeSample->pcmData;
    const auto& hdr = activeSample->header;
    const size_t length = std::min<size_t>(hdr.length, pcm.size());
    const bool looped = hdr.loopEnabled && hdr.loopLength > 2 && hdr.loopStart < length;
    const double loopEnd = looped ? std::min<double>(hdr.loopStart + hdr.loopLength, static_cast<double>(length))
                                  : static_cast<double>(length);

    const double rate = pt2::periodToSampleRate(currentPeriod, isNTSCClock);
    if (rate <= 0.0) return 0.0f;

    if (samplePos >= loopEnd) {
        if (looped) {
            samplePos = hdr.loopStart + std::fmod(samplePos - hdr.loopStart, loopEnd - hdr.loopStart);
        } else {
            active = false;
            return 0.0f;
        }
    }

    const size_t idx1 = static_cast<size_t>(samplePos);
    size_t idx2 = idx1 + 1;
    if (idx2 >= loopEnd) idx2 = looped ? hdr.loopStart : idx1;
    const float v1 = pcm[idx1] / 128.0f;
    const float v2 = pcm[std::min(idx2, pcm.size() - 1)] / 128.0f;
    const float frac = static_cast<float>(samplePos - std::floor(samplePos));
    const float out = (v1 + frac * (v2 - v1)) * (currentVolume / 64.0f);

    samplePos += rate / outputSampleRate;
    return out;
}

float PaulaVoice::renderSample() {
    if (resamplerMode == ResamplerMode::Clean) return renderClean();
    if (!hasOutput()) return 0.0f;

    const int k = oversample;
    if (k == 1) return stepAuthentic();
    float sum = 0.0f;
    for (int i = 0; i < k; ++i) sum += stepAuthentic();
    return sum / static_cast<float>(k);
}

} // namespace paulascape
