#pragma once

#include "mod_loader.hpp"
#include "blep.hpp"

namespace paulascape {

enum class ResamplerMode {
    Authentic,
    Clean
};

class PaulaVoice {
public:
    PaulaVoice();

    void reset();
    void trigger(const ModSample* sample, uint16_t period, uint8_t volume = 64);
    void stop();

    void setPeriod(uint16_t period);
    void setVolume(uint8_t volume); // 0..64
    void setSampleRate(double sampleRate);
    void setResamplerMode(ResamplerMode mode);
    void setNTSC(bool ntsc);

    bool isActive() const { return active; }
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

    double samplePos = 0.0;
    float lastOutputVal = 0.0f;

    Blep blep;
};

} // namespace paulascape
