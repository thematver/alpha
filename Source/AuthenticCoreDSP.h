#pragma once
#include <array>
#include <memory>

namespace alpha {
// Normalized, biased-effective Jiles-Atherton loop. Its parameters describe a
// musical material model, not a calibration to an individual tape formulation.
class MagneticHysteresis {
public:
    void reset() noexcept;
    double process(double input,double coercivity,double reversibility) noexcept;
    double magnetization() const noexcept {return m;}
private:
    double m=0,previousH=0;
};

// Clean-room implementation of Dattorro's published plate-class topology.
// Scaling size and mapping RT60 are additions; this is not an EMT device model.
class PlateReverb {
public:
    PlateReverb();
    ~PlateReverb();
    void prepare(double sampleRate);
    void reset() noexcept;
    std::array<double,2> process(std::array<double,2> input,double size,double decay,double damping) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
