#include "AlphaDSP.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace alpha {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double tau = 2.0 * pi;
constexpr double invSqrt8 = 0.35355339059327376220;

float bounded(float v, float lo, float hi, float fallback = 0.0f) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}
double clean(double v) noexcept { return std::abs(v) < 1.0e-25 ? 0.0 : v; }
double slide(double current, double target, double coefficient) noexcept
{
    const auto difference = target - current;
    return std::abs(difference) < 1.0e-7 ? target : current + coefficient * difference;
}
double lowpassCoefficient(double hz, double sampleRate) noexcept
{
    return 1.0 - std::exp(-tau * std::clamp(hz, 5.0, sampleRate * 0.43) / sampleRate);
}
double logCosh(double x) noexcept
{
    const auto a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994530942;
}

struct Random {
    uint32_t state = 1;
    uint32_t next() noexcept
    {
        auto x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return state = x;
    }
    double bipolar() noexcept { return static_cast<double>(next()) / 2147483648.0 - 1.0; }
};

struct Delay {
    std::vector<double> samples;
    int position = 0;
    void prepare(int length) { samples.assign(static_cast<size_t>(std::max(8, length)), 0.0); position = 0; }
    void reset() noexcept { std::fill(samples.begin(), samples.end(), 0.0); position = 0; }
    void write(double v) noexcept
    {
        samples[static_cast<size_t>(position)] = clean(v);
        if (++position == static_cast<int>(samples.size())) position = 0;
    }
    // Linear fractional reads are intentionally used in the feedback network:
    // their interpolation cannot add energy to an otherwise stable FDN.
    double read(double delay) const noexcept
    {
        auto index = static_cast<double>(position) - std::clamp(delay, 1.0, static_cast<double>(samples.size() - 2));
        if (index < 0.0) index += static_cast<double>(samples.size());
        const auto a = static_cast<int>(index);
        auto b = a + 1;
        if (b == static_cast<int>(samples.size())) b = 0;
        const auto fraction = index - a;
        return samples[static_cast<size_t>(a)] + fraction * (samples[static_cast<size_t>(b)] - samples[static_cast<size_t>(a)]);
    }
};

struct Allpass {
    Delay delay;
    double length = 1;
    double tick(double x) noexcept
    {
        const auto delayed = delay.read(length);
        const auto result = delayed - 0.55 * x;
        delay.write(x + 0.55 * result);
        return result;
    }
};

// First-order antiderivative antialiasing. Unlike evaluating the transfer
// function directly, this integrates it across the sample-to-sample interval.
// It considerably reduces aliasing without oversampling or audio-thread memory.
struct Saturator {
    double previous = 0;
    double tick(double x, double bias, const std::array<double, 3>& weights) noexcept
    {
        x = std::clamp(x, -1.0e6, 1.0e6);
        const auto dx = x - previous;
        double soft, tube, fold;
        if (std::abs(dx) > 1.0e-5) {
            // Rational soft-clip's primitive is sqrt(1+x*x).
            soft = (std::hypot(1.0, x) - std::hypot(1.0, previous)) / dx;
            tube = (logCosh(x + bias) - logCosh(previous + bias)) / dx - std::tanh(bias);
            fold = (std::cos(previous) - std::cos(x)) / dx;
        } else {
            const auto midpoint = 0.5 * (x + previous);
            soft = midpoint / std::hypot(1.0, midpoint);
            tube = std::tanh(midpoint + bias) - std::tanh(bias);
            fold = std::sin(midpoint);
        }
        previous = x;
        return weights[0] * soft + weights[1] * tube + weights[2] * fold;
    }
};

struct Channel {
    Delay wobble;
    std::array<Allpass, 4> diffusion;
    Saturator tapeSaturation, driveSaturation;
    Random random;
    double tapeLow = 0, tapeSub = 0, tapeHigh = 0;
    double driveLow = 0, driveDcIn = 0, driveDcOut = 0, spaceDc = 0;
    double noiseLow = 0, noisePink = 0, noiseColor = 0, crackle = 0, noiseDc = 0;
    double gritPhase = 0, gritHeld = 0;
    void reset(int c) noexcept
    {
        wobble.reset();
        for (auto& a : diffusion) a.delay.reset();
        tapeSaturation = {}; driveSaturation = {};
        tapeLow = tapeSub = tapeHigh = driveLow = driveDcIn = driveDcOut = spaceDc = 0;
        noiseLow = noisePink = noiseColor = crackle = noiseDc = 0;
        gritPhase = gritHeld = 0;
        random.state = c == 0 ? 0x5d112f71u : 0xa4b27c85u;
    }
};

struct AtomicSettings {
    std::array<std::atomic<float>, moduleCount * 6> scenes;
    std::array<std::atomic<float>, moduleCount> modulation;
    std::array<std::atomic<int>, moduleCount> enabled, modes;
    std::atomic<float> morph {0}, character {0.7f}, mix {1}, outputDb {0}, rate {0.25f}, depth {0.5f};
    std::atomic<int> shape {0}, bypass {0}, delta {0}, autoGain {0};

    void store(const Settings& s) noexcept
    {
        for (int i = 0; i < moduleCount; ++i) {
            const auto j = static_cast<size_t>(i);
            scenes[j].store(bounded(s.a.amount[j], 0, 1), std::memory_order_relaxed);
            scenes[j + moduleCount].store(bounded(s.a.tone[j], 0, 1, 0.5f), std::memory_order_relaxed);
            scenes[j + moduleCount * 2].store(bounded(s.a.detail[j], 0, 1, 0.5f), std::memory_order_relaxed);
            scenes[j + moduleCount * 3].store(bounded(s.b.amount[j], 0, 1), std::memory_order_relaxed);
            scenes[j + moduleCount * 4].store(bounded(s.b.tone[j], 0, 1, 0.5f), std::memory_order_relaxed);
            scenes[j + moduleCount * 5].store(bounded(s.b.detail[j], 0, 1, 0.5f), std::memory_order_relaxed);
            modulation[j].store(bounded(s.modulation[j], -1, 1), std::memory_order_relaxed);
            enabled[j].store(s.enabled[j] ? 1 : 0, std::memory_order_relaxed);
            modes[j].store(std::clamp(s.mode[j], 0, 2), std::memory_order_relaxed);
        }
        morph.store(bounded(s.morph, 0, 1), std::memory_order_relaxed);
        character.store(bounded(s.character, 0, 1, 0.7f), std::memory_order_relaxed);
        mix.store(bounded(s.mix, 0, 1, 1), std::memory_order_relaxed);
        outputDb.store(bounded(s.outputDb, -24, 12), std::memory_order_relaxed);
        rate.store(bounded(s.lfoRateHz, 0.01f, 30, 0.25f), std::memory_order_relaxed);
        depth.store(bounded(s.lfoDepth, 0, 1), std::memory_order_relaxed);
        shape.store(std::clamp(s.lfoShape, 0, 3), std::memory_order_relaxed);
        bypass.store(s.bypass ? 1 : 0, std::memory_order_relaxed);
        delta.store(s.delta ? 1 : 0, std::memory_order_relaxed);
        autoGain.store(s.autoGain ? 1 : 0, std::memory_order_relaxed);
    }
    Settings load() const noexcept
    {
        Settings s;
        for (int i = 0; i < moduleCount; ++i) {
            const auto j = static_cast<size_t>(i);
            s.a.amount[j] = scenes[j].load(std::memory_order_relaxed);
            s.a.tone[j] = scenes[j + moduleCount].load(std::memory_order_relaxed);
            s.a.detail[j] = scenes[j + moduleCount * 2].load(std::memory_order_relaxed);
            s.b.amount[j] = scenes[j + moduleCount * 3].load(std::memory_order_relaxed);
            s.b.tone[j] = scenes[j + moduleCount * 4].load(std::memory_order_relaxed);
            s.b.detail[j] = scenes[j + moduleCount * 5].load(std::memory_order_relaxed);
            s.modulation[j] = modulation[j].load(std::memory_order_relaxed);
            s.enabled[j] = enabled[j].load(std::memory_order_relaxed) != 0;
            s.mode[j] = modes[j].load(std::memory_order_relaxed);
        }
        s.morph = morph.load(std::memory_order_relaxed);
        s.character = character.load(std::memory_order_relaxed);
        s.mix = mix.load(std::memory_order_relaxed);
        s.outputDb = outputDb.load(std::memory_order_relaxed);
        s.lfoRateHz = rate.load(std::memory_order_relaxed);
        s.lfoDepth = depth.load(std::memory_order_relaxed);
        s.lfoShape = shape.load(std::memory_order_relaxed);
        s.bypass = bypass.load(std::memory_order_relaxed) != 0;
        s.delta = delta.load(std::memory_order_relaxed) != 0;
        s.autoGain = autoGain.load(std::memory_order_relaxed) != 0;
        return s;
    }
};
static_assert(std::atomic<float>::is_always_lock_free);
}

struct Engine::Impl {
    AtomicSettings target;
    Settings current;
    Scene sound;
    std::array<double, moduleCount> active {1, 1, 1, 1, 1, 1};
    std::array<std::array<double, 3>, moduleCount> modeWeights {};
    std::array<double, 4> shapeWeights {1, 0, 0, 0};
    std::array<Channel, 2> channel;
    std::array<Delay, 8> reverb;
    std::array<double, 8> reverbLow {}, delaySamples {}, delayTargets {}, feedback {};
    std::array<std::atomic<float>, Engine::scopeSize> scope;
    std::array<std::atomic<float>, Engine::analyserSize> analyser;
    std::atomic<unsigned> scopeWrite {0}, analyserWrite {0};
    std::atomic<float> inputMeter {0}, outputMeter {0}, lfoMeter {0};
    double sampleRate = 48000, smoothing = 0.001, switchSmoothing = 0.001;
    double phase = 0, wowPhase = 0, flutterPhase = 0, wowAuxPhase = 0, flutterAuxPhase = 0, humPhase = 0;
    double irregularity = 0, randomLfo = 0, randomLfoTarget = 0;
    double envelope = 0, inputPeak = 0, outputPeak = 0;
    double bypassBlend = 0, deltaBlend = 0, autoGainBlend = 0;
    double dryEnergy = 0, wetEnergy = 0, gainCompensation = 1, energySmoothing = 0.0001;
    double tapeLp = 0.4, tapeHp = 0.004, tapeBass = 0.01, driveLp = 0.4;
    double noiseLp = 0.2, noisePinkLp = 0.02, noiseDcLp = 0.01;
    double roomDamping = 0.4, roomInputHp = 0.01, envelopeAttack = 0.01, envelopeRelease = 0.001, meterRelease = 0.9999;
    double wowRate = 0.3, flutterRate = 7, crackleDecay = 0.99, dcCoefficient = 0.995;
    unsigned controlCounter = 0, scopeCounter = 0, scopeInterval = 12;
    Random modulationRandom;
    bool prepared = false, initial = true;

    Impl()
    {
        target.store(Settings {});
        for (auto& v : scope) v.store(0, std::memory_order_relaxed);
        for (auto& v : analyser) v.store(0, std::memory_order_relaxed);
    }
    static Scene interpolate(const Settings& s) noexcept
    {
        Scene result;
        for (size_t i = 0; i < moduleCount; ++i) {
            result.amount[i] = s.a.amount[i] + s.morph * (s.b.amount[i] - s.a.amount[i]);
            result.tone[i] = s.a.tone[i] + s.morph * (s.b.tone[i] - s.a.tone[i]);
            result.detail[i] = s.a.detail[i] + s.morph * (s.b.detail[i] - s.a.detail[i]);
        }
        return result;
    }
    void snap(const Settings& s, const Scene& interpolated) noexcept
    {
        current = s;
        sound = interpolated;
        bypassBlend = s.bypass ? 1.0 : 0.0;
        deltaBlend = s.delta ? 1.0 : 0.0;
        autoGainBlend = s.autoGain ? 1.0 : 0.0;
        for (size_t i = 0; i < moduleCount; ++i) {
            active[i] = s.enabled[i] ? 1.0 : 0.0;
            for (int m = 0; m < 3; ++m) modeWeights[i][static_cast<size_t>(m)] = s.mode[i] == m ? 1.0 : 0.0;
        }
        for (int m = 0; m < 4; ++m) shapeWeights[static_cast<size_t>(m)] = s.lfoShape == m ? 1.0 : 0.0;
    }
    void smooth(const Settings& s, const Scene& interpolated) noexcept
    {
        auto parameter = [this](float& now, float desired) {
            const auto next = static_cast<float>(slide(now, desired, smoothing));
            // Reach the actual target once float precision can no longer
            // represent a smoothing step (particularly important for Mix=1).
            now = next == now ? desired : next;
        };
        for (size_t i = 0; i < moduleCount; ++i) {
            // Smooth the sound itself. Independently smoothing A, B and Morph
            // creates an audible detour when a user swaps the scene labels.
            // Interpolation before smoothing makes that operation invariant,
            // including during an earlier parameter or morph transition.
            parameter(sound.amount[i], interpolated.amount[i]);
            parameter(sound.tone[i], interpolated.tone[i]);
            parameter(sound.detail[i], interpolated.detail[i]);
            parameter(current.modulation[i], s.modulation[i]);
            active[i] = slide(active[i], s.enabled[i] ? 1.0 : 0.0, switchSmoothing);
            for (int m = 0; m < 3; ++m) {
                auto& weight = modeWeights[i][static_cast<size_t>(m)];
                weight = slide(weight, s.mode[i] == m ? 1.0 : 0.0, switchSmoothing);
            }
        }
        parameter(current.character, s.character);
        parameter(current.mix, s.mix); parameter(current.outputDb, s.outputDb);
        parameter(current.lfoRateHz, s.lfoRateHz); parameter(current.lfoDepth, s.lfoDepth);
        bypassBlend = slide(bypassBlend, s.bypass ? 1.0 : 0.0, switchSmoothing);
        deltaBlend = slide(deltaBlend, s.delta ? 1.0 : 0.0, switchSmoothing);
        autoGainBlend = slide(autoGainBlend, s.autoGain ? 1.0 : 0.0, switchSmoothing);
        for (int m = 0; m < 4; ++m)
            shapeWeights[static_cast<size_t>(m)] = slide(shapeWeights[static_cast<size_t>(m)], s.lfoShape == m ? 1.0 : 0.0, switchSmoothing);
    }
    void coefficients(const std::array<double, moduleCount>& tone, const std::array<double, moduleCount>& detail) noexcept
    {
        const auto& tape = modeWeights[0];
        const auto tapeBrightness = 0.78 * tape[0] + 1.14 * tape[1] + 0.60 * tape[2];
        tapeLp = lowpassCoefficient((4000.0 + 14000.0 * tone[0]) * (1.0 - detail[0] * 0.58) * tapeBrightness, sampleRate);
        tapeHp = lowpassCoefficient(22.0 + 45.0 * detail[0] + 20.0 * tape[2], sampleRate);
        tapeBass = lowpassCoefficient(95.0, sampleRate);
        driveLp = lowpassCoefficient(1100.0 + 18500.0 * tone[3] * tone[3], sampleRate);
        noiseLp = lowpassCoefficient(350.0 + 14500.0 * tone[2] * tone[2], sampleRate);
        wowRate = 0.08 + tone[1] * tone[1] * 1.12;
        flutterRate = 4.0 + 13.0 * tone[1];
        envelopeRelease = 1.0 - std::exp(-1.0 / (sampleRate * (0.08 + 0.32 * detail[2])));
        const auto& room = modeWeights[4];
        const auto scale = (0.45 + tone[4] * 1.55) * (room[0] + 0.72 * room[1] + 1.22 * room[2]);
        const auto rt60 = (0.25 + 8.0 * detail[4] * detail[4]) * (0.72 * room[0] + room[1] + 1.30 * room[2]);
        constexpr std::array<double, 8> times {0.0297, 0.0371, 0.0411, 0.0437, 0.0531, 0.0617, 0.0713, 0.0799};
        for (size_t i = 0; i < 8; ++i) {
            const auto seconds = times[i] * scale;
            delayTargets[i] = seconds * sampleRate;
            if (delaySamples[i] == 0) delaySamples[i] = delayTargets[i];
            feedback[i] = std::pow(0.001, delaySamples[i] / (sampleRate * rt60));
        }
        roomDamping = lowpassCoefficient(4200.0 * room[0] + 9800.0 * room[1] + 2800.0 * room[2], sampleRate);
    }
    double nextLfo() noexcept
    {
        phase += current.lfoRateHz / sampleRate;
        if (phase >= 1.0) { phase -= 1.0; randomLfoTarget = modulationRandom.bipolar(); }
        randomLfo = slide(randomLfo, randomLfoTarget, smoothing * 3.0);
        const auto sine = std::sin(tau * phase);
        const auto triangle = 1.0 - 4.0 * std::abs(phase - 0.5);
        // A rounded square avoids a discontinuous modulation step at each edge.
        const auto square = std::tanh(sine * 9.0);
        return shapeWeights[0] * sine + shapeWeights[1] * triangle + shapeWeights[2] * square + shapeWeights[3] * randomLfo;
    }
};

Engine::Engine() : impl(new Impl) {}
Engine::~Engine() { delete impl; }

void Engine::prepare(double sr, int, int)
{
    auto& p = *impl;
    p.sampleRate = std::isfinite(sr) ? std::clamp(sr, 8000.0, 384000.0) : 48000.0;
    p.smoothing = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.018));
    p.switchSmoothing = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.012));
    p.energySmoothing = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.35));
    p.envelopeAttack = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.003));
    p.meterRelease = std::exp(-1.0 / (p.sampleRate * 0.55));
    p.noisePinkLp = lowpassCoefficient(120.0, p.sampleRate);
    p.noiseDcLp = lowpassCoefficient(28.0, p.sampleRate);
    p.roomInputHp = lowpassCoefficient(65.0, p.sampleRate);
    p.crackleDecay = std::exp(-1.0 / (p.sampleRate * 0.0014));
    p.dcCoefficient = std::exp(-tau * 18.0 / p.sampleRate);
    p.scopeInterval = static_cast<unsigned>(std::max(1.0, std::round(p.sampleRate / 4000.0)));
    constexpr std::array<double, 4> diffusionTimes {0.0047, 0.0063, 0.0091, 0.0127};
    for (size_t c = 0; c < 2; ++c) {
        p.channel[c].wobble.prepare(static_cast<int>(std::ceil(p.sampleRate * 0.04)) + 8);
        for (size_t i = 0; i < 4; ++i) {
            auto& diffuser = p.channel[c].diffusion[i];
            diffuser.length = std::round(diffusionTimes[i] * p.sampleRate * (c == 0 ? 1.0 : 1.083));
            diffuser.delay.prepare(static_cast<int>(diffuser.length) + 4);
        }
    }
    for (auto& delay : p.reverb) delay.prepare(static_cast<int>(std::ceil(p.sampleRate * 0.23)) + 8);
    p.prepared = true;
    reset();
}

void Engine::reset() noexcept
{
    auto& p = *impl;
    for (int c = 0; c < 2; ++c) p.channel[static_cast<size_t>(c)].reset(c);
    for (auto& delay : p.reverb) delay.reset();
    p.reverbLow.fill(0);
    p.delaySamples.fill(0);
    p.delayTargets.fill(0);
    p.phase = p.wowPhase = p.flutterPhase = p.wowAuxPhase = p.flutterAuxPhase = p.humPhase = 0;
    p.irregularity = p.randomLfo = p.randomLfoTarget = 0;
    p.envelope = p.inputPeak = p.outputPeak = 0;
    p.dryEnergy = p.wetEnergy = 0; p.gainCompensation = 1;
    p.modulationRandom.state = 0x1a704c3du;
    p.controlCounter = p.scopeCounter = 0;
    p.scopeWrite.store(0, std::memory_order_release);
    p.analyserWrite.store(0, std::memory_order_release);
    for (auto& v : p.scope) v.store(0, std::memory_order_relaxed);
    for (auto& v : p.analyser) v.store(0, std::memory_order_relaxed);
    p.inputMeter.store(0, std::memory_order_relaxed);
    p.outputMeter.store(0, std::memory_order_relaxed);
    p.lfoMeter.store(0, std::memory_order_relaxed);
    p.initial = true;
}

void Engine::setSettings(const Settings& s) noexcept { impl->target.store(s); }

void Engine::process(float* const* buffers, int channels, int samples) noexcept
{
    auto& p = *impl;
    if (!p.prepared || buffers == nullptr || channels <= 0 || samples <= 0) return;
    const auto channelCount = std::min(channels, 2);
    for (int c = 0; c < channelCount; ++c) if (buffers[c] == nullptr) return;
    const auto desired = p.target.load();
    const auto desiredSound = Impl::interpolate(desired);
    if (p.initial) { p.snap(desired, desiredSound); p.initial = false; }
    double lfo = 0;
    for (int sample = 0; sample < samples; ++sample) {
        p.smooth(desired, desiredSound);
        lfo = p.nextLfo();
        std::array<double, moduleCount> amount, tone, detail;
        for (size_t i = 0; i < moduleCount; ++i) {
            const auto base = p.sound.amount[i];
            // Modulation scales the existing amount. A zero amount and Character
            // at zero are guaranteed to remain neutral, even with assigned LFOs.
            amount[i] = std::clamp(base * (1.0 + lfo * p.current.modulation[i] * p.current.lfoDepth), 0.0, 1.0) * p.current.character * p.active[i];
            tone[i] = p.sound.tone[i];
            detail[i] = p.sound.detail[i];
        }
        if ((p.controlCounter++ & 15u) == 0) p.coefficients(tone, detail);
        std::array<double, 2> dry {}, signal {}, diffused {};
        double peak = 0;
        for (int c = 0; c < channelCount; ++c) {
            const auto input = buffers[c][sample];
            dry[static_cast<size_t>(c)] = std::isfinite(input) ? input : 0.0;
            peak = std::max(peak, std::abs(dry[static_cast<size_t>(c)]));
        }
        const auto envelopeCoefficient = peak > p.envelope ? p.envelopeAttack : p.envelopeRelease;
        p.envelope = clean(p.envelope + envelopeCoefficient * (std::min(peak, 1.0e6) - p.envelope));
        p.inputPeak = std::max(peak, p.inputPeak * p.meterRelease);
        const auto irregularRandom = p.modulationRandom.bipolar();
        p.irregularity = clean(p.irregularity + (2.0 / p.sampleRate) * (irregularRandom - p.irregularity));
        p.wowPhase += p.wowRate / p.sampleRate;
        p.flutterPhase += p.flutterRate / p.sampleRate;
        p.wowAuxPhase += 2.713 * p.wowRate / p.sampleRate;
        p.flutterAuxPhase += 1.731 * p.flutterRate / p.sampleRate;
        p.humPhase += (45.0 + tone[2] * 65.0) / p.sampleRate;
        if (p.wowPhase >= 1.0) p.wowPhase -= 1.0;
        if (p.flutterPhase >= 1.0) p.flutterPhase -= 1.0;
        if (p.wowAuxPhase >= 1.0) p.wowAuxPhase -= 1.0;
        if (p.flutterAuxPhase >= 1.0) p.flutterAuxPhase -= 1.0;
        if (p.humPhase >= 1.0) p.humPhase -= 1.0;
        const auto wowMode = p.modeWeights[1][0] + p.modeWeights[1][1];
        const auto flutterMode = p.modeWeights[1][0] + p.modeWeights[1][2];
        const auto wobbleMix = std::min(1.0, amount[1] * 3.5);
        const auto noiseGate = std::clamp((p.envelope - 0.0002) * 100.0, 0.0, 1.0)
            * (1.0 - detail[2] + detail[2] * std::sqrt(std::min(1.0, p.envelope * 4.0)));
        for (int c = 0; c < channelCount; ++c) {
            const auto j = static_cast<size_t>(c);
            auto& state = p.channel[j];
            auto x = dry[j];
            // Numeric guard affects only pathological data; the dry signal stays
            // bit-exact at normal levels and is never passed through a limiter.
            const auto guarded = std::clamp(x, -1.0e6, 1.0e6);
            state.tapeHigh = clean(state.tapeHigh + p.tapeHp * (guarded - state.tapeHigh));
            const auto highpassed = guarded - state.tapeHigh;
            state.tapeLow = clean(state.tapeLow + p.tapeLp * (highpassed - state.tapeLow));
            state.tapeSub = clean(state.tapeSub + p.tapeBass * (state.tapeLow - state.tapeSub));
            const auto tapeGain = 1.0 + amount[0] * (3.0 + detail[0] * 3.0);
            const auto tapeInput = (state.tapeLow + state.tapeSub * detail[0] * 0.22) * tapeGain;
            const std::array<double, 3> tapeShape {0.2, 0.8, 0.0};
            const auto tapeWet = state.tapeSaturation.tick(tapeInput, detail[0] * 0.16, tapeShape) / (1.0 + amount[0] * 1.7);
            x += amount[0] * (tapeWet - x);

            state.wobble.write(x);
            const auto wow = std::sin(tau * p.wowPhase + c * 0.055)
                + detail[1] * (0.19 * std::sin(tau * p.wowAuxPhase + c * 0.19) + p.irregularity * 15.0);
            const auto flutter = std::sin(tau * p.flutterPhase + 0.7 + c * 0.12)
                + detail[1] * 0.25 * std::sin(tau * p.flutterAuxPhase);
            const auto delay = p.sampleRate * (0.006 + amount[1] * (0.0038 * wowMode * wow + 0.00042 * flutterMode * flutter));
            const auto wobbleWet = state.wobble.read(delay);
            x += wobbleMix * (wobbleWet - x);

            const auto white = state.random.bipolar();
            state.noiseLow = clean(state.noiseLow + p.noiseLp * (white - state.noiseLow));
            state.noisePink = clean(state.noisePink + p.noisePinkLp * (white - state.noisePink));
            const auto event = state.random.next();
            if (event < static_cast<uint32_t>((1.0 + tone[2] * 11.0) * (4294967296.0 / p.sampleRate)))
                state.crackle += white * 2.0;
            state.crackle = clean(state.crackle * p.crackleDecay);
            const auto vinyl = state.noisePink * 4.0 + state.noiseLow * 0.17 + state.crackle;
            const auto hiss = white * 0.62 + state.noiseLow * 0.38;
            const auto mechanical = state.noisePink * 3.0 + state.noiseLow * (0.15 + tone[2] * 0.35)
                + 0.14 * std::sin(tau * p.humPhase + c * 0.3);
            const auto& noiseMode = p.modeWeights[2];
            const auto noise = noiseMode[0] * vinyl + noiseMode[1] * hiss + noiseMode[2] * mechanical;
            state.noiseColor = clean(state.noiseColor + p.noiseLp * (noise - state.noiseColor));
            state.noiseDc = clean(state.noiseDc + p.noiseDcLp * (state.noiseColor - state.noiseDc));
            x += (state.noiseColor - state.noiseDc) * (0.075 * amount[2] * amount[2] * noiseGate);

            // Shape changes the transfer range in every mode: softness for
            // rational clipping, asymmetry/range for Tube, and folds for Fold.
            const auto gain = 1.0 + (8.0 + 32.0 * detail[3]) * amount[3] * amount[3];
            const auto nonlinear = state.driveSaturation.tick(x * gain, 0.05 + detail[3] * 0.75, p.modeWeights[3]);
            const auto compensated = nonlinear / (1.0 + 0.45 * std::sqrt(gain - 1.0));
            state.driveLow = clean(state.driveLow + p.driveLp * (compensated - state.driveLow));
            const auto dc = clean(state.driveLow - state.driveDcIn + p.dcCoefficient * state.driveDcOut);
            state.driveDcIn = state.driveLow; state.driveDcOut = dc;
            x += amount[3] * (dc - x);

            // Grit is deliberately placed before Space while its public index
            // is appended to retain every original scene and automation ID.
            const auto gritInput = std::clamp(x, -1.0e6, 1.0e6);
            state.gritPhase -= 1.0;
            if (state.gritPhase <= 0.0) {
                state.gritPhase += 1.0 + detail[5] * detail[5] * 31.0;
                state.gritHeld = gritInput;
            }
            const auto quantize = [](double input, double bits) {
                const auto levels = std::exp2(bits);
                return std::round(input * levels) / levels;
            };
            const auto crushed = quantize(gritInput * 0.65 + state.gritHeld * 0.35, 3.0 + tone[5] * 10.0);
            const auto decimated = quantize(state.gritHeld, 8.0 + tone[5] * 8.0);
            const auto compressed = std::copysign(std::log1p(31.0 * std::abs(state.gritHeld)) / std::log(32.0), state.gritHeld);
            const auto digitalCode = quantize(compressed, 2.0 + tone[5] * 7.0);
            const auto digital = std::copysign(std::expm1(std::min(40.0, std::abs(digitalCode) * std::log(32.0))) / 31.0, digitalCode);
            const auto& gritMode = p.modeWeights[5];
            const auto grit = gritMode[0] * crushed + gritMode[1] * decimated + gritMode[2] * digital;
            x += amount[5] * (grit - x);
            signal[j] = x;
            const auto spaceInput = std::clamp(x, -1.0e6, 1.0e6);
            state.spaceDc = clean(state.spaceDc + p.roomInputHp * (spaceInput - state.spaceDc));
            auto diffusion = spaceInput - state.spaceDc;
            for (auto& allpass : state.diffusion) diffusion = allpass.tick(diffusion);
            diffused[j] = diffusion;
        }
        if (channelCount == 1) diffused[1] = diffused[0];
        std::array<double, 8> taps, matrix;
        for (size_t i = 0; i < 8; ++i) {
            // Room-size and mode changes slew the read heads continuously.
            // This makes a gentle pitch glide instead of jumping between taps.
            p.delaySamples[i] += std::clamp(p.delayTargets[i] - p.delaySamples[i], -0.3, 0.3);
            taps[i] = p.reverb[i].read(p.delaySamples[i]);
            p.reverbLow[i] = clean(p.reverbLow[i] + p.roomDamping * (taps[i] - p.reverbLow[i]));
            matrix[i] = p.reverbLow[i];
        }
        // The normalized Hadamard transform is orthogonal. Each delay's
        // RT60-derived feedback is strictly below one, including Bloom mode.
        for (size_t width = 1; width < 8; width *= 2) {
            for (size_t i = 0; i < 8; i += width * 2) {
                for (size_t j = 0; j < width; ++j) {
                    const auto a = matrix[i + j], b = matrix[i + j + width];
                    matrix[i + j] = a + b; matrix[i + j + width] = a - b;
                }
            }
        }
        constexpr std::array<double, 8> signsL {1, -1, 1, 1, -1, 1, -1, -1};
        constexpr std::array<double, 8> signsR {1, 1, -1, 1, 1, -1, 1, -1};
        double roomL = 0, roomR = 0;
        for (size_t i = 0; i < 8; ++i) {
            const auto injection = (diffused[0] * signsL[i] + diffused[1] * signsR[i]) * 0.18;
            p.reverb[i].write(injection + matrix[i] * invSqrt8 * p.feedback[i]);
            roomL += taps[i] * signsR[i];
            roomR += taps[i] * signsL[i];
        }
        const auto outputGain = std::pow(10.0, p.current.outputDb * 0.05);
        std::array<double, 2> wet {};
        double dryPower = 0, wetPower = 0;
        for (int c = 0; c < channelCount; ++c) {
            const auto j = static_cast<size_t>(c);
            const auto space = (channelCount == 1 ? (roomL + roomR) * 0.5 : (c == 0 ? roomL : roomR)) * invSqrt8;
            wet[j] = signal[j] + amount[4] * space * 0.72;
            dryPower += dry[j] * dry[j]; wetPower += wet[j] * wet[j];
        }
        p.dryEnergy += p.energySmoothing * (dryPower / channelCount - p.dryEnergy);
        p.wetEnergy += p.energySmoothing * (wetPower / channelCount - p.wetEnergy);
        // Linked stereo RMS compensation is bounded and slewed; silence never
        // asks the gain follower to amplify a decaying reverb or noise floor.
        const auto compensation = p.dryEnergy > 1.0e-10 && p.wetEnergy > 1.0e-10
            ? std::clamp(std::sqrt(p.dryEnergy / p.wetEnergy), 0.25, 4.0) : 1.0;
        p.gainCompensation = slide(p.gainCompensation, compensation, p.smoothing);
        const auto wetGain = 1.0 + p.autoGainBlend * (p.gainCompensation - 1.0);
        double outputPeak = 0, scopeSample = 0;
        for (int c = 0; c < channelCount; ++c) {
            const auto j = static_cast<size_t>(c);
            // Both neutral paths take the actual dry sample; there is no
            // conversion through a filter, delayed copy, or crossfade law.
            const auto mixed = p.current.mix == 0.0f ? dry[j] : dry[j] + p.current.mix * (wet[j] * wetGain - dry[j]);
            const auto monitored = (mixed - p.deltaBlend * dry[j]) * outputGain;
            auto output = p.bypassBlend == 1.0 ? dry[j] : monitored + p.bypassBlend * (dry[j] - monitored);
            if (!std::isfinite(output)) output = 0;
            output = std::clamp(output, -static_cast<double>(std::numeric_limits<float>::max()), static_cast<double>(std::numeric_limits<float>::max()));
            buffers[c][sample] = static_cast<float>(output);
            outputPeak = std::max(outputPeak, std::abs(output));
            scopeSample += output;
        }
        p.outputPeak = std::max(outputPeak, p.outputPeak * p.meterRelease);
        const auto analyserIndex = p.analyserWrite.load(std::memory_order_relaxed);
        p.analyser[analyserIndex].store(static_cast<float>(scopeSample / channelCount), std::memory_order_relaxed);
        p.analyserWrite.store((analyserIndex + 1) % Engine::analyserSize, std::memory_order_release);
        if (++p.scopeCounter >= p.scopeInterval) {
            p.scopeCounter = 0;
            const auto index = p.scopeWrite.load(std::memory_order_relaxed);
            p.scope[index].store(static_cast<float>(scopeSample / channelCount), std::memory_order_relaxed);
            p.scopeWrite.store((index + 1) % Engine::scopeSize, std::memory_order_release);
        }
    }
    p.inputMeter.store(static_cast<float>(std::min(p.inputPeak, static_cast<double>(std::numeric_limits<float>::max()))), std::memory_order_relaxed);
    p.outputMeter.store(static_cast<float>(std::min(p.outputPeak, static_cast<double>(std::numeric_limits<float>::max()))), std::memory_order_relaxed);
    p.lfoMeter.store(static_cast<float>(lfo), std::memory_order_relaxed);
}

float Engine::getLfoValue() const noexcept { return impl->lfoMeter.load(std::memory_order_relaxed); }
float Engine::getInputPeak() const noexcept { return impl->inputMeter.load(std::memory_order_relaxed); }
float Engine::getOutputPeak() const noexcept { return impl->outputMeter.load(std::memory_order_relaxed); }
void Engine::copyScope(std::array<float, scopeSize>& destination) const noexcept
{
    const auto start = impl->scopeWrite.load(std::memory_order_acquire);
    for (unsigned i = 0; i < scopeSize; ++i)
        destination[i] = impl->scope[(start + i) % scopeSize].load(std::memory_order_relaxed);
}
void Engine::copyAnalyser(std::array<float, analyserSize>& destination) const noexcept
{
    const auto start = impl->analyserWrite.load(std::memory_order_acquire);
    for (unsigned i = 0; i < analyserSize; ++i)
        destination[i] = impl->analyser[(start + i) % analyserSize].load(std::memory_order_relaxed);
}

}
