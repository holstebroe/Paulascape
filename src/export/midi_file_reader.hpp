#pragma once

// Minimal Standard MIDI File reader for the export tests.

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace paulascape::testmidi {

struct Ev {
    uint32_t tick;
    int track;
    std::vector<uint8_t> data; // channel message bytes, or {0xFF, type, payload...}
};

struct Midi {
    int format = 0, tracks = 0, division = 0;
    std::vector<Ev> events;
};

inline uint32_t be32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

inline Midi parse(const std::vector<uint8_t>& b) {
    Midi m;
    assert(b.size() > 14 && std::string(b.begin(), b.begin() + 4) == "MThd");
    m.format = (b[8] << 8) | b[9];
    m.tracks = (b[10] << 8) | b[11];
    m.division = (b[12] << 8) | b[13];
    size_t pos = 14;
    for (int t = 0; t < m.tracks; ++t) {
        assert(std::string(b.begin() + pos, b.begin() + pos + 4) == "MTrk");
        const uint32_t len = be32(&b[pos + 4]);
        size_t p = pos + 8;
        const size_t end = p + len;
        uint32_t tick = 0;
        uint8_t running = 0;
        bool ended = false;
        while (p < end) {
            uint32_t delta = 0;
            while (true) { const uint8_t c = b[p++]; delta = (delta << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
            tick += delta;
            uint8_t status = b[p];
            if (status == 0xFF) {
                const uint8_t type = b[p + 1];
                p += 2;
                uint32_t n = 0;
                while (true) { const uint8_t c = b[p++]; n = (n << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                Ev e{tick, t, {0xFF, type}};
                e.data.insert(e.data.end(), b.begin() + p, b.begin() + p + n);
                p += n;
                if (type == 0x2F) ended = true;
                m.events.push_back(e);
            } else {
                if (status & 0x80) { running = status; ++p; } else status = running;
                const int args = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : 2;
                Ev e{tick, t, {status}};
                for (int a = 0; a < args; ++a) e.data.push_back(b[p++]);
                m.events.push_back(e);
            }
        }
        assert(ended && p == end);
        (void)ended;
        pos = end;
    }
    return m;
}

} // namespace paulascape::testmidi
