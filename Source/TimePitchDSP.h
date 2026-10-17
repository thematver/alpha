#pragma once
#include <array>
#include <memory>

namespace alpha {
// PITCH and TIME are independent, stereo-linked streaming effects. Parameters are
// normalized 0..1; the audio thread owns this object. prepare() is the only
// operation that allocates. reset() and process() are bounded and allocation free.
class TimePitchDSP {
public:
    TimePitchDSP();
    ~TimePitchDSP();
    TimePitchDSP(TimePitchDSP&&) noexcept;
    TimePitchDSP& operator=(TimePitchDSP&&) noexcept;
    TimePitchDSP(const TimePitchDSP&) = delete;
    TimePitchDSP& operator=(const TimePitchDSP&) = delete;
    void prepare(double sampleRate, int maximumBlockSize);
    void reset() noexcept;
    // localIndex: 0 PITCH, 1 TIME. PITCH: transpose,grain,fine,spread.
    // TIME: length,speed,smooth,feedback. cycleHz is the shared motion clock;
    // TIME length is quantized to 1/8,1/4,1/2,1,2,4 clock cycles, capped at 4s.
    // PITCH modes: chromatic,smooth,octave. TIME: slip,reverse,repeat.
    std::array<double, 2> process(int localIndex, std::array<double, 2> input,
                                double amount, std::array<double, 4> controls,
                                int mode, double cycleHz, double lfo = 0) noexcept;
    double maximumTimeSeconds() const noexcept { return 4.0; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
