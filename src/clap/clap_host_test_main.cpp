// Minimal CLAP host: loads the built .clap and drives the real entry point, the way a DAW would.
// The GUI part runs only when a display is available (run under Xvfb).
#include <clap/clap.h>
#include <dlfcn.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <X11/Xlib.h>
#endif

static uint32_t g_restartRequests = 0;

static const clap_host_t host = {
    .clap_version = CLAP_VERSION_INIT, .host_data = nullptr, .name = "TestHost", .vendor = "t", .url = "", .version = "1",
    .get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    .request_restart = [](const clap_host_t*) { ++g_restartRequests; },
    .request_process = [](const clap_host_t*) {}, .request_callback = [](const clap_host_t*) {}};

struct EventList {
    std::vector<std::vector<uint8_t>> events;
    template <typename T> void add(const T& ev) {
        const auto* p = reinterpret_cast<const uint8_t*>(&ev);
        events.emplace_back(p, p + sizeof(T));
    }
};

static uint32_t evSize(const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<EventList*>(l->ctx)->events.size()); }
static const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
    return reinterpret_cast<const clap_event_header_t*>(static_cast<EventList*>(l->ctx)->events[i].data());
}
static bool evPush(const clap_output_events_t* l, const clap_event_header_t* h) {
    auto* list = static_cast<EventList*>(l->ctx);
    const auto* p = reinterpret_cast<const uint8_t*>(h);
    list->events.emplace_back(p, p + h->size);
    return true;
}

static clap_event_note_t noteEvent(uint16_t type, uint32_t time, int16_t key, double vel) {
    clap_event_note_t ev{};
    ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
    ev.note_id = -1; ev.port_index = 0; ev.channel = 0; ev.key = key; ev.velocity = vel;
    return ev;
}

static clap_event_midi_t midiEvent(uint32_t time, uint8_t a, uint8_t b, uint8_t c) {
    clap_event_midi_t ev{};
    ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
    ev.data[0] = a; ev.data[1] = b; ev.data[2] = c;
    return ev;
}

int main(int argc, char** argv) {
    assert(argc > 2);
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(lib && "dlopen failed");
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    assert(entry);
    assert(clap_version_is_compatible(entry->clap_version));
    assert(entry->init(argv[1]));
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    assert(factory && factory->get_plugin_count(factory) == 1);
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    assert(desc && std::strcmp(desc->id, "org.holstebroe.paulascape") == 0);

    const clap_plugin_t* plugin = factory->create_plugin(factory, &host, desc->id);
    assert(plugin);
    assert(plugin->init(plugin));

    auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
    auto* ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    auto* cfg = static_cast<const clap_plugin_audio_ports_config_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG));
    auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    auto* gui = static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
    assert(params && state && ports && cfg && notePorts);
#if defined(__linux__)
    assert(gui); // macOS has no GUI yet
#endif

    // Params: every id is consistent, text round-trips
    const uint32_t n = params->count(plugin);
    assert(n == 13);
    for (uint32_t i = 0; i < n; ++i) {
        clap_param_info_t info;
        assert(params->get_info(plugin, i, &info));
        assert(info.id == i && info.min_value <= info.default_value && info.default_value <= info.max_value);
        double v;
        assert(params->get_value(plugin, info.id, &v));
        char text[64];
        assert(params->value_to_text(plugin, info.id, v, text, sizeof(text)));
        double back;
        assert(params->text_to_value(plugin, info.id, text, &back));
        assert(std::fabs(back - v) < 0.011);
    }

    // Ports
    assert(ports->count(plugin, false) == 1 && ports->count(plugin, true) == 0);
    assert(notePorts->count(plugin, true) == 1);
    assert(cfg->count(plugin) == 2);
    clap_audio_ports_config_t c1;
    assert(cfg->get(plugin, 1, &c1) && c1.output_port_count == 4);

    assert(plugin->activate(plugin, 48000.0, 32, 512));
    assert(plugin->start_processing(plugin));

    // Load a MOD through state: build state with the plugin API is not exposed, so use the test MOD via a
    // second plugin-side route: state load of a blob saved by the plugin itself after parameter changes.
    const uint32_t frames = 512;
    std::vector<float> l(frames), r(frames);
    float* chans[2] = {l.data(), r.data()};
    clap_audio_buffer_t outBuf{};
    outBuf.data32 = chans;
    outBuf.channel_count = 2;

    EventList in, out;
    clap_input_events_t inEvents{&in, evSize, evGet};
    clap_output_events_t outEvents{&out, evPush};
    clap_process_t proc{};
    proc.frames_count = frames;
    proc.audio_outputs = &outBuf;
    proc.audio_outputs_count = 1;
    proc.in_events = &inEvents;
    proc.out_events = &outEvents;

    // With no MOD loaded, notes are silent but must not crash; parameter event changes a value
    clap_event_param_value_t pv{};
    pv.header = {sizeof(pv), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    pv.param_id = 4; pv.value = 1; pv.note_id = -1; pv.port_index = pv.channel = pv.key = -1;
    in.add(pv);
    in.add(noteEvent(CLAP_EVENT_NOTE_ON, 10, 60, 0.8));
    in.add(midiEvent(20, 0xB0, 1, 100));
    in.add(midiEvent(30, 0xE0, 0, 0x40));
    in.add(noteEvent(CLAP_EVENT_NOTE_OFF, 100, 60, 0));
    assert(plugin->process(plugin, &proc) != CLAP_PROCESS_ERROR);
    double filter;
    assert(params->get_value(plugin, 4, &filter) && filter == 1.0);

    // State round trip through the host
    std::vector<uint8_t> saved;
    clap_ostream_t os{&saved, [](const clap_ostream_t* s, const void* b, uint64_t len) -> int64_t {
        auto* v = static_cast<std::vector<uint8_t>*>(s->ctx);
        v->insert(v->end(), static_cast<const uint8_t*>(b), static_cast<const uint8_t*>(b) + len);
        return static_cast<int64_t>(len);
    }};
    assert(state->save(plugin, &os));
    struct In { const std::vector<uint8_t>* d; size_t pos; } rd{&saved, 0};
    clap_istream_t is{&rd, [](const clap_istream_t* s, void* b, uint64_t len) -> int64_t {
        auto* i = static_cast<In*>(s->ctx);
        const size_t k = std::min<size_t>(len, i->d->size() - i->pos);
        std::memcpy(b, i->d->data() + i->pos, k);
        i->pos += k;
        return static_cast<int64_t>(k);
    }};
    assert(state->load(plugin, &is));

    // Switching to the 4 x mono config and rendering on four ports
    assert(cfg->select(plugin, 1));
    assert(ports->count(plugin, false) == 4);
    clap_audio_port_info_t pi;
    assert(ports->get(plugin, 2, false, &pi) && pi.channel_count == 1 && pi.port_type && !std::strcmp(pi.port_type, CLAP_PORT_MONO));
    std::vector<float> m[4];
    float* mc[4];
    clap_audio_buffer_t mb[4]{};
    for (int i = 0; i < 4; ++i) { m[i].assign(frames, 0.f); mc[i] = m[i].data(); mb[i].data32 = &mc[i]; mb[i].channel_count = 1; }
    proc.audio_outputs = mb;
    proc.audio_outputs_count = 4;
    in.events.clear();
    assert(plugin->process(plugin, &proc) != CLAP_PROCESS_ERROR);
    assert(cfg->select(plugin, 0));
    proc.audio_outputs = &outBuf;
    proc.audio_outputs_count = 1;

    // Flush reports nothing pending and does not crash
    in.events.clear();
    out.events.clear();
    params->flush(plugin, &inEvents, &outEvents);

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);

#if defined(__linux__)
    if (std::getenv("DISPLAY")) {
        assert(gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false));
        assert(!gui->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, false));
        assert(gui->create(plugin, CLAP_WINDOW_API_X11, false));
        uint32_t w = 0, h = 0;
        assert(gui->get_size(plugin, &w, &h) && w == 640 && h == 400);

        // Host embeds the plugin in its own window
        Display* d = XOpenDisplay(nullptr);
        assert(d);
        Window parent = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, w, h, 0, 0, 0);
        XMapWindow(d, parent);
        XFlush(d);
        clap_window_t win{CLAP_WINDOW_API_X11, {.x11 = parent}};
        assert(gui->set_parent(plugin, &win));
        assert(gui->show(plugin));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        Window root, par, *kids = nullptr;
        unsigned count = 0;
        assert(XQueryTree(d, parent, &root, &par, &kids, &count));
        assert(count == 1); // the plugin window is a child of the host window
        if (kids) XFree(kids);

        assert(gui->hide(plugin));
        gui->destroy(plugin);
        XDestroyWindow(d, parent);
        XCloseDisplay(d);
    }
#else
    (void)gui;
#endif

    plugin->destroy(plugin);
    entry->deinit();
    dlclose(lib);
    std::cout << "CLAP host test passed" << std::endl;
    return 0;
}
