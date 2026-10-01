#include "voice_pool.hpp"
#include "fx_util.hpp"
#include <algorithm>
#include <cmath>

namespace paulascape {

using namespace fx;

namespace {

constexpr double ABS_PERIOD_MIN = 20.0;
constexpr double ABS_PERIOD_MAX = 3424.0;
constexpr double CLEAN_FADE_SECONDS = 0.002;

} // namespace

VoicePool::VoicePool() {
    setSampleRate(44100.0);
    for (auto& f : channelFilters) {
        f.setSampleRate(44100.0);
        f.setFilterModel(filterModel);
        f.setLedFilter(ledFilterOn);
    }
    updateTickLength();
}

void VoicePool::setModule(const Module* mod) {
    activeModule = mod;
    allNotesOff();
}

void VoicePool::setSampleRate(double sr) {
    if (sr <= 0.0) return;
    sampleRate = sr;
    for (auto& slot : voices) slot.voice.setSampleRate(sr);
    for (auto& filter : channelFilters) filter.setSampleRate(sr);
    updateTickLength();
}

void VoicePool::setResamplerMode(ResamplerMode mode) {
    resamplerMode = mode;
    for (auto& slot : voices) slot.voice.setResamplerMode(mode);
}

void VoicePool::setFilterModel(FilterModel model) {
    filterModel = model;
    // Clean mode also skips the Amiga filters; the plugin passes FilterModel::Off for that.
    for (auto& filter : channelFilters) filter.setFilterModel(model);
}

void VoicePool::setLedFilter(bool enable) {
    ledFilterOn = enable;
    for (auto& filter : channelFilters) filter.setLedFilter(enable);
}

void VoicePool::setOutputLayout(OutputLayout layout) { outputLayout = layout; }

void VoicePool::setStereoSeparation(float sep) { stereoSeparation = std::clamp(sep, 0.0f, 1.0f); }

void VoicePool::setPitchMode(PitchMode mode) { pitchMode = mode; }

void VoicePool::setNTSC(bool value) {
    ntsc = value;
    for (auto& slot : voices) slot.voice.setNTSC(value);
}

void VoicePool::setHostTempo(double bpm) {
    if (bpm > 0.0 && bpm != hostBPM) {
        hostBPM = bpm;
        updateTickLength();
    }
}

void VoicePool::updateTickLength() {
    // 6 ticks per row and 4 rows per beat: 24 ticks per beat, so effects follow the host tempo.
    const double tickHz = std::clamp(hostBPM, 20.0, 999.0) / 60.0 * 24.0;
    samplesPerTick = sampleRate / tickHz;
}

void VoicePool::setPlaybackMode(PlaybackMode mode) {
    playbackMode = mode;
    allNotesOff();
}

size_t VoicePool::activeVoiceCount() const {
    size_t n = 0;
    for (const auto& v : voices) n += v.voice.isActive() ? 1 : 0;
    return n;
}

const PaulaVoice* VoicePool::channelVoice(uint8_t midiChannel) const {
    const ActiveVoiceSlot* best = nullptr;
    for (const auto& slot : voices) {
        if (slot.voice.isActive() && !slot.releasing && slot.midiChannel == (midiChannel & 15) && (!best || slot.age > best->age)) best = &slot;
    }
    return best ? &best->voice : nullptr;
}

int VoicePool::allocateVoiceSlot() {
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (!voices[i].voice.isActive()) return static_cast<int>(i);
    }
    // Voice stealing: oldest voice first, preferring voices already released
    int best = 0;
    uint64_t bestScore = ~0ull;
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        const uint64_t score = (voices[i].releasing ? 0ull : (1ull << 32)) + voices[i].age;
        if (score < bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    return best;
}

double VoicePool::keyToPeriod(int key, int8_t finetune) const {
    // MIDI 60 is ProTracker C-3 (period 214); the table covers C-1 (36) to B-3 (71).
    if (pitchMode == PitchMode::Free) {
        const double p = 214.0 * std::pow(2.0, -(key - 60) / 12.0) * std::pow(2.0, -finetune / 96.0);
        return std::clamp(p, ABS_PERIOD_MIN, ABS_PERIOD_MAX);
    }
    const int n = key - 36;
    const uint16_t* row = fineRow(finetune);
    double p;
    if (n < 0) {
        const int oct = (-n + 11) / 12;
        p = static_cast<double>(row[n + oct * 12]) * std::pow(2.0, oct);
    } else if (n > 35) {
        const int oct = (n - 24) / 12;
        p = static_cast<double>(row[n - oct * 12]) / std::pow(2.0, oct);
    } else {
        p = row[n];
    }
    return std::clamp(p, ABS_PERIOD_MIN, ABS_PERIOD_MAX);
}

double VoicePool::channelBendFactor(const ActiveVoiceSlot& slot) const {
    return std::pow(2.0, -channels[slot.midiChannel & 15].bend / 12.0);
}

double VoicePool::channelBendRange(uint8_t midiChannel) const {
    const double r = channels[midiChannel & 15].bendRange;
    return r >= 0.0 ? r : bendRange;
}

void VoicePool::pushOutputPeriod(ActiveVoiceSlot& slot, double period) {
    slot.voice.setPeriod(static_cast<uint16_t>(std::lround(std::clamp(period, ABS_PERIOD_MIN, ABS_PERIOD_MAX))));
}

uint8_t VoicePool::outputVolume(const ActiveVoiceSlot& slot) const {
    return slot.baseVolume;
}

void VoicePool::release(ActiveVoiceSlot& slot) {
    if (!slot.voice.isActive()) return;
    if (resamplerMode == ResamplerMode::Clean) {
        slot.releasing = true; // fades out over about 2 ms
    } else {
        slot.voice.stop();     // Paula cuts hard
    }
    slot.held = false;
}

void VoicePool::startEffectOnVoice(ActiveVoiceSlot& slot, uint8_t cmd, uint8_t param, bool atStart) {
    const ChannelState& ch = channels[slot.midiChannel & 15];
    const uint8_t x = param >> 4;
    const uint8_t y = param & 0x0F;
    slot.cmd = cmd;
    slot.param = param;
    slot.rowTick = 0;
    // speeds, depths and waveforms come from the channel's effect memory (see applyEffect)
    slot.portaSpeed = ch.portaSpeed;
    slot.vibratoCmd = ch.vibratoCmd;
    slot.tremoloCmd = ch.tremoloCmd;
    slot.vibratoWave = ch.vibratoWave;
    slot.tremoloWave = ch.tremoloWave;
    slot.glissando = ch.glissando;
    switch (cmd) {
        case 0x0C:
            slot.baseVolume = std::min<uint8_t>(param, 64);
            break;
        case 0x0E:
            switch (x) {
                case 0x1: slot.period = std::max<double>(slot.period - y, PERIOD_MIN); break;
                case 0x2: slot.period = std::min<double>(slot.period + y, PERIOD_MAX); break;
                case 0x5:
                    slot.finetune = static_cast<int8_t>(y >= 8 ? y - 16 : y);
                    // with a note on the same row the note is played at the new finetune
                    if (atStart && playbackMode != PlaybackMode::Drum) slot.period = keyToPeriod(slot.midiKey, slot.finetune);
                    break;
                case 0xA: slot.baseVolume = static_cast<uint8_t>(std::min<int>(slot.baseVolume + y, 64)); break;
                case 0xB: slot.baseVolume = static_cast<uint8_t>(std::max<int>(slot.baseVolume - y, 0)); break;
                case 0xC: if (y == 0) slot.baseVolume = 0; break; // EC0 cuts at once; later ticks in applyTickEffects
                default: break; // E3/E4/E7 are channel memory, E9 acts per tick, the rest only matter inside a pattern
            }
            break;
        default:
            break;
    }
    // Tick 0 of a row: the voice plays its plain pitch and volume, without last row's vibrato, arpeggio or tremolo
    pushOutputPeriod(slot, slot.period * channelBendFactor(slot));
    slot.voice.setVolume(slot.baseVolume);
}

void VoicePool::applyTickEffects(ActiveVoiceSlot& slot) {
    if (!slot.voice.isActive()) return;
    ChannelState& ch = channels[slot.midiChannel & 15];
    ++slot.ticksSinceStart;
    ++slot.rowTick;
    const uint32_t t = slot.ticksSinceStart;
    const uint32_t rt = slot.rowTick; // tick within the row, as the replayer counts it
    const uint8_t x = slot.param >> 4;
    const uint8_t y = slot.param & 0x0F;

    double volume = slot.baseVolume;
    double outPeriod = 0.0; // 0 = use slot.period
    bool outBent = false;   // outPeriod already includes the pitch bend

    auto volSlide = [&]() {
        if (x) slot.baseVolume = static_cast<uint8_t>(std::min<int>(slot.baseVolume + x, 64));
        else slot.baseVolume = static_cast<uint8_t>(std::max<int>(slot.baseVolume - y, 0));
    };

    switch (slot.cmd) {
        case 0x00:
            if (slot.param) {
                // In table mode the arpeggio steps from the bent pitch (a slide sent as pitch bend), as ProTracker
                // steps from the slid period; Free mode bends the arpeggio as a whole.
                const bool table = pitchMode == PitchMode::PeriodTable;
                const double from = table ? slot.period * channelBendFactor(slot) : slot.period;
                const uint16_t* row = fineRow(slot.finetune);
                const int base = std::min(findInRow(row, static_cast<uint16_t>(std::lround(from))), 35);
                const int step = (rt % 3 == 0) ? 0 : (rt % 3 == 1 ? x : y);
                outPeriod = row[std::min(base + step, 35)];
                outBent = table;
            }
            break;
        case 0x01: slot.period = std::max<double>(slot.period - slot.param, PERIOD_MIN); break;
        case 0x02: slot.period = std::min<double>(slot.period + slot.param, PERIOD_MAX); break;
        case 0x0A: volSlide(); break;
        case 0x0E:
            if (x == 0xC && y > 0 && rt == y) slot.baseVolume = 0; // ECx: volume 0 on tick x, as ProTracker
            break;
        default: break;
    }

    // Tone portamento (explicit 3xx, 5xy, or a legato glide)
    if ((slot.cmd == 0x03 || slot.cmd == 0x05) && slot.wantedPeriod > 0.0) {
        if (slot.period > slot.wantedPeriod) slot.period = std::max<double>(slot.period - slot.portaSpeed, slot.wantedPeriod);
        else slot.period = std::min<double>(slot.period + slot.portaSpeed, slot.wantedPeriod);
        if (slot.period == slot.wantedPeriod) slot.wantedPeriod = 0.0;
        if (slot.glissando) {
            const uint16_t* row = fineRow(slot.finetune);
            outPeriod = row[std::min(findInRow(row, static_cast<uint16_t>(slot.period)), 35)];
        }
    }
    if (slot.cmd == 0x05 || slot.cmd == 0x06) volSlide();

    double period = outPeriod > 0.0 ? outPeriod : slot.period;
    if (!outBent) period *= channelBendFactor(slot);

    // Vibrato: explicit 4xy/6xy, or the mod wheel
    uint8_t vSpeed = 0, vDepth = 0;
    if (slot.cmd == 0x04 || slot.cmd == 0x06) {
        vSpeed = slot.vibratoCmd >> 4;
        vDepth = slot.vibratoCmd & 0x0F;
    } else if (ch.modDepth > 0) {
        vSpeed = ch.modSpeed;
        vDepth = ch.modDepth;
    }
    if (vDepth > 0) {
        const int amp = (waveValue(slot.vibratoWave, slot.vibratoPos) * vDepth) >> 7;
        period += (slot.vibratoPos & 0x20) ? -amp : amp;
        slot.vibratoPos = static_cast<uint8_t>((slot.vibratoPos + vSpeed) & 0x3F);
    }

    // Tremolo: explicit 7xy, or channel pressure
    uint8_t tSpeed = 0, tDepth = 0;
    if (slot.cmd == 0x07) {
        tSpeed = slot.tremoloCmd >> 4;
        tDepth = slot.tremoloCmd & 0x0F;
    } else if (ch.pressureDepth > 0) {
        tSpeed = ch.modSpeed;
        tDepth = ch.pressureDepth;
    }
    volume = slot.baseVolume;
    if (tDepth > 0) {
        const int amp = (waveValue(slot.tremoloWave, slot.tremoloPos) * tDepth) >> 6;
        volume += (slot.tremoloPos & 0x20) ? -amp : amp;
        slot.tremoloPos = static_cast<uint8_t>((slot.tremoloPos + tSpeed) & 0x3F);
    }

    // Retrigger (E9x counts row ticks, the CC counts from the note) and note cut (CC)
    uint8_t retrig = ch.retrigger;
    uint32_t retrigTick = t;
    if (slot.cmd == 0x0E && x == 0x9) { retrig = y; retrigTick = rt; }
    if (retrig > 0 && retrigTick % retrig == 0 && activeModule) {
        const ModSample& smp = activeModule->samples[slot.sampleSlot];
        slot.voice.trigger(&smp, slot.voice.getPeriod(), slot.baseVolume);
    }
    const uint8_t cut = ch.noteCut;
    if (cut > 0 && t >= cut) {
        release(slot);
        return;
    }

    pushOutputPeriod(slot, period);
    slot.voice.setVolume(static_cast<uint8_t>(std::clamp(volume, 0.0, 64.0)));
}

void VoicePool::tick(uint8_t midiChannel) {
    ChannelState& ch = channels[midiChannel];
    if (ch.pendingTicks > 0) --ch.pendingTicks;
    for (auto& slot : voices) {
        if (slot.midiChannel == midiChannel) applyTickEffects(slot);
    }
}

void VoicePool::restartTickClock(ChannelState& ch) {
    // Notes and effect commands start a row (tick 0), so the channel's effect ticks follow them: the first comes
    // after 1.5 ticks. The half tick keeps a row's last tick clear of the next row's events, so a row of speed 6
    // gets 5 effect ticks as in ProTracker, not 6.
    ch.tickCounter = -0.5 * samplesPerTick;
}

void VoicePool::programChange(uint8_t midiChannel, uint8_t program) {
    midiChannel &= 15;
    channelProgram[midiChannel] = static_cast<uint8_t>(std::min<int>(program + 1, 31));
    if (!activeModule || (playbackMode != PlaybackMode::Single && playbackMode != PlaybackMode::MultiChannel)) return;
    // A note that started at this same instant takes the new sample (hosts may send it before the program change)
    const uint8_t sampleSlot = channelProgram[midiChannel];
    const ModSample& smp = activeModule->samples[sampleSlot];
    ChannelState& ch = channels[midiChannel];
    if (ch.silentKey >= 0) {
        const int key = ch.silentKey;
        ch.silentKey = -1;
        noteOn(midiChannel, static_cast<uint8_t>(key), ch.silentVelocity);
    }
    for (auto& slot : voices) {
        if (!slot.fresh || slot.midiChannel != midiChannel || !slot.voice.isActive() || slot.sampleSlot == sampleSlot) continue;
        if (smp.pcmData.empty()) { slot.voice.stop(); continue; }
        slot.sampleSlot = sampleSlot;
        slot.finetune = smp.header.finetune;
        slot.period = keyToPeriod(slot.midiKey, slot.finetune);
        slot.voice.trigger(&smp, static_cast<uint16_t>(std::lround(slot.period * channelBendFactor(slot))), slot.baseVolume);
    }
}

void VoicePool::noteOn(uint8_t midiChannel, uint8_t key, uint8_t velocity) {
    if (!activeModule) return;
    if (velocity == 0) {
        noteOff(midiChannel, key);
        return;
    }
    midiChannel &= 15;
    ChannelState& ch = channels[midiChannel];
    restartTickClock(ch);

    uint8_t sampleSlot = 1;
    switch (playbackMode) {
        case PlaybackMode::Single:
            sampleSlot = channelProgram[midiChannel] ? channelProgram[midiChannel] : selectedSlot;
            break;
        case PlaybackMode::MultiChannel:
            // channel n plays slot n, unless a program change has chosen another sample for that channel
            sampleSlot = channelProgram[midiChannel] ? channelProgram[midiChannel] : std::clamp<uint8_t>(midiChannel + 1, 1, 31);
            break;
        case PlaybackMode::Drum: {
            bool found = false;
            for (uint8_t s = 1; s <= 31; ++s) {
                if (activeModule->samples[s].header.inKey == key && !activeModule->samples[s].pcmData.empty()) {
                    sampleSlot = s;
                    found = true;
                    break;
                }
            }
            if (!found) return; // unmapped keys are silent in drum mode
            break;
        }
        case PlaybackMode::Pattern:
            return; // handled by the replayer
    }

    const ModSample& smp = activeModule->samples[sampleSlot];
    if (smp.pcmData.empty()) {
        ch.silentKey = key;
        ch.silentVelocity = velocity;
        return;
    }

    const bool drum = playbackMode == PlaybackMode::Drum;
    // A tone portamento command (3xx, 5xy) waiting for this note makes it legato, as in a pattern
    const bool pendingPorta = ch.pendingTicks > 0 && (ch.pendingCmd == 0x03 || ch.pendingCmd == 0x05);
    const bool legatoOn = !drum && (ch.legato || smp.header.legato || pendingPorta);
    const int pitchKey = drum ? smp.header.outKey : key;
    const double targetPeriod = keyToPeriod(pitchKey, smp.header.finetune);

    // Legato: a held voice on this channel changes pitch without restarting the sample
    if (legatoOn) {
        for (auto& slot : voices) {
            if (slot.voice.isActive() && !slot.releasing && slot.held && slot.midiChannel == midiChannel &&
                (pendingPorta || slot.sampleSlot == sampleSlot)) {
                slot.midiKey = key;
                slot.finetune = smp.header.finetune;
                if (pendingPorta) {
                    // The voice keeps playing; a new sample number only applies to later (re)triggers
                    slot.sampleSlot = sampleSlot;
                    slot.wantedPeriod = targetPeriod;
                    startEffectOnVoice(slot, ch.pendingCmd, ch.pendingParam, false);
                    ch.pendingTicks = 0;
                } else if (ch.glide > 0) {
                    slot.wantedPeriod = targetPeriod;
                    slot.portaSpeed = static_cast<uint8_t>(std::max(1, (128 - ch.glide) / 2));
                    slot.cmd = 0x03;
                } else {
                    slot.period = targetPeriod;
                    slot.wantedPeriod = 0.0;
                }
                pushOutputPeriod(slot, slot.period * channelBendFactor(slot));
                return;
            }
        }
    }

    // Sample offset: the pending effect command (9xx, or its memory) or CC 70, for this note only
    uint32_t offset = 0;
    const bool offsetCmd = ch.pendingTicks > 0 && ch.pendingCmd == 0x09;
    if (offsetCmd) offset = static_cast<uint32_t>(ch.offsetMemory) << 8;
    else if (ch.sampleOffset > 0) offset = static_cast<uint32_t>(std::min<int>(ch.sampleOffset * 2, 255)) << 8;
    ch.sampleOffset = 0;
    if (offset >= std::min<size_t>(smp.header.length, smp.pcmData.size())) {
        if (offsetCmd) {
            // 9xx past the end of the sample: ProTracker plays nothing
            ch.pendingTicks = 0;
            return;
        }
        offset = 0;
    }

    const int slotIdx = allocateVoiceSlot();
    ActiveVoiceSlot& slot = voices[slotIdx];
    slot.voice.stop();

    slot.midiKey = key;
    slot.midiChannel = midiChannel;
    slot.sampleSlot = sampleSlot;
    // A note that does not overlap another stays on the first channel; overlapping notes spread to the next
    // free channel (least loaded, lowest number first).
    {
        int load[4] = {0, 0, 0, 0};
        for (size_t i = 0; i < MAX_VOICES; ++i) {
            if (static_cast<int>(i) != slotIdx && voices[i].voice.isActive()) ++load[voices[i].amigaChannel & 3];
        }
        int best = 0;
        for (int c = 1; c < 4; ++c) if (load[c] < load[best]) best = c;
        slot.amigaChannel = static_cast<uint8_t>(best);
    }
    slot.age = ++globalAge;
    slot.held = true;
    slot.releasing = false;
    slot.fadeGain = 1.0f;
    slot.period = targetPeriod;
    slot.wantedPeriod = 0.0;
    slot.portaSpeed = 0;
    slot.finetune = smp.header.finetune;
    // Velocity is Cxx: 1-127 scaled to 0-64 (the slot's own volume is the pattern-mode default)
    slot.baseVolume = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((velocity - 1) * 64.0 / 126.0)), 0, 64));
    slot.cmd = slot.param = 0;
    slot.vibratoPos = slot.tremoloPos = 0;
    slot.portaSpeed = ch.portaSpeed;
    slot.vibratoCmd = ch.vibratoCmd;
    slot.tremoloCmd = ch.tremoloCmd;
    slot.vibratoWave = ch.vibratoWave;
    slot.tremoloWave = ch.tremoloWave;
    slot.glissando = ch.glissando;
    slot.ticksSinceStart = 0;
    slot.rowTick = 0;
    slot.fresh = true;

    slot.voice.setSampleRate(sampleRate);
    slot.voice.setResamplerMode(resamplerMode);
    slot.voice.setNTSC(ntsc);
    slot.voice.trigger(&smp, static_cast<uint16_t>(std::lround(targetPeriod * channelBendFactor(slot))), slot.baseVolume, offset);

    if (ch.pendingTicks > 0) startEffectOnVoice(slot, ch.pendingCmd, ch.pendingParam, true);
    ch.pendingTicks = 0;
}

void VoicePool::noteOff(uint8_t midiChannel, uint8_t key) {
    midiChannel &= 15;
    for (auto& slot : voices) {
        if (slot.voice.isActive() && slot.held && slot.midiChannel == midiChannel && slot.midiKey == key) release(slot);
    }
}

void VoicePool::allNotesOff() {
    for (auto& slot : voices) {
        slot.voice.stop();
        slot.held = false;
        slot.releasing = false;
    }
}

void VoicePool::setPitchBend(uint8_t midiChannel, int semitones) {
    setPitchBendValue(midiChannel, 8192 + static_cast<int>(std::lround(semitones / std::max(channelBendRange(midiChannel), 0.01) * 8192.0)));
}

void VoicePool::setPitchBendValue(uint8_t midiChannel, int value14) {
    midiChannel &= 15;
    channels[midiChannel].bend = (std::clamp(value14, 0, 16383) - 8192) / 8192.0 * channelBendRange(midiChannel);
    // Free mode glides smoothly; period-table mode updates on the next tick, like 1xx/2xx.
    if (pitchMode == PitchMode::Free) {
        for (auto& slot : voices) {
            if (slot.voice.isActive() && slot.midiChannel == midiChannel) {
                pushOutputPeriod(slot, slot.period * channelBendFactor(slot));
            }
        }
    }
}

void VoicePool::setLegato(uint8_t midiChannel, bool enable) { channels[midiChannel & 15].legato = enable; }

void VoicePool::channelPressure(uint8_t midiChannel, uint8_t value) {
    channels[midiChannel & 15].pressureDepth = static_cast<uint8_t>(value >> 3);
}

void VoicePool::applyEffect(uint8_t midiChannel, uint8_t cmd, uint8_t param) {
    midiChannel &= 15;
    ChannelState& ch = channels[midiChannel];
    const uint8_t x = param >> 4;
    const uint8_t y = param & 0x0F;
    // Effect memory: a zero parameter (or nibble) continues with the last one, also on later notes
    switch (cmd) {
        case 0x03: if (param) ch.portaSpeed = param; break;
        case 0x04:
            if (x) ch.vibratoCmd = static_cast<uint8_t>((ch.vibratoCmd & 0x0F) | (x << 4));
            if (y) ch.vibratoCmd = static_cast<uint8_t>((ch.vibratoCmd & 0xF0) | y);
            break;
        case 0x07:
            if (x) ch.tremoloCmd = static_cast<uint8_t>((ch.tremoloCmd & 0x0F) | (x << 4));
            if (y) ch.tremoloCmd = static_cast<uint8_t>((ch.tremoloCmd & 0xF0) | y);
            break;
        case 0x09: if (param) ch.offsetMemory = param; break;
        case 0x0E:
            if (x == 0x3) ch.glissando = y != 0;
            else if (x == 0x4) ch.vibratoWave = y;
            else if (x == 0x7) ch.tremoloWave = y;
            break;
        default: break;
    }
    restartTickClock(ch);
    // Applies to the voice sounding now, and waits briefly for a note that follows on the same row.
    for (auto& slot : voices) {
        if (!slot.voice.isActive() || slot.midiChannel != midiChannel || slot.releasing) continue;
        if (slot.fresh && cmd == 0x09 && activeModule) {
            // the note started at this same instant: restart it at the offset
            const ModSample& smp = activeModule->samples[slot.sampleSlot];
            const uint32_t offset = static_cast<uint32_t>(ch.offsetMemory) << 8;
            if (offset >= std::min<size_t>(smp.header.length, smp.pcmData.size())) { slot.voice.stop(); continue; }
            slot.voice.trigger(&smp, slot.voice.getPeriod(), slot.baseVolume, offset);
        }
        startEffectOnVoice(slot, cmd, param, slot.fresh);
    }
    ch.pendingCmd = cmd;
    ch.pendingParam = param;
    ch.pendingTicks = 2;
}

void VoicePool::controlChange(uint8_t midiChannel, uint8_t cc, uint8_t value) {
    midiChannel &= 15;
    ChannelState& ch = channels[midiChannel];
    const MidiMap& m = midiMap;
    if (cc == 120 || cc == 123) {
        for (auto& slot : voices) {
            if (slot.midiChannel == midiChannel) { cc == 120 ? slot.voice.stop() : release(slot); }
        }
    } else if (cc == m.modWheel) {
        ch.modDepth = static_cast<uint8_t>(value >> 3);
    } else if (cc == m.vibratoSpeed) {
        ch.modSpeed = static_cast<uint8_t>(value >> 3);
    } else if (cc == m.legato) {
        ch.legato = value >= 64;
    } else if (cc == m.glide) {
        ch.glide = value;
    } else if (cc == m.sampleOffset) {
        ch.sampleOffset = value;
    } else if (cc == m.retrigger) {
        ch.retrigger = value;
    } else if (cc == m.noteCut) {
        ch.noteCut = value;
    } else if (cc == 101) {
        ch.rpnMsb = value;
    } else if (cc == 100) {
        ch.rpnLsb = value;
    } else if (cc == 6 || cc == 38) {
        // RPN 0: pitch bend range for this channel (data entry MSB = semitones, LSB = cents)
        if (ch.rpnMsb == 0 && ch.rpnLsb == 0) {
            const double base = ch.bendRange >= 0.0 ? ch.bendRange : bendRange;
            ch.bendRange = cc == 6 ? value : std::floor(base) + std::min<int>(value, 99) / 100.0;
        }
    } else if (cc == m.fxNumber) {
        ch.fxNumber = value;
    } else if (cc == m.fxHigh) {
        ch.fxHigh = value & 15;
    } else if (cc == m.fxLow) {
        // effect number 0..15 = 0..F, 16..31 = E0..EF
        uint8_t cmd = ch.fxNumber;
        uint8_t param = static_cast<uint8_t>((ch.fxHigh << 4) | (value & 15));
        if (cmd >= 16) {
            param = static_cast<uint8_t>(((cmd - 16) << 4) | (param & 15));
            cmd = 0x0E;
        }
        applyEffect(midiChannel, cmd & 15, param);
    }
}

void VoicePool::processAudio(float** outputs, uint32_t numChannels, uint32_t numFrames) {
    if (!outputs || numChannels == 0 || numFrames == 0) return;

    const float fadeStep = static_cast<float>(1.0 / (CLEAN_FADE_SECONDS * sampleRate));
    for (auto& slot : voices) slot.fresh = false;
    for (auto& ch : channels) ch.silentKey = -1;

    for (uint32_t f = 0; f < numFrames; ++f) {
        for (uint8_t c = 0; c < 16; ++c) {
            ChannelState& ch = channels[c];
            ch.tickCounter += 1.0;
            if (ch.tickCounter >= samplesPerTick) {
                ch.tickCounter -= samplesPerTick;
                tick(c);
            }
        }

        std::array<float, 4> chSamples{0.0f, 0.0f, 0.0f, 0.0f};
        for (auto& slot : voices) {
            if (!slot.voice.hasOutput()) continue;
            float s = slot.voice.renderSample();
            if (slot.releasing) {
                slot.fadeGain -= fadeStep;
                if (slot.fadeGain <= 0.0f) {
                    slot.voice.stop();
                    slot.releasing = false;
                    slot.fadeGain = 1.0f;
                    continue;
                }
                s *= slot.fadeGain;
            }
            chSamples[slot.amigaChannel & 3] += s;
        }

        for (size_t c = 0; c < 4; ++c) {
            chSamples[c] = channelFilters[c].processSample(chSamples[c]);
            scopeOutputs[c] = chSamples[c];
        }

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
