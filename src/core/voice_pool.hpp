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

// MIDI CC numbers that drive MOD effects. Defaults follow the design document;
// every entry can be changed on the settings screen.
struct MidiMap {
    uint8_t modWheel = 1;       // 4xy vibrato depth
    uint8_t glide = 5;          // 3xx tone portamento speed (with legato)
    uint8_t legato = 68;        // legato footswitch
    uint8_t vibratoSpeed = 76;  // speed for 4xy and 7xy
    uint8_t sampleOffset = 70;  // 9xx
    uint8_t retrigger = 71;     // E9x
    uint8_t noteCut = 72;       // ECx
    uint8_t ledFilter = 74;     // E0x
    uint8_t fxNumber = 20;      // effect command: number
    uint8_t fxHigh = 21;        // effect command: parameter high nibble
    uint8_t fxLow = 22;         // effect command: low nibble, applies the effect
};

struct ActiveVoiceSlot {
    PaulaVoice voice;
    int midiKey = -1;
    uint8_t midiChannel = 0;
    uint8_t sampleSlot = 0; // 1..31
    uint8_t amigaChannel = 0; // 0..3 (for Amiga hard panning L R R L)
    uint32_t age = 0;
    bool held = false;      // key is down

    // Pitch and volume before modulation
    double period = 214.0;       // current pitch period (changed by slides and legato)
    double wantedPeriod = 0.0;   // tone portamento target, 0 = none
    uint8_t portaSpeed = 0;
    uint8_t baseVolume = 64;
    int8_t finetune = 0;

    // Effect state (same meaning as in the replayer)
    uint8_t cmd = 0;
    uint8_t param = 0;
    uint8_t vibratoPos = 0, vibratoWave = 0, tremoloPos = 0, tremoloWave = 0;
    uint8_t vibratoCmd = 0, tremoloCmd = 0;
    bool glissando = false;
    uint32_t ticksSinceStart = 0;
    uint32_t rowTick = 0;        // ticks since the effect command (or note) that started the current row
    uint8_t delayTicks = 0;      // note delay (EDx) remaining
    bool fresh = false;          // started at this instant, nothing rendered yet

    // Clean-mode release fade
    bool releasing = false;
    float fadeGain = 1.0f;
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
    void setNTSC(bool ntsc);
    void setHostTempo(double bpm);
    void setSelectedSlot(uint8_t slot) {
        selectedSlot = slot < 1 ? 1 : (slot > 31 ? 31 : slot);
        channelProgram.fill(0); // choosing a slot by hand overrides MIDI program changes
    }
    void programChange(uint8_t midiChannel, uint8_t program);
    void setMidiMap(const MidiMap& map) { midiMap = map; }
    const MidiMap& getMidiMap() const { return midiMap; }
    void setPitchBendRange(double semitones) { bendRange = semitones; }

    // Voice triggers
    void noteOn(uint8_t midiChannel, uint8_t key, uint8_t velocity);
    void noteOff(uint8_t midiChannel, uint8_t key);
    void allNotesOff();

    // Controllers
    void setPitchBend(uint8_t midiChannel, int semitones);          // -12 .. +12
    void setPitchBendValue(uint8_t midiChannel, int value14);       // 0..16383, 8192 = centre
    void setLegato(uint8_t midiChannel, bool enable);
    void controlChange(uint8_t midiChannel, uint8_t cc, uint8_t value);
    void channelPressure(uint8_t midiChannel, uint8_t value);
    void applyEffect(uint8_t midiChannel, uint8_t cmd, uint8_t param);   // effect command, see MIDI export

    // Audio rendering
    void processAudio(float** outputs, uint32_t numChannels, uint32_t numFrames);

    // Scope tap data (4 Amiga channels)
    float getScopeOutput(size_t ch) const { return scopeOutputs[ch & 3]; }

    // Introspection (tests, GUI)
    size_t activeVoiceCount() const;
    uint16_t voicePeriod(size_t index) const { return voices[index % MAX_VOICES].voice.getPeriod(); }
    bool voiceActive(size_t index) const { return voices[index % MAX_VOICES].voice.isActive(); }
    // The newest sounding (active, not releasing) voice on a MIDI channel, nullptr if none
    const PaulaVoice* channelVoice(uint8_t midiChannel) const;

private:
    struct ChannelState {
        bool legato = false;
        double bend = 0.0;            // semitones
        uint8_t modDepth = 0;         // 0..15 -> 4xy depth
        uint8_t modSpeed = 6;         // 0..15
        uint8_t pressureDepth = 0;    // 0..15 -> 7xy depth
        uint8_t glide = 0;            // CC5 raw
        uint8_t sampleOffset = 0;     // CC70 raw, for the next note
        uint8_t retrigger = 0;        // ticks, 0 = off
        uint8_t noteCut = 0;          // ticks, 0 = off
        uint8_t fxNumber = 0, fxHigh = 0;
        uint8_t rpnMsb = 127, rpnLsb = 127; // selected RPN (127/127 = none)
        double bendRange = -1.0;      // semitones set with RPN 0, below 0 = the global setting
        uint8_t pendingCmd = 0, pendingParam = 0;
        int pendingTicks = 0;         // effect command waiting for a note on the same row (ticks left)
        double tickCounter = 0.0;     // effect tick clock, restarted by notes and effect commands (a new row)
        int silentKey = -1;           // note at this instant that found no sample; a program change may follow
        uint8_t silentVelocity = 0;

        // Effect memory, per channel as in ProTracker, so a new note keeps it
        uint8_t portaSpeed = 0;       // 3xx
        uint8_t vibratoCmd = 0;       // 4xy
        uint8_t tremoloCmd = 0;       // 7xy
        uint8_t vibratoWave = 0;      // E4x
        uint8_t tremoloWave = 0;      // E7x
        bool glissando = false;       // E3x
        uint8_t offsetMemory = 0;     // 9xx
    };

    const Module* activeModule = nullptr;

    double sampleRate = 44100.0;
    PlaybackMode playbackMode = PlaybackMode::Single;
    OutputLayout outputLayout = OutputLayout::Stereo;
    ResamplerMode resamplerMode = ResamplerMode::Authentic;
    FilterModel filterModel = FilterModel::A500;
    bool ledFilterOn = false;
    bool ntsc = false;
    float stereoSeparation = 0.20f;
    PitchMode pitchMode = PitchMode::PeriodTable;
    double bendRange = 2.0;
    double hostBPM = 120.0;
    MidiMap midiMap;

    std::array<ActiveVoiceSlot, MAX_VOICES> voices;
    std::array<RcFilters, 4> channelFilters; // Filters for output channels
    std::array<ChannelState, 16> channels;

    uint8_t selectedSlot = 1;
    std::array<uint8_t, 16> channelProgram{}; // sample slot chosen by MIDI program change, 0 = use selectedSlot
    uint32_t globalAge = 0;
    double samplesPerTick = 918.75;

    std::array<float, 4> scopeOutputs{0.0f, 0.0f, 0.0f, 0.0f};

    int allocateVoiceSlot();
    double keyToPeriod(int key, int8_t finetune) const;
    void updateTickLength();
    void tick(uint8_t midiChannel);
    void restartTickClock(ChannelState& ch);
    void applyTickEffects(ActiveVoiceSlot& slot);
    void startEffectOnVoice(ActiveVoiceSlot& slot, uint8_t cmd, uint8_t param, bool atStart);
    void release(ActiveVoiceSlot& slot);
    double channelBendFactor(const ActiveVoiceSlot& slot) const;
    double channelBendRange(uint8_t midiChannel) const;
    void pushOutputPeriod(ActiveVoiceSlot& slot, double period);
    uint8_t outputVolume(const ActiveVoiceSlot& slot) const;
};

} // namespace paulascape
