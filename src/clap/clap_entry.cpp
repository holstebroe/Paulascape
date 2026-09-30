#include "paulascape_plugin.hpp"
#include <clap/clap.h>
#include <cstring>

static const char* s_features[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

static const clap_plugin_descriptor_t s_paulascape_desc = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "org.holstebroe.paulascape",
    .name = "Paulascape",
    .vendor = "Søren Holstebroe",
    .url = "https://github.com/holstebroe/Paulascape",
    .manual_url = "",
    .support_url = "",
    .version = "1.0.0",
    .description = "Amiga ProTracker MOD Instrument Plugin",
    .features = s_features
};

// Params extension
static uint32_t clap_params_count(const clap_plugin_t* plugin) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->paramsCount();
}

static bool clap_params_get_info(const clap_plugin_t* plugin, uint32_t param_index, clap_param_info_t* param_info) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->paramsInfo(param_index, param_info);
}

static bool clap_params_get_value(const clap_plugin_t* plugin, clap_id param_id, double* out_value) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->paramsValue(param_id, out_value);
}

static bool clap_params_value_to_text(const clap_plugin_t* plugin, clap_id param_id, double value, char* out_buffer, uint32_t out_buffer_capacity) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->paramsValueToText(param_id, value, out_buffer, out_buffer_capacity);
}

static bool clap_params_text_to_value(const clap_plugin_t* plugin, clap_id param_id, const char* param_value_text, double* out_value) {
    return false;
}

static void clap_params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
}

static const clap_plugin_params_t s_params_extension = {
    .count = clap_params_count,
    .get_info = clap_params_get_info,
    .get_value = clap_params_get_value,
    .value_to_text = clap_params_value_to_text,
    .text_to_value = clap_params_text_to_value,
    .flush = clap_params_flush,
};

// State extension
static bool clap_state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->stateSave(stream);
}

static bool clap_state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->stateLoad(stream);
}

static const clap_plugin_state_t s_state_extension = {
    .save = clap_state_save,
    .load = clap_state_load,
};

// Audio ports extension
static uint32_t clap_audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
    return is_input ? 0 : 1;
}

static bool clap_audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
    if (is_input || index != 0 || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::strncpy(info->name, "Main Output", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t s_audio_ports_extension = {
    .count = clap_audio_ports_count,
    .get = clap_audio_ports_get,
};

// Note ports extension
static uint32_t clap_note_ports_count(const clap_plugin_t* plugin, bool is_input) {
    return is_input ? 1 : 0;
}

static bool clap_note_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_note_port_info_t* info) {
    if (!is_input || index != 0 || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::strncpy(info->name, "MIDI In", sizeof(info->name));
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    return true;
}

static const clap_plugin_note_ports_t s_note_ports_extension = {
    .count = clap_note_ports_count,
    .get = clap_note_ports_get,
};

static bool clap_plugin_init(const struct clap_plugin* plugin) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->init();
}

static void clap_plugin_destroy(const struct clap_plugin* plugin) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    self->destroy();
    delete self;
    delete plugin;
}

static bool clap_plugin_activate(const struct clap_plugin* plugin, double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->activate(sample_rate, min_frames_count, max_frames_count);
}

static void clap_plugin_deactivate(const struct clap_plugin* plugin) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    self->deactivate();
}

static bool clap_plugin_start_processing(const struct clap_plugin* plugin) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->startProcessing();
}

static void clap_plugin_stop_processing(const struct clap_plugin* plugin) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    self->stopProcessing();
}

static void clap_plugin_reset(const struct clap_plugin* plugin) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    self->reset();
}

static clap_process_status clap_plugin_process(const struct clap_plugin* plugin, const clap_process_t* process) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->process(process);
}

static const void* clap_plugin_get_extension(const struct clap_plugin* plugin, const char* id) {
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &s_params_extension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &s_state_extension;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports_extension;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &s_note_ports_extension;
    return nullptr;
}

static void clap_plugin_on_main_thread(const struct clap_plugin* plugin) {
}

static const clap_plugin_t* clap_create_plugin(const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id) {
    if (!clap_version_is_compatible(host->clap_version)) return nullptr;
    if (std::strcmp(plugin_id, s_paulascape_desc.id) != 0) return nullptr;

    auto* impl = new paulascape::PaulascapePlugin(host);
    auto* plugin = new clap_plugin_t();

    plugin->desc = &s_paulascape_desc;
    plugin->plugin_data = impl;
    plugin->init = clap_plugin_init;
    plugin->destroy = clap_plugin_destroy;
    plugin->activate = clap_plugin_activate;
    plugin->deactivate = clap_plugin_deactivate;
    plugin->start_processing = clap_plugin_start_processing;
    plugin->stop_processing = clap_plugin_stop_processing;
    plugin->reset = clap_plugin_reset;
    plugin->process = clap_plugin_process;
    plugin->get_extension = clap_plugin_get_extension;
    plugin->on_main_thread = clap_plugin_on_main_thread;

    return plugin;
}

static uint32_t clap_get_plugin_count(const clap_plugin_factory_t* factory) {
    return 1;
}

static const clap_plugin_descriptor_t* clap_get_plugin_descriptor(const clap_plugin_factory_t* factory, uint32_t index) {
    return (index == 0) ? &s_paulascape_desc : nullptr;
}

static const clap_plugin_factory_t s_plugin_factory = {
    .get_plugin_count = clap_get_plugin_count,
    .get_plugin_descriptor = clap_get_plugin_descriptor,
    .create_plugin = clap_create_plugin,
};

static bool clap_entry_init(const char* plugin_path) {
    return true;
}

static void clap_entry_deinit() {}

static const void* clap_entry_get_factory(const char* factory_id) {
    if (std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &s_plugin_factory;
    }
    return nullptr;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = clap_entry_init,
    .deinit = clap_entry_deinit,
    .get_factory = clap_entry_get_factory,
};
