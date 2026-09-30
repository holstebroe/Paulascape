#include "rc_filters.hpp"
#include <algorithm>
#include <cmath>

namespace paulascape {

namespace {

constexpr double PI = 3.14159265358979323846;

void onePole(double audioRate, double cutoff, float& a0, float& b1) {
    cutoff = std::min(cutoff, audioRate / 2.0 - 1e-4);
    const double b = std::exp((-2.0 * PI) * cutoff / audioRate);
    b1 = static_cast<float>(b);
    a0 = static_cast<float>(1.0 - b);
}

} // namespace

RcFilters::RcFilters() { updateCoefficients(); }

void RcFilters::reset() {
    loState = hiState = 0.0f;
    for (float& s : ledState) s = 0.0f;
}

void RcFilters::setSampleRate(double sr) {
    sampleRate = std::max(sr, 8000.0);
    updateCoefficients();
}

void RcFilters::setFilterModel(FilterModel fm) {
    model = fm;
    updateCoefficients();
}

void RcFilters::setLedFilter(bool enable) {
    if (enable != ledFilterOn) {
        for (float& s : ledState) s = 0.0f;
    }
    ledFilterOn = enable;
}

void RcFilters::updateCoefficients() {
    // A500 rev 6A: low-pass R=360 ohm, C=0.1uF (~4421 Hz); high-pass R=1390 ohm, C=22.33uF (~5.13 Hz)
    // A1200 rev 1D4: low-pass ignored (~34.4 kHz, inaudible); high-pass R=1360 ohm, C=22uF (~5.32 Hz)
    useLowpass = model == FilterModel::A500;
    if (model == FilterModel::A1200) {
        onePole(sampleRate, 1.0 / ((2.0 * PI) * 1360.0 * 2.2e-5), hiA0, hiB1);
    } else {
        onePole(sampleRate, 1.0 / ((2.0 * PI) * 360.0 * 1e-7), loA0, loB1);
        onePole(sampleRate, 1.0 / ((2.0 * PI) * 1390.0 * 2.233e-5), hiA0, hiB1);
    }

    // "LED" filter: 2-pole Sallen-Key, R1=R2=10k, C1=6800pF, C2=3900pF (~3090.5 Hz, Q ~0.660)
    const double R1 = 10000.0, R2 = 10000.0, C1 = 6.8e-9, C2 = 3.9e-9;
    const double cutoff = std::min(1.0 / ((2.0 * PI) * std::sqrt(R1 * R2 * C1 * C2)), sampleRate / 2.0 - 1e-4);
    const double q = std::sqrt(R1 * R2 * C1 * C2) / (C2 * (R1 + R2));
    const double a = 1.0 / std::tan((PI * cutoff) / sampleRate);
    const double r = 1.0 / q;
    const double a1 = 1.0 / (1.0 + r * a + a * a);
    ledA1 = static_cast<float>(a1);
    ledA2 = static_cast<float>(2.0 * a1);
    ledB1 = static_cast<float>(2.0 * (1.0 - a * a) * a1);
    ledB2 = static_cast<float>((1.0 - r * a + a * a) * a1);
}

float RcFilters::processSample(float in) {
    if (model == FilterModel::Off) {
        // The LED filter is independent of the model in pt2-clone, but "Off" means no Amiga filtering at all.
        return in;
    }
    float v = in;
    if (useLowpass) {
        loState = v * loA0 + loState * loB1;
        v = loState;
    }
    if (ledFilterOn) {
        const float out = v * ledA1 + ledState[0] * ledA2 + ledState[1] * ledA1 - ledState[2] * ledB1 - ledState[3] * ledB2;
        ledState[1] = ledState[0];
        ledState[0] = v;
        ledState[3] = ledState[2];
        ledState[2] = out;
        v = out;
    }
    hiState = v * hiA0 + hiState * hiB1;
    return v - hiState;
}

} // namespace paulascape
