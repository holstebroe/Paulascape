#include "clap/paulascape_plugin.hpp"
#include <iostream>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

int main() {
    std::cout << "Testing CLAP Plugin interface..." << std::endl;

    static const clap_host_t dummyHost = {
        .clap_version = CLAP_VERSION_INIT,
        .host_data = nullptr,
        .name = "Test Host",
        .vendor = "Test Vendor",
        .url = "https://test.com",
        .version = "1.0.0",
        .get_extension = [](const clap_host_t* host, const char* extension_id) -> const void* { return nullptr; },
        .request_restart = [](const clap_host_t* host) {},
        .request_process = [](const clap_host_t* host) {},
        .request_callback = [](const clap_host_t* host) {}
    };

    paulascape::PaulascapePlugin plugin(&dummyHost);

    assert(plugin.init());
    assert(plugin.activate(44100.0, 32, 512));

    assert(plugin.paramsCount() > 0);

    clap_param_info_t info;
    assert(plugin.paramsInfo(paulascape::PARAM_PLAYBACK_MODE, &info));
    assert(info.id == paulascape::PARAM_PLAYBACK_MODE);

    double val = -1.0;
    assert(plugin.paramsValue(paulascape::PARAM_PLAYBACK_MODE, &val));
    assert(val == 0.0);

    // Load the real test MOD, render a note, round-trip the state.
    const std::string modPath = std::string(PAULASCAPE_TEST_DIR) + "/mods/BEDROCK.MOD";
    assert(plugin.loadModFile(modPath));
    paulascape::UiSnapshot snap = plugin.snapshot();
    assert(snap.numPatterns > 0);
    uint8_t slot = 0;
    for (uint8_t i = 1; i <= 31; ++i) if (snap.slots[i].length > 2) { slot = i; break; }
    assert(slot != 0);
    plugin.selectSlot(slot);

    auto& pool = plugin.getVoicePool();
    pool.noteOn(0, 60, 100);
    std::vector<float> l(2048), r(2048);
    float* outs[2] = {l.data(), r.data()};
    pool.processAudio(outs, 2, 2048);
    float peak = 0;
    for (float v : l) peak = std::max(peak, std::fabs(v));
    assert(peak > 0.01f);

    plugin.setParamFromGui(paulascape::PARAM_FILTER_MODEL, 1);
    plugin.adjustSlot(slot, paulascape::SlotField::Volume, -10);

    std::vector<uint8_t> saved;
    clap_ostream_t os{&saved, [](const clap_ostream_t* s, const void* buf, uint64_t n) -> int64_t {
        auto* v = static_cast<std::vector<uint8_t>*>(s->ctx);
        v->insert(v->end(), static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + n);
        return static_cast<int64_t>(n);
    }};
    assert(plugin.stateSave(&os));

    paulascape::PaulascapePlugin other(&dummyHost);
    assert(other.init());
    struct In { const std::vector<uint8_t>* d; size_t pos; } in{&saved, 0};
    clap_istream_t is{&in, [](const clap_istream_t* s, void* buf, uint64_t n) -> int64_t {
        auto* i = static_cast<In*>(s->ctx);
        size_t k = std::min<size_t>(n, i->d->size() - i->pos);
        std::memcpy(buf, i->d->data() + i->pos, k);
        i->pos += k;
        return static_cast<int64_t>(k);
    }};
    assert(other.stateLoad(&is));
    paulascape::UiSnapshot snap2 = other.snapshot();
    assert(snap2.title == snap.title.substr(0, snap2.title.size()) || snap2.title == plugin.snapshot().title);
    assert(snap2.numPatterns == snap.numPatterns);
    assert(snap2.slots[slot].length == snap.slots[slot].length);
    assert(snap2.slots[slot].volume == plugin.snapshot().slots[slot].volume);
    assert(snap2.params[paulascape::PARAM_FILTER_MODEL] == 1.0);
    assert(snap2.selectedSlot == slot);

    char text[32];
    assert(plugin.paramsValueToText(paulascape::PARAM_FILTER_MODEL, 1, text, sizeof(text)));
    double back = -1;
    assert(plugin.paramsTextToValue(paulascape::PARAM_FILTER_MODEL, text, &back) && back == 1.0);

    plugin.deactivate();
    plugin.destroy();

    std::cout << "CLAP Plugin Interface Test Passed Successfully!" << std::endl;
    return 0;
}
