#include "clap/paulascape_plugin.hpp"
#include <iostream>
#include <cassert>

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

    plugin.deactivate();
    plugin.destroy();

    std::cout << "CLAP Plugin Interface Test Passed Successfully!" << std::endl;
    return 0;
}
