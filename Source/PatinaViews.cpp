#include "PatinaUI.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace patina {
namespace {
constexpr std::array<const char*, 7> divisions { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" };
const juce::Colour graphRule { 0xff2a2e2c }, graphMinor { 0xff181b1a };

void darkControl(juce::Component& component) { component.getProperties().set("dark", true); }
void pill(juce::TextButton& button, const juce::String& label) {
    button.setButtonText(label); button.getProperties().set("variant", "pill");
    button.getProperties().set("dark", true); button.setClickingTogglesState(true);
}

float waveAt(int shape, float phase) {
    if (shape == 1) return 1.0f - 4.0f * std::abs(phase - .5f);
    if (shape == 2) return std::tanh(std::sin(juce::MathConstants<float>::twoPi * phase) * 9.0f);
    return std::sin(juce::MathConstants<float>::twoPi * phase);
}

struct ShapeButton final : juce::TextButton {
    explicit ShapeButton(int index) : shape(index) { getProperties().set("variant", "dark"); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto area = getLocalBounds().toFloat();
        g.setColour(getToggleState() ? light : screenPanel.brighter(over || down ? .13f : 0.0f));
        g.fillRoundedRectangle(area, 6);
        g.setColour(getToggleState() ? screen : juce::Colour(0xffc8ccc9));
        const auto r = area.reduced(7, 7);
        juce::Path path;
        if (shape == 3) {
            path.startNewSubPath(r.getX(), r.getBottom());
            path.lineTo(r.getX() + r.getWidth() * .24f, r.getBottom());
            path.lineTo(r.getX() + r.getWidth() * .24f, r.getY() + 2);
            path.lineTo(r.getX() + r.getWidth() * .62f, r.getY() + 2);
            path.lineTo(r.getX() + r.getWidth() * .62f, r.getCentreY() + 2);
            path.lineTo(r.getRight(), r.getCentreY() + 2);
        } else {
            for (int i = 0; i <= 32; ++i) {
                const float phase = (float) i / 32;
                const float x = r.getX() + phase * r.getWidth();
                const float y = r.getCentreY() - waveAt(shape, phase) * r.getHeight() * .45f;
                if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
            }
        }
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
    int shape;
};

// Factory sound signatures follow the processor's scene A values, in UI order.
constexpr std::array<std::array<float, 6>, 10> signatures {{
    {{9,22,16,0,18,42}}, {{0,5,8,0,12,30}}, {{4,52,10,0,20,35}},
    {{2,0,35,0,24,28}}, {{3,10,30,0,22,55}}, {{0,55,8,0,35,20}},
    {{12,15,42,0,8,62}}, {{3,18,12,0,55,28}}, {{22,38,35,0,14,48}},
    {{0,20,5,0,62,15}}
}};
constexpr std::array<float, 10> movements { 14, 1.5f, 29.25f, 3, 4.9f, 39, 9.9f, 28.8f, 25.2f, 33 };

void signature(juce::Graphics& g, juce::Rectangle<float> r, int program) {
    g.setColour(screen); g.fillRoundedRectangle(r, 5);
    const auto& values = signatures[(size_t) juce::jlimit(0, 9, program)];
    for (int i = 0; i < 6; ++i) {
        const float h = juce::jmax(2.0f, values[(size_t) i] * .31f);
        g.setColour(values[(size_t) i] > 0 ? colours[(size_t) i] : graphRule);
        g.fillRoundedRectangle(r.getX() + 5 + (float) i * 6, r.getBottom() - 4 - h, 4, h, 1);
    }
}
}

struct MotionView::Impl final : juce::Component {
    Impl(MotionView& o, AlphaAudioProcessor& p) : owner(o), processor(p) {
        for (int i = 0; i < 4; ++i) {
            shapes[(size_t) i] = std::make_unique<ShapeButton>(i);
            auto& button = *shapes[(size_t) i]; addAndMakeVisible(button);
            const juce::StringArray labels { "Sine", "Triangle", "Square", "Random" };
            button.setTooltip(labels[i] + " modulation shape"); button.setTitle(labels[i]);
            button.onClick = [this, i] {
                if (juce::roundToInt(processor.value("lfoShape")) == i) return;
                if (owner.beforeChange) owner.beforeChange();
                processor.setValue("lfoShape", (float) i); update();
            };
        }
        for (auto* slider : { &rate, &depth, &route }) {
            setupLinear(*slider, colours[2], {}); darkControl(*slider);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 24);
            slider->onDragStart = [this] { if (owner.beforeChange) owner.beforeChange(); };
            addAndMakeVisible(*slider);
        }
        rate.setTooltip("Modulation speed in Hertz when tempo sync is off.");
        rate.setNumDecimalPlacesToDisplay(2);
        depth.setTooltip("Master depth for every modulation route.");
        route.setTooltip("Modulation of the selected engine amount. Negative values invert the shape.");
        rateAttachment = attachSlider(processor, "lfoRate", rate);
        depthAttachment = attachSlider(processor, "lfoDepth", depth);
        pill(sync, "sync"); addAndMakeVisible(sync);
        sync.setTooltip("Synchronise the motion generator to the host tempo.");
        sync.onClick = [this] {
            if (owner.beforeChange) owner.beforeChange();
            processor.setValue("lfoSync", sync.getToggleState() ? 1.0f : 0.0f); update();
        };
        darkControl(division); division.addItemList({ "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" }, 1);
        addAndMakeVisible(division);
        division.onChange = [this] {
            if (division.getSelectedId() <= 0 || juce::roundToInt(processor.value("lfoDivision")) == division.getSelectedId() - 1) return;
            if (owner.beforeChange) owner.beforeChange();
            processor.setValue("lfoDivision", (float) (division.getSelectedId() - 1)); repaint();
        };
        setTarget(2); update();
    }

    juce::String routeId() const {
        return "mod" + juce::String(AlphaAudioProcessor::moduleNames[(size_t) modules[(size_t) target]]);
    }
    void setTarget(int index) {
        const int next = juce::jlimit(0, 5, index);
        if (next == target && routeAttachment != nullptr) return;
        target = next; routeAttachment.reset();
        routeAttachment = attachSlider(processor, routeId(), route);
        route.setColour(juce::Slider::trackColourId, colours[(size_t) target]);
        depth.setColour(juce::Slider::trackColourId, colours[(size_t) target]);
        repaint();
    }
    void update() {
        std::move(history.begin() + 1, history.end(), history.begin());
        history.back() = processor.engine.getLfoValue();
        const bool isSync = processor.value("lfoSync") > .5f;
        sync.setToggleState(isSync, juce::dontSendNotification);
        division.setSelectedId(juce::roundToInt(processor.value("lfoDivision")) + 1, juce::dontSendNotification);
        rate.setEnabled(!isSync); rate.setVisible(!isSync); division.setVisible(isSync);
        const int shape = juce::roundToInt(processor.value("lfoShape"));
        for (int i = 0; i < 4; ++i) shapes[(size_t) i]->setToggleState(i == shape, juce::dontSendNotification);
        repaint();
    }
    float amountBase() const {
        const int module = modules[(size_t) target];
        const float morph = processor.value("morph") * .01f;
        const float a = processor.value(AlphaAudioProcessor::sceneId(0, module));
        const float b = processor.value(AlphaAudioProcessor::sceneId(1, module));
        return a + (b - a) * morph;
    }
    float amountAt(float lfo) const {
        const float amount = amountBase() * (1 + lfo * processor.value(routeId()) * processor.value("lfoDepth") * .0001f);
        const int module = modules[(size_t) target];
        const bool enabled = processor.value(juce::String(AlphaAudioProcessor::moduleIds[(size_t) module]) + "On") > .5f;
        return enabled ? juce::jlimit(0.0f, 100.0f, amount) * processor.value("character") * .01f : 0;
    }
    void paint(juce::Graphics& g) override {
        const auto colour = colours[(size_t) target];
        const auto plot = juce::Rectangle<float>(56, 96, 704, 200);
        const int shape = juce::roundToInt(processor.value("lfoShape"));
        float low = juce::jmin(amountAt(-1), amountAt(1));
        float high = juce::jmax(amountAt(-1), amountAt(1));
        if (high - low < 2) { low = juce::jmax(0.0f, low - 10); high = juce::jmin(100.0f, high + 10); }
        const auto yFor = [=](float amount) { return plot.getBottom() - 15 - (amount - low) / juce::jmax(1.0f, high - low) * 170; };
        for (int i = 1; i < 16; ++i) {
            g.setColour(i % 4 == 0 ? graphRule : graphMinor);
            g.drawVerticalLine(56 + i * 44, 96, 296);
        }
        g.setColour(graphMinor);
        for (int y = 146; y < 296; y += 50) {
            for (int x = 56; x < 760; x += 7) g.fillRect(x, y, 2, 1);
        }
        g.setColour(graphRule); g.drawRect(plot, 1);
        juce::Path curve;
        if (shape == 3) {
            for (size_t i = 0; i < history.size(); ++i) {
                const float x = plot.getX() + (float) i / (float) (history.size() - 1) * plot.getWidth();
                const float y = yFor(amountAt(history[i]));
                if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
            }
        } else {
            for (int i = 0; i <= 512; ++i) {
                const float phase = (float) i / 512;
                const float x = plot.getX() + phase * plot.getWidth();
                const float y = yFor(amountAt(waveAt(shape, phase)));
                if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
            }
        }
        auto fill = curve; fill.lineTo(plot.getRight(), plot.getBottom()); fill.lineTo(plot.getX(), plot.getBottom()); fill.closeSubPath();
        g.setColour(colour.withAlpha(.16f)); g.fillPath(fill);
        g.setColour(colour); g.strokePath(curve, juce::PathStrokeType(2.6f));
        const float live = amountAt(processor.engine.getLfoValue());
        const float liveY = yFor(live);
        g.setColour(light.withAlpha(.45f)); g.drawLine(plot.getX(), liveY, plot.getRight(), liveY, .8f);
        g.setColour(light); g.fillEllipse(plot.getRight() - 4, liveY - 4, 8, 8);
        monoText(g, juce::String(juce::roundToInt(high)) + "%", { 2, 88, 46, 18 }, 10, screenMuted, juce::Justification::centredRight);
        monoText(g, juce::String(juce::roundToInt(low)) + "%", { 2, 286, 46, 18 }, 10, screenMuted, juce::Justification::centredRight);
        monoText(g, "0", { 56, 302, 50, 20 }, 11, screenMuted);
        monoText(g, "1/4", { 207, 302, 50, 20 }, 11, screenMuted, juce::Justification::centred);
        monoText(g, "1/2", { 383, 302, 50, 20 }, 11, screenMuted, juce::Justification::centred);
        monoText(g, "3/4", { 559, 302, 50, 20 }, 11, screenMuted, juce::Justification::centred);
        const bool isSync = processor.value("lfoSync") > .5f;
        const int div = juce::jlimit(0, 6, juce::roundToInt(processor.value("lfoDivision")));
        const juce::String cycle = shape == 3 ? "live history" : "1 cycle = " + (isSync ? juce::String(divisions[(size_t) div]) : juce::String(processor.value("lfoRate"), 2) + " Hz");
        monoText(g, cycle, { 617, 302, 143, 20 }, 10, screenMuted, juce::Justification::centredRight);
        monoText(g, "shape", { 56, 50, 45, 30 }, 11, screenMuted);
        monoText(g, "rate", { 262, 50, 36, 30 }, 11, screenMuted);
        monoText(g, "depth", { 478, 50, 47, 30 }, 11, screenMuted);
        monoText(g, "TARGET", { 800, 91, 230, 19 }, 10, screenMuted);
        text(g, "amount", { 800, 111, 250, 30 }, 22, light, juce::Justification::centredLeft, true);
        monoText(g, juce::String(juce::roundToInt(amountAt(-1))) + juce::String::fromUTF8("%  →  ") + juce::String(juce::roundToInt(amountAt(1))) + "%", { 800, 140, 270, 20 }, 13, light);
        monoText(g, juce::String(names[(size_t) target]) + juce::String::fromUTF8(" · ") + juce::String(juce::roundToInt(live)) + "% live", { 800, 165, 300, 20 }, 11, colour);
        monoText(g, "route", { 800, 193, 48, 26 }, 11, screenMuted);
        monoText(g, juce::String::fromUTF8("negative: invert · positive: follow"), { 800, 225, 328, 18 }, 10, screenMuted);
        monoText(g, "LIVE MODULATION", { 800, 246, 328, 18 }, 10, screenMuted);
        g.setColour(graphRule); g.drawHorizontalLine(268, 800, 1128); g.drawHorizontalLine(306, 800, 1128);
        juce::Path trace;
        for (size_t i = 0; i < history.size(); ++i) {
            const float x = 800 + (float) i / (float) (history.size() - 1) * 328;
            const float y = 287 - history[i] * 16;
            if (i == 0) trace.startNewSubPath(x, y); else trace.lineTo(x, y);
        }
        g.setColour(colour); g.strokePath(trace, juce::PathStrokeType(1.5f));
        monoText(g, "shape and depth move the selected engine amount", { 56, 332, 704, 24 }, 11, screenMuted);
        monoText(g, juce::String(processor.tempo.load(), 1) + juce::String::fromUTF8(" bpm · ") + (isSync ? "host sync" : "free running"), { 800, 332, 328, 24 }, 11, screenMuted);
    }
    void resized() override {
        for (int i = 0; i < 4; ++i) shapes[(size_t) i]->setBounds(105 + i * 35, 51, 29, 28);
        division.setBounds(301, 51, 93, 28); rate.setBounds(297, 51, 112, 28); sync.setBounds(414, 51, 49, 28);
        depth.setBounds(527, 51, 148, 28); route.setBounds(852, 193, 276, 28);
    }
    MotionView& owner; AlphaAudioProcessor& processor;
    int target = 2;
    std::array<std::unique_ptr<ShapeButton>, 4> shapes;
    juce::Slider rate, depth, route; juce::TextButton sync; juce::ComboBox division;
    std::unique_ptr<SliderAttachment> rateAttachment, depthAttachment, routeAttachment;
    std::array<float, 164> history {};
};

MotionView::MotionView(AlphaAudioProcessor& p) : impl(std::make_unique<Impl>(*this, p)) { addAndMakeVisible(*impl); }
MotionView::~MotionView() = default;
void MotionView::setTarget(int target) { impl->setTarget(target); }
void MotionView::update() { impl->update(); }
void MotionView::paint(juce::Graphics&) {}
void MotionView::resized() { impl->setBounds(getLocalBounds()); }

struct PresetView::Impl final : juce::Component {
    struct Card final : juce::Button {
        Card(Impl& o, int p) : juce::Button(o.processor.getProgramName(p)), owner(o), program(p) {
            setTooltip(AlphaAudioProcessor::presetDescription(program));
            onClick = [this] { owner.selected = program; owner.loadSelected(); };
        }
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            const bool focused = owner.selected == program;
            const bool current = owner.processor.getCurrentProgram() == program;
            const bool bright = over || down || focused;
            g.setColour(bright ? light : screenPanel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(.8f), 10);
            if (current) { g.setColour(bright ? ink : light); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 10, 1.5f); }
            signature(g, { 12, 15, 44, 30 }, program);
            patina::text(g, owner.processor.getProgramName(program), { 68, 10, getWidth() - 80, 24 }, 13.5f, bright ? screen : light, juce::Justification::centredLeft, true);
            monoText(g, AlphaAudioProcessor::presetCategory(program).toLowerCase() + (current ? juce::String::fromUTF8(" · current") : ""), { 68, 32, getWidth() - 80, 17 }, 10.5f, bright ? juce::Colour(0xff4a4d4b) : screenMuted);
        }
        void mouseEnter(const juce::MouseEvent& e) override { juce::Button::mouseEnter(e); owner.hovered = program; owner.repaint(); }
        void mouseExit(const juce::MouseEvent& e) override { juce::Button::mouseExit(e); owner.hovered = -1; owner.repaint(); }
        Impl& owner; int program;
    };
    struct Grid final : juce::Component {
        explicit Grid(Impl& o) : owner(o) {}
        void paint(juce::Graphics& g) override {
            if (owner.filtered.empty()) {
                text(g, "No matching sounds", { 24, 80, getWidth() - 48, 30 }, 18, light, juce::Justification::centred, true);
                monoText(g, "Try another name or category", { 24, 116, getWidth() - 48, 24 }, 11, screenMuted, juce::Justification::centred);
            }
        }
        Impl& owner;
    };
    Impl(PresetView& o, AlphaAudioProcessor& p) : owner(o), processor(p), grid(*this) {
        setWantsKeyboardFocus(true);
        search.setTextToShowWhenEmpty("search name, category", screenMuted); search.setFont(mono(11));
        search.setColour(juce::TextEditor::backgroundColourId, screenPanel);
        search.setColour(juce::TextEditor::textColourId, light);
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour(juce::TextEditor::focusedOutlineColourId, screenMuted);
        search.setColour(juce::TextEditor::highlightColourId, colours[4].withAlpha(.35f));
        search.setIndents(10, 6); addAndMakeVisible(search);
        search.onTextChange = [this] { refresh(); };
        search.onReturnKey = [this] { loadSelected(); };
        search.onEscapeKey = [this] { if (owner.onClose) owner.onClose(); };
        categories.add("all");
        for (int i = 0; i < processor.getNumPrograms(); ++i) categories.addIfNotAlreadyThere(AlphaAudioProcessor::presetCategory(i).toLowerCase());
        for (int i = 0; i < categories.size(); ++i) {
            auto button = std::make_unique<juce::TextButton>(); pill(*button, categories[i]);
            button->onClick = [this, i] { category = i; refresh(); grabKeyboardFocus(); };
            addAndMakeVisible(*button); filters.push_back(std::move(button));
        }
        close.setButtonText(juce::String::fromUTF8("×")); close.setTooltip("Close presets (Escape)"); close.getProperties().set("variant", "dark");
        close.onClick = [this] { if (owner.onClose) owner.onClose(); }; addAndMakeVisible(close);
        for (auto* button : { &save, &open }) { button->getProperties().set("variant", "dark"); addAndMakeVisible(*button); }
        save.setButtonText("save"); open.setButtonText("open");
        save.setTooltip("Save the current sound to a preset file."); open.setTooltip("Open a saved preset file.");
        save.onClick = [this] { if (owner.onFile) owner.onFile(true); };
        open.onClick = [this] { if (owner.onFile) owner.onFile(false); };
        for (auto* button : { &keepAmount, &keepMix, &keepOutput }) addAndMakeVisible(*button);
        pill(keepAmount, "amount"); pill(keepMix, "mix"); pill(keepOutput, "output");
        for (auto* button : { &keepAmount, &keepMix, &keepOutput }) button->onClick = [this] { grabKeyboardFocus(); };
        keepAmount.setTooltip("Keep the main amount when loading a sound.");
        keepMix.setTooltip("Keep the wet/dry mix when loading a sound.");
        keepOutput.setTooltip("Keep the output gain when loading a sound.");
        viewport.setViewedComponent(&grid, false); viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(4); viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff3a3f3d));
        addAndMakeVisible(viewport);
        for (int i = 0; i < processor.getNumPrograms(); ++i) {
            auto card = std::make_unique<Card>(*this, i); grid.addAndMakeVisible(*card); cards.push_back(std::move(card));
        }
        selected = processor.getCurrentProgram(); refresh();
    }
    ~Impl() override { viewport.setViewedComponent(nullptr, false); }
    void refresh() {
        filtered.clear(); const auto query = search.getText().trim();
        for (int i = 0; i < processor.getNumPrograms(); ++i) {
            const auto cat = AlphaAudioProcessor::presetCategory(i).toLowerCase();
            const bool categoryMatch = category == 0 || categories[category] == cat;
            const bool queryMatch = query.isEmpty() || processor.getProgramName(i).containsIgnoreCase(query)
                || AlphaAudioProcessor::presetDescription(i).containsIgnoreCase(query) || cat.containsIgnoreCase(query);
            cards[(size_t) i]->setVisible(categoryMatch && queryMatch);
            if (categoryMatch && queryMatch) filtered.push_back(i);
        }
        if (std::find(filtered.begin(), filtered.end(), selected) == filtered.end()) selected = filtered.empty() ? -1 : filtered.front();
        for (size_t i = 0; i < filters.size(); ++i) filters[i]->setToggleState((int) i == category, juce::dontSendNotification);
        layoutGrid(); viewport.setViewPosition(0, 0); grid.repaint(); repaint();
    }
    void selectCurrent() {
        selected = processor.getCurrentProgram();
        if (std::find(filtered.begin(), filtered.end(), selected) == filtered.end()) {
            category = 0; search.setText({}, false); refresh();
        }
        revealSelected(); grid.repaint(); repaint();
        if (isShowing()) grabKeyboardFocus();
    }
    void revealSelected() {
        const auto it = std::find(filtered.begin(), filtered.end(), selected);
        if (it == filtered.end()) return;
        const int top = ((int) std::distance(filtered.begin(), it) / 2) * 69;
        if (top < viewport.getViewPositionY()) viewport.setViewPosition(0, top);
        if (top + 60 > viewport.getViewPositionY() + viewport.getHeight()) viewport.setViewPosition(0, top + 60 - viewport.getHeight());
    }
    void loadSelected() {
        if (!juce::isPositiveAndBelow(selected, processor.getNumPrograms())) return;
        const float amount = processor.value("character"), mix = processor.value("mix"), output = processor.value("output");
        if (owner.onBeforeLoad) owner.onBeforeLoad();
        processor.setCurrentProgram(selected);
        if (keepAmount.getToggleState()) processor.setValue("character", amount);
        if (keepMix.getToggleState()) processor.setValue("mix", mix);
        if (keepOutput.getToggleState()) processor.setValue("output", output);
        grid.repaint(); repaint();
        if (owner.onLoad) owner.onLoad();
    }
    juce::Point<float> pointFor(int program) const {
        const auto& values = signatures[(size_t) juce::jlimit(0, 9, program)];
        float intensity = 0; for (float value : values) intensity += value;
        return { 48 + juce::jlimit(0.0f, 1.0f, intensity / 190) * 310,
                 326 - juce::jlimit(0.0f, 1.0f, movements[(size_t) program] / 42) * 230 };
    }
    void paint(juce::Graphics& g) override {
        text(g, "presets", { 16, 12, 74, 32 }, 16, light, juce::Justification::centredLeft, true);
        monoText(g, juce::String(processor.getNumPrograms()) + " factory", { 100, 12, 86, 32 }, 11, screenMuted);
        const auto map = juce::Rectangle<float>(16, 56, 376, 304);
        g.setColour(juce::Colour(0xff121413)); g.fillRoundedRectangle(map, 10);
        g.setColour(juce::Colour(0xff232726)); g.drawRoundedRectangle(map.reduced(.5f), 10, 1);
        g.drawVerticalLine(204, 72, 344); g.drawHorizontalLine(208, 40, 368);
        monoText(g, juce::String::fromUTF8("↑ moving"), { 40, 69, 145, 20 }, 10, screenMuted);
        monoText(g, "subtle", { 40, 336, 100, 18 }, 10, screenMuted);
        monoText(g, juce::String::fromUTF8("heavy →"), { 270, 336, 98, 18 }, 10, screenMuted, juce::Justification::centredRight);
        for (int program : filtered) {
            const auto point = pointFor(program);
            const int colour = AlphaAudioProcessor::presetCategory(program) == "Moving" ? 1
                : AlphaAudioProcessor::presetCategory(program) == "Spacious" ? 4
                : AlphaAudioProcessor::presetCategory(program) == "Broken" ? 3 : 2;
            if (program == processor.getCurrentProgram()) { g.setColour(light); g.drawEllipse(point.x - 12, point.y - 12, 24, 24, 1.5f); }
            if (program == selected || program == hovered) { g.setColour(colours[(size_t) colour]); g.drawEllipse(point.x - 15, point.y - 15, 30, 30, 1); }
            g.setColour(juce::Colour(0xff121413)); g.fillEllipse(point.x - 8, point.y - 8, 16, 16);
            g.setColour(colours[(size_t) colour]); g.fillEllipse(point.x - 6, point.y - 6, 12, 12);
        }
        const int labelProgram = hovered >= 0 ? hovered : selected;
        if (labelProgram >= 0) {
            const auto point = pointFor(labelProgram);
            const int labelX = point.x > 230 ? (int) point.x - 162 : (int) point.x + 17;
            const int labelY = juce::jlimit(90, 316, (int) point.y - 22);
            g.setColour(juce::Colour(0xff121413).withAlpha(.9f)); g.fillRoundedRectangle((float) labelX - 4, (float) labelY, 160, 20, 3);
            monoText(g, processor.getProgramName(labelProgram), { labelX, labelY, 152, 20 }, 10.5f, light);
        }
        monoText(g, "keep while browsing", { 408, 336, 135, 26 }, 10.5f, screenMuted);
        monoText(g, juce::String::fromUTF8("click / enter: load · arrows: browse · esc: back"), { 784, 336, 352, 26 }, 10, screenMuted, juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& event) override {
        grabKeyboardFocus();
        if (!juce::Rectangle<int>(16, 56, 376, 304).contains(event.getPosition())) return;
        int closest = -1; float distance = 18;
        for (int program : filtered) {
            const float d = pointFor(program).getDistanceFrom(event.position);
            if (d < distance) { closest = program; distance = d; }
        }
        if (closest >= 0) { selected = closest; loadSelected(); }
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) { if (owner.onClose) owner.onClose(); return true; }
        if (key == juce::KeyPress::returnKey) { loadSelected(); return true; }
        int step = 0;
        if (key == juce::KeyPress::leftKey) step = -1;
        if (key == juce::KeyPress::rightKey) step = 1;
        if (key == juce::KeyPress::upKey) step = -2;
        if (key == juce::KeyPress::downKey) step = 2;
        if (step == 0 || filtered.empty()) return false;
        auto it = std::find(filtered.begin(), filtered.end(), selected);
        const int index = it == filtered.end() ? 0 : (int) std::distance(filtered.begin(), it);
        const int next = juce::jlimit(0, (int) filtered.size() - 1, index + step); selected = filtered[(size_t) next];
        grabKeyboardFocus();
        revealSelected();
        grid.repaint(); repaint(); return true;
    }
    void layoutGrid() {
        const int width = juce::jmax(100, viewport.getWidth() - 8);
        grid.setSize(width, juce::jmax(268, ((int) filtered.size() + 1) / 2 * 69 - 9));
        const int cardWidth = (width - 12) / 2;
        for (size_t i = 0; i < filtered.size(); ++i) cards[(size_t) filtered[i]]->setBounds((int) (i % 2) * (cardWidth + 12), (int) (i / 2) * 69, cardWidth, 60);
    }
    void resized() override {
        search.setBounds(194, 14, 230, 28);
        int x = 438;
        for (size_t i = 0; i < filters.size(); ++i) {
            const int width = 23 + (int) categories[(int) i].length() * 7;
            filters[i]->setBounds(x, 15, width, 26); x += width + 4;
        }
        save.setBounds(982, 14, 48, 28); open.setBounds(1036, 14, 48, 28); close.setBounds(1108, 14, 28, 28);
        viewport.setBounds(408, 56, 728, 268); layoutGrid();
        keepAmount.setBounds(546, 337, 63, 24); keepMix.setBounds(615, 337, 44, 24); keepOutput.setBounds(665, 337, 62, 24);
    }
    PresetView& owner; AlphaAudioProcessor& processor;
    juce::TextEditor search; juce::StringArray categories;
    std::vector<std::unique_ptr<juce::TextButton>> filters;
    juce::TextButton close, save, open, keepAmount, keepMix, keepOutput;
    juce::Viewport viewport; Grid grid;
    std::vector<std::unique_ptr<Card>> cards;
    std::vector<int> filtered;
    int category = 0, selected = -1, hovered = -1;
};

PresetView::PresetView(AlphaAudioProcessor& p) : impl(std::make_unique<Impl>(*this, p)) { addAndMakeVisible(*impl); }
PresetView::~PresetView() = default;
void PresetView::selectCurrent() { impl->selectCurrent(); }
void PresetView::paint(juce::Graphics&) {}
void PresetView::resized() { impl->setBounds(getLocalBounds()); }
}
