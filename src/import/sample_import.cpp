#include "sample_import.hpp"
#include "wav_import.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace paulascape {

namespace {

constexpr size_t MAX_AMIGA_SAMPLE_BYTES = 131070; // 128 KB - 2 bytes limit
constexpr double AMIGA_PAL_C3_FREQ = 16574.0;     // Target sample rate

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
    return p[0] | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::vector<float> fromSigned8(const uint8_t* p, size_t n) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<int8_t>(p[i]) / 128.0f;
    return v;
}
std::vector<float> fromUnsigned8(const uint8_t* p, size_t n) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (static_cast<int>(p[i]) - 128) / 128.0f;
    return v;
}

// ---- IFF 8SVX ---------------------------------------------------------------------------------------------------
bool import8svx(const uint8_t* d, size_t size, ModSample& out) {
    if (size < 12 || std::memcmp(d, "FORM", 4) != 0 || std::memcmp(d + 8, "8SVX", 4) != 0) return false;
    uint32_t oneShot = 0, repeat = 0, rate = 0;
    uint8_t octaves = 1, compression = 0;
    bool haveVhdr = false;
    const uint8_t* body = nullptr;
    size_t bodySize = 0;
    std::string name;

    size_t off = 12;
    while (off + 8 <= size) {
        const uint8_t* id = d + off;
        size_t len = be32(d + off + 4);
        const size_t avail = size - (off + 8);
        if (len > avail) len = avail; // tolerate a truncated final chunk
        const uint8_t* c = d + off + 8;
        if (!std::memcmp(id, "VHDR", 4) && len >= 20) {
            oneShot = be32(c);
            repeat = be32(c + 4);
            rate = be16(c + 12);
            octaves = c[14];
            compression = c[15];
            haveVhdr = true;
        } else if (!std::memcmp(id, "NAME", 4)) {
            name.assign(reinterpret_cast<const char*>(c), len);
            while (!name.empty() && name.back() == '\0') name.pop_back();
        } else if (!std::memcmp(id, "BODY", 4)) {
            body = c;
            bodySize = len;
        }
        off += 8 + len + (len & 1);
    }
    if (!haveVhdr || !body || bodySize == 0 || rate == 0) return false;

    std::vector<float> mono;
    if (compression == 0) {
        // Multi-octave files store the highest octave first; that one is all we need.
        size_t n = bodySize;
        if (octaves > 1 && oneShot + repeat > 0 && oneShot + repeat < n) n = oneShot + repeat;
        mono = fromSigned8(body, n);
    } else if (compression == 1) {
        // Fibonacci-delta: 2 header bytes (pad, initial value), then two 4-bit table indices per byte, high nibble first.
        static const int8_t table[16] = {-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21};
        if (bodySize < 2) return false;
        int v = static_cast<int8_t>(body[1]);
        mono.reserve((bodySize - 2) * 2 + 1);
        mono.push_back(v / 128.0f);
        for (size_t i = 2; i < bodySize; ++i) {
            for (int half = 0; half < 2; ++half) {
                const int nib = half == 0 ? body[i] >> 4 : body[i] & 15;
                v = std::clamp(v + table[nib], -128, 127);
                mono.push_back(v / 128.0f);
            }
        }
        if (octaves > 1 && oneShot + repeat > 0 && oneShot + repeat < mono.size()) mono.resize(oneShot + repeat);
    } else {
        return false;
    }

    int64_t ls = -1, le = -1;
    if (repeat > 2 && oneShot + repeat <= mono.size()) {
        ls = oneShot;
        le = static_cast<int64_t>(oneShot) + repeat;
    }
    if (!convertToAmiga(mono, rate, ls, le, out)) return false;
    out.header.name = name.substr(0, 22);
    return true;
}

// ---- Scream Tracker 3 single-instrument sample file --------------------------------------------------------------
// type(1)=1, dosname(12), memseg(3, in 16-byte paragraphs), length(4), loopBegin(4), loopEnd(4), volume(1), disk(1),
// pack(1), flags(1: 1 loop, 2 stereo, 4 16-bit), c2spd(4), 12 bytes, title(28), "SCRS"(4); sample data is unsigned.
bool isS3mSample(const uint8_t* d, size_t size) {
    return size >= 80 && d[0] == 1 && std::memcmp(d + 76, "SCRS", 4) == 0;
}

bool importS3mSample(const uint8_t* d, size_t size, ModSample& out) {
    if (!isS3mSample(d, size)) return false;
    const size_t memseg = (static_cast<size_t>(d[13]) << 16) | le16(d + 14);
    size_t dataOff = memseg * 16;
    if (dataOff < 80 || dataOff >= size) dataOff = 80;
    uint32_t length = le32(d + 16);
    const uint32_t loopBegin = le32(d + 20), loopEnd = le32(d + 24);
    const uint8_t volume = d[28], pack = d[30], flags = d[31];
    uint32_t c2spd = le32(d + 32);
    if (pack != 0) return false; // ADPCM packing was never really used
    if (c2spd < 100 || c2spd > 400000) c2spd = 8363;
    const bool is16 = (flags & 4) != 0, stereo = (flags & 2) != 0;
    const size_t bytesPer = is16 ? 2 : 1;
    const size_t avail = (size - dataOff) / (bytesPer * (stereo ? 2 : 1));
    if (length > avail) length = static_cast<uint32_t>(avail); // length is in frames
    if (length == 0) return false;

    std::vector<float> mono(length);
    for (size_t i = 0; i < length; ++i) {
        // stereo data is stored as all left frames, then all right frames; use the left channel
        const uint8_t* p = d + dataOff + i * bytesPer;
        mono[i] = is16 ? (static_cast<int>(le16(p)) - 32768) / 32768.0f : (static_cast<int>(p[0]) - 128) / 128.0f;
    }
    int64_t ls = -1, le = -1;
    if ((flags & 1) && loopEnd > loopBegin && loopEnd <= length) {
        ls = loopBegin;
        le = loopEnd;
    }
    if (!convertToAmiga(mono, c2spd, ls, le, out)) return false;
    std::string title(reinterpret_cast<const char*>(d + 48), 28);
    title.resize(std::strlen(title.c_str()));
    if (title.empty()) {
        title.assign(reinterpret_cast<const char*>(d + 1), 12);
        title.resize(std::strlen(title.c_str()));
    }
    out.header.name = title.substr(0, 22);
    out.header.volume = std::min<uint8_t>(volume, 64);
    return true;
}

// ---- Headerless raw data -----------------------------------------------------------------------------------------
// Signed vs unsigned: the two readings differ only in the top bit. Real audio is smooth, so the wrong reading shows up
// as big jumps (about 256) wherever the waveform crosses zero. Pick the reading with the smaller total sample-to-sample
// step; if they are about equal (noise, silence) fall back to whichever sits closer to zero (lower energy).
bool rawLooksUnsigned(const uint8_t* d, size_t n) {
    double stepS = 0, stepU = 0, energyS = 0, energyU = 0;
    int prevS = static_cast<int8_t>(d[0]), prevU = static_cast<int8_t>(d[0] ^ 0x80);
    for (size_t i = 0; i < n; ++i) {
        const int s = static_cast<int8_t>(d[i]);
        const int u = static_cast<int8_t>(d[i] ^ 0x80); // unsigned byte re-centred
        stepS += std::abs(s - prevS);
        stepU += std::abs(u - prevU);
        energyS += static_cast<double>(s) * s;
        energyU += static_cast<double>(u) * u;
        prevS = s;
        prevU = u;
    }
    if (std::max(stepS, stepU) > 1.15 * std::min(stepS, stepU) + 1.0) return stepU < stepS;
    return energyU < energyS;
}

} // namespace

bool convertToAmiga(const std::vector<float>& monoFloat, double sampleRate, int64_t loopStart, int64_t loopEnd,
                    ModSample& outSample) {
    const size_t numSamples = monoFloat.size();
    if (numSamples == 0 || sampleRate <= 0.0) return false;
    // Resample to 16,574 Hz
    double resampleRatio = AMIGA_PAL_C3_FREQ / sampleRate;
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

    // Loop points are given in source frames (loopEnd exclusive); -1 means no loop
    if (loopStart >= 0 && loopEnd > loopStart) {
        const uint32_t resampleStart = static_cast<uint32_t>(loopStart * resampleRatio);
        const uint32_t resampleEnd = static_cast<uint32_t>(loopEnd * resampleRatio);
        if (resampleEnd > resampleStart + 2 && resampleEnd <= targetLength) {
            outSample.header.loopStart = resampleStart & ~1U; // Even byte alignment
            outSample.header.loopLength = (resampleEnd - resampleStart) & ~1U;
            outSample.header.loopEnabled = true;
        }
    }

    outSample.pcmData = std::move(pcm8);
    return true;
}

bool SampleImporter::importSample(const uint8_t* d, size_t size, ModSample& out, std::string* formatOut) {
    auto fmt = [&](const char* s) { if (formatOut) *formatOut = s; };
    if (!d || size < 16) return false;

    if (!std::memcmp(d, "RIFF", 4) && !std::memcmp(d + 8, "WAVE", 4)) {
        fmt("WAV");
        return WavImporter::importWav(d, size, out);
    }
    if (!std::memcmp(d, "FORM", 4)) {
        fmt("IFF 8SVX");
        return import8svx(d, size, out);
    }
    if (isS3mSample(d, size)) {
        fmt("Scream Tracker sample");
        return importS3mSample(d, size, out);
    }
    if (!std::memcmp(d, "ST3 cachefile", 13)) { // Scream Tracker's directory cache, not audio
        fmt("Scream Tracker cache file (not a sample)");
        return false;
    }

    // Headerless raw 8-bit data
    const bool unsig = rawLooksUnsigned(d, size);
    fmt(unsig ? "raw unsigned 8-bit" : "raw signed 8-bit");
    const std::vector<float> mono = unsig ? fromUnsigned8(d, size) : fromSigned8(d, size);
    return convertToAmiga(mono, RAW_DEFAULT_RATE, -1, -1, out);
}

bool SampleImporter::importFile(const std::string& path, ModSample& out, std::string* formatOut) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const std::streamsize size = file.tellg();
    if (size <= 0) return false;
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buf.data()), size)) return false;
    return importSample(buf.data(), buf.size(), out, formatOut);
}

} // namespace paulascape
