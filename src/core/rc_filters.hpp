#pragma once

#include <cstdint>

namespace paulascape {

enum class FilterModel {
    A500,
    A1200,
    Off
};

class RcFilters {
public:
    RcFilters();
    void reset();
    void setSampleRate(double sampleRate);
    void setFilterModel(FilterModel model);
    void setLedFilter(bool enable);

    float processSample(float input);

private:
    double sampleRate = 44100.0;
    FilterModel model = FilterModel::A500;
    bool ledFilterOn = false;

    // Filter states
    double lowPassState1 = 0.0;
    double lowPassState2 = 0.0;
    double highPassState = 0.0;
    double prevInput = 0.0;

    void updateCoefficients();
    double a0_lp1 = 0.0, b1_lp1 = 0.0;
    double a0_lp2 = 0.0, b1_lp2 = 0.0;
    double a0_hp  = 0.0, b1_hp  = 0.0;
};

} // namespace paulascape
