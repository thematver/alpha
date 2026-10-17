#include "../Source/AlphaDSP.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
using Audio = std::array<std::vector<float>, 2>;
int assertions = 0;
void require(bool condition, const std::string& message)
{
    ++assertions;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
alpha::Settings neutral()
{
    alpha::Settings s;
    s.a.amount.fill(0); s.b.amount.fill(0);
    s.modulation.fill(0); s.character = 1; s.mix = 1;
    return s;
}
Audio input(double sampleRate, double seconds, double amplitude = 0.3)
{
    const auto count = static_cast<size_t>(sampleRate * seconds);
    Audio a {std::vector<float>(count), std::vector<float>(count)};
    for (size_t i = 0; i < count; ++i) {
        const auto t = static_cast<double>(i) / sampleRate;
        for (size_t c = 0; c < 2; ++c)
            a[c][i] = static_cast<float>(amplitude * (0.70 * std::sin(2 * pi * (227 + c * 23) * t)
                + 0.20 * std::sin(2 * pi * 3149 * t) + 0.10 * std::sin(2 * pi * 9217 * t)));
    }
    return a;
}
Audio render(const Audio& original, double sr, const alpha::Settings& s, int blockSize = 257, int channels = 2)
{
    auto output = original;
    alpha::Engine engine;
    engine.setSettings(s);
    engine.prepare(sr, blockSize, channels);
    for (size_t offset = 0; offset < output[0].size(); offset += static_cast<size_t>(blockSize)) {
        float* buffers[] {output[0].data() + offset, output[1].data() + offset};
        engine.process(buffers, channels, static_cast<int>(std::min(static_cast<size_t>(blockSize), output[0].size() - offset)));
    }
    return output;
}
double difference(const Audio& a, const Audio& b)
{
    double sum = 0;
    for (size_t c = 0; c < 2; ++c) for (size_t i = 0; i < a[c].size(); ++i) {
        const auto d = static_cast<double>(a[c][i]) - b[c][i]; sum += d * d;
    }
    return std::sqrt(sum / (2 * a[0].size()));
}
double rms(const std::vector<float>& a, size_t begin, size_t end)
{
    double sum = 0;
    for (auto i = begin; i < end; ++i) sum += static_cast<double>(a[i]) * a[i];
    return std::sqrt(sum / std::max<size_t>(1, end - begin));
}
void finiteBounded(const Audio& a, double ceiling, const std::string& description)
{
    double maximum = 0;
    bool finite = true;
    for (const auto& c : a) for (auto sample : c) {
        finite = finite && std::isfinite(sample);
        maximum = std::max(maximum, std::abs(static_cast<double>(sample)));
    }
    require(finite, description + " is finite");
    require(maximum <= ceiling, description + " stays below numeric ceiling");
}

void neutralAndDry(double sr)
{
    const auto original = input(sr, 0.15);
    auto s = alpha::Settings {};
    s.mix = 0;
    require(render(original, sr, s) == original, "Mix zero is bit-exact dry at " + std::to_string(sr));
    s = neutral(); s.modulation.fill(1); s.lfoDepth = 1;
    require(render(original, sr, s) == original, "Zero module amounts are bit-exact at " + std::to_string(sr));
    s = alpha::Settings {}; s.character = 0;
    require(render(original, sr, s) == original, "Character zero is bit-exact at " + std::to_string(sr));
    s = alpha::Settings {}; s.enabled.fill(false);
    require(render(original, sr, s) == original, "Disabled modules are bit-exact at " + std::to_string(sr));
    s = neutral(); s.outputDb = -6;
    const auto quieter = render(original, sr, s);
    require(std::abs(rms(quieter[0], 0, quieter[0].size()) / rms(original[0], 0, original[0].size()) - std::pow(10.0, -6.0 / 20.0)) < 1.0e-6,
            "Output gain follows dB law");
}

void audibleModulesAndModes(double sr)
{
    const auto original = input(sr, 0.35);
    for (size_t module = 0; module < alpha::moduleCount; ++module) {
        std::array<Audio, 3> modes;
        for (int mode = 0; mode < 3; ++mode) {
            auto s = neutral();
            s.a.amount[module] = s.b.amount[module] = 0.8f;
            s.a.tone[module] = s.b.tone[module] = 0.7f;
            s.a.detail[module] = s.b.detail[module] = 0.65f;
            s.mode[module] = mode;
            modes[static_cast<size_t>(mode)] = render(original, sr, s);
            require(difference(modes[static_cast<size_t>(mode)], original) > 0.0005,
                    "Module " + std::to_string(module) + " mode " + std::to_string(mode) + " alters real audio");
            finiteBounded(modes[static_cast<size_t>(mode)], 4, "Module output");
        }
        require(difference(modes[0], modes[1]) > 1.0e-5, "First two modes sound different for module " + std::to_string(module));
        require(difference(modes[1], modes[2]) > 1.0e-5, "Last two modes sound different for module " + std::to_string(module));
    }
}

void silenceAndMono(double sr)
{
    Audio silence {std::vector<float>(static_cast<size_t>(sr / 5)), std::vector<float>(static_cast<size_t>(sr / 5))};
    auto s = alpha::Settings {}; s.a.amount.fill(1); s.b.amount.fill(1);
    for (int mode = 0; mode < 3; ++mode) {
        s.mode.fill(mode);
        const auto out = render(silence, sr, s);
        require(out == silence, "Silence produces exactly zero including gated noise");
    }
    const auto mono = render(input(sr, 0.1), sr, s, 37, 1);
    finiteBounded(mono, 4, "Mono engine");
}

void morphAndSegmentation()
{
    constexpr double sr = 48000;
    const auto original = input(sr, 0.45);
    auto s = neutral();
    s.b.amount = {0.4f, 0.6f, 0.3f, 0.7f, 0.8f};
    s.b.tone = {0.7f, 0.8f, 0.4f, 0.9f, 0.2f};
    s.b.detail = {0.4f, 0.2f, 0.9f, 0.8f, 0.7f};
    s.morph = 0;
    require(render(original, sr, s) == original, "Morph A is the exact neutral scene");
    s.morph = 1;
    const auto sceneB = render(original, sr, s);
    auto direct = s; direct.a = direct.b; direct.morph = 0;
    require(difference(sceneB, render(original, sr, direct)) < 1.0e-6, "Morph B reaches the actual B scene");
    s.morph = 0.5f;
    require(difference(sceneB, render(original, sr, s)) > 0.005, "Intermediate morph has its own audible result");
    s.modulation = {0.2f, -0.6f, 0.4f, 0.5f, -0.3f}; s.lfoRateHz = 9; s.lfoDepth = 1;
    for (int shape = 0; shape < 4; ++shape) {
        s.lfoShape = shape;
        const auto small = render(original, sr, s, 17);
        require(small == render(original, sr, s, 511), "Output is invariant to block boundaries for LFO shape " + std::to_string(shape));
        require(small == render(original, sr, s, static_cast<int>(original[0].size())), "Whole-buffer processing is identical");
    }
}

void noiseFollowsInputAndReverbDecays()
{
    constexpr double sr = 48000;
    auto original = input(sr, 4.0);
    for (auto& c : original) std::fill(c.begin() + 4800, c.end(), 0.0f);
    auto s = neutral(); s.a.amount[2] = s.b.amount[2] = 1; s.a.detail[2] = s.b.detail[2] = 1;
    const auto noise = render(original, sr, s);
    require(difference(noise, original) > 0.0001, "Noise is audible while input is active");
    require(rms(noise[0], 3 * 48000, noise[0].size()) == 0, "Noise gate returns to digital silence after release");
    Audio impulse {std::vector<float>(10 * 48000), std::vector<float>(10 * 48000)};
    impulse[0][0] = impulse[1][0] = 1;
    s = neutral(); s.a.amount[4] = s.b.amount[4] = 1; s.a.detail[4] = s.b.detail[4] = 0.6f;
    const auto room = render(impulse, sr, s);
    const auto early = rms(room[0], 4800, 48000);
    const auto late = rms(room[0], 9 * 48000, room[0].size());
    require(early > 0.00001, "Reverb has a real tail after the impulse");
    require(late < early * 0.0001, "Feedback network decays rather than accumulating energy");
    require(difference(room, Audio {room[1], room[0]}) > 1.0e-5, "Reverb creates stereo decorrelation from a mono source");
}

void hotInputAutomationResetAndMeters()
{
    constexpr double sr = 96000;
    auto s = alpha::Settings {}; s.a.amount.fill(1); s.b.amount.fill(1); s.character = 1; s.outputDb = 12;
    auto loud = input(sr, 0.35, 32.0);
    finiteBounded(render(loud, sr, s), 100, "Hot full-effect signal");
    alpha::Engine engine; engine.prepare(sr, 127, 2); engine.setSettings(s);
    auto original = input(sr, 0.35);
    auto processed = original;
    for (size_t offset = 0; offset < processed[0].size(); offset += 127) {
        s.morph = static_cast<float>((offset / 127) % 17) / 16.0f;
        s.mode.fill(static_cast<int>((offset / 127) % 3));
        s.enabled[(offset / 127) % 5] = !s.enabled[(offset / 127) % 5];
        s.lfoShape = static_cast<int>((offset / 127) % 4);
        engine.setSettings(s);
        float* channels[] {processed[0].data() + offset, processed[1].data() + offset};
        engine.process(channels, 2, static_cast<int>(std::min<size_t>(127, processed[0].size() - offset)));
    }
    finiteBounded(processed, 10, "Rapid automation");
    require(engine.getInputPeak() > 0.1f && engine.getInputPeak() < 1.0f, "Input meter follows actual input samples");
    require(engine.getOutputPeak() > 0.01f && std::isfinite(engine.getOutputPeak()), "Output meter follows actual processed samples");
    require(std::isfinite(engine.getLfoValue()) && std::abs(engine.getLfoValue()) <= 1.01f, "Modulation meter is finite and normalized");
    std::array<float, alpha::Engine::scopeSize> scope {};
    engine.copyScope(scope);
    require(std::any_of(scope.begin(), scope.end(), [](float v) { return std::abs(v) > 0.001f; }), "Scope contains actual audio");
    for (auto v : scope) require(std::isfinite(v), "Scope is finite");
    engine.reset(); engine.copyScope(scope);
    require(std::all_of(scope.begin(), scope.end(), [](float v) { return v == 0; }), "Reset clears scope");
    require(engine.getInputPeak() == 0 && engine.getOutputPeak() == 0, "Reset clears meters");

    s = neutral(); s.mix = std::numeric_limits<float>::quiet_NaN(); s.character = std::numeric_limits<float>::infinity();
    s.a.amount.fill(std::numeric_limits<float>::quiet_NaN());
    engine.setSettings(s);
    Audio invalid {std::vector<float>(127, std::numeric_limits<float>::quiet_NaN()), std::vector<float>(127, std::numeric_limits<float>::infinity())};
    float* pointers[] {invalid[0].data(), invalid[1].data()};
    engine.process(pointers, 2, 127);
    finiteBounded(invalid, 1, "Nonfinite host data sanitization");

    auto settings = alpha::Settings {};
    engine.setSettings(settings); engine.reset();
    auto first = original; float* a[] {first[0].data(), first[1].data()}; engine.process(a, 2, static_cast<int>(first[0].size()));
    engine.reset(); auto second = original; float* b[] {second[0].data(), second[1].data()}; engine.process(b, 2, static_cast<int>(second[0].size()));
    require(first == second, "Reset restores all random, delay and filter state deterministically");
}

void controlAudibilityAndSettling()
{
    constexpr double sr = 48000;
    const auto original = input(sr, 0.3, 0.6);
    for (int mode = 0; mode < 3; ++mode) {
        auto s = neutral(); s.a.amount[3] = s.b.amount[3] = 0.85f; s.mode[3] = mode;
        s.a.detail[3] = s.b.detail[3] = 0;
        const auto low = render(original, sr, s);
        s.a.detail[3] = s.b.detail[3] = 1;
        require(difference(low, render(original, sr, s)) > 0.002, "Drive Shape is effective in mode " + std::to_string(mode));
    }
    auto s = alpha::Settings {};
    alpha::Engine engine; engine.prepare(sr, 257, 2); engine.setSettings(s);
    auto warmup = original; float* w[] {warmup[0].data(), warmup[1].data()}; engine.process(w, 2, static_cast<int>(warmup[0].size()));
    s.mix = 0; engine.setSettings(s);
    auto settled = input(sr, 0.8); const auto dry = settled;
    float* p[] {settled[0].data(), settled[1].data()}; engine.process(p, 2, static_cast<int>(settled[0].size()));
    for (size_t c = 0; c < 2; ++c)
        require(std::equal(settled[c].end() - 4800, settled[c].end(), dry[c].end() - 4800), "Automated dry mix settles to bit-exact dry");
}

void swapScenesPreservesRenderedSound()
{
    constexpr double sr = 48000;
    const auto source = input(sr, 1.0, 0.5);
    auto reference = source, swapped = source;
    auto target = alpha::Settings {};
    target.a.amount = {0.12f, 0.19f, 0.06f, 0.23f, 0.17f};
    target.b.amount = {0.83f, 0.72f, 0.45f, 0.91f, 0.86f};
    target.a.tone = {0.24f, 0.38f, 0.17f, 0.35f, 0.29f};
    target.b.tone = {0.76f, 0.82f, 0.67f, 0.89f, 0.75f};
    target.a.detail = {0.22f, 0.31f, 0.41f, 0.18f, 0.32f};
    target.b.detail = {0.79f, 0.73f, 0.84f, 0.92f, 0.81f};
    target.morph = 0.23f;
    alpha::Engine referenceEngine, swapEngine;
    referenceEngine.prepare(sr, 256, 2); swapEngine.prepare(sr, 256, 2);
    referenceEngine.setSettings(target); swapEngine.setSettings(target);
    size_t offset = 0;
    const auto processPair = [&](size_t count) {
        const auto end = offset + count;
        while (offset < end) {
            const auto length = static_cast<int>(std::min<size_t>(256, end - offset));
            float* a[] {reference[0].data() + offset, reference[1].data() + offset};
            float* b[] {swapped[0].data() + offset, swapped[1].data() + offset};
            referenceEngine.process(a, 2, length); swapEngine.process(b, 2, length);
            offset += static_cast<size_t>(length);
        }
    };
    processPair(541);
    // Change scenes and morph, then swap while their sound is still moving.
    // A settled-state-only check misses the original independent-smoother bug.
    target.a.amount = {0.37f, 0.64f, 0.29f, 0.81f, 0.73f};
    target.b.amount = {0.95f, 0.08f, 0.77f, 0.15f, 0.34f};
    target.a.tone = {0.91f, 0.16f, 0.72f, 0.88f, 0.84f};
    target.b.detail = {0.13f, 0.89f, 0.25f, 0.52f, 0.47f};
    target.morph = 0.73f;
    referenceEngine.setSettings(target); swapEngine.setSettings(target);
    processPair(73);
    auto renamed = target;
    std::swap(renamed.a, renamed.b); renamed.morph = 1.0f - renamed.morph;
    swapEngine.setSettings(renamed);
    processPair(31);
    swapEngine.setSettings(target);
    processPair(128);
    swapEngine.setSettings(renamed);
    processPair(source[0].size() - offset);
    double maximum = 0;
    for (size_t c = 0; c < 2; ++c) for (size_t i = 0; i < source[c].size(); ++i)
        maximum = std::max(maximum, std::abs(static_cast<double>(reference[c][i]) - swapped[c][i]));
    require(maximum < 0.000005, "Repeated A/B swaps preserve every rendered sample during active scene/morph smoothing");
    require(difference(reference, swapped) < 0.000001, "A/B Swap has no audible transient compared with an unswapped reference");
}


void gritMonitoringAndAnalyser()
{
    constexpr double sr = 48000;
    const auto source = input(sr, 1.2, 0.6);
    auto s = neutral(); s.a.amount[5] = s.b.amount[5] = 0.9f;
    for (int mode = 0; mode < 3; ++mode) {
        s.mode[5] = mode;
        s.a.tone[5] = s.b.tone[5] = 0;
        s.a.detail[5] = s.b.detail[5] = 0.7f;
        const auto lowBits = render(source, sr, s);
        s.a.tone[5] = s.b.tone[5] = 1;
        require(difference(lowBits, render(source, sr, s)) > 0.0001, "Grit Bits changes model " + std::to_string(mode));
        s.a.detail[5] = s.b.detail[5] = 0;
        const auto fullRate = render(source, sr, s);
        s.a.detail[5] = s.b.detail[5] = 1;
        require(difference(fullRate, render(source, sr, s)) > 0.005, "Grit Rate changes model " + std::to_string(mode));
    }
    s = alpha::Settings {}; s.bypass = true; s.delta = true; s.autoGain = true; s.outputDb = 12;
    require(render(source, sr, s) == source, "Global bypass takes precedence and is bit-exact dry including output trim");
    s = neutral(); s.delta = true;
    const auto deltaNull = render(source, sr, s);
    require(std::all_of(deltaNull[0].begin(), deltaNull[0].end(), [](float v) { return v == 0; }), "Delta monitoring nulls a neutral chain");
    s.a.amount[3] = s.b.amount[3] = 0.85f;
    const auto delta = render(source, sr, s);
    s.delta = false;
    const auto wet = render(source, sr, s);
    double reconstructionError = 0;
    for (size_t c = 0; c < 2; ++c) for (size_t i = 0; i < source[c].size(); ++i)
        reconstructionError = std::max(reconstructionError, std::abs(static_cast<double>(delta[c][i]) + source[c][i] - wet[c][i]));
    require(reconstructionError < 0.0000002, "Delta equals processed audio minus original audio");
    const auto dryRms = rms(source[0], source[0].size() / 2, source[0].size());
    const auto wetRms = rms(wet[0], wet[0].size() / 2, wet[0].size());
    s.autoGain = true;
    const auto matched = render(source, sr, s);
    const auto matchedRms = rms(matched[0], matched[0].size() / 2, matched[0].size());
    require(std::abs(std::log(matchedRms / dryRms)) < std::abs(std::log(wetRms / dryRms)), "Auto gain reduces the sustained RMS level difference");
    s.mix = 0;
    require(render(source, sr, s) == source, "Auto gain leaves the dry mix bit-exact");

    alpha::Engine engine; engine.prepare(sr, 256, 2); engine.setSettings(neutral());
    auto analysed = source;
    float* channels[] {analysed[0].data(), analysed[1].data()};
    engine.process(channels, 2, static_cast<int>(analysed[0].size()));
    std::array<float, alpha::Engine::analyserSize> analyser {};
    engine.copyAnalyser(analyser);
    bool fullRate = true;
    for (size_t i = 0; i < analyser.size(); ++i) {
        const auto sample = analysed[0].size() - analyser.size() + i;
        fullRate &= std::abs(analyser[i] - (analysed[0][sample] + analysed[1][sample]) * 0.5f) < 0.0000001f;
    }
    require(fullRate, "Analyser ring contains the latest full-rate mono output samples");
    engine.reset(); engine.copyAnalyser(analyser);
    require(std::all_of(analyser.begin(), analyser.end(), [](float v) { return v == 0; }), "Reset clears the analyser ring");

    s.mix = 1; s.delta = true;
    engine.setSettings(s); engine.reset();
    auto automated = source;
    float* start[] {automated[0].data(), automated[1].data()};
    engine.process(start, 2, 256);
    s.bypass = true; engine.setSettings(s);
    float* tail[] {automated[0].data() + 256, automated[1].data() + 256};
    engine.process(tail, 2, static_cast<int>(automated[0].size()) - 256);
    for (size_t c = 0; c < 2; ++c)
        require(std::equal(automated[c].end() - 4800, automated[c].end(), source[c].end() - 4800), "Automated bypass settles to exact dry");
    finiteBounded(automated, 10, "Monitor automation");
}

void benchmark()
{
    constexpr double sr = 48000, seconds = 4;
    auto audio = input(sr, seconds);
    alpha::Engine engine; engine.prepare(sr, 256, 2); engine.setSettings(alpha::Settings {});
    const auto start = std::chrono::steady_clock::now();
    for (size_t offset = 0; offset < audio[0].size(); offset += 256) {
        float* channels[] {audio[0].data() + offset, audio[1].data() + offset};
        engine.process(channels, 2, static_cast<int>(std::min<size_t>(256, audio[0].size() - offset)));
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "BENCH: 4 s stereo / 48 kHz / 256 samples / six modules in " << elapsed * 1000
              << " ms (" << seconds / elapsed << "x realtime, " << elapsed / seconds * 100 << "% of one core)\n";
}
}

int main()
{
    for (auto sr : {44100.0, 48000.0, 96000.0}) {
        neutralAndDry(sr); silenceAndMono(sr); audibleModulesAndModes(sr);
        std::cout << "PASS: dry, silence, mono, all 18 module modes at " << sr << " Hz\n";
    }
    morphAndSegmentation();
    noiseFollowsInputAndReverbDecays();
    hotInputAutomationResetAndMeters();
    controlAudibilityAndSettling();
    swapScenesPreservesRenderedSound();
    gritMonitoringAndAnalyser();
    std::cout << "PASS: " << assertions << " checks; morph, block invariance, gated noise, stable stereo tail, automation, hot input, state reset, meters and scope\n";
    benchmark();
}
