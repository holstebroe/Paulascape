#pragma once

#include "core/mod_loader.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace paulascape {

class MidiExporter {
public:
    static bool exportPatternToMidi(const Module& mod, int patternIndex, const std::string& outputPath);
    static bool exportSongToMidi(const Module& mod, const std::string& outputPath);

    static std::vector<uint8_t> generateMidiBuffer(const Module& mod, int patternIndex = -1);
};

} // namespace paulascape
