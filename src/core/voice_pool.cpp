#include "voice_pool.hpp"
#include "pt2/pt2_tables.hpp"
#include <cmath>
#include <algorithm>

namespace paulascape {

VoicePool::VoicePool() {
    setSampleRate(44100.0);
    for (size_t i = 0; i < 4; ++i) {
        channelFilters[i].setSampleRate(44100.0);
        channelFilters[i].setFilterModel(filterModel);
        channelFilters[i].setLedFilter(ledFilterOn);
    }
}

void VoicePool::setModule(const Module* mod) {
    activeModule = mod;
    allNotesOff();
}

void VoicePool::setSampleRate(double sr) {
    if (sr <= 0.0) return;
    sampleRate = sr;
    for (auto& slot : voices) {
        slot.voice.setSampleRate(sr);
    }
    for (auto& filter : channelFilters) {
        filter.setSampleRate(sr);
    }
}

void VoicePool::setResamplerMode(ResamplerMode mode) {
    resamplerMode = mode;
    for (auto& slot : voices) {
        slot.voice.setResamplerMode(mode);
    }
}

void VoicePool::setFilterModel(FilterModel model) {
    filterModel = model;
    for (auto& filter : channelFilters) {
        filter.setFilterModel(model);
    }
}

void VoicePool::setLedFilter(bool enable) {
    ledFilterOn = enable;
    for (auto& filter : channelFilters) {
        filter.setLedFilter(enable);
    }
}

void VoicePool::setOutputLayout(OutputLayout layout) {
    outputLayout = layout;
}

void VoicePool::setStereoSeparation(float sep) {
    stereoSeparation = std::clamp(sep, 0.0f, 1.0f);
}

void VoicePool::setPitchMode(PitchMode mode) {
    pitchMode = mode;
}

void VoicePool::setPlaybackMode(PlaybackMode mode) {
    playbackMode = mode;
    allNotesOff();
}

int VoicePool::allocateVoiceSlot() {
    // Look for an inactive slot
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (!voices[i].voice.isActive()) {
            return static_cast<int>(i);
        }
    }

    // Voice stealing: find oldest active voice
    uint32_t oldestAge = 0xFFFFFFFF;
    int oldestIdx = 0;

    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (voices[i].age < oldestAge) {
            oldestAge = voices[i].age;
            oldestIdx = static_cast<int>(i);
        }
    }

    return oldestIdx;
}

uint16_t VoicePool::keyToPeriod(uint8_t key, int8_t finetune, int bendSemitones) const {
    // ProTracker notes map MIDI 36 (C-1) .. 71 (B-3)
    int note = static_cast<int>(key) - 36 + bendSemitones;
    note = std::clamp(note, 0, 35);

    if (pitchMode == PitchMode::PeriodTable) {
        return pt2::noteToPeriod(note, finetune);
    } else {
        // Free pitch mode
        double baseFreq = 440.0 * std::pow(2.0, (key + bendSemitones - 69) / 12.0);
        if (baseFreq <= 0.0) return 214;
        double periodD = pt2::PAULA_PAL_CLK / (baseFreq * 2.0);
        return static_cast<uint16_t>(std::clamp(periodD, 113.0, 1000.0));
    }
}

void VoicePool::noteOn(uint8_t midiChannel, uint8_t key, uint8_t velocity) {
    if (!activeModule) return;
    if (velocity == 0) {
        noteOff(midiChannel, key);
        return;
    }

    uint8_t sampleSlot = 1;

    switch (playbackMode) {
        case PlaybackMode::Single:
            sampleSlot = 1; // Primary slot or selected slot
            break;
        case PlaybackMode::MultiChannel:
            sampleSlot = std::clamp<uint8_t>(midiChannel + 1, 1, 31);
            break;
        case PlaybackMode::Drum: {
            // Find sample with matching inKey
            bool found = false;
            for (uint8_t s = 1; s <= 31; ++s) {
                if (activeModule->samples[s].header.inKey == key) {
                    sampleSlot = s;
                    found = true;
                    break;
                }
            }
            if (!found) sampleSlot = 1;
            break;
        }
        case PlaybackMode::Pattern:
            // Handled separately by Replayer engine
            return;
    }

    const ModSample& smp = activeModule->samples[sampleSlot];
    if (smp.pcmData.empty()) return;

    bool legatoOn = channelLegato[midiChannel & 15] || smp.header.legato;

    // Check if legato should re-pitch existing active voice on this channel
    if (legatoOn) {
        for (auto& slot : voices) {
            if (slot.voice.isActive() && slot.midiChannel == midiChannel && slot.sampleSlot == sampleSlot) {
                uint8_t outKey = (playbackMode == PlaybackMode::Drum) ? smp.header.outKey : key;
                uint16_t period = keyToPeriod(outKey, smp.header.finetune, channelPitchBend[midiChannel & 15]);
                slot.voice.setPeriod(period);
                slot.midiKey = key;
                return;
            }
        }
    }

    int slotIdx = allocateVoiceSlot();
    ActiveVoiceSlot& slot = voices[slotIdx];

    slot.midiKey = key;
    slot.midiChannel = midiChannel;
    slot.sampleSlot = sampleSlot;
    slot.amigaChannel = nextAmigaChannel;
    nextAmigaChannel = (nextAmigaChannel + 1) % 4; // Round-robin Amiga channels 0..3
    slot.age = ++globalAge;

    uint8_t outKey = (playbackMode == PlaybackMode::Drum) ? smp.header.outKey : key;
    uint16_t period = keyToPeriod(outKey, smp.header.finetune, channelPitchBend[midiChannel & 15]);

    // Scaled volume by velocity (0..127 to 0..64)
    uint8_t voiceVol = static_cast<uint8_t>((smp.header.volume * velocity) / 127);

    slot.voice.setSampleRate(sampleRate);
    slot.voice.setResamplerMode(resamplerMode);
    slot.voice.trigger(&smp, period, voiceVol);
}

void VoicePool::noteOff(uint8_t midiChannel, uint8_t key) {
    for (auto& slot : voices) {
        if (slot.voice.isActive() && slot.midiChannel == midiChannel && slot.midiKey == key) {
            slot.voice.stop();
        }
    }
}

void VoicePool::allNotesOff() {
    for (auto& slot : voices) {
        slot.voice.stop();
    }
}

void VoicePool::setPitchBend(uint8_t midiChannel, int semitones) {
    channelPitchBend[midiChannel & 15] = semitones;
    for (auto& slot : voices) {
        if (slot.voice.isActive() && slot.midiChannel == midiChannel) {
            const ModSample& smp = activeModule->samples[slot.sampleSlot];
            uint8_t outKey = (playbackMode == PlaybackMode::Drum) ? smp.header.outKey : slot.midiKey;
            uint16_t period = keyToPeriod(outKey, smp.header.finetune, semitones);
            slot.voice.setPeriod(period);
        }
    }
}

void VoicePool::setLegato(uint8_t midiChannel, bool enable) {
    channelLegato[midiChannel & 15] = enable;
}

void VoicePool::processAudio(float** outputs, uint32_t numChannels, uint32_t numFrames) {
    if (!outputs || numChannels == 0 || numFrames == 0) return;

    for (uint32_t f = 0; f < numFrames; ++f) {
        std::array<float, 4> chSamples{0.0f, 0.0f, 0.0f, 0.0f};

        // Render each active voice and sum to Amiga channel
        for (auto& slot : voices) {
            if (slot.voice.isActive()) {
                float s = slot.voice.renderSample();
                chSamples[slot.amigaChannel & 3] += s;
            }
        }

        // Apply channel filters
        for (size_t c = 0; c < 4; ++c) {
            chSamples[c] = channelFilters[c].processSample(chSamples[c]);
            scopeOutputs[c] = chSamples[c];
        }

        if (outputLayout == OutputLayout::FourMono && numChannels >= 4) {
            outputs[0][f] = chSamples[0];
            outputs[1][f] = chSamples[1];
            outputs[2][f] = chSamples[2];
            outputs[3][f] = chSamples[3];
        } else {
            // Stereo panning: Amiga channels 0 & 3 panned Left, channels 1 & 2 panned Right
            float leftPan = 0.5f + 0.5f * stereoSeparation;
            float rightPan = 0.5f - 0.5f * stereoSeparation;

            float left = chSamples[0] * leftPan + chSamples[3] * leftPan +
                         chSamples[1] * rightPan + chSamples[2] * rightPan;
            float right = chSamples[1] * leftPan + chSamples[2] * leftPan +
                          chSamples[0] * rightPan + chSamples[3] * rightPan;

            outputs[0][f] = left;
            if (numChannels > 1) {
                outputs[1][f] = right;
            }
        }
    }
}

} // namespace paulascape
