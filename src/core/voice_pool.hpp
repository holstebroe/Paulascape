#pragma once

#include "paula_voice.hpp"
#include "rc_filters.hpp"
#include "mod_loader.hpp"
#include <array>
#include <vector>

namespace paulascape {

enum class PlaybackMode {
    Single,
    MultiChannel,
    Drum,
    Pattern
};

enum class OutputLayout {
    Stereo,
    FourMono
};

enum class PitchMode {
    PeriodTable,
    Free
};

constexpr size_t MAX_VOICES = 32;

struct ActiveVoiceSlot {
    PaulaVoice voice;
    int midiKey = -1;
    uint8_t midiChannel = 0;
    uint8_t sampleSlot = 0; // 1..31
    uint8_t amigaChannel = 0; // 0..3 (for Amiga hard panning L R R L)
    uint32_t age = 0;
};

class VoicePool {
public:
    VoicePool();

    void setModule(const Module* mod);
    void setSampleRate(double sr);
    void setResamplerMode(ResamplerMode mode);
    void setFilterModel(FilterModel model);
    void setLedFilter(bool enable);
    void setOutputLayout(OutputLayout layout);
    void setStereoSeparation(float separation); // 0.0 .. 1.0 (0 = mono, 1 = hard pan L R R L)
    void setPitchMode(PitchMode mode);
    void setPlaybackMode(PlaybackMode mode);
    void setSelectedSlot(uint8_t slot) { selectedSlot = slot < 1 ? 1 : (slot > 31 ? 31 : slot); }

    // Voice triggers
    void noteOn(uint8_t midiChannel, uint8_t key, uint8_t velocity);
    void noteOff(uint8_t midiChannel, uint8_t key);
    void allNotesOff();

    // Controllers
    void setPitchBend(uint8_t midiChannel, int semitones); // -12 .. +12
    void setLegato(uint8_t midiChannel, bool enable);

    // Audio rendering
    void processAudio(float** outputs, uint32_t numChannels, uint32_t numFrames);

    // Scope tap data (4 Amiga channels)
    float getScopeOutput(size_t ch) const { return scopeOutputs[ch & 3]; }

private:
    const Module* activeModule = nullptr;

    double sampleRate = 44100.0;
    PlaybackMode playbackMode = PlaybackMode::Single;
    OutputLayout outputLayout = OutputLayout::Stereo;
    ResamplerMode resamplerMode = ResamplerMode::Authentic;
    FilterModel filterModel = FilterModel::A500;
    bool ledFilterOn = false;
    float stereoSeparation = 0.20f;
    PitchMode pitchMode = PitchMode::PeriodTable;

    std::array<ActiveVoiceSlot, MAX_VOICES> voices;
    std::array<RcFilters, 4> channelFilters; // Filters for output channels
    std::array<bool, 16> channelLegato{};
    std::array<int, 16> channelPitchBend{};

    uint8_t selectedSlot = 1;
    uint8_t nextAmigaChannel = 0;
    uint32_t globalAge = 0;

    std::array<float, 4> scopeOutputs{0.0f, 0.0f, 0.0f, 0.0f};

    int allocateVoiceSlot();
    uint16_t keyToPeriod(uint8_t key, int8_t finetune, int bendSemitones) const;
};

} // namespace paulascape
