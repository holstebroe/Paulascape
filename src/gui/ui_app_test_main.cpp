#include "gui/ui_app.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

static const clap_host_t dummyHost = {
    .clap_version = CLAP_VERSION_INIT, .host_data = nullptr, .name = "t", .vendor = "t", .url = "", .version = "1",
    .get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    .request_restart = [](const clap_host_t*) {}, .request_process = [](const clap_host_t*) {},
    .request_callback = [](const clap_host_t*) {}};

static void dump(const paulascape::Framebuffer& fb, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", fb.getWidth(), fb.getHeight());
    for (uint32_t i = 0; i < fb.getWidth() * fb.getHeight(); ++i) {
        const uint32_t p = fb.getPixels()[i];
        const unsigned char rgb[3] = {static_cast<unsigned char>(p >> 16), static_cast<unsigned char>(p >> 8), static_cast<unsigned char>(p)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : "";
    paulascape::PaulascapePlugin plugin(&dummyHost);
    assert(plugin.init());
    assert(plugin.activate(44100.0, 32, 512));
    assert(plugin.loadModFile(std::string(PAULASCAPE_TEST_DIR) + "/mods/BEDROCK.MOD"));

    paulascape::UiApp ui(plugin);
    paulascape::Framebuffer fb;
    ui.render(fb);
    assert(fb.getWidth() == 640 && fb.getHeight() == 400);
    if (!outDir.empty()) dump(fb, outDir + "/front_single.ppm");

    // Click slot 3's name: selects it.
    ui.onMouseDown(60, 62 + 2 * 10 + 3, 1, false);
    assert(plugin.snapshot().selectedSlot == 3);

    // Volume cell left-click increments (or stays at 64), right-click decrements.
    const int before = plugin.snapshot().slots[3].volume;
    ui.onMouseDown(250, 62 + 2 * 10 + 3, 3, false);
    assert(plugin.snapshot().slots[3].volume == before - 1);

    // Loop and legato toggles
    const bool loop = plugin.snapshot().slots[3].loop;
    ui.onMouseDown(220, 62 + 2 * 10 + 3, 1, false);
    assert(plugin.snapshot().slots[3].loop == !loop);
    ui.onMouseDown(315, 62 + 2 * 10 + 3, 1, false);
    assert(plugin.snapshot().slots[3].legato);

    // Drum sub-mode button
    ui.onMouseDown(4 + 64 + 4 + 56 + 4 + 10, 34, 1, false);
    assert(plugin.snapshot().params[paulascape::PARAM_SUB_MODE] == 2.0);
    ui.render(fb);
    const int inKey = plugin.snapshot().slots[1].inKey;
    ui.onMouseDown(360, 62 + 3, 1, false);
    assert(plugin.snapshot().slots[1].inKey == inKey + 1);
    if (!outDir.empty()) dump(fb, outDir + "/front_drum.ppm");

    // Wheel scroll
    ui.onWheel(100, 100, -1);
    ui.render(fb);

    // Pattern mode
    ui.onMouseDown(360, 12, 1, false);
    assert(plugin.snapshot().params[paulascape::PARAM_PLAYBACK_MODE] == 1.0);
    ui.render(fb);
    if (!outDir.empty()) dump(fb, outDir + "/front_pattern.ppm");

    // Settings: open via gear, change filter model, go back
    ui.onMouseDown(615, 12, 1, false);
    ui.render(fb);
    if (!outDir.empty()) dump(fb, outDir + "/settings.ppm");
    const double filter = plugin.snapshot().params[paulascape::PARAM_FILTER_MODEL];
    ui.onMouseDown(250, 32 + 28 * 2 + 10, 1, false);
    assert(plugin.snapshot().params[paulascape::PARAM_FILTER_MODEL] != filter);
    ui.onMouseDown(20, 12, 1, false);
    ui.render(fb);

    std::cout << "UI app test passed" << std::endl;
    return 0;
}
