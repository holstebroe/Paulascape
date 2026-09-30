#include "paulascape_plugin.hpp"
#include "core/module_io.hpp"
#include "export/midi_export.hpp"
#include "import/wav_import.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace paulascape {

namespace {

constexpr uint32_t STATE_MAGIC = 0x41545350; // "PSTA"
constexpr uint32_t STATE_VERSION = 1;
constexpr uint32_t SCOPE_DECIMATION = 8;

struct ParamDesc {
    const char* name;
    double min, max, def;
    bool stepped;
};

const ParamDesc PARAM_DESCS[PARAM_COUNT] = {
    {"Mode", 0, 1, 0, true},
    {"Sub Mode", 0, 2, 0, true},
    {"Output Layout", 0, 1, 0, true},
    {"Resampler", 0, 1, 0, true},
    {"Filter Model", 0, 2, 0, true},
    {"LED Filter", 0, 1, 0, true},
    {"Stereo Separation", 0, 1, 0.20, false},
    {"Pitch Mode", 0, 1, 0, true},
    {"Tempo Mode", 0, 1, 0, true},
    {"Rows Per Beat", 0, 1, 0, true}, // 0 = 4, 1 = 8
    {"Pattern Base Note", 0, 127, 24, true},
    {"Song Order Key", 0, 127, 12, true},
};

const char* NOTE_NAMES[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};

std::string noteName(int key) {
    return std::string(NOTE_NAMES[key % 12]) + std::to_string(key / 12 - 1);
}

const char* enumText(clap_id id, int v) {
    switch (id) {
        case PARAM_PLAYBACK_MODE: return v ? "Pattern" : "Instrument";
        case PARAM_SUB_MODE: return v == 0 ? "Single" : (v == 1 ? "Multi-channel" : "Drum");
        case PARAM_OUTPUT_LAYOUT: return v ? "4 x mono" : "Stereo";
        case PARAM_RESAMPLER_MODE: return v ? "Clean" : "Authentic";
        case PARAM_FILTER_MODEL: return v == 0 ? "A500" : (v == 1 ? "A1200" : "Off");
        case PARAM_LED_FILTER: return v ? "On" : "Off";
        case PARAM_PITCH_MODE: return v ? "Free" : "Period table";
        case PARAM_TEMPO_MODE: return v ? "MOD native" : "Follow host";
        case PARAM_ROWS_PER_BEAT: return v ? "8" : "4";
        default: return nullptr;
    }
}

} // namespace

const char* PaulascapePlugin::paramName(clap_id id) { return id < PARAM_COUNT ? PARAM_DESCS[id].name : ""; }
double PaulascapePlugin::paramMin(clap_id id) { return id < PARAM_COUNT ? PARAM_DESCS[id].min : 0; }
double PaulascapePlugin::paramMax(clap_id id) { return id < PARAM_COUNT ? PARAM_DESCS[id].max : 0; }
double PaulascapePlugin::paramDefault(clap_id id) { return id < PARAM_COUNT ? PARAM_DESCS[id].def : 0; }

PaulascapePlugin::PaulascapePlugin(const clap_host_t* host) : host(host) {
    currentModule.title = "Untitled MOD";
    for (clap_id i = 0; i < PARAM_COUNT; ++i) params[i] = PARAM_DESCS[i].def;
}

PaulascapePlugin::~PaulascapePlugin() = default;

bool PaulascapePlugin::init() {
    std::lock_guard<std::mutex> lock(stateMutex);
    voicePool.setModule(&currentModule);
    replayer.setModule(&currentModule);
    applyAllParams();
    return true;
}

void PaulascapePlugin::destroy() {}

bool PaulascapePlugin::activate(double sr, uint32_t, uint32_t) {
    std::lock_guard<std::mutex> lock(stateMutex);
    sampleRate = sr;
    voicePool.setSampleRate(sr);
    replayer.setSampleRate(sr);
    applyAllParams();
    return true;
}

void PaulascapePlugin::deactivate() {}
bool PaulascapePlugin::startProcessing() { return true; }
void PaulascapePlugin::stopProcessing() {}

void PaulascapePlugin::stopAllAudio() {
    voicePool.allNotesOff();
    replayer.stop();
}

void PaulascapePlugin::reset() {
    std::lock_guard<std::mutex> lock(stateMutex);
    stopAllAudio();
}

void PaulascapePlugin::applyParam(clap_id id, double value) {
    if (id >= PARAM_COUNT) return;
    value = std::clamp(value, PARAM_DESCS[id].min, PARAM_DESCS[id].max);
    if (PARAM_DESCS[id].stepped) value = std::round(value);
    const bool modeChanged = (id == PARAM_PLAYBACK_MODE || id == PARAM_SUB_MODE) && params[id] != value;
    params[id] = value;

    const int v = static_cast<int>(value);
    switch (id) {
        case PARAM_PLAYBACK_MODE:
            break;
        case PARAM_SUB_MODE:
            voicePool.setPlaybackMode(static_cast<PlaybackMode>(v));
            break;
        case PARAM_OUTPUT_LAYOUT: {
            const auto layout = v ? OutputLayout::FourMono : OutputLayout::Stereo;
            voicePool.setOutputLayout(layout);
            replayer.setOutputLayout(layout);
            break;
        }
        case PARAM_RESAMPLER_MODE: {
            const auto mode = v ? ResamplerMode::Clean : ResamplerMode::Authentic;
            voicePool.setResamplerMode(mode);
            replayer.setResamplerMode(mode);
            break;
        }
        case PARAM_FILTER_MODEL: {
            const auto model = static_cast<FilterModel>(v);
            voicePool.setFilterModel(model);
            replayer.setFilterModel(model);
            break;
        }
        case PARAM_LED_FILTER:
            voicePool.setLedFilter(v != 0);
            replayer.setLedFilter(v != 0);
            break;
        case PARAM_STEREO_SEPARATION:
            voicePool.setStereoSeparation(static_cast<float>(value));
            replayer.setStereoSeparation(static_cast<float>(value));
            break;
        case PARAM_PITCH_MODE:
            voicePool.setPitchMode(v ? PitchMode::Free : PitchMode::PeriodTable);
            break;
        case PARAM_TEMPO_MODE:
            replayer.setTempoSyncMode(v ? TempoSyncMode::ModNative : TempoSyncMode::FollowHost);
            break;
        case PARAM_ROWS_PER_BEAT:
            replayer.setRowsPerBeat(v ? 8 : 4);
            break;
        case PARAM_PATTERN_BASE_NOTE:
            replayer.setPatternBaseNote(static_cast<uint8_t>(v));
            break;
        case PARAM_SONG_ORDER_KEY:
            replayer.setSongOrderKey(static_cast<uint8_t>(v));
            break;
        default:
            break;
    }
    // Keys sounding from the other engine would hang after a mode switch.
    if (modeChanged) stopAllAudio();
}

void PaulascapePlugin::applyAllParams() {
    voicePool.setSelectedSlot(selectedSlot);
    for (clap_id i = 0; i < PARAM_COUNT; ++i) applyParam(i, params[i]);
}

void PaulascapePlugin::handleNoteOn(uint8_t channel, uint8_t key, uint8_t velocity) {
    if (params[PARAM_PLAYBACK_MODE] >= 0.5) {
        replayer.patternNoteOn(key);
    } else {
        voicePool.noteOn(channel, key, velocity);
    }
}

void PaulascapePlugin::handleNoteOff(uint8_t channel, uint8_t key) {
    if (params[PARAM_PLAYBACK_MODE] >= 0.5) {
        replayer.patternNoteOff(key);
    } else {
        voicePool.noteOff(channel, key);
    }
}

void PaulascapePlugin::handleEvent(const clap_event_header_t* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    switch (header->type) {
        case CLAP_EVENT_NOTE_ON: {
            const auto* ev = reinterpret_cast<const clap_event_note_t*>(header);
            const int vel = std::clamp(static_cast<int>(std::lround(ev->velocity * 127.0)), 1, 127);
            handleNoteOn(static_cast<uint8_t>(std::max<int16_t>(ev->channel, 0)), static_cast<uint8_t>(ev->key), static_cast<uint8_t>(vel));
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE: {
            const auto* ev = reinterpret_cast<const clap_event_note_t*>(header);
            handleNoteOff(static_cast<uint8_t>(std::max<int16_t>(ev->channel, 0)), static_cast<uint8_t>(ev->key));
            break;
        }
        case CLAP_EVENT_PARAM_VALUE: {
            const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(header);
            applyParam(ev->param_id, ev->value);
            break;
        }
        default:
            break;
    }
}

void PaulascapePlugin::renderRange(float** out, uint32_t numCh, uint32_t from, uint32_t to) {
    const bool pattern = params[PARAM_PLAYBACK_MODE] >= 0.5;
    float* ptrs[8] = {};
    const uint32_t chans = std::min<uint32_t>(numCh, 8);
    while (from < to) {
        const uint32_t n = std::min(SCOPE_DECIMATION, to - from);
        for (uint32_t c = 0; c < chans; ++c) ptrs[c] = out[c] + from;
        std::array<float, 4> tap{};
        if (pattern) {
            replayer.processAudio(ptrs, chans, n);
            for (size_t c = 0; c < 4; ++c) tap[c] = replayer.getScopeOutput(c);
        } else {
            voicePool.processAudio(ptrs, chans, n);
            for (size_t c = 0; c < 4; ++c) tap[c] = voicePool.getScopeOutput(c);
        }
        scope.push(tap);
        from += n;
    }
}

void PaulascapePlugin::drainGuiQueue(const clap_output_events_t* out) {
    std::vector<std::pair<clap_id, double>> pending;
    {
        std::lock_guard<std::mutex> lock(guiQueueMutex);
        pending.swap(guiParamQueue);
    }
    if (!out) return;
    for (const auto& [id, value] : pending) {
        clap_event_param_value_t ev{};
        ev.header.size = sizeof(ev);
        ev.header.time = 0;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_PARAM_VALUE;
        ev.header.flags = 0;
        ev.param_id = id;
        ev.cookie = nullptr;
        ev.note_id = -1;
        ev.port_index = -1;
        ev.channel = -1;
        ev.key = -1;
        ev.value = value;
        out->try_push(out, &ev.header);
    }
}

clap_process_status PaulascapePlugin::process(const clap_process_t* proc) {
    if (!proc) return CLAP_PROCESS_ERROR;

    std::lock_guard<std::mutex> lock(stateMutex);

    if (proc->transport && (proc->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) {
        replayer.setHostTempo(proc->transport->tempo);
    }

    float** out = nullptr;
    uint32_t numCh = 0;
    const uint32_t numFrames = proc->frames_count;
    if (proc->audio_outputs_count > 0 && proc->audio_outputs[0].data32) {
        out = proc->audio_outputs[0].data32;
        numCh = proc->audio_outputs[0].channel_count;
        for (uint32_t c = 0; c < numCh; ++c) std::memset(out[c], 0, numFrames * sizeof(float));
    }

    // Sample-accurate: render up to each event, then apply it.
    uint32_t pos = 0;
    const uint32_t numEvents = proc->in_events ? proc->in_events->size(proc->in_events) : 0;
    for (uint32_t i = 0; i < numEvents; ++i) {
        const clap_event_header_t* header = proc->in_events->get(proc->in_events, i);
        const uint32_t t = std::min(header->time, numFrames);
        if (out && t > pos) {
            renderRange(out, numCh, pos, t);
            pos = t;
        }
        handleEvent(header);
    }
    if (out && pos < numFrames) renderRange(out, numCh, pos, numFrames);

    drainGuiQueue(proc->out_events);
    return CLAP_PROCESS_CONTINUE;
}

void PaulascapePlugin::flush(const clap_input_events_t* in, const clap_output_events_t* out) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        const uint32_t n = in ? in->size(in) : 0;
        for (uint32_t i = 0; i < n; ++i) handleEvent(in->get(in, i));
    }
    drainGuiQueue(out);
}

// ---- params ----

uint32_t PaulascapePlugin::paramsCount() const { return PARAM_COUNT; }

bool PaulascapePlugin::paramsInfo(uint32_t index, clap_param_info_t* info) const {
    if (!info || index >= PARAM_COUNT) return false;
    std::memset(info, 0, sizeof(*info));
    const ParamDesc& d = PARAM_DESCS[index];
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (d.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (enumText(index, 0)) info->flags |= CLAP_PARAM_IS_ENUM;
    std::snprintf(info->name, sizeof(info->name), "%s", d.name);
    info->min_value = d.min;
    info->max_value = d.max;
    info->default_value = d.def;
    return true;
}

bool PaulascapePlugin::paramsValue(clap_id id, double* outValue) {
    if (!outValue || id >= PARAM_COUNT) return false;
    std::lock_guard<std::mutex> lock(stateMutex);
    *outValue = params[id];
    return true;
}

bool PaulascapePlugin::paramsValueToText(clap_id id, double value, char* buf, uint32_t cap) {
    if (!buf || cap == 0 || id >= PARAM_COUNT) return false;
    const int v = static_cast<int>(std::lround(value));
    if (const char* t = enumText(id, v)) {
        std::snprintf(buf, cap, "%s", t);
    } else if (id == PARAM_STEREO_SEPARATION) {
        std::snprintf(buf, cap, "%d%%", static_cast<int>(std::lround(value * 100.0)));
    } else {
        std::snprintf(buf, cap, "%s", noteName(std::clamp(v, 0, 127)).c_str());
    }
    return true;
}

bool PaulascapePlugin::paramsTextToValue(clap_id id, const char* text, double* outValue) {
    if (!text || !outValue || id >= PARAM_COUNT) return false;
    if (id == PARAM_STEREO_SEPARATION) {
        *outValue = std::clamp(std::atof(text) / 100.0, 0.0, 1.0);
        return true;
    }
    auto equalsNoCase = [](const char* a, const char* b) {
        for (; *a && *b; ++a, ++b) {
            if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
        }
        return *a == *b;
    };
    for (int v = static_cast<int>(PARAM_DESCS[id].min); v <= static_cast<int>(PARAM_DESCS[id].max); ++v) {
        char buf[32];
        paramsValueToText(id, v, buf, sizeof(buf));
        if (equalsNoCase(buf, text)) {
            *outValue = v;
            return true;
        }
    }
    if (!enumText(id, 0)) {
        const int v = std::atoi(text);
        if (v >= 0 && v <= 127) { *outValue = v; return true; }
    }
    return false;
}

// ---- state ----

bool PaulascapePlugin::stateSave(const clap_ostream_t* stream) {
    if (!stream) return false;
    std::vector<uint8_t> blob;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) blob.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        put32(STATE_MAGIC);
        put32(STATE_VERSION);
        for (double v : params) {
            uint64_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            put32(static_cast<uint32_t>(bits));
            put32(static_cast<uint32_t>(bits >> 32));
        }
        blob.push_back(selectedSlot);
        ModuleIo::write(currentModule, blob);
    }
    size_t done = 0;
    while (done < blob.size()) {
        const int64_t n = stream->write(stream, blob.data() + done, blob.size() - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool PaulascapePlugin::stateLoad(const clap_istream_t* stream) {
    if (!stream) return false;
    std::vector<uint8_t> blob;
    uint8_t chunk[4096];
    for (;;) {
        const int64_t n = stream->read(stream, chunk, sizeof(chunk));
        if (n < 0) return false;
        if (n == 0) break;
        blob.insert(blob.end(), chunk, chunk + n);
    }
    const size_t header = 8 + PARAM_COUNT * 8 + 1;
    if (blob.size() < header) return false;
    auto get32 = [&](size_t off) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(blob[off + i]) << (8 * i);
        return v;
    };
    if (get32(0) != STATE_MAGIC || get32(4) != STATE_VERSION) return false;

    Module mod;
    if (!ModuleIo::read(blob.data() + header, blob.size() - header, mod)) return false;

    std::lock_guard<std::mutex> lock(stateMutex);
    stopAllAudio();
    currentModule = std::move(mod);
    selectedSlot = std::clamp<uint8_t>(blob[header - 1], 1, 31);
    for (clap_id i = 0; i < PARAM_COUNT; ++i) {
        const uint64_t bits = static_cast<uint64_t>(get32(8 + i * 8)) | (static_cast<uint64_t>(get32(12 + i * 8)) << 32);
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        params[i] = v;
    }
    voicePool.setModule(&currentModule);
    replayer.setModule(&currentModule);
    applyAllParams();
    return true;
}

// ---- GUI-facing API ----

UiSnapshot PaulascapePlugin::snapshot() {
    UiSnapshot s;
    std::lock_guard<std::mutex> lock(stateMutex);
    s.title = currentModule.title;
    for (size_t i = 1; i <= 31; ++i) {
        const ModSampleHeader& h = currentModule.samples[i].header;
        SlotView& v = s.slots[i];
        v.name = h.name;
        v.length = static_cast<uint32_t>(currentModule.samples[i].pcmData.size());
        v.loop = h.loopEnabled;
        v.volume = h.volume;
        v.finetune = h.finetune;
        v.legato = h.legato;
        v.inKey = h.inKey;
        v.outKey = h.outKey;
    }
    s.songLength = currentModule.songLength;
    s.numPatterns = currentModule.numPatterns;
    s.orderList = currentModule.orderList;
    s.params = params;
    s.selectedSlot = selectedSlot;
    s.playingPattern = replayer.isPlaying() ? replayer.getCurrentPattern() : -1;
    s.playingRow = replayer.getCurrentRow();
    return s;
}

void PaulascapePlugin::setParamFromGui(clap_id id, double value) {
    if (id >= PARAM_COUNT) return;
    double applied;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        applyParam(id, value);
        applied = params[id];
    }
    {
        std::lock_guard<std::mutex> lock(guiQueueMutex);
        guiParamQueue.emplace_back(id, applied);
    }
    requestHostFlush();
}

void PaulascapePlugin::requestHostFlush() {
    if (!host) return;
    const auto* hostParams = static_cast<const clap_host_params_t*>(
        host->get_extension ? host->get_extension(host, CLAP_EXT_PARAMS) : nullptr);
    if (hostParams && hostParams->request_flush) hostParams->request_flush(host);
}

void PaulascapePlugin::replaceModule(Module&& mod) {
    std::lock_guard<std::mutex> lock(stateMutex);
    stopAllAudio();
    currentModule = std::move(mod);
    selectedSlot = 1;
    voicePool.setSelectedSlot(selectedSlot);
    voicePool.setModule(&currentModule);
    replayer.setModule(&currentModule);
}

bool PaulascapePlugin::loadModMemory(const uint8_t* data, size_t size) {
    Module mod;
    if (!ModLoader::loadFromMemory(data, size, mod)) return false;
    replaceModule(std::move(mod));
    return true;
}

bool PaulascapePlugin::loadModFile(const std::string& path) {
    Module mod;
    if (!ModLoader::loadFromFile(path, mod)) return false;
    replaceModule(std::move(mod));
    return true;
}

bool PaulascapePlugin::importWavToSlot(uint8_t slot, const std::string& path) {
    if (slot < 1 || slot > 31) return false;
    ModSample imported;
    if (!WavImporter::importWavFile(path, imported)) return false;
    std::lock_guard<std::mutex> lock(stateMutex);
    stopAllAudio();
    ModSampleHeader& dst = currentModule.samples[slot].header;
    // Keep the slot's key mapping and legato flag; everything else comes from the import.
    imported.header.legato = dst.legato;
    imported.header.inKey = dst.inKey;
    imported.header.outKey = dst.outKey;
    currentModule.samples[slot] = std::move(imported);
    return true;
}

void PaulascapePlugin::selectSlot(uint8_t slot) {
    if (slot < 1 || slot > 31) return;
    std::lock_guard<std::mutex> lock(stateMutex);
    selectedSlot = slot;
    voicePool.setSelectedSlot(slot);
}

void PaulascapePlugin::adjustSlot(uint8_t slot, SlotField field, int delta) {
    if (slot < 1 || slot > 31) return;
    std::lock_guard<std::mutex> lock(stateMutex);
    ModSampleHeader& h = currentModule.samples[slot].header;
    switch (field) {
        case SlotField::Volume: h.volume = static_cast<uint8_t>(std::clamp(h.volume + delta, 0, 64)); break;
        case SlotField::Finetune: h.finetune = static_cast<int8_t>(std::clamp(h.finetune + delta, -8, 7)); break;
        case SlotField::Loop: h.loopEnabled = !h.loopEnabled; break;
        case SlotField::Legato: h.legato = !h.legato; break;
        case SlotField::InKey: h.inKey = static_cast<uint8_t>(std::clamp(h.inKey + delta, 0, 127)); break;
        case SlotField::OutKey: h.outKey = static_cast<uint8_t>(std::clamp(h.outKey + delta, 0, 127)); break;
    }
}

bool PaulascapePlugin::exportMidi(const std::string& path, int patternIndex) {
    Module copy;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        copy = currentModule;
    }
    return patternIndex >= 0 ? MidiExporter::exportPatternToMidi(copy, patternIndex, path)
                             : MidiExporter::exportSongToMidi(copy, path);
}

} // namespace paulascape
