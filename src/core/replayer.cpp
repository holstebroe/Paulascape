#include "replayer.hpp"
#include "pt2/pt2_tables.hpp"
#include <cmath>
#include <algorithm>

namespace paulascape {

Replayer::Replayer() {
    setSampleRate(44100.0);
    for (size_t i = 0; i < 4; ++i) {
        filters[i].setSampleRate(44100.0);
        filters[i].setFilterModel(FilterModel::A500);
    }
}

void Replayer::setModule(const Module* mod) {
    activeModule = mod;
    playing = false;
    currentSpeed = mod ? mod->initialSpeed : 6;
    currentBPM = mod ? mod->initialBPM : 125;
    calculateTickSamples();
}

void Replayer::setSampleRate(double sr) {
    if (sr <= 0.0) return;
    sampleRate = sr;
    for (auto& ch : channels) {
        ch.voice.setSampleRate(sr);
    }
    for (auto& f : filters) {
        f.setSampleRate(sr);
    }
    calculateTickSamples();
}

void Replayer::setTempoSyncMode(TempoSyncMode mode) {
    tempoMode = mode;
    calculateTickSamples();
}

void Replayer::setHostTempo(double bpm) {
    if (bpm > 0.0) hostBPM = bpm;
    calculateTickSamples();
}

void Replayer::setRowsPerBeat(int rpb) {
    if (rpb > 0) rowsPerBeat = rpb;
    calculateTickSamples();
}

void Replayer::setPatternBaseNote(uint8_t note) {
    patternBaseNote = note;
}

void Replayer::setSongOrderKey(uint8_t note) {
    songOrderKey = note;
}

void Replayer::calculateTickSamples() {
    double effectiveBPM = currentBPM;

    if (tempoMode == TempoSyncMode::FollowHost && activeModule) {
        double startingMusicalBPM = activeModule->initialBPM * (6.0 / activeModule->initialSpeed) * (4.0 / rowsPerBeat);
        double scale = (startingMusicalBPM > 0.0) ? (hostBPM / startingMusicalBPM) : 1.0;
        effectiveBPM = activeModule->initialBPM * scale;
    }

    if (effectiveBPM < 32.0) effectiveBPM = 32.0;

    // ProTracker tick rate: BPM * 0.4 Hz
    double tickFreq = effectiveBPM * 0.4;
    samplesPerTick = sampleRate / tickFreq;
}

void Replayer::patternNoteOn(uint8_t key) {
    if (!activeModule) return;

    if (key == songOrderKey) {
        isSongOrderMode = true;
        currentOrder = 0;
        currentPattern = activeModule->orderList[0];
        currentRow = 0;
        tickCounter = 0;
        playing = true;
        activePatternKey = key;
        calculateTickSamples();
    } else if (key >= patternBaseNote) {
        int patIdx = key - patternBaseNote;
        if (patIdx < activeModule->numPatterns) {
            isSongOrderMode = false;
            currentPattern = patIdx;
            currentRow = 0;
            tickCounter = 0;
            playing = true;
            activePatternKey = key;
            calculateTickSamples();
        }
    }
}

void Replayer::patternNoteOff(uint8_t key) {
    if (playing && activePatternKey == key) {
        playing = false;
        activePatternKey = -1;
        for (auto& ch : channels) {
            ch.voice.stop();
        }
    }
}

void Replayer::triggerCell(size_t chIdx, const NoteCell& cell) {
    auto& ch = channels[chIdx];

    if (cell.sample > 0 && cell.sample <= 31) {
        ch.sample = cell.sample;
        ch.volume = activeModule->samples[cell.sample].header.volume;
    }

    if (cell.period > 0) {
        ch.period = cell.period;
        if (ch.sample > 0 && ch.sample <= 31) {
            const auto& smp = activeModule->samples[ch.sample];
            ch.voice.trigger(&smp, ch.period, ch.volume);
        }
    }

    // Effect processing on tick 0
    switch (cell.effect) {
        case 0x0B: // Jump to order Bxx
            if (isSongOrderMode) {
                currentOrder = cell.param;
                if (currentOrder >= activeModule->songLength) currentOrder = 0;
                currentPattern = activeModule->orderList[currentOrder];
                currentRow = 0;
            }
            break;
        case 0x0C: // Set volume Cxx
            ch.volume = std::min<uint8_t>(cell.param, 64);
            ch.voice.setVolume(ch.volume);
            break;
        case 0x0D: // Pattern break Dxx
            if (isSongOrderMode) {
                currentOrder++;
                if (currentOrder >= activeModule->songLength) currentOrder = activeModule->restartPos;
                currentPattern = activeModule->orderList[currentOrder];
                currentRow = ((cell.param >> 4) * 10) + (cell.param & 0x0F);
                if (currentRow >= 64) currentRow = 0;
            } else {
                currentRow = 0; // Loop single pattern
            }
            break;
        case 0x0F: // Set speed / BPM Fxx
            if (cell.param > 0) {
                if (cell.param <= 32) {
                    currentSpeed = cell.param;
                } else {
                    currentBPM = cell.param;
                    calculateTickSamples();
                }
            }
            break;
        default:
            break;
    }
}

void Replayer::processRow() {
    if (!activeModule || currentPattern >= activeModule->numPatterns) return;

    const Pattern& pat = activeModule->patterns[currentPattern];
    const PatternRow& row = pat[currentRow];

    for (size_t ch = 0; ch < 4; ++ch) {
        triggerCell(ch, row[ch]);
    }
}

void Replayer::processEffectsOnTick() {
    // Process tick-based effects (vibrato, volume slides, portamento)
    for (size_t c = 0; c < 4; ++c) {
        // Voice pitch & volume update
        channels[c].voice.setPeriod(channels[c].period);
        channels[c].voice.setVolume(channels[c].volume);
    }
}

void Replayer::tick() {
    if (tickCounter == 0) {
        processRow();
    } else {
        processEffectsOnTick();
    }

    tickCounter++;
    if (tickCounter >= currentSpeed) {
        tickCounter = 0;
        currentRow++;
        if (currentRow >= 64) {
            currentRow = 0;
            if (isSongOrderMode) {
                currentOrder++;
                if (currentOrder >= activeModule->songLength) {
                    currentOrder = activeModule->restartPos;
                }
                currentPattern = activeModule->orderList[currentOrder];
            }
        }
    }
}

void Replayer::processAudio(float** outputs, uint32_t numChannels, uint32_t numFrames) {
    if (!outputs || numChannels == 0 || numFrames == 0) return;

    for (uint32_t f = 0; f < numFrames; ++f) {
        if (playing) {
            sampleCounter += 1.0;
            if (sampleCounter >= samplesPerTick) {
                sampleCounter -= samplesPerTick;
                tick();
            }
        }

        std::array<float, 4> chSamples{0.0f, 0.0f, 0.0f, 0.0f};
        for (size_t c = 0; c < 4; ++c) {
            chSamples[c] = channels[c].voice.renderSample();
            chSamples[c] = filters[c].processSample(chSamples[c]);
        }

        // Hard stereo panning L R R L
        outputs[0][f] = (chSamples[0] + chSamples[3]) * 0.707f;
        if (numChannels > 1) {
            outputs[1][f] = (chSamples[1] + chSamples[2]) * 0.707f;
        }
    }
}

} // namespace paulascape
