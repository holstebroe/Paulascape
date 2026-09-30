#pragma once

#include "mod_loader.hpp"
#include "paula_voice.hpp"
#include "rc_filters.hpp"
#include "voice_pool.hpp"
#include <array>

namespace paulascape {

enum class TempoSyncMode {
    FollowHost,
    ModNative
};

struct ReplayerChannelState {
    PaulaVoice voice;
    uint8_t sample = 0;       // current sample slot 1..31
    int8_t finetune = 0;
    uint8_t volume = 64;
    uint16_t period = 0;      // period being played (after slides)
    uint16_t notePeriod = 0;  // period of the last note on this channel
    uint16_t wantedPeriod = 0;// tone portamento target

    uint8_t cmd = 0;          // effect of the current row
    uint8_t param = 0;

    uint8_t portaSpeed = 0;   // tone portamento memory
    int8_t portaDir = 0;
    bool glissando = false;
    uint8_t vibratoCmd = 0;   // speed/depth memory
    uint8_t vibratoPos = 0;
    uint8_t vibratoWave = 0;
    uint8_t tremoloCmd = 0;
    uint8_t tremoloPos = 0;
    uint8_t tremoloWave = 0;
    uint8_t sampleOffset = 0; // 9xx memory

    uint8_t loopRow = 0;      // E6x
    uint8_t loopCount = 0;
    uint8_t delayTick = 0;    // EDx, 0 = none
    bool delayedTrigger = false;
    uint8_t pendingSample = 0;
    uint16_t pendingPeriod = 0;
};

class Replayer {
public:
    Replayer();

    void setModule(const Module* mod);
    void setSampleRate(double sr);
    void setTempoSyncMode(TempoSyncMode mode);
    void setHostTempo(double bpm);
    void setRowsPerBeat(int rowsPerBeat);
    void setResamplerMode(ResamplerMode mode);
    void setFilterModel(FilterModel model);
    void setLedFilter(bool enable);
    void setOutputLayout(OutputLayout layout);
    void setStereoSeparation(float separation);
    void setPatternBaseNote(uint8_t note); // e.g., C1 = 24
    void setSongOrderKey(uint8_t note);     // e.g., C0 = 12

    // Pattern Mode key triggers
    void patternNoteOn(uint8_t key);
    void patternNoteOff(uint8_t key);
    void stop();

    void processAudio(float** outputs, uint32_t numChannels, uint32_t numFrames);

    bool isPlaying() const { return playing; }
    int getCurrentPattern() const { return currentPattern; }
    int getCurrentRow() const { return currentRow; }
    uint16_t getChannelPeriod(size_t c) const { return channels[c & 3].voice.getPeriod(); }
    uint8_t getChannelVolume(size_t c) const { return channels[c & 3].voice.getVolume(); }
    bool isChannelActive(size_t c) const { return channels[c & 3].voice.isActive(); }
    float getScopeOutput(size_t ch) const { return scopeOutputs[ch & 3]; }

private:
    const Module* activeModule = nullptr;

    double sampleRate = 44100.0;
    double hostBPM = 120.0;
    TempoSyncMode tempoMode = TempoSyncMode::FollowHost;
    int rowsPerBeat = 4;
    uint8_t patternBaseNote = 24; // C1
    uint8_t songOrderKey = 12;    // C0

    bool playing = false;
    bool isSongOrderMode = false;
    int activePatternKey = -1;

    uint8_t currentSpeed = 6;
    uint8_t currentBPM = 125;
    uint8_t tickCounter = 0;

    int currentOrder = 0;
    int currentPattern = 0;
    int currentRow = 0;

    // Requests raised while processing a row, applied when the row ends
    int jumpOrder = -1;
    int breakRow = -1;
    int loopJumpRow = -1;
    int patternDelay = 0;
    bool inDelayRepeat = false;

    double samplesPerTick = 0.0;
    double sampleCounter = 0.0;

    std::array<ReplayerChannelState, 4> channels;
    std::array<RcFilters, 4> filters;
    OutputLayout outputLayout = OutputLayout::Stereo;
    float stereoSeparation = 0.20f;
    std::array<float, 4> scopeOutputs{0.0f, 0.0f, 0.0f, 0.0f};

    void calculateTickSamples();
    void tick();
    void processRow();
    void processEffectsOnTick();
    void triggerCell(size_t ch, const NoteCell& cell);
    void startNote(ReplayerChannelState& ch, uint32_t offset);
    void advanceRow();
    void applyTickEffect(ReplayerChannelState& ch);
    void resetChannels();
};

} // namespace paulascape
