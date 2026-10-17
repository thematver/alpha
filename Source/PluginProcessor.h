#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AlphaDSP.h"

class AlphaAudioProcessor final : public juce::AudioProcessor {
public:
    AlphaAudioProcessor();
    ~AlphaAudioProcessor() override = default;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "alpha"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    alpha::Engine engine;
    std::atomic<double> tempo { 120.0 };
    void copyScene(int from, int to);
    void swapScenes();
    void setValue(const juce::String& id, float actualValue);
    float value(const juce::String& id) const;
    bool saveUserPreset(const juce::File&);
    bool loadUserPreset(const juce::File&);
    juce::File userPresetDirectory() const;
    static constexpr std::array<const char*, alpha::moduleCount> moduleIds { "tape", "wobble", "noise", "drive", "space", "grit", "pitch", "time", "volume", "filter", "pan", "width" };
    static constexpr std::array<const char*, alpha::moduleCount> moduleNames { "Tape", "Wobble", "Noise", "Drive", "Space", "Grit", "Pitch", "Time", "Volume", "Filter", "Pan", "Width" };
    static juce::String sceneId(int scene, int module, int parameter = 0);
    static juce::StringArray modeNames(int module);
    int effectiveMode(int module) const noexcept;
    void setModuleMode(int module,int mode);
    static juce::String detailName(int module);
    static juce::String toneName(int module);
    static juce::String thirdName(int module);
    static juce::String fourthName(int module);
    static juce::String motionId(int module,const juce::String& suffix);
    static juce::String parameterValueText(int module,int field,float normalized);
    static float parameterValueFromText(int module,int field,const juce::String& text);
    static juce::String presetDescription(int index);
    static juce::String presetCategory(int index);
    static std::array<float,alpha::moduleCount> presetSignature(int index);
    static float presetMovement(int index);
private:
    std::atomic<uint32_t> parameterEpoch { 0 };
    juce::CriticalSection parameterTransactionLock;
    alpha::Settings coherentSettings;
    struct MotionTransport { bool sync=true; int division=2; };
    std::array<MotionTransport,alpha::moduleCount+1> coherentTransport;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::array<std::array<std::atomic<float>*, alpha::moduleCount * 5>, 2> sceneValues {};
    std::array<std::atomic<float>*, alpha::moduleCount> onValues {}, modeValues {}, modValues {};
    std::atomic<float>* heatModelValue=nullptr;
    std::array<std::atomic<float>*,alpha::moduleCount> lowValues {},highValues {},targetValues {};
    struct MotionPointers {
        std::atomic<float>* independent=nullptr;
        std::atomic<float>* rate=nullptr;std::atomic<float>* sync=nullptr;std::atomic<float>* division=nullptr;
        std::atomic<float>* shape=nullptr;std::atomic<float>* follow=nullptr;std::atomic<float>* count=nullptr;
        std::array<std::array<std::atomic<float>*,3>,alpha::curveCapacity> curve {};
    };
    std::array<MotionPointers,alpha::moduleCount> motionValues;
    std::atomic<float>* fluxValue=nullptr;std::atomic<float>* mainToneValue=nullptr;
    std::atomic<float>* mainWidthValue=nullptr;std::atomic<float>* fluxSeedValue=nullptr;
    std::atomic<float>* morphValue = nullptr;
    std::atomic<float>* characterValue = nullptr;
    std::atomic<float>* mixValue = nullptr;
    std::atomic<float>* outputValue = nullptr;
    std::atomic<float>* rateValue = nullptr;
    std::atomic<float>* syncValue = nullptr;
    std::atomic<float>* divisionValue = nullptr;
    std::atomic<float>* shapeValue = nullptr;
    std::atomic<float>* depthValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* deltaValue = nullptr;
    std::atomic<float>* autoGainValue = nullptr;
    std::atomic<int> currentProgram { 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaAudioProcessor)
};
