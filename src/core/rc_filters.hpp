#pragma once

#include <cstdint>

namespace paulascape {

enum class FilterModel {
    A500,
    A1200,
    Off
};

// Amiga output filters as modelled in pt2-clone (BSD-3-Clause): 1-pole RC low-pass (A500 only),
// 1-pole RC high-pass, and the 2-pole Sallen-Key "LED" low-pass. One instance filters one mono bus.
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

    bool useLowpass = true;
    float loA0 = 0, loB1 = 0, loState = 0;
    float hiA0 = 0, hiB1 = 0, hiState = 0;
    float ledA1 = 0, ledA2 = 0, ledB1 = 0, ledB2 = 0;
    float ledState[4] = {};

    void updateCoefficients();
};

} // namespace paulascape
