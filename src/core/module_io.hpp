#pragma once

#include "mod_loader.hpp"
#include <cstdint>
#include <vector>

namespace paulascape {

// Compact binary form of a Module, stored in the plugin state so projects do not
// depend on the MOD or WAV files staying on disk. This is not the .mod format.
class ModuleIo {
public:
    static void write(const Module& mod, std::vector<uint8_t>& out);
    static bool read(const uint8_t* data, size_t size, Module& out);
};

} // namespace paulascape
