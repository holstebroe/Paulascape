#include "paulascape_plugin.hpp"
#include "gui/gui_window.hpp"
#include "gui/ui_app.hpp"
#include <cstdio>
#include <memory>
#include <clap/clap.h>
#include <cstring>

static const char* s_features[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

// The CLAP plugin instance: the engine plus the optional GUI.
struct Instance : paulascape::PaulascapePlugin {
    explicit Instance(const clap_host_t* host) : PaulascapePlugin(host), host(host) {}
    const clap_host_t* host;
    std::unique_ptr<paulascape::UiApp> ui;
    std::unique_ptr<paulascape::GuiWindow> window;
    bool floating = false;

    void ensureUi() {
        if (ui) return;
        ui = std::make_unique<paulascape::UiApp>(*this);
        ui->onScaleChanged = [this](int scale) {
            if (window) window->setScale(scale);
            const auto* gui = static_cast<const clap_host_gui_t*>(host->get_extension ? host->get_extension(host, CLAP_EXT_GUI) : nullptr);
            if (gui && gui->request_resize && !floating) {
                uint32_t w, h;
                window->pixelSize(w, h);
                gui->request_resize(host, w, h);
            }
        };
        window = std::make_unique<paulascape::GuiWindow>(*ui);
    }
};

static Instance* inst(const clap_plugin_t* plugin) {
    return static_cast<Instance*>(static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data));
}

// GUI extension
static bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool) {
    return paulascape::GuiWindow::isSupported() && api && std::strcmp(api, paulascape::GuiWindow::clapApi()) == 0;
}
static bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* is_floating) {
    if (!paulascape::GuiWindow::isSupported()) return false;
    *api = paulascape::GuiWindow::clapApi();
    *is_floating = false;
    return true;
}
static bool gui_create(const clap_plugin_t* plugin, const char* api, bool is_floating) {
    if (!gui_is_api_supported(plugin, api, is_floating)) return false;
    Instance* self = inst(plugin);
    self->ensureUi();
    self->floating = is_floating;
    if (is_floating) return self->window->create(0);
    return true; // the window is created once the host gives us a parent
}
static void gui_destroy(const clap_plugin_t* plugin) {
    Instance* self = inst(plugin);
    if (self->window) self->window->destroy();
}
static bool gui_set_scale(const clap_plugin_t*, double) { return false; }
static bool gui_get_size(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) {
    Instance* self = inst(plugin);
    self->ensureUi();
    self->window->pixelSize(*w, *h);
    return true;
}
static bool gui_can_resize(const clap_plugin_t*) { return false; }
static bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
static bool gui_adjust_size(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) {
    return gui_get_size(plugin, w, h);
}
static bool gui_set_size(const clap_plugin_t*, uint32_t, uint32_t) { return true; }
static bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window) {
    Instance* self = inst(plugin);
    if (!window || !self->window) return false;
#if defined(_WIN32)
    return self->window->create(reinterpret_cast<uintptr_t>(window->win32));
#elif defined(__linux__)
    return self->window->create(static_cast<uintptr_t>(window->x11));
#else
    return false;
#endif
}
static bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }
static void gui_suggest_title(const clap_plugin_t*, const char*) {}
static bool gui_show(const clap_plugin_t* plugin) {
    Instance* self = inst(plugin);
    if (!self->window) return false;
    self->window->show();
    return true;
}
static bool gui_hide(const clap_plugin_t* plugin) {
    Instance* self = inst(plugin);
    if (!self->window) return false;
    self->window->hide();
    return true;
}

static const clap_plugin_gui_t s_gui_extension = {
    .is_api_supported = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = gui_create,
    .destroy = gui_destroy,
    .set_scale = gui_set_scale,
    .get_size = gui_get_size,
    .can_resize = gui_can_resize,
    .get_resize_hints = gui_get_resize_hints,
    .adjust_size = gui_adjust_size,
    .set_size = gui_set_size,
    .set_parent = gui_set_parent,
    .set_transient = gui_set_transient,
    .suggest_title = gui_suggest_title,
    .show = gui_show,
    .hide = gui_hide,
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
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    return self->paramsTextToValue(param_id, param_value_text, out_value);
}

static void clap_params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
    auto* self = static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
    self->flush(in, out);
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

// Audio ports extension: one stereo port, or four mono ports (Amiga channel n on port n)
static paulascape::PaulascapePlugin* core(const clap_plugin_t* plugin) {
    return static_cast<paulascape::PaulascapePlugin*>(plugin->plugin_data);
}

static uint32_t clap_audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
    return is_input ? 0 : core(plugin)->outputPortCount();
}

static bool clap_audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
    if (is_input || !info || index >= core(plugin)->outputPortCount()) return false;
    const bool mono = core(plugin)->fourMonoLayout();
    std::memset(info, 0, sizeof(*info));
    info->id = index;
    if (mono) std::snprintf(info->name, sizeof(info->name), "Channel %u", index + 1);
    else std::snprintf(info->name, sizeof(info->name), "Main Output");
    info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
    info->channel_count = mono ? 1 : 2;
    info->port_type = mono ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t s_audio_ports_extension = {
    .count = clap_audio_ports_count,
    .get = clap_audio_ports_get,
};

// Audio ports config: the two output layouts
static uint32_t clap_ports_config_count(const clap_plugin_t*) { return 2; }

static bool clap_ports_config_get(const clap_plugin_t*, uint32_t index, clap_audio_ports_config_t* config) {
    if (index > 1 || !config) return false;
    std::memset(config, 0, sizeof(*config));
    config->id = index;
    if (index == 0) {
        std::snprintf(config->name, sizeof(config->name), "Stereo");
        config->output_port_count = 1;
        config->has_main_output = true;
        config->main_output_channel_count = 2;
        config->main_output_port_type = CLAP_PORT_STEREO;
    } else {
        std::snprintf(config->name, sizeof(config->name), "4 x mono");
        config->output_port_count = 4;
        config->has_main_output = true;
        config->main_output_channel_count = 1;
        config->main_output_port_type = CLAP_PORT_MONO;
    }
    return true;
}

static bool clap_ports_config_select(const clap_plugin_t* plugin, clap_id config_id) {
    if (config_id > 1) return false;
    core(plugin)->setOutputLayoutFromHost(config_id == 1);
    return true;
}

static const clap_plugin_audio_ports_config_t s_audio_ports_config_extension = {
    .count = clap_ports_config_count,
    .get = clap_ports_config_get,
    .select = clap_ports_config_select,
};

// Config info lets the host see which layout is current and what each one's ports look like
static clap_id clap_ports_config_current(const clap_plugin_t* plugin) { return core(plugin)->fourMonoLayout() ? 1 : 0; }

static bool clap_ports_config_info_get(const clap_plugin_t*, clap_id config_id, uint32_t index, bool is_input,
                                       clap_audio_port_info_t* info) {
    if (is_input || !info || config_id > 1 || index >= (config_id == 1 ? 4u : 1u)) return false;
    const bool mono = config_id == 1;
    std::memset(info, 0, sizeof(*info));
    info->id = index;
    if (mono) std::snprintf(info->name, sizeof(info->name), "Channel %u", index + 1);
    else std::snprintf(info->name, sizeof(info->name), "Main Output");
    info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
    info->channel_count = mono ? 1 : 2;
    info->port_type = mono ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_config_info_t s_audio_ports_config_info_extension = {
    .current_config = clap_ports_config_current,
    .get = clap_ports_config_info_get,
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
    if (auto* i = static_cast<Instance*>(self); i->window) i->window->destroy();
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
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0) return &s_audio_ports_config_extension;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) == 0 || std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT) == 0)
        return &s_audio_ports_config_info_extension;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &s_note_ports_extension;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0 && paulascape::GuiWindow::isSupported()) return &s_gui_extension;
    return nullptr;
}

static void clap_plugin_on_main_thread(const struct clap_plugin* plugin) {
}

static const clap_plugin_t* clap_create_plugin(const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id) {
    if (!clap_version_is_compatible(host->clap_version)) return nullptr;
    if (std::strcmp(plugin_id, s_paulascape_desc.id) != 0) return nullptr;

    auto* impl = static_cast<paulascape::PaulascapePlugin*>(new Instance(host));
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
