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

void Replayer::setResamplerMode(ResamplerMode mode) {
    for (auto& ch : channels) ch.voice.setResamplerMode(mode);
}

void Replayer::setFilterModel(FilterModel model) {
    for (auto& f : filters) f.setFilterModel(model);
}

void Replayer::setLedFilter(bool enable) {
    for (auto& f : filters) f.setLedFilter(enable);
}

void Replayer::setOutputLayout(OutputLayout layout) { outputLayout = layout; }

void Replayer::setStereoSeparation(float separation) { stereoSeparation = separation; }

void Replayer::stop() {
    playing = false;
    activePatternKey = -1;
    for (auto& ch : channels) ch.voice.stop();
}

void Replayer::setPatternBaseNote(uint8_t note) {
    patternBaseNote = note;
}

void Replayer::setSongOrderKey(uint8_t note) {
    songOrderKey = note;
}

namespace {

constexpr int PERIOD_MIN = 113;
constexpr int PERIOD_MAX = 856;

const uint8_t VIBRATO_TABLE[32] = {
    0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24};

// Index of a base (finetune 0) period in the note table, nearest match.
int periodIndex(uint16_t period) {
    int best = 0;
    int bestDiff = 1 << 30;
    for (int i = 0; i < 36; ++i) {
        const int d = std::abs(static_cast<int>(pt2::periodTableFinetune[0][i]) - static_cast<int>(period));
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

// Finetune -8..7 selects table row 8..15 for the negative values.
const uint16_t* fineRow(int8_t finetune) { return pt2::periodTableFinetune[finetune & 15]; }

// Index of the nearest note at or below `period` in a finetune row (ProTracker's scan)
int findInRow(const uint16_t* row, uint16_t period) {
    int i = 0;
    while (i < 36 && row[i] > period) ++i;
    return i;
}

int waveValue(uint8_t wave, uint8_t pos) {
    const uint8_t p = pos & 0x1F;
    switch (wave & 3) {
        case 0: return VIBRATO_TABLE[p];
        case 1: {
            int v = p * 8;
            if (pos & 0x20) v = 255 - v;
            return v;
        }
        default: return 255;
    }
}

} // namespace

void Replayer::calculateTickSamples() {
    double effectiveBPM = currentBPM;

    if (tempoMode == TempoSyncMode::FollowHost && activeModule) {
        // One factor, host tempo / the MOD's starting musical tempo, applied to every later Fxx too.
        const double startingMusicalBPM = activeModule->initialBPM * (6.0 / activeModule->initialSpeed) * (4.0 / rowsPerBeat);
        const double scale = (startingMusicalBPM > 0.0) ? (hostBPM / startingMusicalBPM) : 1.0;
        effectiveBPM = currentBPM * scale;
    }

    effectiveBPM = std::clamp(effectiveBPM, 10.0, 1000.0);

    // ProTracker tick rate: BPM * 0.4 Hz
    samplesPerTick = sampleRate / (effectiveBPM * 0.4);
}

void Replayer::resetChannels() {
    for (auto& ch : channels) {
        ch.voice.stop();
        ch.sample = 0;
        ch.finetune = 0;
        ch.volume = 64;
        ch.period = ch.notePeriod = ch.wantedPeriod = 0;
        ch.cmd = ch.param = 0;
        ch.portaSpeed = 0;
        ch.portaDir = 0;
        ch.glissando = false;
        ch.vibratoCmd = ch.vibratoPos = ch.vibratoWave = 0;
        ch.tremoloCmd = ch.tremoloPos = ch.tremoloWave = 0;
        ch.sampleOffset = 0;
        ch.loopRow = ch.loopCount = 0;
        ch.delayTick = 0;
        ch.delayedTrigger = false;
    }
    jumpOrder = breakRow = loopJumpRow = -1;
    patternDelay = 0;
    inDelayRepeat = false;
    tickCounter = 0;
    currentRow = 0;
    currentSpeed = activeModule ? activeModule->initialSpeed : 6;
    currentBPM = activeModule ? activeModule->initialBPM : 125;
    sampleCounter = 0.0;
}

void Replayer::patternNoteOn(uint8_t key) {
    if (!activeModule || activeModule->numPatterns == 0) return;

    if (key == songOrderKey) {
        resetChannels();
        isSongOrderMode = true;
        currentOrder = 0;
        currentPattern = activeModule->orderList[0];
        playing = currentPattern < activeModule->numPatterns;
        activePatternKey = key;
        calculateTickSamples();
        sampleCounter = samplesPerTick; // first row sounds immediately
    } else if (key >= patternBaseNote) {
        const int patIdx = key - patternBaseNote;
        if (patIdx < activeModule->numPatterns) {
            resetChannels();
            isSongOrderMode = false;
            currentPattern = patIdx;
            playing = true;
            activePatternKey = key;
            calculateTickSamples();
            sampleCounter = samplesPerTick;
        }
    }
}

void Replayer::patternNoteOff(uint8_t key) {
    if (playing && activePatternKey == key) stop();
}

void Replayer::startNote(ReplayerChannelState& ch, uint32_t offset) {
    if (ch.sample < 1 || ch.sample > 31) return;
    const ModSample& smp = activeModule->samples[ch.sample];
    if (smp.pcmData.empty()) { ch.voice.stop(); return; }
    if (offset >= std::min<size_t>(smp.header.length, smp.pcmData.size())) {
        ch.voice.stop(); // offset beyond the end: silent
        return;
    }
    ch.voice.trigger(&smp, ch.period, ch.volume, offset);
}

void Replayer::triggerCell(size_t chIdx, const NoteCell& cell) {
    auto& ch = channels[chIdx];
    ch.cmd = cell.effect;
    ch.param = cell.param;
    const uint8_t x = cell.param >> 4;
    const uint8_t y = cell.param & 0x0F;
    const bool isExt = cell.effect == 0x0E;

    if (cell.sample > 0 && cell.sample <= 31) {
        ch.sample = cell.sample;
        const ModSampleHeader& h = activeModule->samples[cell.sample].header;
        ch.volume = h.volume;
        ch.finetune = h.finetune;
    }

    // E5x set finetune applies to the note on the same row
    if (isExt && x == 0x5) ch.finetune = static_cast<int8_t>(y >= 8 ? y - 16 : y);

    const bool tonePorta = cell.effect == 0x03 || cell.effect == 0x05;
    uint16_t notePeriod = 0;
    if (cell.period > 0) {
        notePeriod = fineRow(ch.finetune)[periodIndex(cell.period)];
    }

    if (notePeriod > 0) {
        if (tonePorta) {
            ch.wantedPeriod = notePeriod;
            ch.portaDir = (ch.period == 0 || ch.period == notePeriod) ? 0 : (ch.period > notePeriod ? 1 : -1);
            if (ch.period == 0) { ch.period = notePeriod; ch.portaDir = 0; }
        } else if (isExt && x == 0xD && y > 0) {
            // EDx note delay: trigger on tick y
            ch.delayTick = y;
            ch.pendingPeriod = notePeriod;
            ch.delayedTrigger = true;
        } else {
            ch.period = ch.notePeriod = notePeriod;
            if ((ch.vibratoWave & 4) == 0) ch.vibratoPos = 0;
            if ((ch.tremoloWave & 4) == 0) ch.tremoloPos = 0;
            uint32_t offset = 0;
            if (cell.effect == 0x09) {
                if (cell.param) ch.sampleOffset = cell.param;
                offset = static_cast<uint32_t>(ch.sampleOffset) << 8;
            }
            startNote(ch, offset);
        }
    } else if (cell.effect == 0x09 && cell.param) {
        ch.sampleOffset = cell.param; // remembered for the next note
    }

    // Effects processed once per row (tick 0)
    switch (cell.effect) {
        case 0x03:
            if (cell.param) ch.portaSpeed = cell.param;
            break;
        case 0x04:
            if (x) ch.vibratoCmd = static_cast<uint8_t>((ch.vibratoCmd & 0x0F) | (x << 4));
            if (y) ch.vibratoCmd = static_cast<uint8_t>((ch.vibratoCmd & 0xF0) | y);
            break;
        case 0x07:
            if (x) ch.tremoloCmd = static_cast<uint8_t>((ch.tremoloCmd & 0x0F) | (x << 4));
            if (y) ch.tremoloCmd = static_cast<uint8_t>((ch.tremoloCmd & 0xF0) | y);
            break;
        case 0x0B:
            jumpOrder = cell.param;
            break;
        case 0x0C:
            ch.volume = std::min<uint8_t>(cell.param, 64);
            break;
        case 0x0D:
            breakRow = std::min(x * 10 + y, 63);
            if (x * 10 + y > 63) breakRow = 0;
            break;
        case 0x0F:
            if (cell.param > 0) {
                if (cell.param < 32) {
                    currentSpeed = cell.param;
                } else {
                    currentBPM = cell.param;
                    calculateTickSamples();
                }
            }
            break;
        case 0x0E:
            switch (x) {
                case 0x1: ch.period = static_cast<uint16_t>(std::max<int>(ch.period - y, PERIOD_MIN)); break;
                case 0x2: ch.period = static_cast<uint16_t>(std::min<int>(ch.period + y, PERIOD_MAX)); break;
                case 0x3: ch.glissando = y != 0; break;
                case 0x4: ch.vibratoWave = y; break;
                case 0x7: ch.tremoloWave = y; break;
                case 0x6:
                    if (y == 0) {
                        ch.loopRow = static_cast<uint8_t>(currentRow);
                    } else {
                        if (ch.loopCount == 0) ch.loopCount = y; else --ch.loopCount;
                        if (ch.loopCount != 0) loopJumpRow = ch.loopRow;
                    }
                    break;
                case 0xA: ch.volume = static_cast<uint8_t>(std::min<int>(ch.volume + y, 64)); break;
                case 0xB: ch.volume = static_cast<uint8_t>(std::max<int>(ch.volume - y, 0)); break;
                case 0xE:
                    if (patternDelay == 0) patternDelay = y;
                    break;
                default: break; // E5 handled above, E8 unused, E9/EC/ED per tick, EF (funk repeat) not supported
            }
            break;
        default:
            break;
    }

    ch.voice.setPeriod(ch.period);
    ch.voice.setVolume(ch.volume);
}

void Replayer::processRow() {
    if (!activeModule || currentPattern >= activeModule->numPatterns) return;
    const PatternRow& row = activeModule->patterns[currentPattern][currentRow];
    for (size_t c = 0; c < 4; ++c) triggerCell(c, row[c]);
}

void Replayer::applyTickEffect(ReplayerChannelState& ch) {
    const uint8_t x = ch.param >> 4;
    const uint8_t y = ch.param & 0x0F;
    const uint8_t t = tickCounter;

    auto volSlide = [&]() {
        if (x) ch.volume = static_cast<uint8_t>(std::min<int>(ch.volume + x, 64));
        else ch.volume = static_cast<uint8_t>(std::max<int>(ch.volume - y, 0));
    };
    auto tonePortamento = [&]() {
        if (ch.portaDir == 0 || ch.wantedPeriod == 0) return;
        if (ch.portaDir > 0) { // sliding up in pitch = period decreases
            ch.period = static_cast<uint16_t>(std::max<int>(ch.period - ch.portaSpeed, 0));
            if (ch.period <= ch.wantedPeriod) { ch.period = ch.wantedPeriod; ch.portaDir = 0; }
        } else {
            ch.period = static_cast<uint16_t>(ch.period + ch.portaSpeed);
            if (ch.period >= ch.wantedPeriod) { ch.period = ch.wantedPeriod; ch.portaDir = 0; }
        }
        if (ch.glissando) {
            const uint16_t* row = fineRow(ch.finetune);
            const int i = std::min(findInRow(row, ch.period), 35);
            ch.voice.setPeriod(row[i]);
        }
    };
    uint16_t vibratoOffsetPeriod = 0;
    bool vibrato = false;
    auto doVibrato = [&]() {
        const uint8_t depth = ch.vibratoCmd & 0x0F;
        const uint8_t speed = ch.vibratoCmd >> 4;
        const int amp = (waveValue(ch.vibratoWave, ch.vibratoPos) * depth) >> 7;
        const int p = (ch.vibratoPos & 0x20) ? ch.period - amp : ch.period + amp;
        vibratoOffsetPeriod = static_cast<uint16_t>(std::clamp(p, PERIOD_MIN, PERIOD_MAX));
        vibrato = true;
        ch.vibratoPos = static_cast<uint8_t>((ch.vibratoPos + speed) & 0x3F);
    };

    uint16_t outPeriod = ch.period;
    uint8_t outVolume = ch.volume;

    switch (ch.cmd) {
        case 0x00:
            if (ch.param && ch.notePeriod) {
                const uint16_t* row = fineRow(ch.finetune);
                const int base = std::min(findInRow(row, ch.period), 35);
                const int step = (t % 3 == 0) ? 0 : (t % 3 == 1 ? x : y);
                outPeriod = row[std::min(base + step, 35)];
            }
            break;
        case 0x01:
            ch.period = static_cast<uint16_t>(std::max<int>(ch.period - ch.param, PERIOD_MIN));
            outPeriod = ch.period;
            break;
        case 0x02:
            ch.period = static_cast<uint16_t>(std::min<int>(ch.period + ch.param, PERIOD_MAX));
            outPeriod = ch.period;
            break;
        case 0x03:
            tonePortamento();
            outPeriod = ch.period;
            break;
        case 0x04:
            doVibrato();
            outPeriod = vibratoOffsetPeriod;
            break;
        case 0x05:
            tonePortamento();
            outPeriod = ch.period;
            volSlide();
            outVolume = ch.volume;
            break;
        case 0x06:
            doVibrato();
            outPeriod = vibratoOffsetPeriod;
            volSlide();
            outVolume = ch.volume;
            break;
        case 0x07: {
            const uint8_t depth = ch.tremoloCmd & 0x0F;
            const uint8_t speed = ch.tremoloCmd >> 4;
            const int amp = (waveValue(ch.tremoloWave, ch.tremoloPos) * depth) >> 6;
            const int v = (ch.tremoloPos & 0x20) ? ch.volume - amp : ch.volume + amp;
            outVolume = static_cast<uint8_t>(std::clamp(v, 0, 64));
            ch.tremoloPos = static_cast<uint8_t>((ch.tremoloPos + speed) & 0x3F);
            break;
        }
        case 0x0A:
            volSlide();
            outVolume = ch.volume;
            break;
        case 0x0E:
            if (x == 0x9 && y > 0 && t % y == 0) startNote(ch, 0);            // E9x retrigger
            if (x == 0xC && t == y) { ch.volume = 0; outVolume = 0; }          // ECx note cut
            break;
        default:
            break;
    }
    (void)vibrato;

    if (ch.cmd != 0x03 || !ch.glissando) ch.voice.setPeriod(outPeriod);
    ch.voice.setVolume(outVolume);
}

void Replayer::processEffectsOnTick() {
    for (auto& ch : channels) {
        // EDx: delayed note fires on its tick
        if (ch.delayedTrigger && tickCounter == ch.delayTick) {
            ch.delayedTrigger = false;
            ch.period = ch.notePeriod = ch.pendingPeriod;
            startNote(ch, 0);
        }
        applyTickEffect(ch);
    }
}

void Replayer::advanceRow() {
    const bool song = isSongOrderMode;

    if (patternDelay > 0) {
        // EEx: repeat this row without retriggering its notes
        --patternDelay;
        inDelayRepeat = true;
        jumpOrder = breakRow = -1;
        return;
    }
    inDelayRepeat = false;

    int nextRow = currentRow + 1;
    bool changePattern = false;
    int nextOrder = currentOrder;

    if (jumpOrder >= 0 || breakRow >= 0) {
        if (song) {
            nextOrder = jumpOrder >= 0 ? jumpOrder : currentOrder + 1;
            nextRow = breakRow >= 0 ? breakRow : 0;
            changePattern = true;
        } else {
            // A looping single pattern: Dxx shortens the loop, Bxx is ignored
            nextRow = (breakRow >= 0) ? 0 : nextRow;
        }
    } else if (loopJumpRow >= 0) {
        nextRow = loopJumpRow;
    }
    jumpOrder = breakRow = loopJumpRow = -1;

    if (nextRow >= 64 && !changePattern) {
        nextRow = 0;
        if (song) {
            nextOrder = currentOrder + 1;
            changePattern = true;
        }
    }

    if (changePattern) {
        if (nextOrder >= activeModule->songLength) nextOrder = activeModule->restartPos < activeModule->songLength ? activeModule->restartPos : 0;
        currentOrder = nextOrder;
        currentPattern = activeModule->orderList[currentOrder];
        if (currentPattern >= activeModule->numPatterns) {
            stop();
            return;
        }
    }
    currentRow = nextRow;
}

void Replayer::tick() {
    if (tickCounter == 0 && !inDelayRepeat) {
        processRow();
    } else {
        processEffectsOnTick();
    }

    if (++tickCounter >= currentSpeed) {
        tickCounter = 0;
        advanceRow();
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
        scopeOutputs = chSamples;

        if (outputLayout == OutputLayout::FourMono && numChannels >= 4) {
            for (size_t c = 0; c < 4; ++c) outputs[c][f] = chSamples[c];
        } else {
            // Amiga panning L R R L, softened by the separation setting
            const float near = 0.5f + 0.5f * stereoSeparation;
            const float far = 0.5f - 0.5f * stereoSeparation;
            outputs[0][f] = (chSamples[0] + chSamples[3]) * near + (chSamples[1] + chSamples[2]) * far;
            if (numChannels > 1) {
                outputs[1][f] = (chSamples[1] + chSamples[2]) * near + (chSamples[0] + chSamples[3]) * far;
            }
        }
    }
}

} // namespace paulascape
