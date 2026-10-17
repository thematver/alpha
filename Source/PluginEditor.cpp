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
    return juce::Font(juce::FontOptions(bold ? heavy : regular).withHeight(size)).withHorizontalScale(bold&&size==20?1.25f:bold&&size>=20?1.18f:bold&&size>=14?1.12f:1.0f);
}
juce::Font mono(float size) {
    static const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::GeistMonoRegular_ttf, BinaryData::GeistMonoRegular_ttfSize);
    return juce::Font(juce::FontOptions(face).withHeight(size));
}
float motionValue(const AlphaAudioProcessor& processor,int module,const juce::String& suffix){if(processor.value(AlphaAudioProcessor::motionId(module,"Enabled"))>.5f)return processor.value(AlphaAudioProcessor::motionId(module,suffix));if(suffix=="Shape")return processor.value("lfoShape");if(suffix=="Rate")return processor.value("lfoRate");if(suffix=="Sync")return processor.value("lfoSync");if(suffix=="Division")return processor.value("lfoDivision");return 0;}
std::pair<float,float> motionWaveRange(const AlphaAudioProcessor& processor,int module){if((int)motionValue(processor,module,"Shape")!=7)return {-1,1};const bool independent=processor.value(AlphaAudioProcessor::motionId(module,"Enabled"))>.5f;const alpha::MotionSettings defaults;const int count=independent?juce::jlimit(2,alpha::curveCapacity,(int)processor.value(AlphaAudioProcessor::motionId(module,"Count"))):defaults.curveCount;float low=1,high=-1;for(int i=0;i<count;++i){const float value=independent?processor.value(AlphaAudioProcessor::motionId(module,"Y"+juce::String(i)))*.01f:defaults.curve[(size_t)i].y;low=juce::jmin(low,value);high=juce::jmax(high,value);}return {low,high};}
void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float size,
          juce::Colour c, juce::Justification align, bool bold) {
    g.setColour(c);auto face=font(size,bold);if(bold&&std::abs(size-10.5f)<.01f)face=face.withExtraKerningFactor(.14f);g.setFont(face);g.drawText(s,r,align,false);
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
    s.setVelocityModeParameters(1.0,1,0.0,false);
    s.setTooltip(tooltip + " Double-click to reset. Shift-drag for fine adjustment. Click the value to type.");
}
void setupLinear(juce::Slider& s, juce::Colour c, const juce::String& tooltip) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);s.setSliderSnapsToMousePosition(false);s.setMouseDragSensitivity(200);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 24);
    s.setColour(juce::Slider::trackColourId, c); s.setNumDecimalPlacesToDisplay(0);
    s.setTooltip(tooltip + " Double-click to reset.");
}
std::unique_ptr<SliderAttachment> attachSlider(AlphaAudioProcessor& p, const juce::String& id, juce::Slider& s) {
    if (auto* param = p.parameters.getParameter(id)) {
        s.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
        auto attachment = std::make_unique<SliderAttachment>(p.parameters, id, s);
        s.textFromValueFunction = [id](double v) {
            if(id=="mainTone")return juce::String((v-50)*.12,1)+" dB";
            if(id=="mainWidth")return juce::String(juce::roundToInt(v*2))+"%";
            if (id == "output") return juce::String(std::abs(v) < .005 ? 0.0 : v, 1) + " dB";
            if (id == "lfoRate" || id.endsWith("MotionRate")) return juce::String(v, 2) + " Hz";
            return juce::String(juce::roundToInt(v)) + (id.startsWith("mod") && v > 0 ? " %" : "%");
        };
        s.valueFromTextFunction=[id](const juce::String& text){const double value=text.replaceCharacter((juce::juce_wchar)0x2212,'-').retainCharacters("-0123456789.").getDoubleValue();if(id=="mainTone")return 50+value/.12;if(id=="mainWidth")return value*.5;return value;};
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
        const bool engineOff=(bool)s.getProperties().getWithDefault("engineOff",false);juce::Graphics::ScopedSaveState state(g);if(engineOff)g.setOpacity(.35f);
        auto bounds=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h);
        const float diameter=master?132.0f:juce::jmin(bounds.getWidth(),bounds.getHeight())<60?44.0f:juce::jmin(bounds.getWidth(),bounds.getHeight())-10;
        auto circle=juce::Rectangle<float>(diameter,diameter).withCentre(bounds.getCentre());
        const auto centre=circle.getCentre();const float radius=diameter*.5f;
        const float angle=start+pos*(end-start);const float weight=master?6.0f:diameter<52?3.0f:4.0f;
        juce::Path arc;arc.addCentredArc(centre.x,centre.y,radius,radius,0,start,end,true);
        g.setColour(dark?juce::Colour(0xff2a2e2c):rule);stroke(g,arc,weight);
        juce::Path value;const float zero=s.getMinimum()<0||(bool)s.getProperties().getWithDefault("bipolar",false)?start+(end-start)*.5f:start;
        value.addCentredArc(centre.x,centre.y,radius,radius,0,zero,angle,true);g.setColour(fg.withAlpha(s.isEnabled()?1.0f:.35f));stroke(g,value,weight);
        if(s.getProperties().contains("modLow")){const float lo=(float)s.getProperties()["modLow"],hi=(float)s.getProperties()["modHigh"];const auto colour=juce::Colour::fromString(s.getProperties()["modColour"].toString());juce::Path modulation;modulation.addCentredArc(centre.x,centre.y,radius,radius,0,start+lo*(end-start),start+hi*(end-start),true);g.setColour(colour);stroke(g,modulation,weight);const float live=start+(float)s.getProperties()["modLive"]*(end-start);g.fillEllipse(centre.x+std::sin(live)*radius-2.5f,centre.y-std::cos(live)*radius-2.5f,5,5);}
        auto cap=circle.reduced(master?14.0f:diameter<52?6.0f:diameter*.12f);
        g.setColour(master||s.isMouseButtonDown()?ink:dark?screenPanel:juce::Colours::white);g.fillEllipse(cap);
        if(!master){g.setColour(fg);g.drawEllipse(cap,1.5f);}
        const auto u=juce::Point<float>(std::sin(angle),-std::cos(angle));const float r=cap.getWidth()*.5f;
        g.setColour(master||s.isMouseButtonDown()?panel:fg);
        if(master){const auto dot=centre+u*r*.82f;g.fillEllipse(dot.x-4,dot.y-4,8,8);text(g,juce::String(juce::roundToInt(s.getValue())),cap.toNearestInt(),28,panel,juce::Justification::centred,true);}
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
            if(!(bool)s.getProperties().getWithDefault("engineOff",false)){g.setColour(c);const float proportion=(float)s.valueToProportionOfLength(s.getValue());juce::Path clip;clip.addRoundedRectangle(r,6);juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(clip);g.fillRect(r.withWidth(r.getWidth()*proportion));}
            else {g.setColour(rule);g.fillRoundedRectangle(r.withWidth(r.getWidth()*(float)s.valueToProportionOfLength(s.getValue())),6);g.setColour(muted.withAlpha(.55f));juce::Path outline,dashed;outline.addRoundedRectangle(r,6);const float dashes[]{4,3};juce::PathStrokeType(1).createDashedStroke(dashed,outline,dashes,2);g.fillPath(dashed);}
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
        if(variant=="tile"||variant=="dot"||variant=="preset"||variant=="lockbadge")return;
        if(variant=="roll"){if(hover||down){g.setColour(rule.withAlpha(.55f));g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(.5f),8);}g.setColour(ink);g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(.5f),8,1);return;}
        if(variant=="bank"){if(b.getToggleState()||hover){g.setColour(b.getToggleState()?screenPanel:juce::Colour(0xff151817));g.fillRoundedRectangle(r,6);}return;}
        if(variant=="nav") {if(b.getToggleState()||hover){g.setColour(b.getToggleState()?b.findColour(juce::TextButton::buttonColourId):screenMuted);g.fillRect(0,b.getHeight()-2,b.getWidth(),2);}return;}
        if(variant=="scene"){if(hover&&!b.getToggleState()){g.setColour(rule);g.fillRoundedRectangle(r,6);}return;}if(variant=="mode"){if(b.getToggleState()||hover){g.setColour(b.getToggleState()?ink:rule);g.fillRoundedRectangle(b.getLocalBounds().toFloat(),6);}return;}
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
        auto c=variant=="bank"?(b.getToggleState()?light:screenMuted):variant=="nav"?(b.getToggleState()?light:screenMuted):dark?(b.getToggleState()?screen:light):b.getToggleState()&&variant!="icon"&&variant!="plain"&&variant!="lockbadge"?panel:ink;
        if(!b.isEnabled())c=c.withAlpha(.3f);
        if(variant=="roll"){drawIcon(g,"roll",{11,(float)b.getHeight()*.5f-8,16,16},c);text(g,"roll",{35,0,23,b.getHeight()},13,c,juce::Justification::centredLeft,true);monoText(g,juce::String::fromUTF8("±25"),{63,0,22,b.getHeight()},11,muted,juce::Justification::centredLeft);return;}
        auto icon=b.getProperties().getWithDefault("icon","").toString();if(variant=="lockbadge"){drawIcon(g,icon,{(float)b.getWidth()*.5f-6,(float)b.getHeight()*.5f-6,12,12},c);return;}
        if(icon.isNotEmpty()){drawIcon(g,icon,{(float)b.getWidth()*.5f-8,(float)b.getHeight()*.5f-8,16,16},c);return;}
        if((bool)b.getProperties().getWithDefault("mono",false)||variant=="nav"||variant=="bank"||dark)monoText(g,b.getButtonText(),b.getLocalBounds().reduced(4,0),variant=="nav"?12.0f:11.0f,c,variant=="nav"?juce::Justification::centredLeft:juce::Justification::centred);
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
        bool dark=(bool)box.getProperties().getWithDefault("dark",false);const bool accent=box.getProperties().contains("accent");g.setColour(accent?juce::Colour::fromString(box.getProperties()["accent"].toString()):dark?screenPanel:panel);g.fillRoundedRectangle(.5f,.5f,(float)w-1,(float)h-1,6);
        g.setColour(dark?juce::Colour(0xff3a3f3d):rule);g.drawRoundedRectangle(.5f,.5f,(float)w-1,(float)h-1,6,1);drawIcon(g,"chevron",{(float)w-21,(float)h*.5f-5,10,10},dark&&!accent?light:ink);
    }
    void positionComboBoxText(juce::ComboBox& box,juce::Label& label) override {
        label.setBounds(9,0,box.getWidth()-30,box.getHeight());label.setFont(mono(11));label.setColour(juce::Label::textColourId,(bool)box.getProperties().getWithDefault("dark",false)&&!box.getProperties().contains("accent")?light:ink);
    }
    juce::Font getPopupMenuFont() override{return font(13);}
};

class EngineTile final : public juce::Button {
public:
    EngineTile(AlphaAudioProcessor& p,int ui):juce::Button(names[(size_t)ui]),processor(p),index(ui) {
        on.getProperties().set("variant","dot");on.setColour(juce::TextButton::buttonColourId,colours[(size_t)ui]);addAndMakeVisible(on);
        on.setTitle(juce::String(names[(size_t)ui])+" enabled");on.setTooltip("Enable or bypass this engine. O toggles the selected engine.");
        on.onClick=[this]{if(beforeChange)beforeChange();processor.setValue(moduleId()+"On",on.getToggleState()?0:1);update();};
        lock.getProperties().set("variant","lockbadge");lock.getProperties().set("icon","lock");lock.setTooltip("Lock this engine against roll. L toggles the selected lock.");addAndMakeVisible(lock);
        lock.onClick=[this]{locked=!locked;lock.setToggleState(locked,juce::dontSendNotification);repaint();};
        setupLinear(amount,colours[(size_t)ui],"Engine amount");amount.getProperties().set("bar",true);amount.setSliderStyle(juce::Slider::LinearBarVertical);amount.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        amount.setMouseDragSensitivity(200);amount.setSliderSnapsToMousePosition(false);amount.setTitle(juce::String(names[(size_t)ui])+" amount");addAndMakeVisible(amount);
        amount.onDragStart=[this]{if(onSelect)onSelect(index);if(beforeChange)beforeChange();};amount.onValueChange=[this]{repaint();};
        onClick=[this]{if(onSelect)onSelect(index);};setTooltip("Select "+juce::String(names[(size_t)ui])+". Drag vertically to change amount; Shift for fine adjustment. Double-click to reset. Right-click for options.");setScene(0);update();
    }
    int module() const {return modules[(size_t)index];}
    juce::String moduleId() const{return AlphaAudioProcessor::moduleIds[(size_t)module()];}
    void setScene(int scene){editScene=scene;baseline=baselineAmounts[(size_t)scene];attachment.reset();attachment=attachSlider(processor,AlphaAudioProcessor::sceneId(scene,module()),amount);amount.setDoubleClickReturnValue(true,baseline);}
    void captureBaseline(){for(int scene=0;scene<2;++scene)baselineAmounts[(size_t)scene]=processor.value(AlphaAudioProcessor::sceneId(scene,module()));baseline=baselineAmounts[(size_t)editScene];amount.setDoubleClickReturnValue(true,baseline);}
    void update(){on.setToggleState(processor.value(moduleId()+"On")>.5f,juce::dontSendNotification);amount.getProperties().set("engineOff",!on.getToggleState());lock.setVisible(locked);lock.setToggleState(locked,juce::dontSendNotification);repaint();}
    void paintButton(juce::Graphics& g,bool hover,bool) override {
        auto bounds=getLocalBounds().toFloat();g.setColour(selected?juce::Colours::white:panel);g.fillRoundedRectangle(bounds,12);
        if(selected||hover||isMouseOver(true)){g.setColour(ink);g.drawRoundedRectangle(bounds.reduced(selected?1.0f:.5f),12,selected?2.0f:1.0f);}
        monoText(g,juce::String(index+1).paddedLeft('0',2),{12,12,20,18},11);
        patina::text(g,names[(size_t)index],{33,12,101,18},14,on.getToggleState()?ink:muted,juce::Justification::centredLeft,true);
        const auto modes=AlphaAudioProcessor::modeNames(module());const auto mode=juce::jlimit(0,modes.size()-1,processor.effectiveMode(module()));
        monoText(g,on.getToggleState()?modes[mode].toLowerCase():"off",{12,34,109,16},11);
        const float modulation=processor.value("mod"+juce::String(AlphaAudioProcessor::moduleNames[(size_t)module()]));
        if(on.getToggleState()&&std::abs(modulation)>.01f){g.setColour(ink);g.fillRoundedRectangle(61,34,43,16,4);const auto division=juce::StringArray{"4 bar","2 bar","1 bar","1/2","1/4","1/8","1/16"};monoText(g,"~ "+(motionValue(processor,module(),"Sync")>.5f?division[juce::jlimit(0,6,(int)motionValue(processor,module(),"Division"))]:juce::String(motionValue(processor,module(),"Rate"),1)),{63,34,39,16},10,panel,juce::Justification::centred);}
        monoText(g,juce::String(juce::roundToInt(amount.getValue())),{121,34,27,16},11,on.getToggleState()?ink:muted,juce::Justification::centredRight);
        if(dragging){g.setColour(ink);g.fillRoundedRectangle(92,51,42,22,5);monoText(g,juce::String(amount.getValue(),fine?1:0),{95,51,36,22},11,panel,juce::Justification::centred);}
    }
    void resized() override {on.setBounds(130,6,26,30);const int labelWidth=juce::GlyphArrangement::getStringWidthInt(font(14,true),names[(size_t)index]);lock.setBounds(33+labelWidth+4,6,20,30);amount.setBounds(12,64,136,24);}
    void mouseDown(const juce::MouseEvent& e) override {
        if(onSelect)onSelect(index);if(e.mods.isPopupMenu()){showMenu();return;}
        juce::Button::mouseDown(e);dragging=false;fine=e.mods.isShiftDown();lastY=e.position.y;dragValue=amount.getValue();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(e.mods.isPopupMenu())return;
        if(!dragging&&std::abs(e.getDistanceFromDragStartY())>3){dragging=true;if(beforeChange)beforeChange();if(auto* p=processor.parameters.getParameter(AlphaAudioProcessor::sceneId(editScene,module())))p->beginChangeGesture();}
        if(dragging){fine=e.mods.isShiftDown();dragValue=juce::jlimit(0.0,100.0,dragValue+(lastY-e.position.y)*(fine?.05:.5));amount.setValue(dragValue,juce::sendNotificationSync);}lastY=e.position.y;repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {if(dragging){if(auto* p=processor.parameters.getParameter(AlphaAudioProcessor::sceneId(editScene,module())))p->endChangeGesture();dragging=false;repaint();}else juce::Button::mouseUp(e);}
    void mouseDoubleClick(const juce::MouseEvent&) override {if(beforeChange)beforeChange();processor.setValue(AlphaAudioProcessor::sceneId(editScene,module()),baseline);amount.setValue(baseline,juce::dontSendNotification);repaint();}
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& wheel) override {if(beforeChange)beforeChange();if(onSelect)onSelect(index);const float next=juce::jlimit(0.0f,100.0f,(float)amount.getValue()+(wheel.deltaY>0?1.0f:-1.0f));processor.setValue(AlphaAudioProcessor::sceneId(editScene,module()),next);amount.setValue(next,juce::dontSendNotification);repaint();}
    void showMenu(){juce::PopupMenu menu;menu.addItem(1,locked?"Unlock":"Lock");menu.addItem(2,on.getToggleState()?"Bypass engine":"Enable engine");menu.addItem(3,"Reset amount to preset");menu.addSeparator();menu.addItem(4,"Copy engine settings");menu.addItem(5,"Paste engine settings",clipboardReady);juce::PopupMenu algorithms;const auto choices=AlphaAudioProcessor::modeNames(module());for(int i=0;i<choices.size();++i)algorithms.addItem(200+i,choices[i],true,processor.effectiveMode(module())==i);menu.addSubMenu("Algorithm",algorithms);auto safe=juce::Component::SafePointer<EngineTile>(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe](int result){if(!safe)return;if(result==4){safe->copySettings();}else if(result==5){if(safe->beforeChange)safe->beforeChange();safe->pasteSettings();}else if(result>=200){if(safe->beforeChange)safe->beforeChange();safe->processor.setModuleMode(safe->module(),result-200);}else if(result==1){safe->locked=!safe->locked;safe->update();}else if(result==2){if(safe->on.onClick)safe->on.onClick();}else if(result==3){if(safe->beforeChange)safe->beforeChange();safe->amount.setValue(safe->baseline,juce::sendNotificationSync);}});}
    void copySettings(){clipboard.clear();for(int field=0;field<5;++field)clipboard.set(juce::Identifier("field"+juce::String(field)),processor.value(AlphaAudioProcessor::sceneId(editScene,module(),field)));clipboard.set("Mode",processor.effectiveMode(module()));for(const auto* suffix:{"On","Lo","Hi","MotionTarget"})clipboard.set(juce::Identifier(suffix),processor.value(moduleId()+suffix));clipboard.set("route",processor.value("mod"+juce::String(AlphaAudioProcessor::moduleNames[(size_t)module()])));for(const auto* suffix:{"Enabled","Shape","Rate","Sync","Division","Follow","Count"})clipboard.set(juce::Identifier("motion"+juce::String(suffix)),processor.value(AlphaAudioProcessor::motionId(module(),suffix)));for(int i=0;i<32;++i)for(const auto* prefix:{"X","Y","Bend"}){const auto suffix=juce::String(prefix)+juce::String(i);clipboard.set(juce::Identifier("motion"+suffix),processor.value(AlphaAudioProcessor::motionId(module(),suffix)));}clipboardReady=true;}
    void pasteSettings(){if(!clipboardReady)return;for(int field=0;field<5;++field)processor.setValue(AlphaAudioProcessor::sceneId(editScene,module(),field),(float)clipboard[juce::Identifier("field"+juce::String(field))]);processor.setModuleMode(module(),(int)clipboard["Mode"]);for(const auto* suffix:{"On","Lo","Hi","MotionTarget"})processor.setValue(moduleId()+suffix,(float)clipboard[juce::Identifier(suffix)]);processor.setValue("mod"+juce::String(AlphaAudioProcessor::moduleNames[(size_t)module()]),(float)clipboard["route"]);for(const auto* suffix:{"Enabled","Shape","Rate","Sync","Division","Follow","Count"})processor.setValue(AlphaAudioProcessor::motionId(module(),suffix),(float)clipboard[juce::Identifier("motion"+juce::String(suffix))]);for(int i=0;i<32;++i)for(const auto* prefix:{"X","Y","Bend"}){const auto suffix=juce::String(prefix)+juce::String(i);processor.setValue(AlphaAudioProcessor::motionId(module(),suffix),(float)clipboard[juce::Identifier("motion"+suffix)]);}update();}
    bool selected=false,locked=false;std::function<void(int)> onSelect;std::function<void()> beforeChange;
private:
    AlphaAudioProcessor& processor;int index,editScene=0;juce::TextButton on,lock;GestureSlider amount;std::unique_ptr<SliderAttachment> attachment;
    bool dragging=false,fine=false;float lastY=0,baseline=0;double dragValue=0;std::array<float,2> baselineAmounts{};inline static juce::NamedValueSet clipboard;inline static bool clipboardReady=false;
};

class SpectrumView final : public juce::Component,public juce::SettableTooltipClient {
public:
    explicit SpectrumView(AlphaAudioProcessor& p):processor(p),fft((int)std::log2(alpha::Engine::analyserSize)),window(alpha::Engine::analyserSize,juce::dsp::WindowingFunction<float>::hann) {
        lastBands.fill({40,2400});setTitle("Frequency focus and output spectrum");setTooltip("Click a coloured band to select it. Drag vertically for amount, horizontally to move its frequency range. Drag the edge handles for low/high frequency; Shift is fine. Double-click toggles full band.");
    }
    void update() {
        std::array<float,alpha::Engine::analyserSize> samples{};processor.engine.copyAnalyser(samples);
        std::copy(samples.begin(),samples.end(),fftData.begin());std::fill(fftData.begin()+alpha::Engine::analyserSize,fftData.end(),0);
        window.multiplyWithWindowingTable(fftData.data(),alpha::Engine::analyserSize);fft.performFrequencyOnlyForwardTransform(fftData.data());
        for(size_t i=0;i<levels.size();++i){const float f=20*std::pow(1000.0f,(float)i/(float)(levels.size()-1));const float bin=f*alpha::Engine::analyserSize/(float)juce::jmax(8000.0,processor.getSampleRate());
            float magnitude=0;if(bin<alpha::Engine::analyserSize/2){const int b=juce::jlimit(0,alpha::Engine::analyserSize/2-2,(int)bin);magnitude=juce::jmax(fftData[(size_t)b],fftData[(size_t)b+1])/(alpha::Engine::analyserSize*.25f);}
            const float v=juce::jlimit(0.0f,1.0f,(juce::Decibels::gainToDecibels(juce::jmax(magnitude,1e-6f))+72)/72);levels[i]=juce::jmax(v,levels[i]-.035f);
        }repaint();
    }
    float amount(int i) const {
        const float morph=processor.value("morph")*.01f;const int m=modules[(size_t)i];
        float value=juce::jmap(morph,processor.value(AlphaAudioProcessor::sceneId(0,m)),processor.value(AlphaAudioProcessor::sceneId(1,m)));if(processor.value(juce::String(AlphaAudioProcessor::moduleIds[(size_t)m])+"MotionTarget")<.5f)value*=1+processor.engine.getLfoValue(m)*processor.value("mod"+juce::String(AlphaAudioProcessor::moduleNames[(size_t)m]))*.0001f*processor.value("lfoDepth");return juce::jlimit(0.0f,100.0f,value)*processor.value("character")*.01f;
    }
    juce::String bandId(int i,const char* suffix) const{return juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)i]])+suffix;}
    float frequency(int i,bool high) const {return juce::jlimit(20.0f,20000.0f,processor.value(bandId(i,high?"Hi":"Lo")));}
    void paint(juce::Graphics& g) override {
        const bool bypass=processor.value("bypass")>.5f;juce::Graphics::ScopedSaveState save(g);if(bypass)g.setOpacity(.35f);
        const std::array<float,10> frequencies {20,50,100,200,500,1000,2000,5000,10000,20000};
        for(size_t i=0;i<frequencies.size();++i){const float x=frequencyX(frequencies[i]);if(i>0&&i<9){g.setColour(juce::Colour(0xff1c1f1e));g.drawVerticalLine((int)x,56,328);}
            monoText(g,i==9?"20k Hz":frequencies[i]>=1000?juce::String((int)(frequencies[i]/1000))+"k":juce::String((int)frequencies[i]),{(int)x-(i==0?0:i==9?50:24),338,i==9?50:48,20},11,screenMuted,i==0?juce::Justification::centredLeft:i==9?juce::Justification::centredRight:juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff2a2e2c));g.drawHorizontalLine(328,56,1128);
        juce::Path live;live.startNewSubPath(56,328);for(size_t i=0;i<levels.size();++i)live.lineTo(56+1072*(float)i/(float)(levels.size()-1),328-levels[i]*220);live.lineTo(1128,328);live.closeSubPath();g.setColour(light.withAlpha(.07f));g.fillPath(live);g.setColour(light.withAlpha(.4f));stroke(g,live,1.2f);
        for(int pass=0;pass<2;++pass)for(int i=bank*6;i<bank*6+6;++i){if((i==selected)!=(pass==1))continue;drawBand(g,i);}
        const auto c=colours[(size_t)selected];const float lo=frequencyX(frequency(selected,false)),hi=frequencyX(frequency(selected,true));
        const auto amountText=juce::String(juce::roundToInt(amount(selected))),rangeText=frequencyText(frequency(selected,false))+juce::String::fromUTF8(" – ")+frequencyText(frequency(selected,true));
        const int nameWidth=(int)std::ceil(juce::GlyphArrangement::getStringWidth(font(12,true),names[(size_t)selected]))+1,valueWidth=(int)std::ceil(juce::GlyphArrangement::getStringWidth(mono(13),amountText))+1,rangeWidth=(int)std::ceil(juce::GlyphArrangement::getStringWidth(mono(11),rangeText))+1,chipWidth=24+20+nameWidth+valueWidth+rangeWidth;
        const float y=juce::jmax(65.0f,328-amount(selected)*2.72f-47),x=juce::jlimit(64.0f,1128.0f-chipWidth,lo+16);
        g.setColour(c);g.fillRoundedRectangle(x,y,(float)chipWidth,30,8);text(g,names[(size_t)selected],{(int)x+12,(int)y,nameWidth,30},12,ink,juce::Justification::centredLeft,true);
        monoText(g,amountText,{(int)x+22+nameWidth,(int)y,valueWidth,30},13,ink);monoText(g,rangeText,{(int)x+32+nameWidth+valueWidth,(int)y,rangeWidth,30},11,ink);
        if(dragKind!=0){g.setColour(c.withAlpha(.35f));g.drawVerticalLine((int)(dragKind==2?hi:lo),56,328);}
        meter(g,"in",processor.engine.getInputPeak(),912,screenMuted);meter(g,"out",processor.engine.getOutputPeak(),1023,light);
        if(bypass)monoText(g,"BYPASSED",{760,12,135,22},11,light,juce::Justification::centredRight);
        if(processor.value("delta")>.5f)monoText(g,juce::String::fromUTF8("Δ"),{866,13,30,20},12,light);
    }
    void mouseMove(const juce::MouseEvent& e) override {const float lo=frequencyX(frequency(selected,false)),hi=frequencyX(frequency(selected,true));setMouseCursor(std::abs(e.position.x-lo)<8||std::abs(e.position.x-hi)<8?juce::MouseCursor::LeftRightResizeCursor:juce::MouseCursor::DraggingHandCursor);}
    void mouseDown(const juce::MouseEvent& e) override {
        if(e.position.y<56||e.position.y>328)return;
        const float oldLo=frequencyX(frequency(selected,false)),oldHi=frequencyX(frequency(selected,true));
        if(std::abs(e.position.x-oldLo)<8)dragKind=1;else if(std::abs(e.position.x-oldHi)<8)dragKind=2;else{dragKind=3;int closest=selected;float distance=1000;
            for(int i=bank*6;i<bank*6+6;++i)if(e.position.x>=frequencyX(frequency(i,false))-57&&e.position.x<=frequencyX(frequency(i,true))+57){const float d=std::abs(e.position.y-(328-amount(i)*2.72f));if(d<distance){distance=d;closest=i;}}selected=closest;
        }
        if(onSelect)onSelect(selected);if(beforeChange)beforeChange();dragId=AlphaAudioProcessor::sceneId(scene,modules[(size_t)selected]);dragValue=processor.value(dragId);dragLo=frequency(selected,false);dragHi=frequency(selected,true);lastPoint=e.position;axis=0;
        for(const auto& id:{dragId,bandId(selected,"Lo"),bandId(selected,"Hi")})if(auto* p=processor.parameters.getParameter(id))p->beginChangeGesture();repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(dragKind==0)return;const float sensitivity=e.mods.isShiftDown()?.1f:1.0f;const auto delta=e.position-lastPoint;lastPoint=e.position;
        if(dragKind==1||dragKind==2){auto& f=dragKind==1?dragLo:dragHi;f*=std::pow(10.0f,delta.x*sensitivity/357.333333f);const float ratio=std::pow(2.0f,1.0f/3);f=dragKind==1?juce::jlimit(20.0f,dragHi/ratio,f):juce::jlimit(dragLo*ratio,20000.0f,f);setActual(bandId(selected,dragKind==1?"Lo":"Hi"),f);}
        else {if(axis==0&&e.getDistanceFromDragStart()>6)axis=std::abs(e.getDistanceFromDragStartX())>std::abs(e.getDistanceFromDragStartY())?1:2;
            if(axis==1||e.mods.isCommandDown()){const float ratio=std::pow(10.0f,delta.x*sensitivity/357.333333f);const float bounded=juce::jlimit(20.0f/dragLo,20000.0f/dragHi,ratio);dragLo*=bounded;dragHi*=bounded;setActual(bandId(selected,"Lo"),dragLo);setActual(bandId(selected,"Hi"),dragHi);}
            if(axis==2||e.mods.isCommandDown()){dragValue=juce::jlimit(0.0f,100.0f,dragValue-delta.y*sensitivity*.5f);setActual(dragId,dragValue);}
        }repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {if(dragKind)for(const auto& id:{dragId,bandId(selected,"Lo"),bandId(selected,"Hi")})if(auto* p=processor.parameters.getParameter(id))p->endChangeGesture();dragKind=0;dragId.clear();repaint();}
    void mouseDoubleClick(const juce::MouseEvent&) override {if(beforeChange)beforeChange();const auto n=(size_t)selected;if(frequency(selected,false)<=20.01f&&frequency(selected,true)>=19999){processor.setValue(bandId(selected,"Lo"),lastBands[n].first);processor.setValue(bandId(selected,"Hi"),lastBands[n].second);}else{lastBands[n]={frequency(selected,false),frequency(selected,true)};processor.setValue(bandId(selected,"Lo"),20);processor.setValue(bandId(selected,"Hi"),20000);}repaint();}
    int selected=2,scene=0,bank=0;std::function<void(int)> onSelect;std::function<void()> beforeChange;
private:
    static float frequencyX(float f){return 56+357.333333f*std::log10(juce::jlimit(20.0f,20000.0f,f)/20);}
    static juce::String frequencyText(float f){return f>=1000?juce::String(f/1000,f>=10000?0:1)+" kHz":juce::String(juce::roundToInt(f))+" Hz";}
    void setActual(const juce::String& id,float v){if(auto* p=processor.parameters.getParameter(id))p->setValueNotifyingHost(p->convertTo0to1(v));}
    void meter(juce::Graphics& g,const char* label,float peak,int x,juce::Colour c){monoText(g,label,{x,12,24,18},11,screenMuted);g.setColour(juce::Colour(0xff262a28));g.fillRoundedRectangle((float)x+29,18,64,4,2);g.setColour(peak>1?colours[2]:c);const float value=juce::jlimit(0.0f,1.0f,(juce::Decibels::gainToDecibels(juce::jmax(peak,1e-6f))+60)/60);g.fillRoundedRectangle((float)x+29,18,64*value,4,2);}
    void drawBand(juce::Graphics& g,int i){const bool chosen=i==selected;const auto c=colours[(size_t)i];const float y=328-amount(i)*2.72f;const float lo=frequencyX(frequency(i,false)),hi=frequencyX(frequency(i,true));juce::Path p;
        if(lo<=56)p.startNewSubPath(56,y);else{p.startNewSubPath(juce::jmax(56.0f,lo-57),328);p.cubicTo(lo-28,328,lo-28,y,lo,y);}p.lineTo(hi,y);if(hi<1128)p.cubicTo(hi+28,y,hi+28,328,juce::jmin(1128.0f,hi+57),328);
        const bool on=processor.value(bandId(i,"On"))>.5f;
        if(on){auto fill=p;fill.lineTo(1128,328);fill.lineTo(56,328);fill.closeSubPath();g.setColour(c.withAlpha(chosen?.28f:.16f));g.fillPath(fill);g.setColour(c);stroke(g,p,chosen?3.0f:2.0f);}
        else {juce::Path dashed;const float lengths[]{5,5};juce::PathStrokeType(1.5f).createDashedStroke(dashed,p,lengths,2);g.setColour(c.withAlpha(.4f));g.fillPath(dashed);}
        if(!chosen&&amount(i)>.01f)text(g,names[(size_t)i],{(int)lo+8,(int)y-23,100,18},11,c,juce::Justification::centredLeft,true);
        else {g.setColour(c);g.fillRoundedRectangle((lo+hi)*.5f-14,y-5,28,10,5);g.setColour(screen);g.drawRoundedRectangle((lo+hi)*.5f-14,y-5,28,10,5,2);
            const float handleY=(y+328)*.5f-20;for(float x:{lo,hi}){g.setColour(c);g.fillRoundedRectangle(x-4,handleY,8,40,4);g.setColour(screen);g.drawRoundedRectangle(x-4,handleY,8,40,4,2);}g.setColour(c);g.fillRoundedRectangle(lo,325,hi-lo,4,2);}
    }
    AlphaAudioProcessor& processor;juce::dsp::FFT fft;juce::dsp::WindowingFunction<float> window;std::array<float,alpha::Engine::analyserSize*2> fftData{};std::array<float,256> levels{};
    std::array<std::pair<float,float>,engineCount> lastBands{};juce::String dragId;float dragValue=0,dragLo=20,dragHi=20000;int dragKind=0,axis=0;juce::Point<float> lastPoint;
};
}

struct AlphaAudioProcessorEditor::Impl final : juce::Component,juce::Timer {
    explicit Impl(AlphaAudioProcessor& p):processor(p),spectrum(p),motion(p),presets(p),tooltip(this,650) {
        setLookAndFeel(&look);setWantsKeyboardFocus(true);
        for(int i=0;i<engineCount;++i){tiles[(size_t)i]=std::make_unique<EngineTile>(p,i);addAndMakeVisible(*tiles[(size_t)i]);tiles[(size_t)i]->onSelect=[this](int n){selectEngine(n);};tiles[(size_t)i]->beforeChange=[this]{remember();};}
        if(p.getCurrentProgram()==10){tiles[4]->locked=true;tiles[4]->update();}
        addAndMakeVisible(spectrum);addChildComponent(motion);addChildComponent(presets);
        spectrum.onSelect=[this](int n){selectEngine(n);};spectrum.beforeChange=[this]{remember();};motion.beforeChange=[this]{remember();};
        presets.onPreview=[this]{refresh();};presets.onClose=[this]{showView(0);};presets.onBeforeLoad=[this]{remember();};presets.onLoad=[this]{selectScene(0,false);lastProgram=processor.getCurrentProgram();refresh();};presets.onFile=[this](bool save){choosePresetFile(save);};
        button(prev,"", "icon", "prev");button(next,"", "icon", "next");button(preset,"PRESETS","preset");
        button(roll,"roll","roll");button(sceneA,"A","scene");button(sceneB,"B","scene");button(undo,"","icon","undo");button(redo,"","icon","redo");button(settings,"","icon","settings");button(bypass,"","icon","power");
        prev.onClick=[this]{stepPreset(-1);};next.onClick=[this]{stepPreset(1);};preset.onClick=[this]{showView(view==2?0:2);};
        roll.onClick=[this]{rollVariation();};sceneA.onClick=[this]{selectScene(0,true);};sceneB.onClick=[this]{selectScene(1,true);};
        undo.onClick=[this]{restore(false);};redo.onClick=[this]{restore(true);};settings.onClick=[this]{showSettings();};bypass.onClick=[this]{remember();processor.setValue("bypass",processor.value("bypass")>.5f?0:1);refresh();};
        preset.setTooltip("Open the preset browser. Factory sounds, search, save and load.");roll.setTooltip(juce::String::fromUTF8("Roll a variation within ±25% of unlocked engine settings. Undo restores the previous sound."));
        undo.setTooltip("Undo the last editor change.");redo.setTooltip("Redo.");settings.setTooltip("Presets, scenes and window scale.");bypass.setTooltip("Bypass all processing and hear the original input.");
        sceneA.setTooltip("Edit scene A and listen at its morph endpoint.");sceneB.setTooltip("Edit scene B and listen at its morph endpoint.");
        button(spectrumTab,"spectrum","nav");button(motionTab,"motion","nav");spectrumTab.onClick=[this]{showView(0);};motionTab.onClick=[this]{showView(1);};
        button(bankCore,"core","bank");button(bankExtra,"shapers","bank");bankCore.setTooltip("Show DUST, DRIFT, HEAT, GRIT, SPACE, TAPE");bankExtra.setTooltip("Show PITCH, TIME, VOLUME, FILTER, PAN, WIDTH");bankCore.onClick=[this]{selectEngine(0);};bankExtra.onClick=[this]{selectEngine(6);};
        button(main,"MAIN","tile");main.onClick=[this]{selectEngine(mainIndex);};
        for(auto& b:modes){button(b,"","mode");b.getProperties().set("mono",true);}
        for(int i=0;i<6;++i)modes[(size_t)i].onClick=[this,i]{if(selected>=engineCount)return;remember();processor.setModuleMode(modules[(size_t)selected],visibleModeIds[(size_t)i]);refresh();};
        button(enable,"on");enable.getProperties().set("mono",true);enable.onClick=[this]{if(selected<engineCount){remember();const auto id=juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)selected]])+"On";processor.setValue(id,processor.value(id)>.5f?0:1);refresh();}};
        button(lock,"lock");lock.getProperties().set("mono",true);lock.onClick=[this]{if(selected<engineCount){auto& tile=*tiles[(size_t)selected];tile.locked=!tile.locked;lock.setToggleState(tile.locked,juce::dontSendNotification);tile.repaint();}};
        for(auto& s:details){setupKnob(s,"Engine character.");s.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);addAndMakeVisible(s);s.onDragStart=[this]{remember();};}
        for(auto& label:values){addAndMakeVisible(label);label.setFont(mono(12));label.setJustificationType(juce::Justification::centred);label.setEditable(false,true,false);label.setColour(juce::Label::textColourId,ink);label.setColour(juce::TextEditor::backgroundColourId,panel);label.setColour(juce::TextEditor::textColourId,ink);label.setColour(juce::TextEditor::outlineColourId,ink);}
        for(size_t i=0;i<values.size();++i){values[i].onEditorShow=[this]{remember();};values[i].onTextChange=[this,i]{juce::Slider::ScopedDragNotification gesture(details[i]);details[i].setValue(details[i].getValueFromText(values[i].getText()),juce::sendNotificationSync);};details[i].onValueChange=[this,i]{updateDetailValue(i);};}
        setupKnob(character,"Scale the strength of all twelve engines.");character.setComponentID("character");character.setTitle("AMOUNT");character.getProperties().set("master",true);character.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);addAndMakeVisible(character);characterAttachment=attachSlider(p,"character",character);character.setDoubleClickReturnValue(true,100);character.onDragStart=[this]{remember();};
        setupKnob(mix,"Blend dry and processed signal.");mix.setComponentID("mix");mix.setTitle("MIX");mix.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);addAndMakeVisible(mix);mixAttachment=attachSlider(p,"mix",mix);mix.onDragStart=[this]{remember();};
        button(delta,juce::String::fromUTF8("Δ delta"));button(autoGain,"auto-gain");delta.getProperties().set("mono",true);autoGain.getProperties().set("mono",true);
        delta.onClick=[this]{remember();processor.setValue("delta",processor.value("delta")>.5f?0:1);refresh();};autoGain.onClick=[this]{remember();processor.setValue("autoGain",processor.value("autoGain")>.5f?0:1);refresh();};
        delta.setTooltip("Hear the difference between the dry input and processed signal.");autoGain.setTooltip("Match processed level to input with smooth, bounded RMS gain compensation.");
        lastProgram=processor.getCurrentProgram();selectScene(processor.value("morph")>=50?1:0,false);selectEngine(2);captureBaseline();showView(0);spectrum.update();motion.update();sendLookAndFeelChange();startTimerHz(60);
    }
    ~Impl() override{stopTimer();presets.endPreview();chooser.reset();setLookAndFeel(nullptr);}
    void button(juce::TextButton& b,juce::String label,juce::String variant="",juce::String icon="") {b.setButtonText(label);b.getProperties().set("variant",variant);if(icon.isNotEmpty())b.getProperties().set("icon",icon);b.setTitle(label.isNotEmpty()?label:icon=="prev"?"Previous preset":icon=="next"?"Next preset":icon=="power"?"Bypass":icon);addAndMakeVisible(b);}
    void paint(juce::Graphics& g) override {
        g.setColour(paper);g.fillRoundedRectangle(getLocalBounds().toFloat(),18);
        // Reserve the original brand group width so alpha does not move the preset controls.
        drawLogo(g);text(g,"alpha",{55,14,110,36},20,ink,juce::Justification::centredLeft,true);
        {juce::Graphics::ScopedSaveState state(g);juce::Path clip;clip.addRoundedRectangle(940.5f,16.5f,69,31,8);g.reduceClipRegion(clip);g.setColour(ink);g.fillRect(editScene==0?941:975,17,34,30);}g.setColour(ink);g.drawRoundedRectangle(940.5f,16.5f,69,31,8,1);
        // The preset button provides the hit area; its actual name and category stay visible here.
        g.setColour(panel);g.fillRoundedRectangle(195,14,270,36,8);g.setColour(rule);g.drawRoundedRectangle(195.5f,14.5f,269,35,8,1);
        text(g,processor.getProgramName(processor.getCurrentProgram()),{207,15,158,34},14,ink,juce::Justification::centredLeft,true);
        if(modified&&!presets.isPreviewing()){g.setColour(ink);const float width=juce::GlyphArrangement::getStringWidth(font(14,true),processor.getProgramName(processor.getCurrentProgram()));g.fillEllipse(207+juce::jmin(161.0f,width)+6,29,5,5);}
        monoText(g,processor.getProgramName(processor.getCurrentProgram())=="Sunday Tape Loop"?juce::String::fromUTF8("lo-fi · keys"):AlphaAudioProcessor::presetCategory(processor.getCurrentProgram()).toLowerCase(),{361,15,78,34},11,muted,juce::Justification::centredRight);drawIcon(g,"chevron",{444,26,12,12},ink);
        g.setColour(screen);g.fillRoundedRectangle(24,64,1152,376,14);
        if(selected==mainIndex){g.setColour(juce::Colours::white);g.fillRoundedRectangle(1032,456,144,100,12);g.setColour(ink);g.drawRoundedRectangle(1033,457,142,98,12,2);}else{g.setColour(panel);g.fillRoundedRectangle(1032,456,144,100,12);}
        monoText(g,"M",{1044,467,18,18},11);text(g,"MAIN",{1069,466,90,20},14,ink,juce::Justification::centredLeft,true);
        monoText(g,juce::String::fromUTF8("flux · tone"),{1044,492,120,16},11);monoText(g,juce::String::fromUTF8("width · out"),{1044,508,120,16},11);
        for(auto r:{juce::Rectangle<float>(24,572,232,204),{272,572,600,204},{888,572,288,204}}){g.setColour(panel);g.fillRoundedRectangle(r,14);}
        const auto c=selected<engineCount?colours[(size_t)selected]:ink;
        g.setColour(c);g.fillEllipse(39,592,12,12);g.setColour(ink);g.drawEllipse(39,592,12,12,1.5f);
        monoText(g,selected<engineCount?juce::String(selected+1).paddedLeft('0',2)+juce::String::fromUTF8(" · ")+descriptions[(size_t)selected]:juce::String::fromUTF8("M · global controls"),{59,588,180,22},11);
        text(g,selected<engineCount?names[(size_t)selected]:"MAIN",{38,613+(int)std::round((1-selectionProgress)*6),200,35},28,ink,juce::Justification::centredLeft,true);
        if(selected==mainIndex){monoText(g,"Global character",{46,658,196,23},12);monoText(g,"Flux, tone, width & output",{46,685,196,23},11);monoText(g,"For the whole signal chain",{46,712,196,23},10);}
        text(g,(selected<engineCount?juce::String(names[(size_t)selected]):"MAIN")+juce::String::fromUTF8(" · CHARACTER"),{288,583,560,22},10.5f,muted,juce::Justification::centredLeft,true);
        for(size_t i=0;i<4;++i){const int x=288+(int)i*144;text(g,detailNames[i],{x,690,136,20},13,ink,juce::Justification::centred,true);text(g,detailHints[i],{x,730,136,14},11,muted,juce::Justification::centred);}
        text(g,"AMOUNT",{904,584,160,20},10.5f,muted,juce::Justification::centredLeft,true);text(g,"MIX",{1071,661,88,18},10.5f,muted,juce::Justification::centred,true);monoText(g,mix.getTextFromValue(mix.getValue()),{1071,679,88,18},12,ink,juce::Justification::centred);
        if(statusTicks>0)monoText(g,status,{658,20,248,24},10,muted,juce::Justification::centredRight);
    }
    void paintOverChildren(juce::Graphics& g) override {
        // Header preset uses a transparent native button with an accessible name.
        if(view==2){g.setColour(rule);g.drawRoundedRectangle(195.5f,14.5f,269,35,8,1);}
    }
    void resized() override {
        prev.setBounds(155,16,32,32);preset.setBounds(195,14,270,36);next.setBounds(473,16,32,32);roll.setBounds(521,14,96,36);
        sceneA.setBounds(941,17,34,30);sceneB.setBounds(975,17,34,30);undo.setBounds(1024,16,32,32);redo.setBounds(1064,16,32,32);settings.setBounds(1104,16,32,32);bypass.setBounds(1144,16,32,32);
        spectrum.setBounds(24,64,1152,376);motion.setBounds(24,64,1152,376);presets.setBounds(24,64,1152,376);
        spectrumTab.setBounds(40,76,67,26);motionTab.setBounds(126,76,view==1?123:55,26);bankCore.setBounds(356,77,62,24);bankExtra.setBounds(426,77,74,24);
        for(int i=0;i<engineCount;++i){tiles[(size_t)i]->setBounds(24+(i%6)*168,456,160,100);tiles[(size_t)i]->setVisible(i/6==bank);}main.setBounds(1032,456,144,100);
        for(int i=0;i<6;++i)modes[(size_t)i].setBounds(38,654+i*24,204,23);
        enable.setBounds(38,739,98,25);lock.setBounds(144,739,98,25);
        for(size_t i=0;i<4;++i){const int x=288+(int)i*144;details[i].setBounds(x+30,612,76,76);values[i].setBounds(x+6,709,124,20);}
        character.setBounds(904,608,148,148);mix.setBounds(1089,607,52,52);delta.setBounds(1070,704,90,28);autoGain.setBounds(1070,740,90,28);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        const auto c=key.getTextCharacter();if(c>='1'&&c<='6'){selectEngine(bank*6+c-'1');return true;}if(c=='m'||c=='M'){selectEngine(mainIndex);return true;}
        if(key==juce::KeyPress::escapeKey){showView(0);return true;}if(key==juce::KeyPress::tabKey){showView(view==1?0:1);return true;}
        if((c=='z'||c=='Z')&&key.getModifiers().isCommandDown()){restore(key.getModifiers().isShiftDown());return true;}
        if((c=='l'||c=='L')&&selected<engineCount){if(lock.onClick)lock.onClick();return true;}if((c=='s'||c=='S')&&key.getModifiers().isCommandDown()){choosePresetFile(true);return true;}if(c=='r'||c=='R'){rollVariation();return true;}if((c=='o'||c=='O')&&selected<engineCount){if(enable.onClick)enable.onClick();return true;}return false;
    }
private:
    void drawLogo(juce::Graphics& g){for(int i=0;i<6;++i){juce::Path p;const float start=(float)i*juce::MathConstants<float>::twoPi/6+.1f;p.addCentredArc(35,32,9,9,0,start,start+.84f,true);g.setColour(colours[(size_t)i]);stroke(g,p,3.6f);}g.setColour(ink);g.fillEllipse(32.4f,29.4f,5.2f,5.2f);}
    void updateDetailValue(size_t i){if(!values[i].isBeingEdited())values[i].setText(details[i].getTextFromValue(details[i].getValue()),juce::dontSendNotification);}
    void bindDetails(){
        for(auto& a:detailAttachments)a.reset();
        juce::StringArray ids;
        if(selected<engineCount){const int m=modules[(size_t)selected];auto id=juce::String(AlphaAudioProcessor::moduleIds[(size_t)m]);ids.add(AlphaAudioProcessor::sceneId(editScene,m,1));ids.add(AlphaAudioProcessor::sceneId(editScene,m,2));ids.add(AlphaAudioProcessor::sceneId(editScene,m,3));ids.add(AlphaAudioProcessor::sceneId(editScene,m,4));
            detailNames={AlphaAudioProcessor::toneName(m).toLowerCase(),AlphaAudioProcessor::detailName(m).toLowerCase(),AlphaAudioProcessor::thirdName(m).toLowerCase(),AlphaAudioProcessor::fourthName(m).toLowerCase()};
            detailHints={juce::String::fromUTF8("dark ↔ bright"),juce::String::fromUTF8("subtle ↔ character"),juce::String::fromUTF8("soft ↔ hard"),juce::String::fromUTF8("narrow ↔ wide")};
            if(m==1)detailHints={juce::String::fromUTF8("slow ↔ fast"),juce::String::fromUTF8("steady ↔ drifting"),juce::String::fromUTF8("mono ↔ stereo"),juce::String::fromUTF8("steady ↔ random")};
            if(m==3)detailHints={juce::String::fromUTF8("dark ↔ bright"),juce::String::fromUTF8("even ↔ odd"),juce::String::fromUTF8("soft ↔ hard"),juce::String::fromUTF8("stiff ↔ breathing")};
            if(m==4)detailHints={juce::String::fromUTF8("small ↔ large"),juce::String::fromUTF8("short ↔ long"),juce::String::fromUTF8("dark ↔ bright"),juce::String::fromUTF8("close ↔ distant")};
            if(m==5)detailHints={juce::String::fromUTF8("crushed ↔ clean"),juce::String::fromUTF8("stepped ↔ smooth"),juce::String::fromUTF8("raw ↔ smooth"),juce::String::fromUTF8("even ↔ odd")};

            auto options=AlphaAudioProcessor::modeNames(m);visibleModeIds=m==3?std::array<int,6>{1,5,3,2,4,0}:std::array<int,6>{0,1,2,3,4,5};const int visibleCount=m==3?5:juce::jmin(6,options.size());for(int i=0;i<6;++i){const bool visible=i<visibleCount&&visibleModeIds[(size_t)i]<options.size();modes[(size_t)i].setVisible(visible);if(visible)modes[(size_t)i].setButtonText(options[visibleModeIds[(size_t)i]].toLowerCase());}enable.setVisible(false);lock.setVisible(false);
        }else{ids={"flux","mainTone","mainWidth","output"};detailNames={"flux","tone","width","output"};detailHints={juce::String::fromUTF8("stable ↔ alive"),juce::String::fromUTF8("dark ↔ bright"),juce::String::fromUTF8("mono ↔ wide"),"final level"};for(auto& b:modes)b.setVisible(false);enable.setVisible(false);lock.setVisible(false);}
        for(size_t i=0;i<4;++i){details[i].setTooltip(detailNames[i]+". Double-click to reset; shift-drag for fine control. Double-click the value to type.");details[i].setComponentID(ids[(int)i]);details[i].setTitle(detailNames[i]);detailAttachments[i]=attachSlider(processor,ids[(int)i],details[i]);
            if(selected<engineCount){const int module=modules[(size_t)selected],field=(int)i+1;details[i].textFromValueFunction=[module,field](double value){return AlphaAudioProcessor::parameterValueText(module,field,(float)value);};details[i].valueFromTextFunction=[module,field](const juce::String& value){return (double)AlphaAudioProcessor::parameterValueFromText(module,field,value.replaceCharacter((juce::juce_wchar)0x2212,'-'));};}
            if(selected<engineCount)details[i].setDoubleClickReturnValue(true,baselineValues[(size_t)editScene][(size_t)selected][i]);details[i].getProperties().set("bipolar",(selected==mainIndex&&i==1)||(detailNames[i]=="tone")||(detailNames[i]=="bias")||(detailNames[i]=="transpose")||(detailNames[i]=="fine")||(detailNames[i]=="position"));updateDetailValue(i);}
    }
    void captureBaseline(){modified=false;for(auto& t:tiles)t->captureBaseline();for(int scene=0;scene<2;++scene)for(int ui=0;ui<engineCount;++ui)for(int field=0;field<4;++field)baselineValues[(size_t)scene][(size_t)ui][(size_t)field]=processor.value(AlphaAudioProcessor::sceneId(scene,modules[(size_t)ui],field+1));for(auto& slider:details)slider.setDoubleClickReturnValue(true,processor.value(slider.getComponentID()));}
    void selectEngine(int n){markActivity();if(selected!=n)selectionTime=juce::Time::getMillisecondCounterHiRes();selected=juce::jlimit(0,mainIndex,n);for(int i=0;i<engineCount;++i){tiles[(size_t)i]->selected=i==selected;tiles[(size_t)i]->repaint();}if(selected<engineCount){bank=selected/6;spectrum.bank=bank;spectrum.selected=selected;motion.setTarget(selected);resized();bankCore.setToggleState(bank==0,juce::dontSendNotification);bankExtra.setToggleState(bank==1,juce::dontSendNotification);if(view==1)motionTab.setButtonText(juce::String::fromUTF8("motion · ")+names[(size_t)selected]);}bindDetails();refresh();}
    void selectScene(int scene,bool listen){if(listen)remember();editScene=scene;for(auto& t:tiles)t->setScene(scene);spectrum.scene=scene;bindDetails();if(listen)processor.setValue("morph",scene==0?0:100);refresh();}
    void showView(int n){markActivity();if(view!=n)viewTime=juce::Time::getMillisecondCounterHiRes();if(n!=2)presets.endPreview();view=n;motionTab.setButtonText(n==1?juce::String::fromUTF8("motion · ")+names[(size_t)spectrum.selected]:"motion");motionTab.setBounds(126,76,n==1?123:55,26);spectrum.setVisible(n==0);motion.setVisible(n==1);presets.setVisible(n==2);spectrumTab.setVisible(n!=2);motionTab.setVisible(n!=2);bankCore.setVisible(n!=2);bankExtra.setVisible(n!=2);spectrumTab.setToggleState(n==0,juce::dontSendNotification);motionTab.setToggleState(n==1,juce::dontSendNotification);if(n==2)presets.selectCurrent();repaint();}
    void refresh(){spectrumTab.setColour(juce::TextButton::buttonColourId,selected<engineCount?colours[(size_t)selected]:ink);motionTab.setColour(juce::TextButton::buttonColourId,selected<engineCount?colours[(size_t)selected]:ink);sceneA.setToggleState(editScene==0,juce::dontSendNotification);sceneB.setToggleState(editScene==1,juce::dontSendNotification);bypass.setToggleState(processor.value("bypass")>.5f,juce::dontSendNotification);delta.setToggleState(processor.value("delta")>.5f,juce::dontSendNotification);autoGain.setToggleState(processor.value("autoGain")>.5f,juce::dontSendNotification);undo.setEnabled(!undoStates.empty());redo.setEnabled(!redoStates.empty());for(auto& t:tiles)t->update();
        if(selected<engineCount){const auto id=juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)selected]]);for(int i=0;i<6;++i)modes[(size_t)i].setToggleState(processor.effectiveMode(modules[(size_t)selected])==visibleModeIds[(size_t)i],juce::dontSendNotification);enable.setToggleState(processor.value(id+"On")>.5f,juce::dontSendNotification);enable.setButtonText(enable.getToggleState()?"on":"bypassed");lock.setToggleState(tiles[(size_t)selected]->locked,juce::dontSendNotification);lock.setButtonText(lock.getToggleState()?"locked":"lock");}
        for(size_t i=0;i<4;++i){updateDetailValue(i);values[i].setEnabled(true);details[i].setEnabled(true);details[i].getProperties().remove("modLow");details[i].getProperties().set("engineOff",selected<engineCount&&processor.value(juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)selected]])+"On")<.5f);
            if(selected<engineCount){const int module=modules[(size_t)selected];if((int)processor.value(juce::String(AlphaAudioProcessor::moduleIds[(size_t)module])+"MotionTarget")==int(i)+1){const float depth=processor.value("mod"+juce::String(AlphaAudioProcessor::moduleNames[(size_t)module]))*processor.value("lfoDepth")*.0001f,base=(float)details[i].getValue()*.01f;const auto range=motionWaveRange(processor,module);if(std::abs(depth)>.00001f){const float lo=base+range.first*depth,hi=base+range.second*depth;details[i].getProperties().set("modLow",juce::jlimit(0.0f,1.0f,juce::jmin(lo,hi)));details[i].getProperties().set("modHigh",juce::jlimit(0.0f,1.0f,juce::jmax(lo,hi)));details[i].getProperties().set("modColour",colours[(size_t)selected].toString());details[i].getProperties().set("modLive",juce::jlimit(0.0f,1.0f,base+processor.engine.getLfoValue(module)*depth));}}}
        }repaint();
    }
    void markActivity(){lastActivity=juce::Time::getMillisecondCounterHiRes();if(timerHz!=60){timerHz=60;startTimerHz(60);}}
    void timerCallback() override {const double now=juce::Time::getMillisecondCounterHiRes();if(!isShowing()){if(timerHz!=10){timerHz=10;startTimerHz(10);}return;}const bool signal=processor.engine.getInputPeak()>1.0e-5f||processor.engine.getOutputPeak()>1.0e-5f;const bool gesture=juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown();if(signal||gesture)lastActivity=now;const int desiredHz=now-lastActivity>2000?10:60;if(timerHz!=desiredHz){timerHz=desiredHz;startTimerHz(timerHz);}selectionProgress=reducedMotion?1.0f:juce::jlimit(0.0f,1.0f,(float)((now-selectionTime)/180));const float viewProgress=reducedMotion?1.0f:juce::jlimit(0.0f,1.0f,(float)((now-viewTime)/140));for(size_t i=0;i<4;++i){details[i].setAlpha(selectionProgress);values[i].setAlpha(selectionProgress);}spectrum.setAlpha(viewProgress);motion.setAlpha(viewProgress);presets.setAlpha(viewProgress);const int program=processor.getCurrentProgram();if(program!=lastProgram&&!presets.isPreviewing()){lastProgram=program;selectScene(processor.value("morph")>=50?1:0,false);captureBaseline();presets.selectCurrent();}if(isShowing()){if(view==0)spectrum.update();if(view==1)motion.update();}if(statusTicks>0)--statusTicks;refresh();}
    juce::MemoryBlock state(){juce::MemoryBlock b;processor.getStateInformation(b);return b;}
    void remember(){markActivity();modified=true;auto b=state();if(undoStates.empty()||undoStates.back().data!=b||undoStates.back().scene!=editScene){undoStates.push_back({std::move(b),editScene});if(undoStates.size()>200)undoStates.pop_front();}redoStates.clear();}
    void restore(bool redoAction){auto& from=redoAction?redoStates:undoStates;auto& to=redoAction?undoStates:redoStates;if(from.empty())return;to.push_back({state(),editScene});const auto entry=from.back();from.pop_back();processor.setStateInformation(entry.data.getData(),(int)entry.data.getSize());lastProgram=processor.getCurrentProgram();selectScene(entry.scene,false);presets.selectCurrent();refresh();}
    void stepPreset(int step){remember();const int n=processor.getNumPrograms();processor.setCurrentProgram((processor.getCurrentProgram()+step+n)%n);lastProgram=processor.getCurrentProgram();selectScene(0,false);captureBaseline();presets.selectCurrent();refresh();}
    void rollVariation(){remember();for(int i=0;i<engineCount;++i)if(!tiles[(size_t)i]->locked)for(int j=0;j<5;++j){auto id=AlphaAudioProcessor::sceneId(editScene,modules[(size_t)i],j);processor.setValue(id,juce::jlimit(0.0f,100.0f,processor.value(id)+(random.nextFloat()*2-1)*25));}status=juce::String::fromUTF8("Variation rolled · undo to restore");statusTicks=90;refresh();}
    void showSettings(){juce::PopupMenu menu;menu.addItem(1,juce::String::fromUTF8("Save sound…"));menu.addItem(2,juce::String::fromUTF8("Open sound…"));menu.addSeparator();menu.addItem(3,editScene==0?"Copy A to B":"Copy B to A");menu.addItem(4,"Swap scenes A / B");menu.addSeparator();juce::PopupMenu scales;scales.addItem(75,"75%");scales.addItem(100,"100%");scales.addItem(125,"125%");scales.addItem(150,"150%");scales.addItem(200,"200%");menu.addSubMenu("Window size",scales);menu.addItem(5,"Reduce motion",true,reducedMotion);
        auto safe=juce::Component::SafePointer<Impl>(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settings),[safe](int result){if(!safe)return;if(result==1||result==2)safe->choosePresetFile(result==1);else if(result==3){safe->remember();safe->processor.copyScene(safe->editScene,1-safe->editScene);safe->refresh();}else if(result==4){safe->remember();safe->processor.swapScenes();safe->refresh();}else if(result==5){safe->reducedMotion=!safe->reducedMotion;}else if(result>=75){if(auto* editor=safe->findParentComponentOfClass<AlphaAudioProcessorEditor>())editor->setSize(designWidth*result/100,designHeight*result/100);}});
    }
    void choosePresetFile(bool save){if(save){const auto result=processor.userPresetDirectory().createDirectory();if(result.failed()){status="Cannot create preset directory";statusTicks=120;return;}}
        chooser=std::make_unique<juce::FileChooser>(save?"Save alpha sound":"Open alpha sound",save?processor.userPresetDirectory().getChildFile("My sound.alphapreset"):processor.userPresetDirectory(),"*.alphapreset",true);
        auto safe=juce::Component::SafePointer<Impl>(this);const int flags=save?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles);
        chooser->launchAsync(flags,[safe,save](const juce::FileChooser& dialog){if(!safe)return;auto f=dialog.getResult();if(f==juce::File())return;if(save)f=f.withFileExtension("alphapreset");else safe->remember();const bool ok=save?safe->processor.saveUserPreset(f):safe->processor.loadUserPreset(f);if(ok&&save)safe->modified=false;safe->status=ok?(save?"Saved ":"Opened ")+f.getFileNameWithoutExtension():"Could not "+juce::String(save?"save":"open")+" sound";safe->statusTicks=120;if(ok&&!save){safe->lastProgram=safe->processor.getCurrentProgram();safe->selectScene(safe->processor.value("morph")>=50?1:0,false);}safe->presets.selectCurrent();safe->refresh();});
    }
    AlphaAudioProcessor& processor;PatinaLookAndFeel look;SpectrumView spectrum;patina::MotionView motion;patina::PresetView presets;
    std::array<std::unique_ptr<EngineTile>,engineCount> tiles;
    juce::TextButton prev,next,preset,roll,sceneA,sceneB,undo,redo,settings,bypass,spectrumTab,motionTab,bankCore,bankExtra,main,enable,lock,delta,autoGain;
    std::array<juce::TextButton,6> modes;std::array<int,6> visibleModeIds{0,1,2,3,4,5};std::array<GestureSlider,4> details;std::array<juce::Label,4> values;
    std::array<juce::String,4> detailNames,detailHints;std::array<std::unique_ptr<SliderAttachment>,4> detailAttachments;
    GestureSlider character,mix;std::unique_ptr<SliderAttachment> characterAttachment,mixAttachment;
    juce::TooltipWindow tooltip;std::unique_ptr<juce::FileChooser> chooser;juce::Random random;
    struct HistoryState {juce::MemoryBlock data;int scene;};
    std::deque<HistoryState> undoStates,redoStates;int selected=2,editScene=0,view=0,bank=0,statusTicks=0,lastProgram=0;int timerHz=60;double lastActivity=0,selectionTime=-1000,viewTime=-1000;float selectionProgress=1;bool reducedMotion=false,modified=false;juce::String status;std::array<std::array<std::array<float,4>,engineCount>,2> baselineValues{};
};

AlphaAudioProcessorEditor::AlphaAudioProcessorEditor(AlphaAudioProcessor& p):AudioProcessorEditor(p),impl(std::make_unique<Impl>(p)) {
    addAndMakeVisible(*impl);setResizable(true,true);setResizeLimits(900,600,2400,1600);
    if(auto* c=getConstrainer())c->setFixedAspectRatio((double)designWidth/designHeight);setSize(designWidth,designHeight);
}
AlphaAudioProcessorEditor::~AlphaAudioProcessorEditor()=default;
void AlphaAudioProcessorEditor::paint(juce::Graphics& g){g.fillAll(patina::paper);}
void AlphaAudioProcessorEditor::resized(){if(impl){impl->setBounds(0,0,designWidth,designHeight);impl->setTransform(juce::AffineTransform::scale((float)getWidth()/designWidth,(float)getHeight()/designHeight));}}
