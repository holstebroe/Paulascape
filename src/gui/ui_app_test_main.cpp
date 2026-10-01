#include "gui/ui_app.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
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

    // Click slot 3's name: selects it and plays it for as long as the mouse is held
    ui.onMouseDown(60, 62 + 2 * 10 + 3, 1, false);
    assert(plugin.snapshot().selectedSlot == 3);
    plugin.flush(nullptr, nullptr);
    assert(plugin.getVoicePool().activeVoiceCount() == 1);
    ui.onMouseUp(60, 62 + 2 * 10 + 3, 1);
    plugin.flush(nullptr, nullptr);
    assert(plugin.getVoicePool().activeVoiceCount() == 0);

    // The wave panel describes the selected sample (slot 3 of BEDROCK loops)
    {
        const auto snap3 = plugin.snapshot(620);
        assert(snap3.waveMin.size() == 620 && snap3.slots[3].loopEnd > snap3.slots[3].loopStart);
        bool any = false;
        for (size_t i = 0; i < 620; ++i) any = any || snap3.waveMax[i] != snap3.waveMin[i];
        assert(any);
    }
    ui.render(fb);
    if (!outDir.empty()) dump(fb, outDir + "/front_wave.ppm");

    // Clicking the waveform plays the selected sample for as long as the button is held; dragging moves the key
    ui.onMouseDown(100, 200, 1, false);
    plugin.flush(nullptr, nullptr);
    assert(plugin.getVoicePool().activeVoiceCount() == 1);
    ui.onMouseMove(500, 200);
    plugin.flush(nullptr, nullptr);
    assert(plugin.getVoicePool().activeVoiceCount() == 1);
    ui.onMouseUp(500, 200, 1);
    plugin.flush(nullptr, nullptr);
    assert(plugin.getVoicePool().activeVoiceCount() == 0);

    // Dragging the MIDI button out: nothing happens on a plain click, a drag exports a file and starts the OS drag
    {
        std::string dragged;
        ui.onDragFile = [&](const std::string& path) { dragged = path; return true; };
        ui.onMouseDown(500, 12, 1, false);
        ui.onMouseUp(500, 12, 1);
        assert(dragged.empty());
        ui.onMouseDown(500, 12, 1, false);
        ui.onMouseMove(503, 12);
        assert(dragged.empty()); // not far enough yet
        ui.onMouseMove(540, 20);
        assert(dragged.size() > 4 && dragged.substr(dragged.size() - 4) == ".mid");
        assert(std::filesystem::exists(dragged) && std::filesystem::file_size(dragged) > 50);
        ui.onMouseUp(540, 20, 1);
        dragged.clear();
        ui.onMouseDown(540, 12, 1, false); // NOT
        ui.onMouseMove(600, 30);
        assert(dragged.find("_notes.mid") != std::string::npos);
        ui.onMouseUp(600, 30, 1);
        ui.onDragFile = nullptr;
    }

    // Volume cell left-click increments (or stays at 64), right-click decrements.
    const int before = plugin.snapshot().slots[3].volume;
    ui.onMouseDown(250, 62 + 2 * 10 + 3, 3, false);
    assert(plugin.snapshot().slots[3].volume == before - 1);

    // Loop and legato toggles; a sample without a loop range ignores clicks on its loop cell
    {
        const auto before = plugin.snapshot();
        int noLoop = 0;
        for (int i = 1; i <= 31 && !noLoop; ++i)
            if (!before.slots[i].loopDefined) noLoop = i;
        assert(noLoop > 0 && noLoop < 11);
        ui.onMouseDown(220, 62 + (noLoop - 1) * 10 + 3, 1, false);
        assert(plugin.snapshot().slots[noLoop].loop == before.slots[noLoop].loop);
    }
    const bool loop = plugin.snapshot().slots[3].loop;
    ui.onMouseDown(220, 62 + 2 * 10 + 3, 1, false);
    assert(plugin.snapshot().slots[3].loop == !loop);
    assert(plugin.snapshot().slots[3].loopDefined); // still defined after switching off
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

    // Pattern is one of the mode buttons next to single, multi and drum
    ui.onMouseDown(200, 34, 1, false);
    assert(plugin.snapshot().params[paulascape::PARAM_PLAYBACK_MODE] == 1.0);
    ui.render(fb);
    // Holding a pattern row plays that pattern; the first row is the whole song
    ui.onMouseDown(40, 62 + 10 + 3, 1, false);
    plugin.flush(nullptr, nullptr);
    assert(plugin.getReplayer().isPlaying() && plugin.getReplayer().getCurrentPattern() == 0);
    ui.onMouseUp(40, 75, 1);
    plugin.flush(nullptr, nullptr);
    assert(!plugin.getReplayer().isPlaying());
    ui.onMouseDown(40, 62 + 3, 1, false);
    plugin.flush(nullptr, nullptr);
    assert(plugin.getReplayer().isPlaying());
    ui.onMouseUp(40, 65, 1);
    plugin.flush(nullptr, nullptr);
    assert(!plugin.getReplayer().isPlaying());
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
