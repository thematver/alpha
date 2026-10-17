#include "PluginEditor.h"
#include "PatinaUI.h"
#include "BinaryData.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <deque>

namespace patina {
juce::Font font(float size, bool bold) {
    static const auto regular = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoRegular_ttf, BinaryData::ArchivoRegular_ttfSize);
    static const auto heavy = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoBold_ttf, BinaryData::ArchivoBold_ttfSize);
    return juce::Font(juce::FontOptions(bold ? heavy : regular).withHeight(size));
}
juce::Font mono(float size) {
    static const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::GeistMonoRegular_ttf, BinaryData::GeistMonoRegular_ttfSize);
    return juce::Font(juce::FontOptions(face).withHeight(size));
}
void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float size,
          juce::Colour c, juce::Justification align, bool bold) {
    g.setColour(c); g.setFont(font(size, bold)); g.drawText(s, r, align, false);
}
void monoText(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float size,
              juce::Colour c, juce::Justification align) {
    g.setColour(c); g.setFont(mono(size)); g.drawText(s, r, align, false);
}
void setupKnob(juce::Slider& s, const juce::String& tooltip) {
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 22);
    s.setMouseDragSensitivity(200); s.setNumDecimalPlacesToDisplay(0);
    s.setVelocityModeParameters(.15,1,0.0,true,juce::ModifierKeys::shiftModifier);
    s.setTooltip(tooltip + " Double-click to reset. Shift-drag for fine adjustment. Click the value to type.");
}
void setupLinear(juce::Slider& s, juce::Colour c, const juce::String& tooltip) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 24);
    s.setColour(juce::Slider::trackColourId, c); s.setNumDecimalPlacesToDisplay(0);
    s.setTooltip(tooltip + " Double-click to reset.");
}
std::unique_ptr<SliderAttachment> attachSlider(AlphaAudioProcessor& p, const juce::String& id, juce::Slider& s) {
    if (auto* param = p.parameters.getParameter(id)) {
        s.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
        auto attachment = std::make_unique<SliderAttachment>(p.parameters, id, s);
        s.textFromValueFunction = [id](double v) {
            if (id == "output") return juce::String(std::abs(v) < .005 ? 0.0 : v, 1) + " dB";
            if (id == "lfoRate") return juce::String(v, 2) + " Hz";
            return juce::String(juce::roundToInt(v)) + (id.startsWith("mod") && v > 0 ? " %" : "%");
        };
        s.valueFromTextFunction = [](const juce::String& value) { return value.retainCharacters("-0123456789.").getDoubleValue(); };
        s.setTextValueSuffix({}); s.updateText(); return attachment;
    }
    return {};
}
}

namespace {
using namespace patina;
constexpr int designWidth = 1200, designHeight = 800;

void stroke(juce::Graphics& g, const juce::Path& p, float width = 1.6f) {
    g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
void drawIcon(juce::Graphics& g, const juce::String& kind, juce::Rectangle<float> r, juce::Colour c) {
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::scale(r.getWidth()/16, r.getHeight()/16).translated(r.getX(),r.getY()));
    g.setColour(c); juce::Path p;
    if (kind == "prev" || kind == "next") {
        const bool prev = kind == "prev"; p.startNewSubPath(prev?10:6,3.5f);p.lineTo(prev?5.5f:10.5f,8);p.lineTo(prev?10:6,12.5f);stroke(g,p);
    } else if (kind == "roll") {
        g.drawRoundedRectangle(2.5f,2.5f,11,11,2.6f,1.5f);
        for(auto pt: {juce::Point<float>(5.5f,5.5f),{10.5f,5.5f},{8,8},{5.5f,10.5f},{10.5f,10.5f}})g.fillEllipse(pt.x-1,pt.y-1,2,2);
    } else if (kind == "undo" || kind == "redo") {
        if(kind=="redo")g.addTransform(juce::AffineTransform(-1,0,16,0,1,0));
        p.startNewSubPath(6,4);p.lineTo(3,7);p.lineTo(6,10);p.startNewSubPath(3.5f,7);p.lineTo(10,7);p.cubicTo(15,7,15,14,10,14);p.lineTo(7,14);stroke(g,p);
    } else if (kind == "settings") {
        for(int i=0;i<3;++i){float y=4.5f+3.5f*(float)i;g.drawLine(2.5f,y,13.5f,y,1.5f);float x=i==0?5.5f:i==1?10.5f:7;g.setColour(paper);g.fillEllipse(x-1.6f,y-1.6f,3.2f,3.2f);g.setColour(c);g.drawEllipse(x-1.6f,y-1.6f,3.2f,3.2f,1.5f);}
    } else if (kind == "power") {
        p.startNewSubPath(8,2);p.lineTo(8,7.5f);stroke(g,p);p.clear();p.addCentredArc(8,8.5f,5,5,0,.7f,juce::MathConstants<float>::twoPi-.7f,true);stroke(g,p);
    } else if(kind=="lock") {
        g.drawRoundedRectangle(3.5f,7.5f,9,6.5f,1.5f,1.4f);p.addCentredArc(8,5.5f,2.5f,2.5f,0,-juce::MathConstants<float>::halfPi,juce::MathConstants<float>::halfPi,true);p.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi,8,5.5f));stroke(g,p,1.4f);
    } else if(kind=="chevron") {p.startNewSubPath(3,6);p.lineTo(8,11);p.lineTo(13,6);stroke(g,p);}
}

class PatinaLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    PatinaLookAndFeel() {
        setColour(juce::PopupMenu::backgroundColourId,panel);setColour(juce::PopupMenu::textColourId,ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId,rule);setColour(juce::PopupMenu::highlightedTextColourId,ink);
        setColour(juce::TextEditor::backgroundColourId,panel);setColour(juce::TextEditor::textColourId,ink);
        setColour(juce::TextEditor::outlineColourId,rule);setColour(juce::TextEditor::focusedOutlineColourId,ink);
        setColour(juce::TextEditor::highlightColourId,rule);setColour(juce::ScrollBar::thumbColourId,juce::Colour(0xff3a3f3d));
        setColour(juce::TooltipWindow::backgroundColourId,ink);setColour(juce::TooltipWindow::textColourId,panel);setColour(juce::TooltipWindow::outlineColourId,ink);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider& s) override {
        const bool master=(bool)s.getProperties().getWithDefault("master",false);
        const bool dark=(bool)s.getProperties().getWithDefault("dark",false);
        const auto fg=dark?light:ink;
        auto bounds=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h);
        const float diameter=juce::jmin(bounds.getWidth(),bounds.getHeight())-10;
        auto circle=juce::Rectangle<float>(diameter,diameter).withCentre(bounds.getCentre());
        const auto centre=circle.getCentre();const float radius=diameter*.5f;
        const float angle=start+pos*(end-start);const float weight=master?6.0f:diameter<52?3.0f:4.0f;
        juce::Path arc;arc.addCentredArc(centre.x,centre.y,radius,radius,0,start,end,true);
        g.setColour(dark?juce::Colour(0xff2a2e2c):rule);stroke(g,arc,weight);
        juce::Path value;const float zero=s.getMinimum()<0?start+(end-start)*.5f:start;
        value.addCentredArc(centre.x,centre.y,radius,radius,0,zero,angle,true);g.setColour(fg.withAlpha(s.isEnabled()?1.0f:.35f));stroke(g,value,weight);
        auto cap=circle.reduced(master?12.0f:diameter*.12f);
        g.setColour(master?ink:dark?screenPanel:juce::Colours::white);g.fillEllipse(cap);
        if(!master){g.setColour(fg);g.drawEllipse(cap,1.5f);}
        const auto u=juce::Point<float>(std::sin(angle),-std::cos(angle));const float r=cap.getWidth()*.5f;
        g.setColour(master?panel:fg);
        if(master){const auto dot=centre+u*r*.82f;g.fillEllipse(dot.x-4,dot.y-4,8,8);text(g,juce::String(juce::roundToInt(s.getValue())),cap.toNearestInt(),30,panel,juce::Justification::centred,true);}
        else g.drawLine({centre+u*r*.4f,centre+u*r*.82f},diameter<52?2.5f:3.0f);
        if(s.isMouseOverOrDragging()){g.setColour(fg.withAlpha(.18f));g.drawEllipse(circle.expanded(5),1);}
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,float sliderPos,float,float,juce::Slider::SliderStyle,juce::Slider& s) override {
        const bool bar=(bool)s.getProperties().getWithDefault("bar",false);
        const bool dark=(bool)s.getProperties().getWithDefault("dark",false);
        const auto c=s.findColour(juce::Slider::trackColourId);
        const auto r=juce::Rectangle<float>((float)x,(float)y,(float)width,(float)height);
        if(bar){
            g.setColour(juce::Colour(0xffe3e1dc));g.fillRoundedRectangle(r,6);
            if(s.isEnabled()){g.setColour(c);const float proportion=(float)s.valueToProportionOfLength(s.getValue());juce::Path clip;clip.addRoundedRectangle(r,6);juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(clip);g.fillRect(r.withWidth(r.getWidth()*proportion));}
            else {g.setColour(rule);for(float xx=r.getX();xx<r.getRight();xx+=9)g.drawLine(xx,r.getCentreY(),juce::jmin(xx+5,r.getRight()),r.getCentreY(),2);}
            return;
        }
        const float cy=r.getCentreY();g.setColour(dark?juce::Colour(0xff3a3f3d):rule);g.fillRoundedRectangle(r.getX(),cy-2,r.getWidth(),4,2);
        float zero=s.getMinimum()<0?r.getCentreX():r.getX();g.setColour(c);g.fillRoundedRectangle(juce::jmin(zero,sliderPos),cy-2,std::abs(sliderPos-zero),4,2);
        g.setColour(dark?light:ink);g.fillEllipse(sliderPos-5,cy-5,10,10);
    }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override {
        if((bool)s.getProperties().getWithDefault("bar",false))return { s.getLocalBounds(),{} };
        return LookAndFeel_V4::getSliderLayout(s);
    }
    juce::Label* createSliderTextBox(juce::Slider& s) override {
        auto* label=LookAndFeel_V4::createSliderTextBox(s);label->setFont(mono(12));label->setJustificationType(juce::Justification::centred);
        const auto c=(bool)s.getProperties().getWithDefault("dark",false)?light:ink;
        label->setColour(juce::Label::textColourId,c);label->setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);label->setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);
        label->setColour(juce::TextEditor::textColourId,c);label->setColour(juce::TextEditor::backgroundColourId,(bool)s.getProperties().getWithDefault("dark",false)?screenPanel:panel);label->setColour(juce::TextEditor::outlineColourId,rule);
        return label;
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down) override {
        auto r=b.getLocalBounds().toFloat().reduced(.75f);auto variant=b.getProperties().getWithDefault("variant","").toString();
        if(variant=="tile"||variant=="dot"||variant=="preset")return;
        if(variant=="nav") {if(b.getToggleState()||hover){g.setColour(b.getToggleState()?patina::colours[2]:screenMuted);g.fillRect(0,b.getHeight()-2,b.getWidth(),2);}return;}
        if(variant=="mode"||variant=="scene"){if(b.getToggleState()||hover){g.setColour(b.getToggleState()?ink:rule);g.fillRoundedRectangle(r,6);}return;}
        const bool dark=variant=="dark"||variant=="pill";
        if(variant=="plain"||variant=="icon") {if(hover||down||b.getToggleState()){g.setColour(b.getToggleState()?rule:rule.withAlpha(.55f));g.fillRoundedRectangle(r,8);}return;}
        g.setColour(b.getToggleState()?(dark?light:ink):dark?screenPanel:down?rule:hover?juce::Colours::white:panel);g.fillRoundedRectangle(r,variant=="pill"?13.0f:7.0f);
        if(!dark){g.setColour(ink);g.drawRoundedRectangle(r,7,1);}
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool hover,bool) override {
        auto variant=b.getProperties().getWithDefault("variant","").toString();
        if(variant=="tile"||variant=="preset")return;
        if(variant=="dot") {auto r=b.getLocalBounds().toFloat().withSizeKeepingCentre(10,10);g.setColour(b.getToggleState()?b.findColour(juce::TextButton::buttonColourId):panel);g.fillEllipse(r);g.setColour(ink);g.drawEllipse(r,1.5f);if(hover)g.drawEllipse(r.expanded(3),.8f);return;}
        const bool dark=variant=="dark"||variant=="pill";
        auto c=variant=="nav"?(b.getToggleState()?light:screenMuted):dark?(b.getToggleState()?screen:light):b.getToggleState()&&variant!="icon"&&variant!="plain"?panel:ink;
        if(!b.isEnabled())c=c.withAlpha(.3f);
        if(variant=="roll"){drawIcon(g,"roll",{10,(float)b.getHeight()*.5f-8,16,16},c);text(g,"roll",{31,0,36,b.getHeight()},13,c,juce::Justification::centred,true);monoText(g,juce::String::fromUTF8("±25"),{69,0,36,b.getHeight()},11,muted,juce::Justification::centred);return;}
        auto icon=b.getProperties().getWithDefault("icon","").toString();
        if(icon.isNotEmpty()){drawIcon(g,icon,{(float)b.getWidth()*.5f-8,(float)b.getHeight()*.5f-8,16,16},c);return;}
        if((bool)b.getProperties().getWithDefault("mono",false)||variant=="nav"||dark)monoText(g,b.getButtonText(),b.getLocalBounds().reduced(4,0),variant=="nav"?12.0f:11.0f,c,juce::Justification::centred);
        else text(g,b.getButtonText(),b.getLocalBounds().reduced(4,0),13,c,juce::Justification::centred,true);
    }
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool hover,bool) override {
        const bool dark=(bool)b.getProperties().getWithDefault("dark",false);auto r=b.getLocalBounds().toFloat().reduced(.5f);
        g.setColour(b.getToggleState()?(dark?light:ink):dark?screenPanel:panel);g.fillRoundedRectangle(r,6);
        g.setColour(dark?screenMuted:ink);g.drawRoundedRectangle(r,6,1);
        monoText(g,b.getButtonText(),b.getLocalBounds().reduced(5,0),11,b.getToggleState()?(dark?ink:light):dark?light:ink,juce::Justification::centred);
        if(hover){g.setColour((dark?light:ink).withAlpha(.12f));g.fillRoundedRectangle(r,6);}
    }
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox& box) override {
        bool dark=(bool)box.getProperties().getWithDefault("dark",false);g.setColour(dark?screenPanel:panel);g.fillRoundedRectangle(.5f,.5f,(float)w-1,(float)h-1,6);
        g.setColour(dark?juce::Colour(0xff3a3f3d):rule);g.drawRoundedRectangle(.5f,.5f,(float)w-1,(float)h-1,6,1);drawIcon(g,"chevron",{(float)w-21,(float)h*.5f-5,10,10},dark?light:ink);
    }
    void positionComboBoxText(juce::ComboBox& box,juce::Label& label) override {
        label.setBounds(9,0,box.getWidth()-30,box.getHeight());label.setFont(mono(11));label.setColour(juce::Label::textColourId,(bool)box.getProperties().getWithDefault("dark",false)?light:ink);
    }
    juce::Font getPopupMenuFont() override{return font(13);}
};

class EngineTile final : public juce::Component {
public:
    EngineTile(AlphaAudioProcessor& p,int ui):processor(p),index(ui) {
        select.setButtonText(names[(size_t)ui]);select.getProperties().set("variant","tile");addAndMakeVisible(select);
        select.onClick=[this]{if(onSelect)onSelect(index);};select.setTooltip("Select "+juce::String(names[(size_t)index])+". Drag the coloured strip to set its amount.");
        on.getProperties().set("variant","dot");on.setColour(juce::TextButton::buttonColourId,colours[(size_t)ui]);addAndMakeVisible(on);
        on.setTooltip("Enable or bypass this engine.");on.onClick=[this]{if(beforeChange)beforeChange();processor.setValue(juce::String(AlphaAudioProcessor::moduleIds[(size_t)module()])+"On",on.getToggleState()?0:1);update();};
        lock.getProperties().set("variant","icon");lock.getProperties().set("icon","lock");lock.setTooltip("Keep this engine unchanged when rolling a variation.");addAndMakeVisible(lock);
        lock.onClick=[this]{locked=!locked;lock.setToggleState(locked,juce::dontSendNotification);repaint();};
        setupLinear(amount,colours[(size_t)ui],"Amount for "+juce::String(names[(size_t)ui]));amount.getProperties().set("bar",true);amount.setSliderStyle(juce::Slider::LinearBarVertical);amount.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        amount.setMouseDragSensitivity(200);amount.setVelocityModeParameters(.15,1,0.0,true,juce::ModifierKeys::shiftModifier);amount.setSliderSnapsToMousePosition(false);addAndMakeVisible(amount);
        amount.onDragStart=[this]{if(onSelect)onSelect(index);if(beforeChange)beforeChange();};
        amount.onValueChange=[this]{repaint();};setScene(0);update();
    }
    int module() const {return modules[(size_t)index];}
    void setScene(int scene){attachment.reset();attachment=attachSlider(processor,AlphaAudioProcessor::sceneId(scene,module()),amount);}
    void update(){on.setToggleState(processor.value(juce::String(AlphaAudioProcessor::moduleIds[(size_t)module()])+"On")>.5f,juce::dontSendNotification);amount.setEnabled(on.getToggleState());lock.setVisible(locked||isMouseOver(true));repaint();}
    void paint(juce::Graphics& g) override {
        g.setColour(selected?juce::Colours::white:panel);g.fillRoundedRectangle(getLocalBounds().toFloat(),12);
        if(selected){g.setColour(ink);g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1),12,2);}
        monoText(g,juce::String(index+1).paddedLeft('0',2),{12,11,20,19},11);
        text(g,names[(size_t)index],{38,10,92,20},14,on.getToggleState()?ink:muted,juce::Justification::centredLeft,true);
        auto modes=AlphaAudioProcessor::modeNames(module());auto mode=juce::jlimit(0,modes.size()-1,(int)processor.value(juce::String(AlphaAudioProcessor::moduleIds[(size_t)module()])+"Mode"));
        monoText(g,modes[mode].toLowerCase(),{12,34,109,17},11);
        monoText(g,juce::String(juce::roundToInt(amount.getValue())),{121,34,27,17},11,ink,juce::Justification::centredRight);
    }
    void resized() override {select.setBounds(getLocalBounds());on.setBounds(132,5,26,30);lock.setBounds(105,5,26,30);amount.setBounds(12,64,136,24);}
    bool selected=false,locked=false;
    std::function<void(int)> onSelect;std::function<void()> beforeChange;
private:
    AlphaAudioProcessor& processor;int index;juce::TextButton select,on,lock;juce::Slider amount;std::unique_ptr<SliderAttachment> attachment;
};

class SpectrumView final : public juce::Component,public juce::SettableTooltipClient {
public:
    explicit SpectrumView(AlphaAudioProcessor& p):processor(p),fft(11),window(2048,juce::dsp::WindowingFunction<float>::hann) {
        setTooltip("Engine balance with a live output spectrum. Coloured shapes show relative character amounts, not EQ filters. Drag a coloured plateau vertically to adjust its engine.");
    }
    void update() {
        std::array<float,alpha::Engine::analyserSize> samples{};processor.engine.copyAnalyser(samples);
        std::copy(samples.begin(),samples.end(),fftData.begin());std::fill(fftData.begin()+2048,fftData.end(),0);
        window.multiplyWithWindowingTable(fftData.data(),2048);fft.performFrequencyOnlyForwardTransform(fftData.data());
        for(size_t i=0;i<levels.size();++i){const float f=20*std::pow(1000.0f,(float)i/(float)(levels.size()-1));const double rate=processor.getSampleRate()>0?processor.getSampleRate():48000.0;const float bin=f*2048/(float)juce::jlimit(8000.0,384000.0,rate);
            float magnitude=0;if(bin<1024){const int b=juce::jlimit(0,1022,(int)bin);magnitude=juce::jmap(bin-(float)b,fftData[(size_t)b],fftData[(size_t)b+1])/512;}
            float v=juce::jlimit(0.0f,1.0f,(juce::Decibels::gainToDecibels(juce::jmax(magnitude,1e-6f))+72)/72);levels[i]=juce::jmax(v,levels[i]-.035f);
        }repaint();
    }
    float amount(int i) const {
        const float morph=processor.value("morph")*.01f;const int m=modules[(size_t)i];
        return juce::jmap(morph,processor.value(AlphaAudioProcessor::sceneId(0,m)),processor.value(AlphaAudioProcessor::sceneId(1,m)))*processor.value("character")*.01f;
    }
    void paint(juce::Graphics& g) override {
        const bool bypass=processor.value("bypass")>.5f;juce::Graphics::ScopedSaveState save(g);if(bypass)g.setOpacity(.35f);
        const std::array<float,10> frequencies {20,50,100,200,500,1000,2000,5000,10000,20000};
        for(size_t i=0;i<frequencies.size();++i){const float x=frequencyX(frequencies[i]);if(i>0&&i<9){g.setColour(juce::Colour(0xff1c1f1e));g.drawVerticalLine((int)x,56,328);}
            monoText(g,i==9?"20k Hz":frequencies[i]>=1000?juce::String((int)(frequencies[i]/1000))+"k":juce::String((int)frequencies[i]),{(int)x-(i==0?0:i==9?50:24),338,i==9?50:48,20},11,screenMuted,i==0?juce::Justification::centredLeft:i==9?juce::Justification::centredRight:juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff2a2e2c));g.drawHorizontalLine(328,56,1128);
        juce::Path live;live.startNewSubPath(56,328);for(size_t i=0;i<levels.size();++i)live.lineTo(56+1072*(float)i/(float)(levels.size()-1),328-levels[i]*220);live.lineTo(1128,328);live.closeSubPath();
        g.setColour(light.withAlpha(.07f));g.fillPath(live);g.setColour(light.withAlpha(.4f));stroke(g,live,1.2f);
        for(int pass=0;pass<2;++pass)for(int i=0;i<6;++i){if((i==selected)!=(pass==1))continue;drawBand(g,i);}
        const auto c=colours[(size_t)selected];const float y=juce::jmax(56.0f,328-amount(selected)*2.72f-45);const float x=juce::jlimit(80.0f,880.0f,starts[(size_t)selected]+18);
        g.setColour(c);g.fillRoundedRectangle(x,y,222,30,8);
        text(g,names[(size_t)selected],{(int)x+12,(int)y,55,30},12,ink,juce::Justification::centredLeft,true);
        monoText(g,juce::String(juce::roundToInt(amount(selected))),{(int)x+72,(int)y,36,30},13,ink);
        monoText(g,"effective amount",{(int)x+103,(int)y,117,30},10,ink);
        meter(g,"in",processor.engine.getInputPeak(),912,screenMuted);meter(g,"out",processor.engine.getOutputPeak(),1023,light);
        if(bypass)monoText(g,"BYPASSED",{500,17,160,22},11,light,juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if(e.position.y<56||e.position.y>328)return;int closest=selected;float distance=1000;
        for(int i=0;i<6;++i)if(e.position.x>=starts[(size_t)i]-45&&e.position.x<=ends[(size_t)i]+45){const float d=std::abs(e.position.y-(328-amount(i)*2.72f));if(d<distance){distance=d;closest=i;}}
        selected=closest;if(onSelect)onSelect(closest);if(beforeChange)beforeChange();dragId=AlphaAudioProcessor::sceneId(scene,modules[(size_t)selected]);dragValue=processor.value(dragId);if(auto* p=processor.parameters.getParameter(dragId))p->beginChangeGesture();repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {if(dragId.isNotEmpty()){auto* p=processor.parameters.getParameter(dragId);const float v=juce::jlimit(0.0f,100.0f,dragValue-(float)e.getDistanceFromDragStartY()*(e.mods.isShiftDown()?.05f:.5f));if(p)p->setValueNotifyingHost(p->convertTo0to1(v));}}
    void mouseUp(const juce::MouseEvent&) override {if(auto* p=processor.parameters.getParameter(dragId))p->endChangeGesture();dragId.clear();}
    int selected=2,scene=0;std::function<void(int)> onSelect;std::function<void()> beforeChange;
private:
    static float frequencyX(float f){return 56+357.333333f*std::log10(f/20);}
    void meter(juce::Graphics& g,const char* label,float peak,int x,juce::Colour c){monoText(g,label,{x,12,24,18},11,screenMuted);g.setColour(juce::Colour(0xff262a28));g.fillRoundedRectangle((float)x+29,18,64,4,2);g.setColour(peak>1?colours[2]:c);const float value=juce::jlimit(0.0f,1.0f,(juce::Decibels::gainToDecibels(juce::jmax(peak,1e-6f))+60)/60);g.fillRoundedRectangle((float)x+29,18,64*value,4,2);}
    void drawBand(juce::Graphics& g,int i){const bool chosen=i==selected;auto c=colours[(size_t)i].withAlpha(chosen?1.0f:.65f);const float y=328-amount(i)*2.72f;const float lo=starts[(size_t)i],hi=ends[(size_t)i];juce::Path p;
        if(lo<=56)p.startNewSubPath(56,y);else{p.startNewSubPath(lo-57,328);p.cubicTo(lo-28,328,lo-28,y,lo,y);}p.lineTo(hi,y);if(hi<1128)p.cubicTo(hi+28,y,hi+28,328,juce::jmin(1128.0f,hi+57),328);
        const bool on=processor.value(juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)i]])+"On")>.5f;
        if(on){auto fill=p;fill.lineTo(1128,328);fill.lineTo(56,328);fill.closeSubPath();g.setColour(c.withMultipliedAlpha(chosen?.28f:.16f));g.fillPath(fill);g.setColour(c);stroke(g,p,chosen?3.0f:2.0f);}
        else {juce::Path dashed;const float lengths[]{5,5};juce::PathStrokeType(1.5f).createDashedStroke(dashed,p,lengths,2);g.setColour(screenMuted.withAlpha(.4f));g.fillPath(dashed);}
        if(!chosen)text(g,names[(size_t)i],{(int)lo+8,(int)y-23,100,18},11,c,juce::Justification::centredLeft,true);
        else{g.setColour(c);g.fillRoundedRectangle((lo+hi)*.5f-14,y-5,28,10,5);g.setColour(screen);g.drawRoundedRectangle((lo+hi)*.5f-14,y-5,28,10,5,2);}
    }
    AlphaAudioProcessor& processor;juce::dsp::FFT fft;juce::dsp::WindowingFunction<float> window;
    std::array<float,4096> fftData{};std::array<float,256> levels{};
    const std::array<float,6> starts {805,379,164,754,500,56},ends {1093,1128,799,1004,1049,1128};
    juce::String dragId;float dragValue=0;
};
}

struct AlphaAudioProcessorEditor::Impl final : juce::Component,juce::Timer {
    explicit Impl(AlphaAudioProcessor& p):processor(p),spectrum(p),motion(p),presets(p),tooltip(this,650) {
        setLookAndFeel(&look);setWantsKeyboardFocus(true);
        for(int i=0;i<6;++i){tiles[(size_t)i]=std::make_unique<EngineTile>(p,i);addAndMakeVisible(*tiles[(size_t)i]);tiles[(size_t)i]->onSelect=[this](int n){selectEngine(n);};tiles[(size_t)i]->beforeChange=[this]{remember();};}
        addAndMakeVisible(spectrum);addChildComponent(motion);addChildComponent(presets);
        spectrum.onSelect=[this](int n){selectEngine(n);};spectrum.beforeChange=[this]{remember();};motion.beforeChange=[this]{remember();};
        presets.onClose=[this]{showView(0);};presets.onBeforeLoad=[this]{remember();};presets.onLoad=[this]{selectScene(0,false);lastProgram=processor.getCurrentProgram();refresh();};presets.onFile=[this](bool save){choosePresetFile(save);};
        button(prev,"", "icon", "prev");button(next,"", "icon", "next");button(preset,"PRESETS","preset");
        button(roll,"roll","roll");button(sceneA,"A","scene");button(sceneB,"B","scene");button(undo,"","icon","undo");button(redo,"","icon","redo");button(settings,"","icon","settings");button(bypass,"","icon","power");
        prev.onClick=[this]{stepPreset(-1);};next.onClick=[this]{stepPreset(1);};preset.onClick=[this]{showView(view==2?0:2);};
        roll.onClick=[this]{rollVariation();};sceneA.onClick=[this]{selectScene(0,true);};sceneB.onClick=[this]{selectScene(1,true);};
        undo.onClick=[this]{restore(false);};redo.onClick=[this]{restore(true);};settings.onClick=[this]{showSettings();};bypass.onClick=[this]{remember();processor.setValue("bypass",processor.value("bypass")>.5f?0:1);refresh();};
        preset.setTooltip("Open the preset browser. Factory sounds, search, save and load.");roll.setTooltip(juce::String::fromUTF8("Roll a variation within ±25% of unlocked engine settings. Undo restores the previous sound."));
        undo.setTooltip("Undo the last editor change.");redo.setTooltip("Redo.");settings.setTooltip("Presets, scenes and window scale.");bypass.setTooltip("Bypass all processing and hear the original input.");
        sceneA.setTooltip("Edit scene A and listen at its morph endpoint.");sceneB.setTooltip("Edit scene B and listen at its morph endpoint.");
        button(spectrumTab,"spectrum","nav");button(motionTab,"motion","nav");spectrumTab.onClick=[this]{showView(0);};motionTab.onClick=[this]{showView(1);};
        button(main,"MAIN","tile");main.onClick=[this]{selectEngine(6);};
        for(auto& b:modes){button(b,"","mode");b.getProperties().set("mono",true);}
        for(int i=0;i<3;++i)modes[(size_t)i].onClick=[this,i]{remember();processor.setValue(juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)selected]])+"Mode",(float)i);refresh();};
        button(enable,"on");enable.getProperties().set("mono",true);enable.onClick=[this]{if(selected<6){remember();const auto id=juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)selected]])+"On";processor.setValue(id,processor.value(id)>.5f?0:1);refresh();}};
        button(lock,"lock");lock.getProperties().set("mono",true);lock.onClick=[this]{if(selected<6){auto& tile=*tiles[(size_t)selected];tile.locked=!tile.locked;lock.setToggleState(tile.locked,juce::dontSendNotification);tile.repaint();}};
        for(auto& s:details){setupKnob(s,"Engine character.");s.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);addAndMakeVisible(s);s.onDragStart=[this]{remember();};}
        for(auto& label:values){addAndMakeVisible(label);label.setFont(mono(12));label.setJustificationType(juce::Justification::centred);label.setEditable(false,true,false);label.setColour(juce::Label::textColourId,ink);label.setColour(juce::TextEditor::backgroundColourId,panel);label.setColour(juce::TextEditor::textColourId,ink);label.setColour(juce::TextEditor::outlineColourId,ink);}
        for(size_t i=0;i<values.size();++i){values[i].onEditorShow=[this]{remember();};values[i].onTextChange=[this,i]{juce::Slider::ScopedDragNotification gesture(details[i]);details[i].setValue(details[i].getValueFromText(values[i].getText()),juce::sendNotificationSync);};details[i].onValueChange=[this,i]{updateDetailValue(i);};}
        setupKnob(character,"Scale the strength of all six engines.");character.setComponentID("character");character.getProperties().set("master",true);character.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);addAndMakeVisible(character);characterAttachment=attachSlider(p,"character",character);character.onDragStart=[this]{remember();};
        setupKnob(mix,"Blend dry and processed signal.");mix.setComponentID("mix");mix.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);addAndMakeVisible(mix);mixAttachment=attachSlider(p,"mix",mix);mix.onDragStart=[this]{remember();};
        button(delta,juce::String::fromUTF8("Δ delta"));button(autoGain,"auto-gain");delta.getProperties().set("mono",true);autoGain.getProperties().set("mono",true);
        delta.onClick=[this]{remember();processor.setValue("delta",processor.value("delta")>.5f?0:1);refresh();};autoGain.onClick=[this]{remember();processor.setValue("autoGain",processor.value("autoGain")>.5f?0:1);refresh();};
        delta.setTooltip("Hear the difference between the dry input and processed signal.");autoGain.setTooltip("Match processed level to input with smooth, bounded RMS gain compensation.");
        lastProgram=processor.getCurrentProgram();selectScene(processor.value("morph")>=50?1:0,false);selectEngine(2);showView(0);spectrum.update();motion.update();sendLookAndFeelChange();startTimerHz(30);
    }
    ~Impl() override{stopTimer();chooser.reset();setLookAndFeel(nullptr);}
    void button(juce::TextButton& b,juce::String label,juce::String variant="",juce::String icon="") {b.setButtonText(label);b.getProperties().set("variant",variant);if(icon.isNotEmpty())b.getProperties().set("icon",icon);addAndMakeVisible(b);}
    void paint(juce::Graphics& g) override {
        g.setColour(paper);g.fillRoundedRectangle(getLocalBounds().toFloat(),18);
        drawLogo(g);text(g,"patina",{55,15,110,36},20,ink,juce::Justification::centredLeft,true);
        g.setColour(ink);g.drawRoundedRectangle(910.5f,16.5f,67,31,8,1);
        // The preset button provides the hit area; its actual name and category stay visible here.
        g.setColour(panel);g.fillRoundedRectangle(208,14,270,36,8);g.setColour(rule);g.drawRoundedRectangle(208.5f,14.5f,269,35,8,1);
        text(g,processor.getProgramName(processor.getCurrentProgram()),{220,15,177,34},14,ink,juce::Justification::centredLeft,true);
        monoText(g,AlphaAudioProcessor::presetCategory(processor.getCurrentProgram()).toLowerCase(),{397,15,55,34},10,muted,juce::Justification::centredRight);drawIcon(g,"chevron",{457,26,12,12},ink);
        drawIcon(g,"roll",{541,24,16,16},ink);
        g.setColour(screen);g.fillRoundedRectangle(24,64,1152,376,14);
        if(selected==6){g.setColour(juce::Colours::white);g.fillRoundedRectangle(1032,456,144,100,12);g.setColour(ink);g.drawRoundedRectangle(1033,457,142,98,12,2);}else{g.setColour(panel);g.fillRoundedRectangle(1032,456,144,100,12);}
        monoText(g,"M",{1044,467,18,18},11);text(g,"MAIN",{1069,466,90,20},14,ink,juce::Justification::centredLeft,true);
        monoText(g,juce::String::fromUTF8("morph · rate"),{1044,492,120,16},11);monoText(g,juce::String::fromUTF8("depth · out"),{1044,508,120,16},11);
        for(auto r:{juce::Rectangle<float>(24,572,232,204),{272,572,600,204},{888,572,288,204}}){g.setColour(panel);g.fillRoundedRectangle(r,14);}
        const auto c=selected<6?colours[(size_t)selected]:ink;
        g.setColour(c);g.fillEllipse(39,592,12,12);g.setColour(ink);g.drawEllipse(39,592,12,12,1.5f);
        monoText(g,selected<6?juce::String(selected+1).paddedLeft('0',2)+juce::String::fromUTF8(" · ")+descriptions[(size_t)selected]:juce::String::fromUTF8("M · global controls"),{59,588,180,22},11);
        text(g,selected<6?names[(size_t)selected]:"MAIN",{38,613,200,35},28,ink,juce::Justification::centredLeft,true);
        if(selected==6){monoText(g,"A / B scene morph",{46,658,196,23},12);monoText(g,"Shared motion & output",{46,685,196,23},11);monoText(g,"Character for the whole chain",{46,712,196,23},10);}
        text(g,(selected<6?juce::String(names[(size_t)selected]):"MAIN")+juce::String::fromUTF8(" · CHARACTER"),{288,583,560,22},10.5f,muted,juce::Justification::centredLeft,true);
        for(size_t i=0;i<4;++i){const int x=288+(int)i*142;text(g,detailNames[i],{x,690,142,20},13,ink,juce::Justification::centred,true);text(g,detailHints[i],{x,738,142,18},11,muted,juce::Justification::centred);}
        text(g,"AMOUNT",{904,585,160,20},10.5f,muted,juce::Justification::centredLeft,true);text(g,"MIX",{1071,665,88,18},10.5f,muted,juce::Justification::centred,true);monoText(g,mix.getTextFromValue(mix.getValue()),{1071,686,88,18},12,ink,juce::Justification::centred);
        if(statusTicks>0)monoText(g,status,{658,20,248,24},10,muted,juce::Justification::centredRight);
    }
    void paintOverChildren(juce::Graphics& g) override {
        // Header preset uses a transparent native button with an accessible name.
        if(view==2){g.setColour(rule);g.drawRoundedRectangle(208.5f,14.5f,269,35,8,1);}
    }
    void resized() override {
        prev.setBounds(168,16,32,32);preset.setBounds(208,14,270,36);next.setBounds(486,16,32,32);roll.setBounds(534,14,112,36);
        sceneA.setBounds(910,16,34,32);sceneB.setBounds(944,16,34,32);undo.setBounds(992,16,32,32);redo.setBounds(1032,16,32,32);settings.setBounds(1072,16,32,32);bypass.setBounds(1112,16,32,32);
        spectrum.setBounds(24,64,1152,376);motion.setBounds(24,64,1152,376);presets.setBounds(24,64,1152,376);
        spectrumTab.setBounds(40,76,67,26);motionTab.setBounds(126,76,view==1?123:55,26);
        for(int i=0;i<6;++i)tiles[(size_t)i]->setBounds(24+i*168,456,160,100);main.setBounds(1032,456,144,100);
        for(int i=0;i<3;++i)modes[(size_t)i].setBounds(38,654+i*24,204,23);
        enable.setBounds(38,739,98,25);lock.setBounds(144,739,98,25);
        for(size_t i=0;i<4;++i){const int x=288+(int)i*142;details[i].setBounds(x+33,610,76,76);values[i].setBounds(x+9,714,124,22);}
        character.setBounds(900,609,148,148);mix.setBounds(1089,608,52,52);delta.setBounds(1070,711,90,28);autoGain.setBounds(1070,747,90,28);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        const auto c=key.getTextCharacter();if(c>='1'&&c<='6'){selectEngine(c-'1');return true;}if(c=='m'||c=='M'){selectEngine(6);return true;}
        if(key==juce::KeyPress::escapeKey){showView(0);return true;}if(key==juce::KeyPress::tabKey){showView(view==1?0:1);return true;}
        if((c=='z'||c=='Z')&&key.getModifiers().isCommandDown()){restore(key.getModifiers().isShiftDown());return true;}
        if(c=='r'||c=='R'){rollVariation();return true;}if((c=='o'||c=='O')&&selected<6){if(enable.onClick)enable.onClick();return true;}return false;
    }
private:
    void drawLogo(juce::Graphics& g){for(int i=0;i<6;++i){juce::Path p;const float start=(float)i*juce::MathConstants<float>::twoPi/6+.1f;p.addCentredArc(35,32,9,9,0,start,start+.84f,true);g.setColour(colours[(size_t)i]);stroke(g,p,3.6f);}g.setColour(ink);g.fillEllipse(32.4f,29.4f,5.2f,5.2f);}
    void updateDetailValue(size_t i){if(!values[i].isBeingEdited())values[i].setText(details[i].getTextFromValue(details[i].getValue()),juce::dontSendNotification);}
    void bindDetails(){
        for(auto& a:detailAttachments)a.reset();
        juce::StringArray ids;
        if(selected<6){const int m=modules[(size_t)selected];auto id=juce::String(AlphaAudioProcessor::moduleIds[(size_t)m]);ids.add(AlphaAudioProcessor::sceneId(editScene,m,1));ids.add(AlphaAudioProcessor::sceneId(editScene,m,2));ids.add(AlphaAudioProcessor::sceneId(editScene,m,0));ids.add("mod"+juce::String(AlphaAudioProcessor::moduleNames[(size_t)m]));
            detailNames={AlphaAudioProcessor::toneName(m).toLowerCase(),AlphaAudioProcessor::detailName(m).toLowerCase(),"amount","motion"};
            detailHints={m==1?juce::String::fromUTF8("slow ↔ fast"):m==4?juce::String::fromUTF8("small ↔ wide"):m==5?juce::String::fromUTF8("crushed ↔ clean"):juce::String::fromUTF8("dark ↔ bright"),m==1?juce::String::fromUTF8("steady ↔ drifting"):m==4?juce::String::fromUTF8("short ↔ long"):m==5?juce::String::fromUTF8("smooth ↔ stepped"):juce::String::fromUTF8("subtle ↔ character"),"engine strength",juce::String::fromUTF8("inverse ↔ follow")};
            auto options=AlphaAudioProcessor::modeNames(m);for(int i=0;i<3;++i){modes[(size_t)i].setButtonText(options[i].toLowerCase());modes[(size_t)i].setVisible(true);}enable.setVisible(true);lock.setVisible(true);
        }else{ids={"morph","lfoRate","lfoDepth","output"};detailNames={"morph","rate","depth","output"};detailHints={juce::String::fromUTF8("scene A ↔ scene B"),"shared motion","master modulation","final level"};for(auto& b:modes)b.setVisible(false);enable.setVisible(false);lock.setVisible(false);}
        for(size_t i=0;i<4;++i){details[i].setTooltip(detailNames[i]+". Double-click to reset; shift-drag for fine control. Double-click the value to type.");details[i].setComponentID(ids[(int)i]);detailAttachments[i]=attachSlider(processor,ids[(int)i],details[i]);updateDetailValue(i);}
    }
    void selectEngine(int n){selected=juce::jlimit(0,6,n);for(int i=0;i<6;++i){tiles[(size_t)i]->selected=i==selected;tiles[(size_t)i]->repaint();}if(selected<6){spectrum.selected=selected;motion.setTarget(selected);if(view==1)motionTab.setButtonText(juce::String::fromUTF8("motion · ")+names[(size_t)selected]);}bindDetails();refresh();}
    void selectScene(int scene,bool listen){if(listen)remember();editScene=scene;for(auto& t:tiles)t->setScene(scene);spectrum.scene=scene;bindDetails();if(listen)processor.setValue("morph",scene==0?0:100);refresh();}
    void showView(int n){view=n;motionTab.setButtonText(n==1?juce::String::fromUTF8("motion · ")+names[(size_t)spectrum.selected]:"motion");motionTab.setBounds(126,76,n==1?123:55,26);spectrum.setVisible(n==0);motion.setVisible(n==1);presets.setVisible(n==2);spectrumTab.setVisible(n!=2);motionTab.setVisible(n!=2);spectrumTab.setToggleState(n==0,juce::dontSendNotification);motionTab.setToggleState(n==1,juce::dontSendNotification);if(n==2)presets.selectCurrent();repaint();}
    void refresh(){sceneA.setToggleState(editScene==0,juce::dontSendNotification);sceneB.setToggleState(editScene==1,juce::dontSendNotification);bypass.setToggleState(processor.value("bypass")>.5f,juce::dontSendNotification);delta.setToggleState(processor.value("delta")>.5f,juce::dontSendNotification);autoGain.setToggleState(processor.value("autoGain")>.5f,juce::dontSendNotification);undo.setEnabled(!undoStates.empty());redo.setEnabled(!redoStates.empty());for(auto& t:tiles)t->update();
        if(selected<6){const auto id=juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)selected]]);for(int i=0;i<3;++i)modes[(size_t)i].setToggleState((int)processor.value(id+"Mode")==i,juce::dontSendNotification);enable.setToggleState(processor.value(id+"On")>.5f,juce::dontSendNotification);enable.setButtonText(enable.getToggleState()?"on":"bypassed");lock.setToggleState(tiles[(size_t)selected]->locked,juce::dontSendNotification);lock.setButtonText(lock.getToggleState()?"locked":"lock");}
        for(size_t i=0;i<4;++i){updateDetailValue(i);values[i].setEnabled(true);details[i].setEnabled(true);}if(selected==6&&processor.value("lfoSync")>.5f){details[1].setEnabled(false);values[1].setEnabled(false);values[1].setText("host sync",juce::dontSendNotification);detailHints[1]="edit rate in motion";}else if(selected==6)detailHints[1]="shared motion";repaint();
    }
    void timerCallback() override {const int program=processor.getCurrentProgram();if(program!=lastProgram){lastProgram=program;selectScene(processor.value("morph")>=50?1:0,false);presets.selectCurrent();}if(view==0)spectrum.update();if(view==1)motion.update();if(statusTicks>0)--statusTicks;refresh();}
    juce::MemoryBlock state(){juce::MemoryBlock b;processor.getStateInformation(b);return b;}
    void remember(){auto b=state();if(undoStates.empty()||undoStates.back().data!=b||undoStates.back().scene!=editScene){undoStates.push_back({std::move(b),editScene});if(undoStates.size()>40)undoStates.pop_front();}redoStates.clear();}
    void restore(bool redoAction){auto& from=redoAction?redoStates:undoStates;auto& to=redoAction?undoStates:redoStates;if(from.empty())return;to.push_back({state(),editScene});const auto entry=from.back();from.pop_back();processor.setStateInformation(entry.data.getData(),(int)entry.data.getSize());lastProgram=processor.getCurrentProgram();selectScene(entry.scene,false);presets.selectCurrent();refresh();}
    void stepPreset(int step){remember();const int n=processor.getNumPrograms();processor.setCurrentProgram((processor.getCurrentProgram()+step+n)%n);lastProgram=processor.getCurrentProgram();selectScene(0,false);presets.selectCurrent();refresh();}
    void rollVariation(){remember();for(int i=0;i<6;++i)if(!tiles[(size_t)i]->locked)for(int j=0;j<3;++j){auto id=AlphaAudioProcessor::sceneId(editScene,modules[(size_t)i],j);processor.setValue(id,juce::jlimit(0.0f,100.0f,processor.value(id)+(random.nextFloat()*2-1)*25));}status=juce::String::fromUTF8("Variation rolled · undo to restore");statusTicks=90;refresh();}
    void showSettings(){juce::PopupMenu menu;menu.addItem(1,juce::String::fromUTF8("Save sound…"));menu.addItem(2,juce::String::fromUTF8("Open sound…"));menu.addSeparator();menu.addItem(3,editScene==0?"Copy A to B":"Copy B to A");menu.addItem(4,"Swap scenes A / B");menu.addSeparator();juce::PopupMenu scales;scales.addItem(75,"75%");scales.addItem(100,"100%");scales.addItem(125,"125%");scales.addItem(150,"150%");menu.addSubMenu("Window size",scales);
        auto safe=juce::Component::SafePointer<Impl>(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settings),[safe](int result){if(!safe)return;if(result==1||result==2)safe->choosePresetFile(result==1);else if(result==3){safe->remember();safe->processor.copyScene(safe->editScene,1-safe->editScene);safe->refresh();}else if(result==4){safe->remember();safe->processor.swapScenes();safe->refresh();}else if(result>=75){if(auto* editor=safe->findParentComponentOfClass<AlphaAudioProcessorEditor>())editor->setSize(designWidth*result/100,designHeight*result/100);}});
    }
    void choosePresetFile(bool save){if(save){const auto result=processor.userPresetDirectory().createDirectory();if(result.failed()){status="Cannot create preset directory";statusTicks=120;return;}}
        chooser=std::make_unique<juce::FileChooser>(save?"Save patina sound":"Open patina sound",save?processor.userPresetDirectory().getChildFile("My sound.alphapreset"):processor.userPresetDirectory(),"*.alphapreset",true);
        auto safe=juce::Component::SafePointer<Impl>(this);const int flags=save?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles);
        chooser->launchAsync(flags,[safe,save](const juce::FileChooser& dialog){if(!safe)return;auto f=dialog.getResult();if(f==juce::File())return;if(save)f=f.withFileExtension("alphapreset");else safe->remember();const bool ok=save?safe->processor.saveUserPreset(f):safe->processor.loadUserPreset(f);safe->status=ok?(save?"Saved ":"Opened ")+f.getFileNameWithoutExtension():"Could not "+juce::String(save?"save":"open")+" sound";safe->statusTicks=120;if(ok&&!save){safe->lastProgram=safe->processor.getCurrentProgram();safe->selectScene(safe->processor.value("morph")>=50?1:0,false);}safe->presets.selectCurrent();safe->refresh();});
    }
    AlphaAudioProcessor& processor;PatinaLookAndFeel look;SpectrumView spectrum;patina::MotionView motion;patina::PresetView presets;
    std::array<std::unique_ptr<EngineTile>,6> tiles;
    juce::TextButton prev,next,preset,roll,sceneA,sceneB,undo,redo,settings,bypass,spectrumTab,motionTab,main,enable,lock,delta,autoGain;
    std::array<juce::TextButton,3> modes;std::array<juce::Slider,4> details;std::array<juce::Label,4> values;
    std::array<juce::String,4> detailNames,detailHints;std::array<std::unique_ptr<SliderAttachment>,4> detailAttachments;
    juce::Slider character,mix;std::unique_ptr<SliderAttachment> characterAttachment,mixAttachment;
    juce::TooltipWindow tooltip;std::unique_ptr<juce::FileChooser> chooser;juce::Random random;
    struct HistoryState {juce::MemoryBlock data;int scene;};
    std::deque<HistoryState> undoStates,redoStates;int selected=2,editScene=0,view=0,statusTicks=0,lastProgram=0;juce::String status;
};

AlphaAudioProcessorEditor::AlphaAudioProcessorEditor(AlphaAudioProcessor& p):AudioProcessorEditor(p),impl(std::make_unique<Impl>(p)) {
    addAndMakeVisible(*impl);setResizable(true,true);setResizeLimits(900,600,2400,1600);
    if(auto* c=getConstrainer())c->setFixedAspectRatio((double)designWidth/designHeight);setSize(designWidth,designHeight);
}
AlphaAudioProcessorEditor::~AlphaAudioProcessorEditor()=default;
void AlphaAudioProcessorEditor::paint(juce::Graphics& g){g.fillAll(patina::paper);}
void AlphaAudioProcessorEditor::resized(){if(impl){impl->setBounds(0,0,designWidth,designHeight);impl->setTransform(juce::AffineTransform::scale((float)getWidth()/designWidth,(float)getHeight()/designHeight));}}
