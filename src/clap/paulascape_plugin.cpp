#include "paulascape_plugin.hpp"
#include <cstring>
#include <algorithm>

namespace paulascape {

PaulascapePlugin::PaulascapePlugin(const clap_host_t* host) : host(host) {
    // Initialize default empty module with 1 dummy sample
    currentModule.title = "Untitled MOD";
}

PaulascapePlugin::~PaulascapePlugin() = default;

bool PaulascapePlugin::init() {
    voicePool.setModule(&currentModule);
    replayer.setModule(&currentModule);
    return true;
}

void PaulascapePlugin::destroy() {
}

bool PaulascapePlugin::activate(double sr, uint32_t minFrames, uint32_t maxFrames) {
    sampleRate = sr;
    voicePool.setSampleRate(sr);
    replayer.setSampleRate(sr);
    return true;
}

void PaulascapePlugin::deactivate() {
}

bool PaulascapePlugin::startProcessing() {
    return true;
}

void PaulascapePlugin::stopProcessing() {
}

void PaulascapePlugin::reset() {
    voicePool.allNotesOff();
}

void PaulascapePlugin::handleNoteOn(uint8_t channel, uint8_t key, uint8_t velocity) {
    if (mainMode == PlaybackMode::Pattern) {
        replayer.patternNoteOn(key);
    } else {
        voicePool.noteOn(channel, key, velocity);
    }
}

void PaulascapePlugin::handleNoteOff(uint8_t channel, uint8_t key) {
    if (mainMode == PlaybackMode::Pattern) {
        replayer.patternNoteOff(key);
    } else {
        voicePool.noteOff(channel, key);
    }
}

clap_process_status PaulascapePlugin::process(const clap_process_t* process) {
    if (!process) return CLAP_PROCESS_ERROR;

    // Process events
    const uint32_t numEvents = process->in_events ? process->in_events->size(process->in_events) : 0;
    for (uint32_t i = 0; i < numEvents; ++i) {
        const clap_event_header_t* header = process->in_events->get(process->in_events, i);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            if (header->type == CLAP_EVENT_NOTE_ON) {
                const auto* noteEv = reinterpret_cast<const clap_event_note_t*>(header);
                uint8_t vel = static_cast<uint8_t>(noteEv->velocity * 127.0f);
                handleNoteOn(noteEv->channel, noteEv->key, vel);
            } else if (header->type == CLAP_EVENT_NOTE_OFF) {
                const auto* noteEv = reinterpret_cast<const clap_event_note_t*>(header);
                handleNoteOff(noteEv->channel, noteEv->key);
            }
        }
    }

    // Process transport tempo
    if (process->transport && (process->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) {
        replayer.setHostTempo(process->transport->tempo);
    }

    // Process audio output
    if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32) {
        float** out = process->audio_outputs[0].data32;
        uint32_t numCh = process->audio_outputs[0].channel_count;
        uint32_t numFrames = process->frames_count;

        if (mainMode == PlaybackMode::Pattern) {
            replayer.processAudio(out, numCh, numFrames);
        } else {
            voicePool.processAudio(out, numCh, numFrames);
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

uint32_t PaulascapePlugin::paramsCount() const {
    return PARAM_COUNT;
}

bool PaulascapePlugin::paramsInfo(uint32_t paramIndex, clap_param_info_t* info) const {
    if (!info || paramIndex >= PARAM_COUNT) return false;

    std::memset(info, 0, sizeof(clap_param_info_t));
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;

    switch (paramIndex) {
        case PARAM_PLAYBACK_MODE:
            info->id = PARAM_PLAYBACK_MODE;
            std::strncpy(info->name, "Mode", sizeof(info->name));
            info->min_value = 0; info->max_value = 1; info->default_value = 0; // 0=Instrument, 1=Pattern
            break;
        case PARAM_SUB_MODE:
            info->id = PARAM_SUB_MODE;
            std::strncpy(info->name, "Sub Mode", sizeof(info->name));
            info->min_value = 0; info->max_value = 2; info->default_value = 0; // 0=Single, 1=Multi, 2=Drum
            break;
        case PARAM_OUTPUT_LAYOUT:
            info->id = PARAM_OUTPUT_LAYOUT;
            std::strncpy(info->name, "Output Layout", sizeof(info->name));
            info->min_value = 0; info->max_value = 1; info->default_value = 0; // 0=Stereo, 1=4-Mono
            break;
        case PARAM_RESAMPLER_MODE:
            info->id = PARAM_RESAMPLER_MODE;
            std::strncpy(info->name, "Resampler", sizeof(info->name));
            info->min_value = 0; info->max_value = 1; info->default_value = 0; // 0=Authentic, 1=Clean
            break;
        case PARAM_FILTER_MODEL:
            info->id = PARAM_FILTER_MODEL;
            std::strncpy(info->name, "Filter Model", sizeof(info->name));
            info->min_value = 0; info->max_value = 2; info->default_value = 0; // 0=A500, 1=A1200, 2=Off
            break;
        case PARAM_LED_FILTER:
            info->id = PARAM_LED_FILTER;
            std::strncpy(info->name, "LED Filter", sizeof(info->name));
            info->min_value = 0; info->max_value = 1; info->default_value = 0;
            break;
        case PARAM_STEREO_SEPARATION:
            info->id = PARAM_STEREO_SEPARATION;
            std::strncpy(info->name, "Stereo Separation", sizeof(info->name));
            info->min_value = 0.0; info->max_value = 1.0; info->default_value = 0.20;
            break;
        default:
            return false;
    }
    return true;
}

bool PaulascapePlugin::paramsValue(clap_id id, double* outValue) {
    if (!outValue) return false;
    switch (id) {
        case PARAM_PLAYBACK_MODE: *outValue = (mainMode == PlaybackMode::Pattern) ? 1.0 : 0.0; break;
        case PARAM_SUB_MODE: *outValue = static_cast<double>(subMode); break;
        case PARAM_OUTPUT_LAYOUT: *outValue = (outputLayout == OutputLayout::FourMono) ? 1.0 : 0.0; break;
        case PARAM_RESAMPLER_MODE: *outValue = (resamplerMode == ResamplerMode::Clean) ? 1.0 : 0.0; break;
        case PARAM_FILTER_MODEL: *outValue = static_cast<double>(filterModel); break;
        case PARAM_LED_FILTER: *outValue = ledFilterOn ? 1.0 : 0.0; break;
        case PARAM_STEREO_SEPARATION: *outValue = stereoSeparation; break;
        default: return false;
    }
    return true;
}

bool PaulascapePlugin::paramsValueToText(clap_id id, double value, char* outBuffer, uint32_t cap) {
    if (!outBuffer || cap == 0) return false;
    std::snprintf(outBuffer, cap, "%.2f", value);
    return true;
}

bool PaulascapePlugin::stateSave(const clap_ostream_t* stream) {
    if (!stream) return false;
    // Write title
    uint32_t titleLen = static_cast<uint32_t>(currentModule.title.size());
    stream->write(stream, &titleLen, sizeof(titleLen));
    if (titleLen > 0) {
        stream->write(stream, currentModule.title.data(), titleLen);
    }
    return true;
}

bool PaulascapePlugin::stateLoad(const clap_istream_t* stream) {
    if (!stream) return false;
    uint32_t titleLen = 0;
    if (stream->read(stream, &titleLen, sizeof(titleLen)) != sizeof(titleLen)) return false;
    if (titleLen > 0 && titleLen < 256) {
        std::vector<char> buf(titleLen);
        if (stream->read(stream, buf.data(), titleLen) == static_cast<int64_t>(titleLen)) {
            currentModule.title.assign(buf.data(), titleLen);
        }
    }
    return true;
}

} // namespace paulascape
