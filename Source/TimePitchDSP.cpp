#include "TimePitchDSP.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace alpha {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
double bounded(double x, double lo, double hi) noexcept { return std::isfinite(x) ? std::clamp(x, lo, hi) : lo; }
double unit(double x) noexcept { return bounded(x, 0, 1); }
double wrap(double x) noexcept { return x - std::floor(x); }
double slew(double current, double target, double coefficient) noexcept {
    const double value = current + (target - current) * coefficient;
    return std::abs(value - target) < 1e-10 ? target : value;
}

struct Ring {
    std::array<std::vector<float>, 2> data;
    std::uint64_t mask = 0;
    std::int64_t count = 0;
    void prepare(std::size_t minimum) {
        std::size_t size = 16; while (size < minimum) size *= 2;
        for (auto& channel : data) channel.assign(size, 0);
        mask = size - 1; count = 0;
    }
    void reset() noexcept { count = 0; }
    void write(const std::array<double, 2>& value) noexcept {
        for (int ch = 0; ch < 2; ++ch) data[(size_t) ch][(std::uint64_t) count & mask] = (float) value[(size_t) ch];
        ++count;
    }
    double at(int ch, std::int64_t position) const noexcept {
        if (position < 0 || position >= count || count - position > (std::int64_t) mask + 1) return 0;
        return data[(size_t) ch][(std::uint64_t) position & mask];
    }
    double read(int ch, double position) const noexcept {
        const auto index = (std::int64_t) std::floor(position);
        const double t = position - (double) index;
        // Four-point, third-order Lagrange interpolation, centered on the read interval.
        const double a = -t * (t - 1) * (t - 2) / 6;
        const double b = (t + 1) * (t - 1) * (t - 2) * .5;
        const double c = -(t + 1) * t * (t - 2) * .5;
        const double d = (t + 1) * t * (t - 1) / 6;
        return a * at(ch, index - 1) + b * at(ch, index) + c * at(ch, index + 1) + d * at(ch, index + 2);
    }
};

struct Lowpass {
    struct State { double z1 = 0, z2 = 0; };
    std::array<std::array<State, 2>, 2> state {};
    std::array<std::array<double, 5>, 2> coefficients {};
    void reset() noexcept { state = {}; }
    void cutoff(double sampleRate, double hz) noexcept {
        const double k = std::tan(pi * bounded(hz / sampleRate, .005, .45));
        constexpr std::array<double, 2> q { .541196100146197, 1.30656296487638 };
        for (int i = 0; i < 2; ++i) {
            const double norm = 1 / (1 + k / q[(size_t) i] + k * k);
            coefficients[(size_t) i] = { k*k*norm, 2*k*k*norm, k*k*norm,
                2*(k*k-1)*norm, (1-k/q[(size_t)i]+k*k)*norm };
        }
    }
    std::array<double, 2> process(std::array<double, 2> input) noexcept {
        for (int ch = 0; ch < 2; ++ch) for (int stage = 0; stage < 2; ++stage) {
            auto& s = state[(size_t) ch][(size_t) stage]; const auto& c = coefficients[(size_t) stage];
            const double output = input[(size_t) ch] * c[0] + s.z1;
            s.z1 = input[(size_t) ch] * c[1] - output * c[3] + s.z2;
            s.z2 = input[(size_t) ch] * c[2] - output * c[4];
            if (std::abs(s.z1) < 1e-25) s.z1 = 0;
            if (std::abs(s.z2) < 1e-25) s.z2 = 0;
            input[(size_t) ch] = output;
        }
        return input;
    }
};
}

struct TimePitchDSP::Impl {
    Ring pitchMemory, timeMemory;
    Lowpass pitchFilter, timeFilter;
    std::array<double, 2049> windows {};
    double sampleRate = 48000, smoothing = .002, slowSmoothing = .0005;
    std::array<double, 2> pitchPhase { 0, 0 };
    std::array<double, 2> pitchRatio { 1, 1 };
    double pitchGrain = 2880, pitchEnabled = 0;
    double detectorPrevious = 0, detectorCrossing = -1, detectedPeriod = 0;
    int periodConfidence = 0;
    std::array<double, 3> timeModeWeights { 1, 0, 0 };
    struct TimeBank {
        double length = 24000, repeatPhase = 0;
        double clockFactor = 1;
        bool clockLinked = false;
        std::array<double, 2> phase {}, anchor {}, loopPhase {};
    };
    std::array<TimeBank, 2> timeBanks {};
    int activeTimeBank = 0;
    bool timeFading = false;
    bool timeClockInitialized = false, absoluteClock = false, pendingClockReset = false;
    double clockPosition = 0, lastClockInput = 0, currentCycleHz = .5;
    double timeFade = 0, timeSpeed = 1, timeFeedback = 0, timeEnabled = 0, timeSmooth = .5;
    std::array<double, 2> previousTime {}, dcInput {}, dcOutput {};
    int pitchCounter = 0, timeCounter = 0;
    bool prepared = false, pitchFirst = true, timeFirst = true;

    double window(double phase) const noexcept {
        const double p = wrap(phase) * 2048; const int index = (int) p;
        return windows[(size_t) index] + (windows[(size_t) index + 1] - windows[(size_t) index]) * (p - index);
    }
    void reset() noexcept {
        pitchMemory.reset(); timeMemory.reset(); pitchFilter.reset(); timeFilter.reset();
        pitchPhase = {}; pitchRatio = { 1, 1 }; pitchEnabled = 0;
        detectorPrevious = 0; detectorCrossing = -1; detectedPeriod = 0; periodConfidence = 0;
        timeBanks = {}; activeTimeBank = 0; timeFading = false; timeFade = 0;
        timeClockInitialized = absoluteClock = pendingClockReset = false;
        clockPosition = lastClockInput = 0; currentCycleHz = .5;
        previousTime = {}; dcInput = {}; dcOutput = {};
        pitchCounter = timeCounter = 0; pitchFirst = timeFirst = true;
        timeModeWeights = { 1, 0, 0 };
    }
    std::array<double, 2> pitch(std::array<double, 2> input, double amount,
                                const std::array<double, 4>& controls, int mode) noexcept {
        double semitones = (controls[0] - .5) * 24;
        if (mode == 0) semitones = std::round(semitones);
        if (mode == 2) semitones = std::round(semitones / 12) * 12;
        const double fine = (controls[2] - .5); // semitones: +/- 50 cents.
        const double spread = controls[3] * .2;
        const std::array<double, 2> targetRatio { std::exp2((semitones + fine - spread) / 12), std::exp2((semitones + fine + spread) / 12) };
        double targetGrain = sampleRate * (.024 + controls[1] * .096);
        // Align the splice distance to complete cycles when a stable periodic
        // waveform is present. Unstable/polyphonic crossings leave the requested
        // grain size alone; this is splice alignment, not formant correction.
        const double detector = input[0] + input[1] * .3;
        if (detector > 0 && detectorPrevious <= 0) {
            const double crossing = (double) pitchMemory.count - 1 + (-detectorPrevious / (detector - detectorPrevious));
            if (detectorCrossing >= 0) {
                const double period = crossing - detectorCrossing;
                if (period >= sampleRate / 2000 && period <= sampleRate / 45) {
                    if (detectedPeriod > 0 && std::abs(period - detectedPeriod) < detectedPeriod * .025) {
                        periodConfidence = std::min(12, periodConfidence + 1);
                        detectedPeriod = slew(detectedPeriod, period, .15);
                    } else {
                        periodConfidence = std::max(0, periodConfidence - 2);
                        if (periodConfidence == 0) detectedPeriod = period;
                    }
                } else periodConfidence = 0;
            }
            detectorCrossing = crossing;
        }
        detectorPrevious = detector;
        if (periodConfidence >= 4 && detectedPeriod > 0)
            targetGrain = std::max(1.0, std::round(targetGrain / detectedPeriod)) * detectedPeriod;
        const double enabled = std::abs(semitones + fine) + spread > 1e-9 ? 1 : 0;
        if (pitchFirst) { pitchRatio = targetRatio; pitchGrain = targetGrain; pitchEnabled = enabled; pitchFirst = false; }
        pitchGrain = slew(pitchGrain, targetGrain, slowSmoothing);
        pitchEnabled = slew(pitchEnabled, enabled, smoothing);
        for (int ch = 0; ch < 2; ++ch) pitchRatio[(size_t) ch] = slew(pitchRatio[(size_t) ch], targetRatio[(size_t) ch], smoothing);
        if ((pitchCounter++ & 31) == 0) pitchFilter.cutoff(sampleRate, sampleRate * .44 / std::max({1.0, pitchRatio[0], pitchRatio[1]}));
        pitchMemory.write(pitchFilter.process(input));
        std::array<double, 2> output = input;
        for (int ch = 0; ch < 2; ++ch) {
            const double p = pitchPhase[(size_t) ch], q = wrap(p + .5);
            const double latest = (double) pitchMemory.count - 1;
            const double wet = window(p) * pitchMemory.read(ch, latest - 4 - p * pitchGrain)
                             + window(q) * pitchMemory.read(ch, latest - 4 - q * pitchGrain);
            pitchPhase[(size_t) ch] = wrap(p + (1 - pitchRatio[(size_t) ch]) / pitchGrain);
            if (amount > 0 && pitchEnabled > 0) output[(size_t) ch] += (wet - input[(size_t) ch]) * amount * pitchEnabled;
        }
        return output;
    }
    double movingWindow(double phase) const noexcept {
        const double w = window(phase);
        if (timeSmooth < .5) return w + (w*w*(3-2*w) - w) * (1-2*timeSmooth);
        const double triangle = 1 - std::abs(2 * wrap(phase) - 1);
        return w + (triangle - w) * (2*timeSmooth-1);
    }
    double movingRead(int ch, double phase, double length) const noexcept {
        const double q = wrap(phase + .5), latest = (double) timeMemory.count - 1;
        return movingWindow(phase) * timeMemory.read(ch, latest - 4 - phase * length)
             + movingWindow(q) * timeMemory.read(ch, latest - 4 - q * length);
    }
    double loopRead(int ch, double anchor, double offset, double length, double crossfade) const noexcept {
        const double fade = std::clamp(crossfade, 8.0, length * .24);
        const double period = length - fade;
        const double p = wrap(offset / period) * period;
        const double first = timeMemory.read(ch, anchor + p + fade);
        if (p < period - fade) return first;
        const double blend = .5 - .5 * std::cos(pi * (p - (period - fade)) / fade);
        const double second = timeMemory.read(ch, anchor + p - (period - fade));
        return first + (second - first) * blend;
    }
    void initialiseBank(TimeBank& bank, double length, const TimeBank* previous = nullptr) noexcept {
        bank.length = length;
        bank.phase = previous != nullptr ? previous->phase : std::array<double, 2>{0,0};
        bank.clockFactor = length * currentCycleHz / sampleRate;
        bank.clockLinked = timeClockInitialized;
        bank.repeatPhase = bank.clockLinked ? wrap(clockPosition / bank.clockFactor)
                                           : previous != nullptr ? previous->repeatPhase : 0;
        for (int head = 0; head < 2; ++head) {
            const double p = wrap(bank.repeatPhase + (double) head * .5);
            bank.anchor[(size_t) head] = (double) timeMemory.count - 1 - length * (1 + p) - 4;
            bank.loopPhase[(size_t) head] = bank.clockLinked ? wrap(p*timeSpeed)
                : previous != nullptr ? previous->loopPhase[(size_t) head] : wrap(p*timeSpeed);
        }
    }
    double bankRead(const TimeBank& bank, int ch, double crossfade) const noexcept {
        const double stopGain = std::min(1.0, timeSpeed * 20);
        const double slip = movingRead(ch, bank.phase[0], bank.length) * stopGain;
        const double reverse = movingRead(ch, bank.phase[1], bank.length) * stopGain;
        double repeat = 0;
        for (int head = 0; head < 2; ++head) {
            const double p = wrap(bank.repeatPhase + (double) head * .5);
            const double offset = bank.loopPhase[(size_t)head] * (bank.length - std::min(crossfade, bank.length * .24));
            repeat += window(p) * loopRead(ch, bank.anchor[(size_t) head], offset, bank.length, crossfade);
        }
        return slip * timeModeWeights[0] + reverse * timeModeWeights[1] + repeat * timeModeWeights[2];
    }
    void advanceBank(TimeBank& bank) noexcept {
        bank.phase[0] = wrap(bank.phase[0] + (1-timeSpeed)/bank.length);
        bank.phase[1] = wrap(bank.phase[1] + (1+timeSpeed)/bank.length);
        const double oldPhase = bank.repeatPhase;
        bank.repeatPhase = bank.clockLinked && timeClockInitialized
            ? wrap((clockPosition + currentCycleHz/sampleRate) / bank.clockFactor)
            : wrap(bank.repeatPhase + 1/bank.length);
        const double period = bank.length - std::min(sampleRate*(.002+timeSmooth*.058),bank.length*.24);
        for (int head = 0; head < 2; ++head) {
            bank.loopPhase[(size_t)head] = wrap(bank.loopPhase[(size_t)head] + timeSpeed/period);
            if (wrap(bank.repeatPhase + (double) head * .5) < wrap(oldPhase + (double) head * .5)) {
                bank.anchor[(size_t) head] = (double) timeMemory.count - 1 - bank.length - 4;
                bank.loopPhase[(size_t) head] = 0;
            }
        }
    }
    std::array<double, 2> time(std::array<double, 2> input, double amount,
                              const std::array<double, 4>& controls, int mode,
                              double cycleHz, double hostPhase) noexcept {
        currentCycleHz = cycleHz;
        const bool validClock = std::isfinite(hostPhase) && hostPhase >= 0;
        bool clockJump = false;
        if (validClock) {
            if (!timeClockInitialized) {
                clockPosition = hostPhase; absoluteClock = hostPhase >= 1;
                clockJump = !timeFirst; timeClockInitialized = true;
            } else {
                if (hostPhase >= 1) absoluteClock = true;
                double delta = hostPhase - lastClockInput;
                if (!absoluteClock) {
                    if (delta < -.5) delta += 1;
                    if (delta > .5) delta -= 1;
                }
                const double expected = cycleHz/sampleRate;
                clockJump = std::abs(delta-expected) > std::max(expected*8,1e-6);
                clockPosition = absoluteClock ? hostPhase : clockPosition + delta;
                // A wrapped caller cannot identify a whole-cycle seek; preserve
                // its local cycle count. Unwrapped host callers are unambiguous.
                if (clockJump && !absoluteClock) clockPosition = std::floor(clockPosition)+hostPhase;
            }
            lastClockInput = hostPhase;
        } else if (timeClockInitialized) {
            timeClockInitialized = absoluteClock = false;
            for (auto& bank : timeBanks) bank.clockLinked = false;
        }
        if (clockJump) {
            // Keep every currently audible read configuration continuous while
            // its replacement is anchored to the new transport position.
            for (auto& bank : timeBanks) bank.clockLinked = false;
            pendingClockReset = true;
        }
        constexpr std::array<double, 6> lengthFactors { .125, .25, .5, 1, 2, 4 };
        const int lengthIndex = std::clamp((int) std::floor(controls[0] * 5 + .5), 0, 5);
        const double targetLength = bounded(sampleRate * lengthFactors[(size_t) lengthIndex] / cycleHz, sampleRate * .025, sampleRate * 4);
        const double targetSpeed = controls[1] * 2;
        const double targetFeedback = controls[3] * .85;
        const double enable = mode != 0 || std::abs(targetSpeed - 1) > 1e-9 || targetFeedback > 0 ? 1 : 0;
        if (timeFirst) {
            timeSpeed = targetSpeed; timeFeedback = targetFeedback;
            timeSmooth = controls[2];
            timeModeWeights = { mode == 0 ? 1.0 : 0.0, mode == 1 ? 1.0 : 0.0, mode == 2 ? 1.0 : 0.0 };
            timeEnabled = enable; initialiseBank(timeBanks[0],targetLength); initialiseBank(timeBanks[1],targetLength);
            timeFirst = false;
        }
        // A length jump crossfades complete read configurations. Ramping a long
        // delay through intermediate positions would impose a large Doppler chirp.
        if (!timeFading && (pendingClockReset || std::abs(timeBanks[(size_t)activeTimeBank].length-targetLength) > .5)) {
            initialiseBank(timeBanks[(size_t)(1-activeTimeBank)],targetLength,&timeBanks[(size_t)activeTimeBank]);
            timeBanks[(size_t)activeTimeBank].clockLinked = false;
            timeFading = true; timeFade = 0;
            pendingClockReset = false;
        }
        for (auto& bank : timeBanks) if (bank.clockLinked && timeClockInitialized)
            bank.repeatPhase = wrap(clockPosition / bank.clockFactor);
        timeSpeed = slew(timeSpeed, targetSpeed, smoothing);
        timeFeedback = slew(timeFeedback, targetFeedback, smoothing);
        timeSmooth = slew(timeSmooth, controls[2], smoothing);
        timeEnabled = slew(timeEnabled, enable, smoothing);
        for (int i = 0; i < 3; ++i) timeModeWeights[(size_t) i] = slew(timeModeWeights[(size_t) i], mode == i ? 1.0 : 0.0, smoothing);
        if ((timeCounter++ & 31) == 0) timeFilter.cutoff(sampleRate, sampleRate * .44 / std::max(1.0, timeSpeed));
        auto record = timeFilter.process(input);
        for (int ch = 0; ch < 2; ++ch) {
            const double feedback = previousTime[(size_t) ch] * timeFeedback * amount;
            record[(size_t) ch] += feedback / (1 + .03 * std::abs(feedback));
            record[(size_t) ch] = bounded(record[(size_t) ch], -16, 16);
        }
        timeMemory.write(record);
        const double crossfade = sampleRate * (.002 + timeSmooth * .058);
        const double fadeBlend = timeFading ? .5-.5*std::cos(pi*timeFade) : 0;
        std::array<double, 2> output = input;
        for (int ch = 0; ch < 2; ++ch) {
            double wet = bankRead(timeBanks[(size_t)activeTimeBank],ch,crossfade);
            if (timeFading) wet += (bankRead(timeBanks[(size_t)(1-activeTimeBank)],ch,crossfade)-wet)*fadeBlend;
            // Remove the held-sample DC that otherwise remains at tape-stop speed.
            const double dc = wet - dcInput[(size_t) ch] + .9993 * dcOutput[(size_t) ch];
            dcInput[(size_t) ch] = wet; dcOutput[(size_t) ch] = std::abs(dc) < 1e-25 ? 0 : dc;
            previousTime[(size_t) ch] = dcOutput[(size_t) ch];
            if (amount > 0 && timeEnabled > 0) output[(size_t) ch] += (previousTime[(size_t) ch] - input[(size_t) ch]) * amount * timeEnabled;
        }
        advanceBank(timeBanks[(size_t)activeTimeBank]);
        if (timeFading) {
            advanceBank(timeBanks[(size_t)(1-activeTimeBank)]);
            timeFade += 1/(sampleRate*.035);
            if (timeFade >= 1) { activeTimeBank=1-activeTimeBank; timeFading=false; timeFade=0; }
        }
        return output;
    }
};

TimePitchDSP::TimePitchDSP() : impl(std::make_unique<Impl>()) {}
TimePitchDSP::~TimePitchDSP() = default;
TimePitchDSP::TimePitchDSP(TimePitchDSP&&) noexcept = default;
TimePitchDSP& TimePitchDSP::operator=(TimePitchDSP&&) noexcept = default;
void TimePitchDSP::prepare(double sampleRate, int maximumBlockSize) {
    (void) maximumBlockSize;
    impl->sampleRate = bounded(sampleRate, 8000, 384000);
    impl->smoothing = 1 - std::exp(-1 / (impl->sampleRate * .012));
    impl->slowSmoothing = 1 - std::exp(-1 / (impl->sampleRate * .050));
    impl->pitchMemory.prepare((size_t) std::ceil(impl->sampleRate * .25) + 16);
    impl->timeMemory.prepare((size_t) std::ceil(impl->sampleRate * 12.1) + 16);
    for (size_t i = 0; i < impl->windows.size(); ++i) impl->windows[i] = .5 - .5 * std::cos(2 * pi * (double) i / 2048);
    impl->pitchFilter.cutoff(impl->sampleRate, impl->sampleRate * .44);
    impl->timeFilter.cutoff(impl->sampleRate, impl->sampleRate * .44);
    impl->prepared = true; impl->reset();
}
void TimePitchDSP::reset() noexcept { impl->reset(); }
std::array<double, 2> TimePitchDSP::process(int localIndex, std::array<double, 2> input,
        double amount, std::array<double, 4> controls, int mode, double cycleHz,
        double lfo, double phase) noexcept {
    (void) lfo;
    if (!impl->prepared || localIndex < 0 || localIndex > 1) return input;
    for (auto& sample : input) if (!std::isfinite(sample)) sample = 0;
    for (auto& control : controls) control = unit(control);
    amount = unit(amount); mode = std::clamp(mode, 0, 2); cycleHz = bounded(cycleHz, .03, 24);
    return localIndex == 0 ? impl->pitch(input, amount, controls, mode) : impl->time(input, amount, controls, mode, cycleHz, phase);
}
}
