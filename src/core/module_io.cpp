#include "module_io.hpp"
#include <cstring>

namespace paulascape {

namespace {

constexpr uint32_t MAGIC = 0x4C555050; // "PPUL"
constexpr uint32_t VERSION = 1;

class Writer {
public:
    explicit Writer(std::vector<uint8_t>& buf) : buf(buf) {}
    void u8(uint8_t v) { buf.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    void str(const std::string& s) { u32(static_cast<uint32_t>(s.size())); bytes(s.data(), s.size()); }
private:
    std::vector<uint8_t>& buf;
};

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d(d), n(n) {}
    bool u8(uint8_t& v) { if (pos + 1 > n) return false; v = d[pos++]; return true; }
    bool u32(uint32_t& v) {
        if (pos + 4 > n) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d[pos + i]) << (8 * i);
        pos += 4;
        return true;
    }
    bool bytes(void* p, size_t len) { if (len > n - pos) return false; std::memcpy(p, d + pos, len); pos += len; return true; }
    bool str(std::string& s) {
        uint32_t len;
        if (!u32(len) || len > n - pos) return false;
        s.assign(reinterpret_cast<const char*>(d + pos), len);
        pos += len;
        return true;
    }
private:
    const uint8_t* d; size_t n; size_t pos = 0;
};

} // namespace

void ModuleIo::write(const Module& mod, std::vector<uint8_t>& out) {
    Writer w(out);
    w.u32(MAGIC);
    w.u32(VERSION);
    w.str(mod.title);
    w.u8(mod.songLength);
    w.u8(mod.restartPos);
    w.u8(mod.initialSpeed);
    w.u8(mod.initialBPM);
    w.u8(mod.is15SampleFormat ? 1 : 0);
    w.bytes(mod.orderList.data(), mod.orderList.size());
    w.u32(static_cast<uint32_t>(mod.patterns.size()));
    for (const Pattern& pat : mod.patterns) {
        for (const PatternRow& row : pat) {
            for (const NoteCell& c : row) {
                w.u8(c.sample);
                w.u8(static_cast<uint8_t>(c.period >> 8));
                w.u8(static_cast<uint8_t>(c.period & 0xFF));
                w.u8(c.effect);
                w.u8(c.param);
            }
        }
    }
    for (size_t i = 1; i < mod.samples.size(); ++i) {
        const ModSample& s = mod.samples[i];
        const ModSampleHeader& h = s.header;
        w.str(h.name);
        w.u32(h.length);
        w.u8(static_cast<uint8_t>(h.finetune));
        w.u8(h.volume);
        w.u32(h.loopStart);
        w.u32(h.loopLength);
        w.u8(h.loopEnabled ? 1 : 0);
        w.u8(h.legato ? 1 : 0);
        w.u8(h.inKey);
        w.u8(h.outKey);
        w.u32(static_cast<uint32_t>(s.pcmData.size()));
        w.bytes(s.pcmData.data(), s.pcmData.size());
    }
}

bool ModuleIo::read(const uint8_t* data, size_t size, Module& out) {
    Reader r(data, size);
    uint32_t magic = 0, version = 0;
    if (!r.u32(magic) || magic != MAGIC || !r.u32(version) || version != VERSION) return false;

    Module m;
    uint8_t flag = 0;
    if (!r.str(m.title) || !r.u8(m.songLength) || !r.u8(m.restartPos) || !r.u8(m.initialSpeed) ||
        !r.u8(m.initialBPM) || !r.u8(flag) || !r.bytes(m.orderList.data(), m.orderList.size())) return false;
    m.is15SampleFormat = flag != 0;

    uint32_t numPatterns = 0;
    if (!r.u32(numPatterns) || numPatterns > 128) return false;
    m.numPatterns = static_cast<uint8_t>(numPatterns);
    m.patterns.resize(numPatterns);
    for (Pattern& pat : m.patterns) {
        for (PatternRow& row : pat) {
            for (NoteCell& c : row) {
                uint8_t hi = 0, lo = 0;
                if (!r.u8(c.sample) || !r.u8(hi) || !r.u8(lo) || !r.u8(c.effect) || !r.u8(c.param)) return false;
                c.period = static_cast<uint16_t>((hi << 8) | lo);
            }
        }
    }
    for (size_t i = 1; i < m.samples.size(); ++i) {
        ModSample& s = m.samples[i];
        ModSampleHeader& h = s.header;
        uint8_t ft = 0, loop = 0, leg = 0;
        uint32_t pcmSize = 0;
        if (!r.str(h.name) || !r.u32(h.length) || !r.u8(ft) || !r.u8(h.volume) || !r.u32(h.loopStart) ||
            !r.u32(h.loopLength) || !r.u8(loop) || !r.u8(leg) || !r.u8(h.inKey) || !r.u8(h.outKey) ||
            !r.u32(pcmSize) || pcmSize > (1u << 24)) return false;
        h.finetune = static_cast<int8_t>(ft);
        h.loopEnabled = loop != 0;
        h.legato = leg != 0;
        s.pcmData.resize(pcmSize);
        if (!r.bytes(s.pcmData.data(), pcmSize)) return false;
    }
    out = std::move(m);
    return true;
}

} // namespace paulascape
