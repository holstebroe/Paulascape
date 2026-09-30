#include "wav_import.hpp"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace paulascape {

constexpr size_t MAX_AMIGA_SAMPLE_BYTES = 131070; // 128 KB - 2 bytes limit
constexpr double AMIGA_PAL_C3_FREQ = 16574.0;     // Target sample rate

static uint16_t readLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool WavImporter::importWavFile(const std::string& filepath, ModSample& outSample) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size <= 0) return false;
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;

    return importWav(buffer.data(), buffer.size(), outSample);
}

bool WavImporter::importWav(const uint8_t* data, size_t size, ModSample& outSample) {
    if (!data || size < 44) return false;

    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t numChannels = 1;
    uint32_t sampleRate = 44100;
    uint16_t bitsPerSample = 16;
    uint16_t audioFormat = 1; // 1 = PCM, 3 = IEEE Float

    const uint8_t* dataChunk = nullptr;
    size_t dataSize = 0;

    const uint8_t* smplChunk = nullptr;

    size_t offset = 12;
    while (offset + 8 <= size) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, data + offset, 4);
        uint32_t chunkSize = readLE32(data + offset + 4);

        if (std::strcmp(chunkId, "fmt ") == 0 && offset + 8 + chunkSize <= size) {
            const uint8_t* fmt = data + offset + 8;
            audioFormat = readLE16(fmt);
            numChannels = readLE16(fmt + 2);
            sampleRate = readLE32(fmt + 4);
            bitsPerSample = readLE16(fmt + 14);
        } else if (std::strcmp(chunkId, "data") == 0 && offset + 8 + chunkSize <= size) {
            dataChunk = data + offset + 8;
            dataSize = chunkSize;
        } else if (std::strcmp(chunkId, "smpl") == 0 && offset + 8 + chunkSize <= size) {
            smplChunk = data + offset + 8;
        }

        offset += 8 + chunkSize + (chunkSize & 1); // Pad byte
    }

    if (!dataChunk || dataSize == 0) return false;

    // Convert raw PCM bytes to float normalized samples [-1.0, 1.0]
    size_t numSamples = dataSize / ((bitsPerSample / 8) * numChannels);
    std::vector<float> monoFloat(numSamples, 0.0f);

    size_t bytesPerFrame = (bitsPerSample / 8) * numChannels;

    for (size_t i = 0; i < numSamples; ++i) {
        const uint8_t* frame = dataChunk + i * bytesPerFrame;
        float frameSum = 0.0f;

        for (uint16_t ch = 0; ch < numChannels; ++ch) {
            const uint8_t* sPtr = frame + ch * (bitsPerSample / 8);
            float val = 0.0f;

            if (audioFormat == 1) { // Integer PCM
                if (bitsPerSample == 8) {
                    val = (*sPtr - 128) / 128.0f;
                } else if (bitsPerSample == 16) {
                    int16_t s16 = static_cast<int16_t>(readLE16(sPtr));
                    val = s16 / 32768.0f;
                } else if (bitsPerSample == 24) {
                    int32_t s24 = static_cast<int32_t>(sPtr[0]) | (static_cast<int32_t>(sPtr[1]) << 8) | (static_cast<int32_t>(sPtr[2]) << 16);
                    if (s24 & 0x800000) s24 |= 0xFF000000;
                    val = s24 / 8388608.0f;
                } else if (bitsPerSample == 32) {
                    int32_t s32 = static_cast<int32_t>(readLE32(sPtr));
                    val = s32 / 2147483648.0f;
                }
            } else if (audioFormat == 3 && bitsPerSample == 32) { // Float
                float fval;
                std::memcpy(&fval, sPtr, 4);
                val = fval;
            }

            frameSum += val;
        }

        monoFloat[i] = frameSum / static_cast<float>(numChannels);
    }

    // Resample to 16,574 Hz
    double resampleRatio = AMIGA_PAL_C3_FREQ / static_cast<double>(sampleRate);
    size_t targetLength = static_cast<size_t>(numSamples * resampleRatio);

    if (targetLength > MAX_AMIGA_SAMPLE_BYTES) {
        targetLength = MAX_AMIGA_SAMPLE_BYTES;
    }

    // Ensure even length for ProTracker
    if (targetLength & 1) targetLength--;
    if (targetLength == 0) return false;

    std::vector<int8_t> pcm8(targetLength, 0);

    // Peak find for normalization
    float maxPeak = 0.0f;
    for (size_t i = 0; i < targetLength; ++i) {
        double srcPos = i / resampleRatio;
        size_t idx1 = static_cast<size_t>(srcPos);
        size_t idx2 = std::min(idx1 + 1, numSamples - 1);
        float frac = static_cast<float>(srcPos - idx1);

        float interp = monoFloat[idx1] + frac * (monoFloat[idx2] - monoFloat[idx1]);
        if (std::abs(interp) > maxPeak) maxPeak = std::abs(interp);
    }

    float normFactor = (maxPeak > 1e-4f) ? (1.0f / maxPeak) : 1.0f;

    for (size_t i = 0; i < targetLength; ++i) {
        double srcPos = i / resampleRatio;
        size_t idx1 = static_cast<size_t>(srcPos);
        size_t idx2 = std::min(idx1 + 1, numSamples - 1);
        float frac = static_cast<float>(srcPos - idx1);

        float interp = monoFloat[idx1] + frac * (monoFloat[idx2] - monoFloat[idx1]);
        float scaled = std::clamp(interp * normFactor * 127.0f, -128.0f, 127.0f);
        pcm8[i] = static_cast<int8_t>(scaled);
    }

    // Fade out end of crop
    if (targetLength >= 16) {
        for (size_t i = 0; i < 16; ++i) {
            size_t idx = targetLength - 16 + i;
            float fade = 1.0f - (i / 16.0f);
            pcm8[idx] = static_cast<int8_t>(pcm8[idx] * fade);
        }
    }

    outSample.header.length = static_cast<uint32_t>(targetLength);
    outSample.header.volume = 64;
    outSample.header.finetune = 0;
    outSample.header.loopStart = 0;
    outSample.header.loopLength = 2;
    outSample.header.loopEnabled = false;

    // Parse smpl chunk if present
    if (smplChunk) {
        uint32_t numLoops = readLE32(smplChunk + 28);
        if (numLoops > 0) {
            uint32_t lStart = readLE32(smplChunk + 36 + 8);
            uint32_t lEnd = readLE32(smplChunk + 36 + 12);
            uint32_t resampleStart = static_cast<uint32_t>(lStart * resampleRatio);
            uint32_t resampleEnd = static_cast<uint32_t>(lEnd * resampleRatio);

            if (resampleEnd > resampleStart + 2 && resampleEnd <= targetLength) {
                outSample.header.loopStart = resampleStart & ~1U; // Even byte alignment
                outSample.header.loopLength = (resampleEnd - resampleStart) & ~1U;
                outSample.header.loopEnabled = true;
            }
        }
    }

    outSample.pcmData = std::move(pcm8);
    return true;
}

} // namespace paulascape
