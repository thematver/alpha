#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace alpha {
constexpr int moduleCount = 6;
struct Scene {
    std::array<float, moduleCount> amount { 0.42f, 0.22f, 0.10f, 0.16f, 0.18f, 0.0f };
    std::array<float, moduleCount> tone { 0.45f, 0.35f, 0.42f, 0.45f, 0.36f, 0.5f };
    std::array<float, moduleCount> detail { 0.30f, 0.36f, 0.70f, 0.32f, 0.35f, 0.35f };
};
struct Settings {
    Scene a, b;
    std::array<bool, moduleCount> enabled { true, true, true, true, true, true };
    std::array<int, moduleCount> mode { 0, 0, 0, 0, 0, 0 };
    std::array<float, moduleCount> modulation { 0, 0.28f, 0, 0, 0.12f, 0 };
    float morph = 0, character = 0.70f, mix = 1, outputDb = 0;
    float lfoRateHz = 0.25f, lfoDepth = 0.50f;
    int lfoShape = 0;
    bool bypass = false, delta = false, autoGain = false;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    void prepare(double sampleRate, int maxBlockSize, int channels);
    void reset() noexcept;
    void setSettings(const Settings&) noexcept;
    void process(float* const* channels, int channelCount, int sampleCount) noexcept;
    float getLfoValue() const noexcept;
    float getInputPeak() const noexcept;
    float getOutputPeak() const noexcept;
    static constexpr int scopeSize = 256;
    static constexpr int analyserSize = 2048;
    void copyScope(std::array<float, scopeSize>& destination) const noexcept;
    void copyAnalyser(std::array<float, analyserSize>& destination) const noexcept;
private:
    struct Impl;
    Impl* impl;
};
}
