#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace patina {
inline const juce::Colour paper { 0xffe9e8e4 }, panel { 0xfff5f4f1 }, ink { 0xff121212 };
inline const juce::Colour muted { 0xff5e5d59 }, rule { 0xffd3d1cb }, screen { 0xff0e0f0f };
inline const juce::Colour light { 0xfff2f2ee }, screenMuted { 0xff858a87 }, screenPanel { 0xff1a1d1c };
inline const std::array<juce::Colour, 6> colours {
    juce::Colour(0xffecbd51), juce::Colour(0xffab92f0), juce::Colour(0xfff7693b),
    juce::Colour(0xff50d492), juce::Colour(0xff58a2f2), juce::Colour(0xffef799d)
};
inline constexpr std::array<int, 6> modules { 2, 1, 3, 5, 4, 0 };
inline constexpr std::array<const char*, 6> names { "DUST", "DRIFT", "HEAT", "GRIT", "SPACE", "TAPE" };
inline constexpr std::array<const char*, 6> descriptions { "noise & texture", "pitch & movement", "saturation", "digital texture", "reverb", "magnetic colour" };
juce::Font font(float size, bool bold = false);
juce::Font mono(float size);
void text(juce::Graphics&, const juce::String&, juce::Rectangle<int>, float,
          juce::Colour = ink, juce::Justification = juce::Justification::centredLeft, bool bold = false);
void monoText(juce::Graphics&, const juce::String&, juce::Rectangle<int>, float,
              juce::Colour = muted, juce::Justification = juce::Justification::centredLeft);
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
void setupKnob(juce::Slider&, const juce::String& tooltip);
void setupLinear(juce::Slider&, juce::Colour, const juce::String& tooltip);
std::unique_ptr<SliderAttachment> attachSlider(AlphaAudioProcessor&, const juce::String&, juce::Slider&);

class MotionView final : public juce::Component {
public:
    explicit MotionView(AlphaAudioProcessor&);
    ~MotionView() override;
    void setTarget(int uiIndex);
    void update();
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> beforeChange;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class PresetView final : public juce::Component {
public:
    explicit PresetView(AlphaAudioProcessor&);
    ~PresetView() override;
    void selectCurrent();
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose, onLoad, onBeforeLoad;
    std::function<void(bool)> onFile;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
