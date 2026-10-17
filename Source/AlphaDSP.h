#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace alpha {
constexpr int moduleCount = 12;
constexpr int curveCapacity = 32;
struct CurvePoint { float x = 0, y = 0, curve = 0; };
struct MotionSettings {
    bool independent=false,followInput=false;
    float rateHz=.25f,phaseFromHost=-1;
    double hostCyclesPerBeat=0;
    int shape=0,curveCount=4;
    std::array<CurvePoint,curveCapacity> curve {{{0,0,0},{.12f,1,0},{.45f,.6f,0},{1,0,0}}};
};
template <typename T, std::size_t N> constexpr std::array<T, N> filled(T value) {
    std::array<T, N> result {}; result.fill(value); return result;
}
struct Scene {
    std::array<float, moduleCount> amount { .42f, .22f, .10f, .16f, .18f, 0, 0, 0, 0, 0, 0, 0 };
    std::array<float, moduleCount> tone { .50f, .32f, .50f, .50f, .50f, .57f, .5f, .5f, .5f, .8f, .5f, .5f };
    std::array<float, moduleCount> detail { .33f, .25f, .50f, .50f, .40f, .37f, .5f, .5f, 0, .15f, .5f, 0 };
    std::array<float, moduleCount> third { .167f, .20f, .40f, .40f, .60f, .10f, .5f, .5f, .1f, 0, 0, 0 };
    std::array<float, moduleCount> fourth { .167f, .40f, .70f, 0, .20f, .30f, 0, 0, .3f, 0, .3f, .1f };
};
struct Settings {
    Scene a, b;
    std::array<bool, moduleCount> enabled = filled<bool, moduleCount>(true);
    std::array<int, moduleCount> mode {};
    std::array<float, moduleCount> modulation { 0, 0.28f, 0, 0, 0.12f, 0 };
    std::array<int, moduleCount> motionTarget {};
    std::array<float, moduleCount> lowHz = filled<float, moduleCount>(20);
    std::array<float, moduleCount> highHz = filled<float, moduleCount>(20000);
    float morph = 0, character = 0.70f, mix = 1, outputDb = 0;
    float flux = 0, mainTone = .5f, mainWidth = .5f;
    uint32_t fluxSeed=79581;
    float lfoRateHz = 0.25f, lfoDepth = 0.50f;
    int lfoShape = 0;
    std::array<CurvePoint, curveCapacity> customCurve {{{0,-.7f,0},{.12f,1,0},{.45f,.2f,0},{1,-.7f,0}}};
    int curveCount = 4;
    std::array<MotionSettings,moduleCount> motions;
    float phaseFromHost=-1;
    double hostPpq=0,hostTempoBpm=120,hostCyclesPerBeat=0;
    bool hostPlaying=false,hostPositionValid=false;
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
    float getLfoValue(int module=-1) const noexcept;
    float getLfoPhase(int module=-1) const noexcept;
    float getInputPeak() const noexcept;
    float getOutputPeak() const noexcept;
    static constexpr int scopeSize = 256;
    static constexpr int analyserSize = 4096;
    void copyScope(std::array<float, scopeSize>& destination) const noexcept;
    void copyAnalyser(std::array<float, analyserSize>& destination) const noexcept;
private:
    struct Impl;
    Impl* impl;
};
}
