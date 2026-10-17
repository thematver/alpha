#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class AlphaAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit AlphaAudioProcessorEditor(AlphaAudioProcessor&);
    ~AlphaAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaAudioProcessorEditor)
};
