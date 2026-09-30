#include "mod_loader.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace paulascape {

static uint16_t readBE16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

static std::string readCleanString(const uint8_t* p, size_t len) {
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        char c = (char)p[i];
        if (c == '\0') break;
        if (c >= 32 && c <= 126) s.push_back(c);
        else s.push_back(' ');
    }
    // Trim trailing spaces
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    return s;
}

bool ModLoader::loadFromFile(const std::string& filepath, Module& outMod) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size <= 0) return false;
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;

    return loadFromMemory(buffer.data(), buffer.size(), outMod);
}

bool ModLoader::loadFromMemory(const uint8_t* data, size_t size, Module& outMod) {
    if (!data || size < 1080) return false;

    outMod = Module{};
    outMod.title = readCleanString(data, 20);

    // Check tag at offset 1080
    bool is15Samples = false;
    uint8_t numSamples = 31;
    if (size >= 1084) {
        char tag[5] = {0};
        std::memcpy(tag, data + 1080, 4);
        if (std::strcmp(tag, "M.K.") == 0 || std::strcmp(tag, "M!K!") == 0 ||
            std::strcmp(tag, "4CHN") == 0 || std::strcmp(tag, "FLT4") == 0) {
            is15Samples = false;
            numSamples = 31;
        } else {
            is15Samples = true;
            numSamples = 15;
        }
    } else {
        is15Samples = true;
        numSamples = 15;
    }

    outMod.is15SampleFormat = is15Samples;

    size_t offset = 20;

    // Read sample headers
    for (uint8_t i = 1; i <= numSamples; ++i) {
        ModSampleHeader& sh = outMod.samples[i].header;
        sh.name = readCleanString(data + offset, 22);
        sh.length = readBE16(data + offset + 22) * 2;

        int8_t fine = data[offset + 24] & 0x0F;
        if (fine & 0x08) fine |= 0xF0; // Sign extend 4-bit finetune
        sh.finetune = fine;

        sh.volume = std::min<uint8_t>(data[offset + 25], 64);
        sh.loopStart = readBE16(data + offset + 26) * 2;
        sh.loopLength = readBE16(data + offset + 28) * 2;
        sh.loopEnabled = (sh.loopLength > 2);

        // Sanity adjustments
        if (sh.loopStart + sh.loopLength > sh.length && sh.loopEnabled) {
            if (sh.loopStart >= sh.length) {
                sh.loopEnabled = false;
            } else {
                sh.loopLength = sh.length - sh.loopStart;
                if (sh.loopLength <= 2) sh.loopEnabled = false;
            }
        }

        // Key assignments
        sh.inKey = 24 + i; // Default map spread across keys
        sh.outKey = 60;    // C-3

        offset += 30;
    }

    if (is15Samples) {
        outMod.songLength = data[offset];
        outMod.restartPos = data[offset + 1];
        if (outMod.songLength == 0 || outMod.songLength > 128) outMod.songLength = 128;
        offset += 2;

        std::memcpy(outMod.orderList.data(), data + offset, 128);
        offset += 128;
    } else {
        outMod.songLength = data[offset];
        outMod.restartPos = data[offset + 1];
        if (outMod.songLength == 0 || outMod.songLength > 128) outMod.songLength = 128;
        offset += 2;

        std::memcpy(outMod.orderList.data(), data + offset, 128);
        offset += 128;
        offset += 4; // Skip 4-byte tag (M.K.)
    }

    // Determine highest pattern index
    uint8_t maxPat = 0;
    for (size_t i = 0; i < 128; ++i) {
        if (outMod.orderList[i] > maxPat) {
            maxPat = outMod.orderList[i];
        }
    }
    outMod.numPatterns = maxPat + 1;
    outMod.patterns.resize(outMod.numPatterns);

    // Read pattern data
    for (uint8_t p = 0; p < outMod.numPatterns; ++p) {
        if (offset + 1024 > size) break;
        for (size_t r = 0; r < 64; ++r) {
            for (size_t ch = 0; ch < 4; ++ch) {
                const uint8_t* cellData = data + offset;
                NoteCell& cell = outMod.patterns[p][r][ch];

                cell.sample = (cellData[0] & 0xF0) | (cellData[2] >> 4);
                cell.period = ((uint16_t)(cellData[0] & 0x0F) << 8) | cellData[1];
                cell.effect = cellData[2] & 0x0F;
                cell.param = cellData[3];

                offset += 4;
            }
        }
    }

    // Read sample PCM data
    for (uint8_t i = 1; i <= numSamples; ++i) {
        ModSample& smp = outMod.samples[i];
        if (smp.header.length == 0) continue;

        if (offset >= size) break;
        size_t bytesToRead = std::min<size_t>(smp.header.length, size - offset);
        smp.pcmData.resize(smp.header.length, 0);
        std::memcpy(smp.pcmData.data(), data + offset, bytesToRead);

        offset += bytesToRead;
    }

    return true;
}

} // namespace paulascape
