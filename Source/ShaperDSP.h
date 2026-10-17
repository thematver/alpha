#pragma once
#include <array>
#include <vector>

namespace alpha {
// Four stereo shapers with bounded, preallocated state. Controls use 0..1.
class ShaperDSP {
public:
    using Stereo = std::array<double, 2>;
    using Controls = std::array<double, 4>;
    void prepare(double sampleRate);
    void reset() noexcept;
    Stereo process(int index, Stereo input, double amount, Controls controls,
                   int mode, double lfo, double phase = -1) noexcept;
private:
    struct FilterState { double integrator1=0,integrator2=0,previousDrive=0; };
    std::array<FilterState,2> filters {};
    std::array<Controls,4> smoothedControls {};
    std::array<bool,4> initialized {};
    double controlSmoothing=.001,modeSmoothing=.001;
    std::array<std::array<double,3>,4> modeWeights {};
    std::vector<double> widthDelay;
    std::array<double,512> panWave {};
    size_t writePosition=0;
    double rate=48000, volumeGain=1, panPosition=0, sideLow=0;
    double filterG=.1,filterK=1,filterA1=1,filterA2=0,filterA3=0;
    double panSmooth=.001,widthBassCoefficient=0;
    unsigned filterCounter=0;
    double delayRead(double samples) const noexcept;
};
}
