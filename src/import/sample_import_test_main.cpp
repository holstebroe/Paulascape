#include "import/sample_import.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace paulascape;

static bool load(const std::string& name, ModSample& s, std::string& fmt) {
    return SampleImporter::importFile(std::string(PAULASCAPE_TEST_DIR) + "/resources/samples/" + name, s, &fmt);
}

static void check(const char* name, const char* expectFmt) {
    ModSample s;
    std::string fmt;
    const bool ok = load(name, s, fmt);
    std::cout << name << ": " << fmt << " -> " << s.header.length << " bytes, name '" << s.header.name << "'\n";
    assert(ok);
    assert(fmt == expectFmt);
    assert(s.header.length > 0 && s.header.length <= 131070 && (s.header.length & 1) == 0);
    assert(s.pcmData.size() == s.header.length);
}

int main() {
    check("HIHAT2.SMP", "raw signed 8-bit");
    check("INVSINE.SMP", "raw signed 8-bit");
    check("hammond.sam", "raw signed 8-bit");
    check("BAKBEE1.PCM", "raw unsigned 8-bit");
    check("BEATNISE.PCM", "raw unsigned 8-bit");
    check("BDRUM1.IFF", "IFF 8SVX");
    check("BEATBREA.336", "Scream Tracker sample");

    ModSample s;
    std::string fmt;
    assert(!load("CACHE.ST3", s, fmt)); // directory cache, not audio

    // The same waveform stored signed and unsigned must import identically.
    std::vector<uint8_t> sg(4000), un(4000);
    for (size_t i = 0; i < sg.size(); ++i) {
        const int v = static_cast<int>(100.0 * std::sin(i * 0.05)) + 20; // loud, with DC offset
        sg[i] = static_cast<uint8_t>(static_cast<int8_t>(v));
        un[i] = static_cast<uint8_t>(v + 128);
    }
    ModSample a, b;
    assert(SampleImporter::importSample(sg.data(), sg.size(), a, &fmt) && fmt == "raw signed 8-bit");
    assert(SampleImporter::importSample(un.data(), un.size(), b, &fmt) && fmt == "raw unsigned 8-bit");
    assert(a.pcmData == b.pcmData);

    std::cout << "Sample importer test passed\n";
    return 0;
}
