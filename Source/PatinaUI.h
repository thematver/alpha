#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace patina {
inline const juce::Colour paper { 0xffe9e8e4 }, panel { 0xfff5f4f1 }, ink { 0xff121212 };
inline const juce::Colour muted { 0xff5e5d59 }, rule { 0xffd3d1cb }, screen { 0xff0e0f0f };
inline const juce::Colour light { 0xfff2f2ee }, screenMuted { 0xff858a87 }, screenPanel { 0xff1a1d1c };
inline constexpr int engineCount = 12, mainIndex = 12;
inline const std::array<juce::Colour, engineCount> colours {
    juce::Colour(0xffecbd51), juce::Colour(0xffab92f0), juce::Colour(0xfff7693b),
    juce::Colour(0xff50d492), juce::Colour(0xff58a2f2), juce::Colour(0xffef799d),
    juce::Colour(0xffdf9b65), juce::Colour(0xff91bfe5), juce::Colour(0xffb6cc73),
    juce::Colour(0xffd68ac6), juce::Colour(0xff6dc6b5), juce::Colour(0xffb4a3e5)
};
inline constexpr std::array<int, engineCount> modules { 2, 1, 3, 5, 4, 0, 6, 7, 8, 9, 10, 11 };
inline constexpr std::array<const char*, engineCount> names { "DUST", "DRIFT", "HEAT", "GRIT", "SPACE", "TAPE", "PITCH", "TIME", "VOLUME", "FILTER", "PAN", "WIDTH" };
inline constexpr std::array<const char*, engineCount> descriptions { "texture", "pitch & movement", "saturation", "digital texture", "reverb", "magnetic colour", "pitch shifting", "time & delay", "dynamics", "filtering", "stereo position", "stereo width" };
juce::Font font(float size, bool bold = false);
juce::Font mono(float size);
float motionValue(const AlphaAudioProcessor&,int module,const juce::String& suffix);
std::pair<float,float> motionWaveRange(const AlphaAudioProcessor&,int module);
void text(juce::Graphics&, const juce::String&, juce::Rectangle<int>, float,
          juce::Colour = ink, juce::Justification = juce::Justification::centredLeft, bool bold = false);
void monoText(juce::Graphics&, const juce::String&, juce::Rectangle<int>, float,
              juce::Colour = muted, juce::Justification = juce::Justification::centredLeft);
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
class GestureSlider : public juce::Slider {
public:
    void mouseDown(const juce::MouseEvent& event) override {lastPosition=virtualPosition=event.position;juce::Slider::mouseDown(event);}
    void mouseDrag(const juce::MouseEvent& event) override {virtualPosition+=(event.position-lastPosition)*(event.mods.isShiftDown()?.1f:1.0f);lastPosition=event.position;juce::Slider::mouseDrag(event.withNewPosition(virtualPosition));}
    bool keyPressed(const juce::KeyPress& key) override {const bool positive=key==juce::KeyPress::upKey||key==juce::KeyPress::rightKey,negative=key==juce::KeyPress::downKey||key==juce::KeyPress::leftKey;if(positive||negative){if(onDragStart)onDragStart();setValue(getValue()+(positive?1:-1)*(key.getModifiers().isShiftDown()?10:1),juce::sendNotificationSync);if(onDragEnd)onDragEnd();return true;}return juce::Slider::keyPressed(key);}
    void mouseWheelMove(const juce::MouseEvent& event,const juce::MouseWheelDetails& wheel) override {if(!isEnabled())return;const double target=juce::jlimit(getMinimum(),getMaximum(),getValue()+(wheel.deltaY>0?1:-1)*(event.mods.isShiftDown()?.1:1));auto adjusted=wheel;adjusted.deltaX=0;adjusted.deltaY=(float)((valueToProportionOfLength(target)-valueToProportionOfLength(getValue()))/.15);adjusted.isReversed=false;juce::Slider::mouseWheelMove(event,adjusted);}

private:
    juce::Point<float> lastPosition,virtualPosition;
};
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
    void endPreview();
    bool isPreviewing() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose, onLoad, onBeforeLoad, onPreview;
    std::function<void(bool)> onFile;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
