#include "import/wav_import.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <cmath>

static void writeLE16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

static void writeLE32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

int main() {
    std::cout << "Testing WAV Importer pipeline..." << std::endl;

    // Create a 16-bit 44.1kHz mono sine wave WAV buffer
    uint32_t sampleRate = 44100;
    size_t numSamples = 44100; // 1 second
    size_t dataSize = numSamples * 2;
    size_t totalSize = 44 + dataSize;

    std::vector<uint8_t> wavData(totalSize, 0);

    // RIFF Header
    std::memcpy(wavData.data(), "RIFF", 4);
    writeLE32(wavData.data() + 4, static_cast<uint32_t>(totalSize - 8));
    std::memcpy(wavData.data() + 8, "WAVE", 4);

    // fmt chunk
    std::memcpy(wavData.data() + 12, "fmt ", 4);
    writeLE32(wavData.data() + 16, 16);
    writeLE16(wavData.data() + 20, 1); // PCM
    writeLE16(wavData.data() + 22, 1); // 1 channel
    writeLE32(wavData.data() + 24, sampleRate);
    writeLE32(wavData.data() + 28, sampleRate * 2);
    writeLE16(wavData.data() + 32, 2);
    writeLE16(wavData.data() + 34, 16); // 16 bits

    // data chunk
    std::memcpy(wavData.data() + 36, "data", 4);
    writeLE32(wavData.data() + 40, static_cast<uint32_t>(dataSize));

    int16_t* pcmPtr = reinterpret_cast<int16_t*>(wavData.data() + 44);
    for (size_t i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        pcmPtr[i] = static_cast<int16_t>(30000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * t));
    }

    paulascape::ModSample convertedSample;
    bool success = paulascape::WavImporter::importWav(wavData.data(), wavData.size(), convertedSample);

    assert(success);
    assert(convertedSample.header.length > 0);
    assert(convertedSample.header.length <= 131070);
    assert(!convertedSample.pcmData.empty());

    std::cout << "WAV Importer Test Passed Successfully!" << std::endl;
    return 0;
}
