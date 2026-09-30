#pragma once

#include "core/mod_loader.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace paulascape {

class MidiExporter {
public:
    // Pattern-mode clip: one note per order-list position on the pattern's key, as long as the pattern
    // plays. The plugin then plays its own patterns. One track, tempo map from the MOD's start tempo.
    static std::vector<uint8_t> generatePatternClip(const Module& mod, int patternBaseNote = 24, int rowsPerBeat = 4);
    static bool exportPatternClip(const Module& mod, const std::string& outputPath, int patternBaseNote = 24, int rowsPerBeat = 4);

    // Full note-by-note export: four tracks (MOD channels) plus a tempo track. Effects that notes cannot
    // express are sent as effect-command CCs 20-22. patternIndex < 0 exports the unrolled song.
    static std::vector<uint8_t> generateMidiBuffer(const Module& mod, int patternIndex = -1, int rowsPerBeat = 4);
    static bool exportPatternToMidi(const Module& mod, int patternIndex, const std::string& outputPath);
    static bool exportSongToMidi(const Module& mod, const std::string& outputPath);
};

} // namespace paulascape
