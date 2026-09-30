// Generates reference renders of pt2-clone's Paula emulation (pt2_paula.c, pt2_blep.c, pt2_rcfilters.c) for
// test/resources/paula/. See README.md in this folder. Not part of the normal build.
extern "C" {
#include "pt2_paula.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define DMACON 0xDFF096

static std::vector<int8_t> testData() {
    std::vector<int8_t> data(400);
    for (int i = 0; i < 400; ++i) data[i] = (int8_t)(((i * 7) % 120) - 60 + (i > 200 ? 30 : 0));
    return data;
}

int main(int argc, char** argv) {
    // pt2_paula.c keeps its state in statics, so every case is rendered by a fresh process:
    // gen_reference <output dir> <period> <config index 0..2>
    if (argc < 4) { std::fprintf(stderr, "usage: gen_reference <output dir> <period> <config 0..2>\n"); return 1; }
    const int onlyPeriod = std::atoi(argv[2]);
    const int onlyConfig = std::atoi(argv[3]);
    const double sr = 44100.0;
    const int N = 6000;
    const int periods[] = {214, 428, 124, 113};
    const struct { const char* name; uint32_t model; bool led; } configs[] = {
        {"a500", MODEL_A500, false}, {"a500led", MODEL_A500, true}, {"a1200", MODEL_A1200, false}};
    auto data = testData();
    (void)periods;
    for (int period : {onlyPeriod}) {
        for (const auto& cfg : {configs[onlyConfig]}) {
            paulaSetup(sr, cfg.model);
            if (cfg.led) paulaWriteByte(0xBFE001, 2);
            paulaWritePtr(0xDFF0A0, data.data());
            paulaWriteWord(0xDFF0A4, 200); // words
            paulaWriteWord(0xDFF0A6, (uint16_t)period);
            paulaWriteWord(0xDFF0A8, 64);
            paulaWriteWord(DMACON, 0x8001);
            // loop registers, written after DMA has started as ProTracker does
            paulaWritePtr(0xDFF0A0, data.data());
            paulaWriteWord(0xDFF0A4, 200);
            std::vector<float> L(N), R(N);
            paulaGenerateSamples(L.data(), R.data(), N);
            char path[512];
            std::snprintf(path, sizeof(path), "%s/paula_%d_%s.f32", argv[1], period, cfg.name);
            FILE* f = std::fopen(path, "wb");
            std::fwrite(L.data(), sizeof(float), N, f);
            std::fclose(f);
        }
    }
    return 0;
}
