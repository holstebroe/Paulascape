#pragma once

#include "mod_loader.hpp"
#include "blep.hpp"

namespace paulascape {

enum class ResamplerMode {
    Authentic,
    Clean
};

// One Paula channel. Authentic mode follows pt2-clone's pt2_paula.c (DMA fetch, period refetch, BLEP synthesis);
// clean mode is a linear-interpolating sample player.
class PaulaVoice {
public:
    PaulaVoice();

    void reset();
    void trigger(const ModSample* sample, uint16_t period, uint8_t volume = 64, uint32_t startOffset = 0);
    void stop();

    void setPeriod(uint16_t period);
    void setVolume(uint8_t volume); // 0..64
    void setSampleRate(double sampleRate);
    void setResamplerMode(ResamplerMode mode);
    void setNTSC(bool ntsc);

    bool isActive() const { return active; }
    // The BLEP tail of a stopped voice still has to be rendered for a few samples.
    bool hasOutput() const { return active || blep.samplesLeft > 0; }
    uint16_t getPeriod() const { return currentPeriod; }
    uint8_t getVolume() const { return currentVolume; }
    const ModSample* getSample() const { return activeSample; }

    float renderSample();

private:
    bool active = false;
    const ModSample* activeSample = nullptr;

    uint16_t currentPeriod = 214;
    uint8_t currentVolume = 64;
    double outputSampleRate = 44100.0;
    bool isNTSCClock = false;
    ResamplerMode resamplerMode = ResamplerMode::Authentic;

    // ---- authentic (Paula) state ----
    Blep blep;
    bool sampleJustStarted = false, nextSampleStage = false;
    int8_t audDat[2] = {0, 0};
    const int8_t* location = nullptr;
    const int8_t* storedLocation = nullptr;
    uint32_t lengthCounter = 0, storedLength = 0; // in words
    int sampleCounter = 0;
    float sample = 0.0f;            // current sample point, multiplied by volume
    float delta = 0.0f, phase = 0.0f;
    float blepDelta = 0.0f, blepPhase = 0.0f;
    float storedVol = 0.0f, storedDelta = 0.0f;
    int oversample = 1, storedOversample = 1;

    void startDma(uint32_t startOffset);
    void refetchPeriod();
    void nextSample();
    float stepAuthentic();
    float renderClean();

    // ---- clean state ----
    double samplePos = 0.0;
};

} // namespace paulascape
