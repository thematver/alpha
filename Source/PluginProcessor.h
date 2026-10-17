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
    double getTailLengthSeconds() const override { return 15.0; }
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
    static constexpr std::array<const char*, alpha::moduleCount> moduleIds { "tape", "wobble", "noise", "drive", "space", "grit" };
    static constexpr std::array<const char*, alpha::moduleCount> moduleNames { "Tape", "Wobble", "Noise", "Drive", "Space", "Grit" };
    static juce::String sceneId(int scene, int module, int parameter = 0);
    static juce::StringArray modeNames(int module);
    static juce::String detailName(int module);
    static juce::String toneName(int module);
    static juce::String presetDescription(int index);
    static juce::String presetCategory(int index);
private:
    std::atomic<uint32_t> parameterEpoch { 0 };
    juce::CriticalSection parameterTransactionLock;
    alpha::Settings coherentSettings;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::array<std::array<std::atomic<float>*, alpha::moduleCount * 3>, 2> sceneValues {};
    std::array<std::atomic<float>*, alpha::moduleCount> onValues {}, modeValues {}, modValues {};
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
