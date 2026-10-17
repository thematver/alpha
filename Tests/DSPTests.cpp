#include "../Source/AlphaDSP.h"
#include "../Source/AuthenticCoreDSP.h"
#include "../Source/MotionCurve.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <set>
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
        std::array<Audio,6> modes;
        for (int mode = 0; mode < (module==3?6:3); ++mode) {
            auto s = neutral();
            s.a.amount[module] = s.b.amount[module] = 0.8f;
            s.a.tone[module] = s.b.tone[module] = module==6?0.95f:0.7f;
            s.a.detail[module] = s.b.detail[module] = .65f;
            if(module==7){s.lfoRateHz=4;s.a.tone[module]=s.b.tone[module]=.3f;}
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

Audio pureTone(double sr,double seconds,double frequency,double amplitude=.2)
{
    Audio a{std::vector<float>(static_cast<size_t>(sr*seconds)),std::vector<float>(static_cast<size_t>(sr*seconds))};
    for(size_t i=0;i<a[0].size();++i)a[0][i]=a[1][i]=static_cast<float>(amplitude*std::sin(2*pi*frequency*static_cast<double>(i)/sr));
    return a;
}
double differenceAfter(const Audio& a,const Audio& b,size_t begin)
{
    double energy=0;for(size_t c=0;c<2;++c)for(size_t i=begin;i<a[c].size();++i){const auto d=static_cast<double>(a[c][i])-b[c][i];energy+=d*d;}
    return std::sqrt(energy/(2*(a[0].size()-begin)));
}
double spectralBin(const std::vector<float>& x,int bin,size_t start,size_t count)
{
    double real=0,imag=0;for(size_t i=0;i<count;++i){const auto phase=2*pi*bin*static_cast<double>(i)/static_cast<double>(count);real+=x[start+i]*std::cos(phase);imag-=x[start+i]*std::sin(phase);}
    return 2*std::hypot(real,imag)/static_cast<double>(count);
}
double rt60Estimate(const std::vector<float>& x,double sr)
{
    std::vector<double> energy(x.size());double total=0;
    for(size_t i=x.size();i-->1;){total+=static_cast<double>(x[i])*x[i];energy[i]=total;}
    double early=-1,late=-1;
    for(size_t i=1;i<energy.size();++i){const auto db=10*std::log10(std::max(1e-30,energy[i]/std::max(1e-30,total)));if(early<0&&db< -5)early=static_cast<double>(i)/sr;if(db< -35){late=static_cast<double>(i)/sr;break;}}
    return early>=0&&late>early?(late-early)*2:0;
}
double highFrequencyRatio(const std::vector<float>& x,size_t begin,size_t end)
{
    double energy=0,high=0;for(size_t i=begin;i<end;++i){const auto y=static_cast<double>(x[i]),d=y-x[i-1];energy+=y*y;high+=d*d;}return std::sqrt(high/std::max(1e-30,energy));
}
void magneticTapeCharacterization()
{
    alpha::MagneticHysteresis positive,negative;
    for(int i=0;i<200;++i){positive.process(3,.3,.6);negative.process(-3,.3,.6);}
    const auto positiveRemanence=positive.process(0,.3,.6),negativeRemanence=negative.process(0,.3,.6);
    require(positiveRemanence>.01&&negativeRemanence< -.01,"magnetic loop retains polarity-dependent remanence at the same zero field");
    double area=0,previousField=-3;positive.reset();positive.process(-3,.3,.6);
    for(int cycle=0;cycle<2;++cycle)for(int i=1;i<=2000;++i){const auto field=i<=1000?-3+6.0*i/1000:3-6.0*(i-1000)/1000;const auto magnet=positive.process(field,.3,.6);if(cycle==1)area+=magnet*(field-previousField);previousField=field;}
    require(std::abs(area)>.1,"magnetic major loop encloses nonzero hysteresis area");
    auto s=neutral();s.a.amount[0]=s.b.amount[0]=1;s.a.third[0]=s.b.third[0]=0;s.a.fourth[0]=s.b.fourth[0]=0;s.a.detail[0]=s.b.detail[0]=0;
    const auto bright=pureTone(48000,.8,12000,.03);s.a.tone[0]=s.b.tone[0]=0;const auto fresh=render(bright,48000,s);
    s.a.tone[0]=s.b.tone[0]=1;const auto aged=render(bright,48000,s);
    const auto freshHigh=rms(fresh[0],24000,fresh[0].size()),agedHigh=rms(aged[0],24000,aged[0].size());
    require(agedHigh<freshHigh*.65,"tape aging produces measured high-frequency head loss");
    const auto bass=pureTone(48000,.8,95,.01);s.a.tone[0]=s.b.tone[0]=.3f;s.a.detail[0]=s.b.detail[0]=0;const auto flat=render(bass,48000,s);
    s.a.detail[0]=s.b.detail[0]=1;const auto bumped=render(bass,48000,s);
    const auto bumpDb=20*std::log10(rms(bumped[0],24000,bumped[0].size())/rms(flat[0],24000,flat[0].size()));
    require(bumpDb>3&&bumpDb<8,"tape bump yields a bounded physical boost around 95 Hz");
    const auto hot=pureTone(48000,.8,1000,.8);s.a.detail[0]=s.b.detail[0]=.33f;s.a.fourth[0]=s.b.fourth[0]=0;const auto unsquashed=render(hot,48000,s);
    s.a.fourth[0]=s.b.fourth[0]=1;const auto squashed=render(hot,48000,s);
    require(rms(squashed[0],24000,squashed[0].size())<rms(unsquashed[0],24000,unsquashed[0].size())*.85,"tape squash reduces sustained hot-level gain");
    std::cout<<"MEASURE tape: loop area="<<std::abs(area)<<", HF aging="<<20*std::log10(agedHigh/freshHigh)<<" dB, 95Hz bump="<<bumpDb<<" dB\n";
}
void plateAndFocusCharacterization()
{
    constexpr double sr=48000;Audio impulse{std::vector<float>(10*48000),std::vector<float>(10*48000)};impulse[0][0]=impulse[1][0]=1;
    auto s=neutral();s.a.amount[4]=s.b.amount[4]=1;s.a.tone[4]=s.b.tone[4]=.5f;s.a.detail[4]=s.b.detail[4]=.25f;s.a.third[4]=s.b.third[4]=.6f;s.a.fourth[4]=s.b.fourth[4]=0;s.mode[4]=1;
    const auto shortPlate=render(impulse,sr,s);const auto shortRt=rt60Estimate(shortPlate[0],sr);
    s.a.detail[4]=s.b.detail[4]=.7f;const auto longPlate=render(impulse,sr,s);const auto longRt=rt60Estimate(longPlate[0],sr);
    require(shortRt>.08&&longRt>shortRt*2,"Dattorro plate decay control increases measured Schroeder RT60");
    size_t nonzero=0;for(size_t i=2400;i<7200;++i)nonzero+=std::abs(longPlate[0][i])>1e-7;
    const auto density=static_cast<double>(nonzero)/4800;
    require(density>.75,"plate tank builds a dense early reflection field");
    require(difference(longPlate,Audio{longPlate[1],longPlate[0]})>1e-5,"published plate output taps create stereo decorrelation");
    s.a.third[4]=s.b.third[4]=0;const auto damped=render(impulse,sr,s);const auto dampedRatio=highFrequencyRatio(damped[0],9600,96000);
    s.a.third[4]=s.b.third[4]=1;const auto open=render(impulse,sr,s);const auto openRatio=highFrequencyRatio(open[0],9600,96000);
    require(dampedRatio<openRatio*.65,"plate damping produces a faster high-frequency decay");
    s.a.fourth[4]=s.b.fourth[4]=1;const auto delayed=render(impulse,sr,s);double preEnergy=0;for(size_t i=1;i<11500;++i)preEnergy+=std::abs(delayed[0][i]);
    require(preEnergy<1e-12,"250 ms plate predelay has no premature wet output");
    std::cout<<"MEASURE plate: RT60="<<shortRt<<" / "<<longRt<<" s, early density="<<density<<", HF ratios="<<dampedRatio<<" / "<<openRatio<<"\n";
    const auto low=pureTone(sr,.8,100),inside=pureTone(sr,.8,3000);
    for(int module:{0,1,3,4,5}){auto focused=neutral();focused.a.amount[static_cast<size_t>(module)]=focused.b.amount[static_cast<size_t>(module)]=.9f;focused.lowHz[static_cast<size_t>(module)]=2000;focused.highHz[static_cast<size_t>(module)]=6000;
        const auto lowOut=render(low,sr,focused),insideOut=render(inside,sr,focused);
        const auto outsideDifference=differenceAfter(low,lowOut,24000),insideDifference=differenceAfter(inside,insideOut,24000);
        require(insideDifference>outsideDifference*8,"focus confines processed delta for core module "+std::to_string(module));
    }
}
void expandedMotionAndMasterCharacterization()
{
    auto s=neutral();const auto source=input(48000,.3,.3);s.flux=1;s.fluxSeed=12345;
    require(render(source,48000,s)==source,"Flux preserves exact neutral audio when engine amounts are zero");
    s.a.amount[1]=s.b.amount[1]=.5f;
    require(render(source,48000,s,17)==render(source,48000,s,511),"seeded Flux is deterministic across block boundaries");
    s=neutral();s.mainWidth=0;auto anti=pureTone(48000,.2,440);for(size_t i=0;i<anti[0].size();++i)anti[1][i]=-anti[0][i];const auto mono=render(anti,48000,s);
    require(rms(mono[0],0,mono[0].size())==0&&rms(mono[1],0,mono[1].size())==0,"Main Width zero removes pure side content without changing the mid signal");
    alpha::Engine engine;engine.prepare(48000,12000,2);s=neutral();s.motions[0].independent=true;s.motions[0].rateHz=1;s.motions[1].independent=true;s.motions[1].rateHz=.5f;engine.setSettings(s);auto silent=Audio{std::vector<float>(12000),std::vector<float>(12000)};float* channels[]{silent[0].data(),silent[1].data()};engine.process(channels,2,12000);
    require(std::abs(engine.getLfoPhase(0)-.25f)<1e-4&&std::abs(engine.getLfoPhase(1)-.125f)<1e-4,"independent engine motion rates have independent measured phases");
    require(std::abs(engine.getLfoValue(0)-1)<.001&&std::abs(engine.getLfoValue(1)-std::sqrt(.5f))<.001,"per-engine telemetry reports its actual modulation value");
    s.phaseFromHost=.6f;s.motions[0].phaseFromHost=.3f;engine.setSettings(s);engine.process(channels,2,4800);
    require(std::abs(engine.getLfoPhase()-.625f)<1e-4&&std::abs(engine.getLfoPhase(0)-.4f)<1e-4,"host-provided phase anchors shared and independent motion deterministically");
    s=neutral();s.a.amount[3]=s.b.amount[3]=.8f;s.a.fourth[3]=s.b.fourth[3]=.6f;s.modulation[3]=.25f;s.lfoDepth=1;s.motionTarget[3]=4;s.motions[3].independent=true;s.motions[3].shape=7;s.motions[3].curveCount=2;s.motions[3].curve[0]={0,1,0};s.motions[3].curve[1]={1,1,0};
    const auto routed=render(source,48000,s);s.modulation[3]=0;s.a.fourth[3]=s.b.fourth[3]=.85f;
    require(difference(routed,render(source,48000,s))<1e-6,"custom per-engine motion routes the full bipolar amount into the selected fourth character control");
    for(int shape=0;shape<8;++shape){s=neutral();s.motions[2].independent=true;s.motions[2].shape=shape;s.motions[2].curveCount=32;for(int i=0;i<32;++i)s.motions[2].curve[static_cast<size_t>(i)]={static_cast<float>(31-i)/31,std::sin(static_cast<float>(i)),(i%2?1.0f:-1.0f)};engine.reset();engine.setSettings(s);engine.process(channels,2,12000);require(std::isfinite(engine.getLfoValue(2))&&std::abs(engine.getLfoValue(2))<=1.001f,"all eight per-engine shapes remain finite with a sorted 32-node curve");}
}

void nonlinearAndNoiseCharacterization()
{
    constexpr int count=8192,fundamental=997;constexpr double sr=48000;
    const auto frequency=sr*fundamental/count;const auto source=pureTone(sr,static_cast<double>(count*4)/sr,frequency,.8);
    const auto aliasPower=[&](const std::vector<float>& x){double sum=0;std::set<int> bins;for(int harmonic=5;harmonic<=31;++harmonic){auto bin=(fundamental*harmonic)%count;if(bin>count/2)bin=count-bin;if(bins.insert(bin).second){const auto magnitude=spectralBin(x,bin,x.size()-count,count);sum+=magnitude*magnitude;}}const auto fundamentalMagnitude=spectralBin(x,fundamental,x.size()-count,count);return std::sqrt(sum)/std::max(1e-12,fundamentalMagnitude);};
    for(int mode=0;mode<5;++mode){
        auto s=neutral();s.a.amount[3]=s.b.amount[3]=1;s.mode[3]=mode;s.a.tone[3]=s.b.tone[3]=.5f;s.a.detail[3]=s.b.detail[3]=.5f;s.a.third[3]=s.b.third[3]=.5f;s.a.fourth[3]=s.b.fourth[3]=0;
        const auto actual=render(source,sr,s);std::vector<float> naive(source[0].size());double low=0,dcIn=0,dcOut=0;
        const auto transfer=[&](double x){switch(mode){case 1:return std::tanh(x);case 2:return std::sin(x);case 3:{const auto sign=x<0?-1.0:1.0,a=.025,n=x<0?.26:.18,u=std::abs(x);auto voltage=n*std::log1p(u/a);for(int i=0;i<12;++i){const auto e=std::exp(voltage/n);voltage-=(voltage+a*(e-1)-u)/(1+a/n*e);}return sign*voltage;}case 4:return std::clamp(x,-.45,.7);default:return x/std::hypot(1.0,x);}};
        const auto slope=(1.5+.5*6.5)/3,lp=1-std::exp(-2*pi*18000/sr),dc=std::exp(-2*pi*18/sr);
        for(size_t i=0;i<naive.size();++i){const auto nonlinear=transfer(source[0][i]*25*slope)/slope/(1+.45*std::sqrt(24.0));low+=lp*(nonlinear-low);const auto high=low-dcIn+dc*dcOut;dcIn=low;dcOut=high;naive[i]=static_cast<float>(high);}
        const auto actualAliases=aliasPower(actual[0]),naiveAliases=aliasPower(naive),improvement=20*std::log10(naiveAliases/std::max(1e-12,actualAliases));
        std::cout<<"MEASURE heat mode"<<mode<<": alias/fund "<<20*std::log10(actualAliases)<<" dBc, ADAA improvement "<<improvement<<" dB\n";
        require(improvement>15,"antiderivative processing reduces measured folded harmonics for Heat mode "+std::to_string(mode));
    }
    const auto dcSource=pureTone(sr,.8,1000,.5);
    for(int mode=0;mode<6;++mode){auto s=neutral();s.a.amount[3]=s.b.amount[3]=1;s.mode[3]=mode;s.a.detail[3]=s.b.detail[3]=.9f;const auto output=render(dcSource,sr,s);double mean=0;for(size_t i=output[0].size()-4800;i<output[0].size();++i)mean+=output[0][i];mean/=4800;
        require(std::abs(mean)<.00001,"Heat mode "+std::to_string(mode)+" rejects sustained bias-induced DC");
    }
    auto noiseSource=pureTone(sr,1.5,1000,.2);std::array<Audio,3> textures;
    for(int mode=0;mode<3;++mode){auto s=neutral();s.a.amount[2]=s.b.amount[2]=1;s.mode[2]=mode;s.a.third[2]=s.b.third[2]=0;const auto output=render(noiseSource,sr,s);textures[static_cast<size_t>(mode)]=output;for(size_t c=0;c<2;++c)for(size_t i=0;i<output[c].size();++i)textures[static_cast<size_t>(mode)][c][i]-=noiseSource[c][i];}
    const auto crest=[&](const std::vector<float>& x){double peak=0;for(size_t i=24000;i<x.size();++i)peak=std::max(peak,std::abs(static_cast<double>(x[i])));return peak/rms(x,24000,x.size());};
    const auto vinylCrest=crest(textures[0][0]),hissCrest=crest(textures[1][0]);
    require(vinylCrest>hissCrest*1.2,"Vinyl impulse texture is measurably more non-Gaussian than hiss");
    auto s=neutral();s.a.amount[2]=s.b.amount[2]=1;s.mode[2]=1;s.a.fourth[2]=s.b.fourth[2]=0;const auto centered=render(noiseSource,sr,s);
    require(centered[0]==centered[1],"Dust Width zero produces exactly correlated noise for a mono input");
    s.lowHz[2]=2000;s.highHz[2]=6000;const auto focused=render(noiseSource,sr,s);auto delta=focused;
    for(size_t c=0;c<2;++c)for(size_t i=0;i<focused[c].size();++i)delta[c][i]-=noiseSource[c][i];
    double lowBand=0,middleBand=0;for(int i=0;i<20;++i){const auto low=spectralBin(delta[0],10+i,delta[0].size()-8192,8192),mid=spectralBin(delta[0],450+i*4,delta[0].size()-8192,8192);lowBand+=low*low;middleBand+=mid*mid;}
    require(lowBand<middleBand*.01,"Dust focus removes sub-band noise energy while retaining the selected band");
    std::cout<<"MEASURE dust: Vinyl crest="<<vinylCrest<<", Hiss crest="<<hissCrest<<", focused LF rejection="<<10*std::log10(lowBand/middleBand)<<" dB\n";
}

void synchronizedTransportAndCurveCharacterization()
{
    const auto source=pureTone(48000,.2,440);auto s=neutral();s.a.amount[3]=s.b.amount[3]=.6f;s.modulation[3]=.8f;s.lfoDepth=1;
    s.hostPositionValid=true;s.hostPlaying=false;s.hostPpq=10000;s.hostTempoBpm=120;s.phaseFromHost=.375f;
    require(render(source,48000,s,17)==render(source,48000,s,511),"stopped PPQ-synced modulation is invariant to block size");
    alpha::Engine engine;engine.prepare(48000,1024,2);s.motions[1].independent=true;s.motions[1].phaseFromHost=.25f;s.motions[1].rateHz=3;s.motions[2].independent=true;s.motions[2].phaseFromHost=-1;s.motions[2].rateHz=1;
    engine.setSettings(s);std::array<std::vector<float>,2> buffer{std::vector<float>(1024),std::vector<float>(1024)};float* channels[]{buffer[0].data(),buffer[1].data()};engine.process(channels,2,1024);
    require(std::abs(engine.getLfoPhase()-.375f)<1e-6&&std::abs(engine.getLfoPhase(1)-.25f)<1e-6,"shared and independent synced clocks freeze at stopped host phase");
    require(engine.getLfoPhase(2)>.02f,"free-running engine motion continues while the host is stopped");
    engine.reset();s=neutral();s.hostPositionValid=true;s.hostPlaying=true;s.hostTempoBpm=120;s.hostPpq=2000.4;s.motions[0].independent=true;s.motions[0].followInput=true;s.motions[0].rateHz=4;s.motions[0].phaseFromHost=.8f;
    for(auto& channel:buffer)std::fill(channel.begin(),channel.end(),.5f);
    engine.setSettings(s);engine.process(channels,2,1024);const auto triggeredPhase=engine.getLfoPhase(0);
    s.hostPpq+=1024.0*2/48000;s.motions[0].phaseFromHost=static_cast<float>(std::fmod(s.hostPpq*2,1));engine.setSettings(s);engine.process(channels,2,1024);
    require(triggeredPhase<.1f&&std::abs(engine.getLfoPhase(0)-triggeredPhase-1024.0*4/48000)<1e-4,"Follow Input keeps its retrigger offset across subsequent PPQ-synced blocks");
    engine.reset();s=neutral();s.hostPositionValid=true;s.hostPlaying=true;s.phaseFromHost=0;s.lfoRateHz=66.6f;s.motions[0].independent=true;s.motions[0].rateHz=66.6f;s.motions[0].phaseFromHost=0;engine.setSettings(s);engine.process(channels,2,1024);
    const auto fastPhase=std::fmod(66.6*1024/48000,1);
    require(std::abs(engine.getLfoPhase()-fastPhase)<.0001&&std::abs(engine.getLfoPhase(0)-fastPhase)<.0001,"high-BPM synced divisions retain rates above 30 Hz");
    s.lfoRateHz=10;s.phaseFromHost=.25f;engine.setSettings(s);engine.process(channels,2,1024);
    require(std::abs(engine.getLfoPhase()-(.25+1024.0*10/48000))<.0001,"synced tempo changes use the desired clock rate without smoothing-induced phase drift");
    // Host divisions are exact double ratios: reconstructing them from a rounded
    // float rate at a distant PPQ position shifts Repeat into a different slice.
    const auto clockedRepeat=[&](double bpm,double startPpq,bool independent){
        auto audio=input(48000,.6);auto settings=neutral();alpha::Engine clock;clock.prepare(48000,256,2);
        settings.a.amount[7]=settings.b.amount[7]=1;settings.a.tone[7]=settings.b.tone[7]=.2f;settings.mode[7]=2;
        settings.hostPositionValid=settings.hostPlaying=true;settings.hostTempoBpm=bpm;
        const auto ratio=independent?1.0/3:1.0/4;
        settings.lfoRateHz=static_cast<float>(bpm/60*ratio);settings.hostCyclesPerBeat=ratio;
        settings.motions[7].independent=independent;settings.motions[7].rateHz=settings.lfoRateHz;settings.motions[7].hostCyclesPerBeat=ratio;
        for(size_t offset=0;offset<audio[0].size();offset+=256){
            settings.hostPpq=startPpq+static_cast<double>(offset)/48000*(bpm/60);
            const auto phase=settings.hostPpq*ratio;settings.phaseFromHost=static_cast<float>(phase-std::floor(phase));settings.motions[7].phaseFromHost=settings.phaseFromHost;
            clock.setSettings(settings);float* pointers[]{audio[0].data()+offset,audio[1].data()+offset};clock.process(pointers,2,static_cast<int>(std::min<size_t>(256,audio[0].size()-offset)));
        }
        return audio;
    };
    for(const auto bpm:{123.0,999.0})for(const auto independent:{false,true}){
        const auto error=difference(clockedRepeat(bpm,64.375,independent),clockedRepeat(bpm,120000064.375,independent));
        std::cout<<"MEASURE transport: "<<bpm<<" BPM, "<<(independent?"independent":"shared")<<" distant PPQ Repeat error="<<error<<'\n';
        require(error<2e-6,"exact host division keeps "+std::string(independent?"independent":"shared")+" Time Repeat invariant at distant PPQ and "+std::to_string(bpm)+" BPM");
    }
    auto curve=s.motions[0].curve;bool bounded=true;
    for(int i=0;i<=10000;++i){const auto phase=static_cast<double>(i)/10000,value=alpha::evaluateMotionCurve(curve,4,phase);bounded&=std::isfinite(value)&&value>=0&&value<=1;}
    require(bounded,"shared PCHIP curve has no overshoot between the pump nodes");
    const auto epsilon=1e-5;
    require(std::abs(alpha::evaluateMotionCurve(curve,4,epsilon)-curve[0].y)/epsilon<.01&&std::abs(alpha::evaluateMotionCurve(curve,4,1-epsilon)-curve[3].y)/epsilon<.01,"motion curve has flat loop endpoint slopes");
    curve[1].x=curve[0].x;curve[2].curve=1;
    require(std::isfinite(alpha::evaluateMotionCurve(curve,4,.2)),"coincident curve nodes and extreme bend remain finite");
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
    std::cout << "BENCH: 4 s stereo / 48 kHz / 256 samples / 12-engine graph (5 active) in " << elapsed * 1000
              << " ms (" << seconds / elapsed << "x realtime, " << elapsed / seconds * 100 << "% of one core)\n";
}
}

int main()
{
    for (auto sr : {44100.0, 48000.0, 96000.0}) {
        neutralAndDry(sr); silenceAndMono(sr); audibleModulesAndModes(sr);
        std::cout << "PASS: dry, silence, mono, all 39 module modes at " << sr << " Hz\n";
    }
    morphAndSegmentation();
    noiseFollowsInputAndReverbDecays();
    hotInputAutomationResetAndMeters();
    controlAudibilityAndSettling();
    swapScenesPreservesRenderedSound();
    gritMonitoringAndAnalyser();
    magneticTapeCharacterization();
    plateAndFocusCharacterization();
    expandedMotionAndMasterCharacterization();
    nonlinearAndNoiseCharacterization();
    synchronizedTransportAndCurveCharacterization();
    std::cout << "PASS: " << assertions << " checks; morph, block invariance, gated noise, stable stereo tail, automation, hot input, state reset, meters and scope\n";
    benchmark();
}
