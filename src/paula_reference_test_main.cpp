// Compares the Paula voice and Amiga filters with renders made by pt2-clone's own code (tools/paula_reference).
#include "core/paula_voice.hpp"
#include "core/rc_filters.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace paulascape;

int main() {
    const double sr = 44100.0;
    const int N = 6000;
    std::vector<int8_t> data(400);
    for (int i = 0; i < 400; ++i) data[i] = static_cast<int8_t>(((i * 7) % 120) - 60 + (i > 200 ? 30 : 0));

    ModSample s;
    s.header.length = 400;
    s.header.volume = 64;
    s.header.loopEnabled = true;
    s.header.loopStart = 0;
    s.header.loopLength = 400;
    s.pcmData = data;

    const int periods[] = {214, 428, 124, 113};
    const struct { const char* name; FilterModel model; bool led; } configs[] = {
        {"a500", FilterModel::A500, false}, {"a500led", FilterModel::A500, true}, {"a1200", FilterModel::A1200, false}};

    int compared = 0;
    for (int period : periods) {
        for (const auto& cfg : configs) {
            const std::string path = std::string(PAULASCAPE_TEST_DIR) + "/resources/paula/paula_" + std::to_string(period) + "_" + cfg.name + ".f32";
            FILE* f = std::fopen(path.c_str(), "rb");
            assert(f && "missing reference render");
            std::vector<float> ref(N);
            assert(std::fread(ref.data(), sizeof(float), N, f) == static_cast<size_t>(N));
            std::fclose(f);

            PaulaVoice v;
            v.setSampleRate(sr);
            v.setResamplerMode(ResamplerMode::Authentic);
            RcFilters filter;
            filter.setSampleRate(sr);
            filter.setFilterModel(cfg.model);
            filter.setLedFilter(cfg.led);
            v.trigger(&s, static_cast<uint16_t>(period), 64);

            double maxDiff = 0, maxRef = 0;
            for (int i = 0; i < N; ++i) {
                const float out = filter.processSample(v.renderSample());
                maxDiff = std::fmax(maxDiff, std::fabs(out - ref[i]));
                maxRef = std::fmax(maxRef, std::fabs(ref[i]));
            }
            std::cout << "period " << period << " " << cfg.name << ": max diff " << maxDiff << " (peak " << maxRef << ")" << std::endl;
            assert(maxRef > 0.3);
            assert(maxDiff < 1e-3);
            ++compared;
        }
    }
    assert(compared == 12);
    std::cout << "Paula reference test passed" << std::endl;
    return 0;
}
