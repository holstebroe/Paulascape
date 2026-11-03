#pragma once

#include "core/mod_loader.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace paulascape {

// Imports an 8-bit (or 16-bit) sample from a file of unknown origin. The format is detected from the content, not
// the extension:
//   RIFF/WAVE            -> WavImporter
//   IFF 8SVX             -> signed 8-bit, optional Fibonacci-delta packing, loop from the VHDR chunk
//   Scream Tracker 3     -> single-instrument sample file (.SMP/.WAV-less ".879", ".336"...), unsigned, "SCRS" tag
//   anything else        -> headerless raw 8-bit; signed vs unsigned is guessed from the data, rate assumed 8363 Hz
// Directory/cache files (e.g. Scream Tracker's "ST3 cachefile") are rejected.
class SampleImporter {
public:
    static constexpr double RAW_DEFAULT_RATE = 8363.0;

    // formatOut (optional) receives a short description such as "8SVX", "S3M sample", "raw signed 8-bit".
    static bool importSample(const uint8_t* data, size_t size, ModSample& out, std::string* formatOut = nullptr);
    static bool importFile(const std::string& path, ModSample& out, std::string* formatOut = nullptr);
};

// Shared tail of every importer: mono float samples at `sampleRate` -> peak-normalised 8-bit at 16,574 Hz, cropped to
// 128 KB, DC offset removed, ends faded. Loop points are in source frames (loopEnd exclusive); pass -1 for no loop.
bool convertToAmiga(const std::vector<float>& monoFloat, double sampleRate, int64_t loopStart, int64_t loopEnd,
                    ModSample& outSample);

} // namespace paulascape
