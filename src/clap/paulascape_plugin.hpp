#pragma once

#include <clap/clap.h>
#include "core/voice_pool.hpp"
#include "core/replayer.hpp"
#include "core/mod_loader.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace paulascape {

enum ParamId : clap_id {
    PARAM_PLAYBACK_MODE = 0,
    PARAM_SUB_MODE,
    PARAM_OUTPUT_LAYOUT,
    PARAM_RESAMPLER_MODE,
    PARAM_FILTER_MODEL,
    PARAM_LED_FILTER,
    PARAM_STEREO_SEPARATION,
    PARAM_PITCH_MODE,
    PARAM_TEMPO_MODE,
    PARAM_ROWS_PER_BEAT,
    PARAM_PATTERN_BASE_NOTE,
    PARAM_SONG_ORDER_KEY,
    PARAM_CLOCK,
    PARAM_COUNT
};

// Lock-free ring the audio thread fills and the GUI reads for the channel scopes.
struct ScopeTap {
    static constexpr size_t SIZE = 512;
    std::array<std::array<float, SIZE>, 4> data{};
    std::atomic<uint32_t> writeIndex{0};

    void push(const std::array<float, 4>& v) {
        const uint32_t w = writeIndex.load(std::memory_order_relaxed);
        for (size_t c = 0; c < 4; ++c) data[c][w % SIZE] = v[c];
        writeIndex.store(w + 1, std::memory_order_release);
    }
};

struct SlotView {
    std::string name;
    uint32_t length = 0;
    bool loop = false;
    uint8_t volume = 64;
    int8_t finetune = 0;
    bool legato = false;
    uint8_t inKey = 60;
    uint8_t outKey = 60;
};

// Everything the GUI needs, copied under the state lock so drawing never blocks audio.
struct UiSnapshot {
    std::string title;
    std::array<SlotView, 32> slots; // 1..31 used
    uint8_t songLength = 1;
    uint8_t numPatterns = 0;
    std::array<uint8_t, 128> orderList{};
    std::array<double, PARAM_COUNT> params{};
    uint8_t selectedSlot = 1;
    int playingPattern = -1;
    int playingRow = 0;
};

enum class SlotField { Volume, Finetune, Loop, Legato, InKey, OutKey };

class PaulascapePlugin {
public:
    PaulascapePlugin(const clap_host_t* host);
    virtual ~PaulascapePlugin();

    bool init();
    void destroy();
    bool activate(double sampleRate, uint32_t minFramesCount, uint32_t maxFramesCount);
    void deactivate();
    bool startProcessing();
    void stopProcessing();
    void reset();

    clap_process_status process(const clap_process_t* process);
    void flush(const clap_input_events_t* in, const clap_output_events_t* out);

    // Params
    uint32_t paramsCount() const;
    bool paramsInfo(uint32_t paramIndex, clap_param_info_t* paramInfo) const;
    bool paramsValue(clap_id paramId, double* outValue);
    bool paramsValueToText(clap_id paramId, double value, char* outBuffer, uint32_t outBufferCapacity);
    bool paramsTextToValue(clap_id paramId, const char* text, double* outValue);

    // State
    bool stateSave(const clap_ostream_t* stream);
    bool stateLoad(const clap_istream_t* stream);

    // GUI-facing API (main thread). Each call takes the state lock briefly.
    UiSnapshot snapshot();
    void setParamFromGui(clap_id paramId, double value);
    bool loadModFile(const std::string& path);
    bool loadModMemory(const uint8_t* data, size_t size);
    bool importWavToSlot(uint8_t slot, const std::string& path);
    void selectSlot(uint8_t slot);
    void adjustSlot(uint8_t slot, SlotField field, int delta);
    bool exportMidi(const std::string& path, int patternIndex);   // full note export, -1 = whole song
    bool exportPatternClip(const std::string& path);              // pattern-mode clip for this plugin
    const ScopeTap& scopeTap() const { return scope; }

    // MIDI CC mapping (settings screen). Index order matches midiMapEntryName().
    static constexpr size_t MIDI_MAP_ENTRIES = 11;
    static const char* midiMapEntryName(size_t index);
    MidiMap getMidiMap();
    void adjustMidiMap(size_t index, int delta);

    // Output layout as seen by the audio ports extension
    bool fourMonoLayout();
    void setOutputLayoutFromHost(bool fourMono);
    uint32_t outputPortCount();

    static const char* paramName(clap_id id);
    static double paramMin(clap_id id);
    static double paramMax(clap_id id);
    static double paramDefault(clap_id id);

    // Direct accessors (tests)
    Module& getModule() { return currentModule; }
    const Module& getModule() const { return currentModule; }
    VoicePool& getVoicePool() { return voicePool; }
    Replayer& getReplayer() { return replayer; }

private:
    const clap_host_t* host = nullptr;
    double sampleRate = 44100.0;

    std::mutex stateMutex;
    Module currentModule;
    VoicePool voicePool;
    Replayer replayer;
    std::array<double, PARAM_COUNT> params{};
    uint8_t selectedSlot = 1;
    ScopeTap scope;

    std::mutex guiQueueMutex;
    std::vector<std::pair<clap_id, double>> guiParamQueue; // GUI changes to report to the host

    void applyParam(clap_id id, double value);       // caller holds stateMutex
    void applyAllParams();                           // caller holds stateMutex
    void handleEvent(const clap_event_header_t* header);
    void handleNoteOn(uint8_t channel, uint8_t key, uint8_t velocity);
    void handleNoteOff(uint8_t channel, uint8_t key);
    void renderRange(float** out, uint32_t numCh, uint32_t from, uint32_t to);
    void drainGuiQueue(const clap_output_events_t* out);
    void stopAllAudio();                             // caller holds stateMutex
    void replaceModule(Module&& mod);
    void requestHostFlush();
    void requestPortRescan();
    void handleMidi(const uint8_t* data);
    void applyFilters();                             // caller holds stateMutex
    MidiMap midiMap;
};

} // namespace paulascape
