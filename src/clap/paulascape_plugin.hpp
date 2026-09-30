#pragma once

#include <clap/clap.h>
#include "core/voice_pool.hpp"
#include "core/replayer.hpp"
#include "core/mod_loader.hpp"
#include <memory>
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
    PARAM_COUNT
};

class PaulascapePlugin {
public:
    PaulascapePlugin(const clap_host_t* host);
    ~PaulascapePlugin();

    bool init();
    void destroy();
    bool activate(double sampleRate, uint32_t minFramesCount, uint32_t maxFramesCount);
    void deactivate();
    bool startProcessing();
    void stopProcessing();
    void reset();

    clap_process_status process(const clap_process_t* process);

    // Params
    uint32_t paramsCount() const;
    bool paramsInfo(uint32_t paramIndex, clap_param_info_t* paramInfo) const;
    bool paramsValue(clap_id paramId, double* outValue);
    bool paramsValueToText(clap_id paramId, double value, char* outBuffer, uint32_t outBufferCapacity);

    // State
    bool stateSave(const clap_ostream_t* stream);
    bool stateLoad(const clap_istream_t* stream);

    // Module accessor
    Module& getModule() { return currentModule; }
    const Module& getModule() const { return currentModule; }

    VoicePool& getVoicePool() { return voicePool; }
    Replayer& getReplayer() { return replayer; }

private:
    const clap_host_t* host = nullptr;
    double sampleRate = 44100.0;

    Module currentModule;
    VoicePool voicePool;
    Replayer replayer;

    PlaybackMode mainMode = PlaybackMode::Single;
    PlaybackMode subMode = PlaybackMode::Single;
    OutputLayout outputLayout = OutputLayout::Stereo;
    ResamplerMode resamplerMode = ResamplerMode::Authentic;
    FilterModel filterModel = FilterModel::A500;
    bool ledFilterOn = false;
    double stereoSeparation = 0.20;
    PitchMode pitchMode = PitchMode::PeriodTable;
    TempoSyncMode tempoMode = TempoSyncMode::FollowHost;
    int rowsPerBeat = 4;
    int patternBaseNote = 24;
    int songOrderKey = 12;

    void handleNoteOn(uint8_t channel, uint8_t key, uint8_t velocity);
    void handleNoteOff(uint8_t channel, uint8_t key);
};

} // namespace paulascape
