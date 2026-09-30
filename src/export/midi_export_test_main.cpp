#include "export/midi_export.hpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Testing MIDI Export engine..." << std::endl;

    paulascape::Module mod;
    mod.title = "Export Test";
    mod.songLength = 1;
    mod.orderList[0] = 0;
    mod.numPatterns = 1;
    mod.patterns.resize(1);

    // Row 0 Ch 0: Sample 1, Note C-3 (214), Effect C40 (Set volume 64)
    mod.patterns[0][0][0].sample = 1;
    mod.patterns[0][0][0].period = 214;
    mod.patterns[0][0][0].effect = 0x0C;
    mod.patterns[0][0][0].param = 0x40;

    auto midiBuf = paulascape::MidiExporter::generateMidiBuffer(mod, 0);

    assert(!midiBuf.empty());
    assert(midiBuf.size() > 50);
    assert(midiBuf[0] == 'M' && midiBuf[1] == 'T' && midiBuf[2] == 'h' && midiBuf[3] == 'd');

    std::cout << "MIDI Exporter Test Passed Successfully!" << std::endl;
    return 0;
}
