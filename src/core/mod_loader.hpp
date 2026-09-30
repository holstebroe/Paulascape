#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace paulascape {

struct ModSampleHeader {
    std::string name;
    uint32_t length = 0;       // Length in bytes (even number)
    int8_t finetune = 0;       // Finetune -8..+7
    uint8_t volume = 64;       // Default volume 0..64
    uint32_t loopStart = 0;    // Loop start in bytes
    uint32_t loopLength = 2;   // Loop length in bytes (>2 means looped)
    bool loopEnabled = false;

    // Slot settings (for drum / legato modes)
    bool legato = false;
    uint8_t inKey = 60;        // Default C-3
    uint8_t outKey = 60;       // Default C-3
};

struct ModSample {
    ModSampleHeader header;
    std::vector<int8_t> pcmData;
};

struct NoteCell {
    uint8_t sample = 0;      // Sample 1..31 (0 = no sample)
    uint16_t period = 0;     // Amiga period value (0 = no note)
    uint8_t effect = 0;      // Effect command 0x0..0xF
    uint8_t param = 0;       // Effect parameter
};

using PatternRow = std::array<NoteCell, 4>;
using Pattern = std::array<PatternRow, 64>;

struct Module {
    std::string title;
    uint8_t songLength = 1;
    uint8_t restartPos = 0;
    std::array<uint8_t, 128> orderList{};
    uint8_t numPatterns = 0;
    std::vector<Pattern> patterns;
    std::array<ModSample, 32> samples; // 1-31 active samples (index 0 unused or dummy)
    bool is15SampleFormat = false;

    // Helper properties
    uint8_t initialSpeed = 6;
    uint8_t initialBPM = 125;
};

class ModLoader {
public:
    static bool loadFromMemory(const uint8_t* data, size_t size, Module& outMod);
    static bool loadFromFile(const std::string& filepath, Module& outMod);
};

} // namespace paulascape
