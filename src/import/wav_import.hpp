#pragma once

#include "core/mod_loader.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace paulascape {

class WavImporter {
public:
    static bool importWav(const uint8_t* wavData, size_t size, ModSample& outSample);
    static bool importWavFile(const std::string& filepath, ModSample& outSample);
};

} // namespace paulascape
