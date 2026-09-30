#include "core/mod_loader.hpp"
#include "core/pt2/pt2_tables.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>

int main() {
    std::cout << "Testing PT2 tables and MOD loader..." << std::endl;

    // Test PT2 tables
    uint16_t period = paulascape::pt2::noteToPeriod(24, 0); // C-3
    assert(period == 214);
    double rate = paulascape::pt2::periodToSampleRate(period, false);
    assert(rate > 16500.0 && rate < 16600.0);

    // Build synthetic MOD file buffer (1084 bytes header + 1 pattern 1024 bytes + 128 bytes PCM sample)
    std::vector<uint8_t> modBuf(1084 + 1024 + 128, 0);

    // Title at offset 0
    std::memcpy(modBuf.data(), "Test MOD File", 13);

    // Sample 1 header at offset 20
    std::memcpy(modBuf.data() + 20, "Lead Synth", 10);
    modBuf[20 + 22] = 0;  // length in words (64 words = 128 bytes)
    modBuf[20 + 23] = 64;
    modBuf[20 + 24] = 0;  // finetune 0
    modBuf[20 + 25] = 48; // volume 48
    modBuf[20 + 26] = 0;  // loop start word 0
    modBuf[20 + 27] = 0;
    modBuf[20 + 28] = 0;  // loop length words 32
    modBuf[20 + 29] = 32;

    // Song info at offset 950
    modBuf[950] = 1; // song length
    modBuf[951] = 0; // restart pos
    modBuf[952] = 0; // order 0 = pattern 0

    // Magic tag "M.K." at offset 1080
    std::memcpy(modBuf.data() + 1080, "M.K.", 4);

    // Pattern 0, Row 0, Ch 0: Sample 1, Note C-3 (period 214 = 0x00D6), Effect C40 (Set Volume 64)
    // Sample 1: sample_hi = 0, sample_lo = 1
    // Byte 0: sample_hi (0x00) | period_hi (0x00)
    // Byte 1: period_lo (0xD6)
    // Byte 2: (sample_lo << 4) (0x10) | effect (0x0C)
    // Byte 3: param (0x40)
    uint8_t* cell = modBuf.data() + 1084;
    cell[0] = 0x00; // Sample hi (0) | Period hi (0)
    cell[1] = 0xD6; // Period lo (214)
    cell[2] = 0x1C; // Sample lo (1 << 4 = 10) | Effect C (12)
    cell[3] = 0x40; // Param 64

    paulascape::Module mod;
    bool success = paulascape::ModLoader::loadFromMemory(modBuf.data(), modBuf.size(), mod);
    assert(success);
    assert(mod.title == "Test MOD File");
    assert(mod.samples[1].header.name == "Lead Synth");
    assert(mod.samples[1].header.length == 128);
    assert(mod.samples[1].header.volume == 48);
    assert(mod.samples[1].header.loopEnabled == true);
    assert(mod.numPatterns == 1);
    assert(mod.patterns[0][0][0].sample == 1);
    assert(mod.patterns[0][0][0].period == 214);
    assert(mod.patterns[0][0][0].effect == 0x0C);
    assert(mod.patterns[0][0][0].param == 0x40);

    std::cout << "MOD Loader Test Passed Successfully!" << std::endl;
    return 0;
}
