#include "rc_filters.hpp"
#include <cmath>

namespace paulascape {

RcFilters::RcFilters() {
    setSampleRate(44100.0);
}

void RcFilters::reset() {
    lowPassState1 = 0.0;
    lowPassState2 = 0.0;
    highPassState = 0.0;
    prevInput = 0.0;
}

void RcFilters::setSampleRate(double sr) {
    if (sr < 8000.0) sr = 8000.0;
    sampleRate = sr;
    updateCoefficients();
}

void RcFilters::setFilterModel(FilterModel fm) {
    model = fm;
    updateCoefficients();
}

void RcFilters::setLedFilter(bool enable) {
    ledFilterOn = enable;
    updateCoefficients();
}

void RcFilters::updateCoefficients() {
    if (model == FilterModel::Off) return;

    double dt = 1.0 / sampleRate;

    // A500 low-pass cutoff ~4900 Hz, A1200 low-pass cutoff ~28000 Hz
    double lp1Cutoff = (model == FilterModel::A500) ? 4900.0 : 28000.0;
    double rc_lp1 = 1.0 / (2.0 * 3.14159265358979323846 * lp1Cutoff);
    a0_lp1 = dt / (rc_lp1 + dt);
    b1_lp1 = rc_lp1 / (rc_lp1 + dt);

    // High-pass RC filter cutoff ~5 Hz
    double hpCutoff = 5.0;
    double rc_hp = 1.0 / (2.0 * 3.14159265358979323846 * hpCutoff);
    a0_hp = rc_hp / (rc_hp + dt);
    b1_hp = rc_hp / (rc_hp + dt);

    // 2-pole LED filter cutoff ~3270 Hz
    double ledCutoff = 3270.0;
    double rc_led = 1.0 / (2.0 * 3.14159265358979323846 * ledCutoff);
    a0_lp2 = dt / (rc_led + dt);
    b1_lp2 = rc_led / (rc_led + dt);
}

float RcFilters::processSample(float input) {
    if (model == FilterModel::Off) {
        return input;
    }

    // High-pass filter
    double hpOut = a0_hp * (highPassState + input - prevInput);
    prevInput = input;
    highPassState = hpOut;

    // Fixed hardware low-pass filter
    lowPassState1 = a0_lp1 * hpOut + b1_lp1 * lowPassState1;
    double out = lowPassState1;

    // LED filter (2-pole RC low-pass)
    if (ledFilterOn) {
        lowPassState2 = a0_lp2 * out + b1_lp2 * lowPassState2;
        out = lowPassState2;
    }

    return static_cast<float>(out);
}

} // namespace paulascape
