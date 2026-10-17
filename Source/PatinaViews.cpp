#include "PatinaUI.h"
#include "MotionCurve.h"
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
    if(shape==4)return phase*2-1;
    if(shape==5)return 1-phase*2;
    if(shape==6)return std::sin(juce::MathConstants<float>::twoPi*phase)*.65f+std::sin(juce::MathConstants<float>::twoPi*phase*3)*.25f;
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
        if(shape==7){path.startNewSubPath(r.getX()+1,r.getBottom()-1);path.lineTo(r.getX()+3,r.getCentreY()+1);path.lineTo(r.getRight()-3,r.getY()+1);path.lineTo(r.getRight()-1,r.getY()+3);path.lineTo(r.getX()+4,r.getBottom()-1);path.closeSubPath();}
        else if (shape == 3) {
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

void signature(juce::Graphics& g, juce::Rectangle<float> r, int program) {
    g.setColour(screen);g.fillRoundedRectangle(r,5);const auto values=AlphaAudioProcessor::presetSignature(program);bool extras=false;for(int i=6;i<engineCount;++i)extras=extras||values[(size_t)modules[(size_t)i]]>0;
    const int count=extras?engineCount:6;const float bar=extras?2.0f:4.0f,gap=extras?1.0f:2.0f;
    for(int i=0;i<count;++i){const float value=values[(size_t)modules[(size_t)i]],height=juce::jmax(2.0f,value*.31f);g.setColour(value>0?colours[(size_t)i]:graphRule);g.fillRoundedRectangle(r.getX()+5+(float)i*(bar+gap),r.getBottom()-4-height,bar,height,1);}
}
}

struct MotionView::Impl final : juce::Component,juce::SettableTooltipClient {
    Impl(MotionView& o, AlphaAudioProcessor& p) : owner(o), processor(p) {
        const juce::StringArray labels {"Sine","Triangle","Square","Random","Ramp up","Ramp down","Smooth random","Custom curve"};
        for(int i=0;i<8;++i){shapes[(size_t)i]=std::make_unique<ShapeButton>(i);auto& b=*shapes[(size_t)i];addAndMakeVisible(b);b.setTooltip(labels[i]);b.setTitle(labels[i]);b.onClick=[this,i]{if(owner.beforeChange)owner.beforeChange();setMotion("Shape",(float)i);update();};}
        for(int i=0;i<7;++i){auto& b=rates[(size_t)i];b.getProperties().set("variant","dark");b.setButtonText(juce::StringArray{"1/16","1/8","1/4","1/2","1","2","4 bar"}[i]);b.setTooltip("One cycle every "+b.getButtonText());addAndMakeVisible(b);b.onClick=[this,i]{if(owner.beforeChange)owner.beforeChange();setMotion("Division",(float)(6-i));setMotion("Sync",1);update();};}
        for(auto* slider:{&rate,&depth}){setupLinear(*slider,colours[2],{});darkControl(*slider);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,50,28);slider->onDragStart=[this]{if(owner.beforeChange)owner.beforeChange();enableProfile();};addAndMakeVisible(*slider);}
        rate.setTooltip("Free motion rate in Hz");depth.setTooltip("Motion depth for the selected engine; negative values invert the curve");
        pill(follow,"follow input");follow.setTooltip("Restart the motion curve on input attacks");addAndMakeVisible(follow);follow.onClick=[this]{if(owner.beforeChange)owner.beforeChange();setMotion("Follow",follow.getToggleState()?1:0);update();};
        pill(sync,"sync");sync.setTooltip("Synchronise to the host tempo");addAndMakeVisible(sync);sync.onClick=[this]{if(owner.beforeChange)owner.beforeChange();setMotion("Sync",sync.getToggleState()?1:0);update();};
        darkControl(targetChoice);targetChoice.setTooltip("Parameter moved by this engine's motion");addAndMakeVisible(targetChoice);targetChoice.onChange=[this]{if(targetChoice.getSelectedId()>0&&processor.value(targetId())!=(float)(targetChoice.getSelectedId()-1)){if(owner.beforeChange)owner.beforeChange();enableProfile();processor.setValue(targetId(),(float)(targetChoice.getSelectedId()-1));repaint();}};
        setWantsKeyboardFocus(true);setTitle("Motion editor");setTooltip("Drag a node to change the custom curve. Double-click adds a node, Alt-click deletes. Drag a diamond to bend a segment. Shift is fine; Command bypasses timing snap.");setTarget(2);update();
    }
    juce::String motionId(const juce::String& suffix) const{return AlphaAudioProcessor::motionId(modules[(size_t)target],suffix);}
    juce::String legacyId(const juce::String& suffix) const{if(suffix=="Shape")return "lfoShape";if(suffix=="Rate")return "lfoRate";if(suffix=="Sync")return "lfoSync";if(suffix=="Division")return "lfoDivision";if(suffix=="Count")return "curveCount";return "curve"+suffix;}
    float motionValue(const juce::String& suffix) const {if(processor.value(motionId("Enabled"))>.5f)return processor.value(motionId(suffix));if(suffix=="Follow")return 0;const alpha::MotionSettings defaults;if(suffix=="Count")return (float)defaults.curveCount;if(suffix.startsWith("X")||suffix.startsWith("Y")||suffix.startsWith("Bend")){const auto& point=defaults.curve[(size_t)juce::jlimit(0,alpha::curveCapacity-1,suffix.getTrailingIntValue())];return (suffix.startsWith("X")?point.x:suffix.startsWith("Y")?point.y:point.curve)*100;}return processor.value(legacyId(suffix));}
    void enableProfile(){if(processor.value(motionId("Enabled"))>.5f)return;for(const auto* suffix:{"Shape","Rate","Sync","Division","Count"})processor.setValue(motionId(suffix),motionValue(suffix));const int count=juce::jlimit(2,alpha::curveCapacity,(int)motionValue("Count"));for(int i=0;i<count;++i)for(const auto* prefix:{"X","Y","Bend"}){const auto suffix=juce::String(prefix)+juce::String(i);processor.setValue(motionId(suffix),motionValue(suffix));}processor.setValue(motionId("Enabled"),1);}
    void setMotion(const juce::String& suffix,float value){enableProfile();processor.setValue(motionId(suffix),value);}
    juce::String routeId() const{return "mod"+juce::String(AlphaAudioProcessor::moduleNames[(size_t)modules[(size_t)target]]);}
    juce::String targetId() const{return juce::String(AlphaAudioProcessor::moduleIds[(size_t)modules[(size_t)target]])+"MotionTarget";}
    void setTarget(int index){const int next=juce::jlimit(0,engineCount-1,index);if(next==target&&depthAttachment)return;target=next;targetChoice.getProperties().set("accent",colours[(size_t)target].toString());rateAttachment.reset();rateAttachment=attachSlider(processor,motionId("Rate"),rate);depthAttachment.reset();depthAttachment=attachSlider(processor,routeId(),depth);depth.setColour(juce::Slider::trackColourId,colours[(size_t)target]);
        const int m=modules[(size_t)target];targetChoice.clear(juce::dontSendNotification);targetChoice.addItemList({"amount",AlphaAudioProcessor::toneName(m).toLowerCase(),AlphaAudioProcessor::detailName(m).toLowerCase(),AlphaAudioProcessor::thirdName(m).toLowerCase(),AlphaAudioProcessor::fourthName(m).toLowerCase()},1);update();}
    void update(){readCurve();std::move(history.begin()+1,history.end(),history.begin());history.back()=processor.engine.getLfoValue(modules[(size_t)target]);const bool isSync=motionValue("Sync")>.5f;if(!rate.isMouseButtonDown())rate.setValue(motionValue("Rate"),juce::dontSendNotification);sync.setToggleState(isSync,juce::dontSendNotification);follow.setToggleState(motionValue("Follow")>.5f,juce::dontSendNotification);rate.setVisible(!isSync);for(int i=0;i<7;++i){rates[(size_t)i].setVisible(isSync);rates[(size_t)i].setToggleState(6-i==(int)motionValue("Division"),juce::dontSendNotification);}for(int i=0;i<8;++i)shapes[(size_t)i]->setToggleState(i==(int)motionValue("Shape"),juce::dontSendNotification);targetChoice.setSelectedId((int)processor.value(targetId())+1,juce::dontSendNotification);repaint();}
    int field() const{return juce::jlimit(0,4,(int)processor.value(targetId()));}
    float base() const{const int m=modules[(size_t)target];const float morph=processor.value("morph")*.01f;return juce::jmap(morph,processor.value(AlphaAudioProcessor::sceneId(0,m,field())),processor.value(AlphaAudioProcessor::sceneId(1,m,field())));}
    float valueAt(float wave) const{const float route=processor.value(routeId())*.01f,master=processor.value("lfoDepth")*.01f;const float value=juce::jlimit(0.0f,100.0f,field()==0?base()*(1+wave*route*master):base()+wave*route*master*100);return field()==0?value*processor.value("character")*.01f:value;}
    juce::String valueText(float value) const{if(field()==0)return juce::String(juce::roundToInt(value))+"%";return AlphaAudioProcessor::parameterValueText(modules[(size_t)target],field(),value);}
    int nodeCount() const{return juce::jlimit(2,32,(int)motionValue("Count"));}
    float nodeX(int i) const{return motionValue("X"+juce::String(i))*.01f;}
    float nodeY(int i) const{return motionValue("Y"+juce::String(i))*.01f;}
    void readCurve(){cachedCount=nodeCount();for(int i=0;i<cachedCount;++i)cachedCurve[(size_t)i]={nodeX(i),nodeY(i),motionValue("Bend"+juce::String(i))*.01f};}
    float customValue(float phase) const{return alpha::evaluateMotionCurve(cachedCurve,cachedCount,phase);}
    float wave(float phase) const{const int shape=(int)motionValue("Shape");return shape==7?customValue(phase):waveAt(shape,phase);}
    juce::Point<float> nodePoint(int i) const{return {56+nodeX(i)*704,yFor(valueAt(nodeY(i)))};}
    juce::Point<float> diamondPoint(int i) const{const float phase=(nodeX(i)+nodeX(i+1))*.5f;return {56+phase*704,yFor(valueAt(customValue(phase)))};}
    float yFor(float value) const{return 266-(value-low)/juce::jmax(1.0f,high-low)*150;}
    float waveScale() const{const float route=processor.value(routeId())*.01f*processor.value("lfoDepth")*.01f;return field()==0?base()*processor.value("character")*.01f*route:100*route;}
    float waveForY(float y) const{const float scale=waveScale();if(std::abs(scale)<.001f)return juce::jlimit(-1.0f,1.0f,(196-y)*.01f);return juce::jlimit(-1.0f,1.0f,(high-(y-116)/150*(high-low)-valueAt(0))/scale);}

    void paint(juce::Graphics& g) override {
        readCurve();const auto colour=colours[(size_t)target];low=100;high=0;for(int i=0;i<=512;++i){const float value=valueAt(wave((float)i/512));low=juce::jmin(low,value);high=juce::jmax(high,value);}if(high-low<2){low=juce::jmax(0.0f,low-10);high=juce::jmin(100.0f,high+10);}
        for(int i=1;i<16;++i){g.setColour(i%4==0?graphRule:graphMinor);g.drawVerticalLine(56+i*44,96,296);}g.setColour(graphMinor);for(int y=146;y<296;y+=50)for(int x=56;x<760;x+=7)g.fillRect(x,y,2,1);g.setColour(graphRule);g.drawRect(56.5f,96.5f,703.0f,199.0f,1.0f);
        juce::Path curve;const int shape=(int)motionValue("Shape");for(int i=0;i<=512;++i){const float phase=(float)i/512;float v=shape==3?history[(size_t)juce::jlimit(0,(int)history.size()-1,(int)(phase*(history.size()-1)))]:wave(phase);const float x=56+phase*704,y=yFor(valueAt(v));if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}auto fill=curve;fill.lineTo(760,296);fill.lineTo(56,296);fill.closeSubPath();g.setColour(colour.withAlpha(.16f));g.fillPath(fill);g.setColour(colour);g.strokePath(curve,juce::PathStrokeType(3,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        if(shape==7){for(int i=0;i<nodeCount()-1;++i){const auto pt=diamondPoint(i);juce::Path d;d.startNewSubPath(pt.x,pt.y-5);d.lineTo(pt.x+5,pt.y);d.lineTo(pt.x,pt.y+5);d.lineTo(pt.x-5,pt.y);d.closeSubPath();g.setColour(screen);g.fillPath(d);g.setColour(colour);g.strokePath(d,juce::PathStrokeType(1.6f));}for(int i=0;i<nodeCount();++i){const auto pt=nodePoint(i);const float radius=i==selectedNode?7.5f:6.0f;g.setColour(i==selectedNode?colour:screen);g.fillEllipse(pt.x-radius,pt.y-radius,radius*2,radius*2);g.setColour(i==selectedNode?light:colour);g.drawEllipse(pt.x-radius,pt.y-radius,radius*2,radius*2,2);}}
        const float live=valueAt(processor.engine.getLfoValue(modules[(size_t)target]));const float liveY=yFor(live),playX=56+processor.engine.getLfoPhase(modules[(size_t)target])*704;g.setColour(light.withAlpha(.6f));g.drawLine(playX,96,playX,296,1);g.setColour(light);g.fillEllipse(playX-4,liveY-4,8,8);
        monoText(g,valueText(high),{0,88,48,18},10,screenMuted,juce::Justification::centredRight);monoText(g,valueText(low),{0,286,48,18},10,screenMuted,juce::Justification::centredRight);
        monoText(g,"0",{56,302,40,20},11,screenMuted);monoText(g,juce::String::fromUTF8("¼"),{212,302,40,20},11,screenMuted,juce::Justification::centred);monoText(g,juce::String::fromUTF8("½"),{388,302,40,20},11,screenMuted,juce::Justification::centred);monoText(g,juce::String::fromUTF8("¾"),{564,302,40,20},11,screenMuted,juce::Justification::centred);
        const int div=juce::jlimit(0,6,(int)motionValue("Division"));const bool isSync=motionValue("Sync")>.5f;const juce::String cycle=shape==3?"live history":"1 cycle = "+(isSync?juce::String(divisions[(size_t)div]):juce::String(motionValue("Rate"),2)+" Hz");monoText(g,cycle,{618,302,142,20},10,screenMuted,juce::Justification::centredRight);
        monoText(g,"shape",{56,50,44,30},11,screenMuted);monoText(g,"rate",{366,50,36,30},11,screenMuted);monoText(g,"target",{662,50,44,30},11,screenMuted);monoText(g,"depth",{817,50,45,30},11,screenMuted);
        monoText(g,"TARGET",{800,91,230,19},10,screenMuted);text(g,targetChoice.getText(),{800,111,250,30},22,light,juce::Justification::centredLeft,true);monoText(g,valueText(low)+juce::String::fromUTF8(" → ")+valueText(high),{800,140,320,20},13,light);
        // Miniature of the selected target and its modulation range.
        const float start=juce::MathConstants<float>::pi*1.25f,end=juce::MathConstants<float>::pi*2.75f;const float a=start+base()*.01f*(end-start);juce::Path ring;ring.addCentredArc(858,212,34,34,0,start,end,true);g.setColour(graphRule);g.strokePath(ring,juce::PathStrokeType(4));ring.clear();ring.addCentredArc(858,212,34,34,0,start,a,true);g.setColour(light);g.strokePath(ring,juce::PathStrokeType(4));ring.clear();ring.addCentredArc(858,212,34,34,0,start+low*.01f*(end-start),start+high*.01f*(end-start),true);g.setColour(colour);g.strokePath(ring,juce::PathStrokeType(4));g.setColour(screenPanel);g.fillEllipse(835,189,46,46);g.setColour(light);g.drawEllipse(835,189,46,46,1.5f);const auto v=juce::Point<float>(std::sin(a),-std::cos(a));g.drawLine(858+v.x*10,212+v.y*10,858+v.x*20,212+v.y*20,2.5f);
        monoText(g,"depth  "+depth.getTextFromValue(depth.getValue()),{914,180,214,20},12,light);monoText(g,"rate   "+(isSync?juce::String(divisions[(size_t)div]):juce::String(motionValue("Rate"),2)+" Hz"),{914,200,214,20},12,light);monoText(g,juce::String(names[(size_t)target])+juce::String::fromUTF8(" · ")+valueText(live),{914,220,214,20},11,screenMuted);
        monoText(g,"LIVE MODULATION",{800,246,328,18},10,screenMuted);g.setColour(graphRule);g.drawHorizontalLine(268,800,1128);g.drawHorizontalLine(296,800,1128);juce::Path trace;for(size_t i=0;i<history.size();++i){const float x=800+(float)i/(float)(history.size()-1)*328,y=282-history[i]*12;if(i==0)trace.startNewSubPath(x,y);else trace.lineTo(x,y);}g.setColour(colour);g.strokePath(trace,juce::PathStrokeType(1.5f));
        monoText(g,shape==7?juce::String::fromUTF8("drag node · double-click: add · alt-click: delete · drag ◆: curve · shift: fine"):"Choose a shape or the pencil to edit a custom curve",{56,332,704,24},11,screenMuted);monoText(g,juce::String(processor.tempo.load(),1)+juce::String::fromUTF8(" bpm · ")+(isSync?"host sync":"free running"),{800,14,328,20},11,screenMuted,juce::Justification::centredRight);
        if(dragNode>=0){const auto pt=nodePoint(dragNode);g.setColour(light);g.fillRoundedRectangle(pt.x+12,pt.y-28,132,22,6);monoText(g,valueText(valueAt(nodeY(dragNode)))+" / "+juce::String(nodeX(dragNode),2),{(int)pt.x+18,(int)pt.y-28,120,22},10,screen);}
    }
    void mouseDown(const juce::MouseEvent& event) override {
        grabKeyboardFocus();if((int)motionValue("Shape")!=7)return;selectedNode=-1;dragNode=-1;dragBend=-1;
        for(int i=0;i<nodeCount();++i)if(nodePoint(i).getDistanceFrom(event.position)<10){if(event.mods.isAltDown()&&i>0&&i<nodeCount()-1){if(owner.beforeChange)owner.beforeChange();const int count=nodeCount();for(int n=i;n<count-1;++n)for(const auto* key:{"X","Y","Bend"})setMotion(juce::String(key)+juce::String(n),motionValue(juce::String(key)+juce::String(n+1)));setMotion("Count",(float)(count-1));repaint();return;}dragNode=selectedNode=i;break;}
        if(dragNode<0)for(int i=0;i<nodeCount()-1;++i)if(diamondPoint(i).getDistanceFrom(event.position)<9){dragBend=i;break;}
        if(dragNode>=0||dragBend>=0){if(owner.beforeChange)owner.beforeChange();enableProfile();lastPoint=event.position;if(dragNode>=0){rawX=motionValue("X"+juce::String(dragNode));rawY=motionValue("Y"+juce::String(dragNode));}if(dragBend>=0)rawBend=motionValue("Bend"+juce::String(dragBend));for(const auto& id:gestureIds())if(auto* parameter=processor.parameters.getParameter(motionId(id)))parameter->beginChangeGesture();}repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        const float fine=event.mods.isShiftDown()?.1f:1;const auto delta=event.position-lastPoint;lastPoint=event.position;
        if(dragNode>=0){const int count=nodeCount();if(dragNode>0&&dragNode<count-1){rawX+=delta.x/704*100*fine;float x=rawX;if(!event.mods.isCommandDown()&&!event.mods.isShiftDown())x=std::round(x/6.25f)*6.25f;setActual("X"+juce::String(dragNode),juce::jlimit(nodeX(dragNode-1)*100+1,nodeX(dragNode+1)*100-1,x));}const float scale=waveScale();rawY-=delta.y*fine*(std::abs(scale)>.001f?(high-low)/150/scale*100:1);rawY=juce::jlimit(-100.0f,100.0f,rawY);setActual("Y"+juce::String(dragNode),rawY);}
        if(dragBend>=0){const auto id="Bend"+juce::String(dragBend);rawBend=juce::jlimit(-100.0f,100.0f,rawBend-delta.y*fine);setActual(id,rawBend);}repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {for(const auto& id:gestureIds())if(auto* parameter=processor.parameters.getParameter(motionId(id)))parameter->endChangeGesture();dragNode=-1;dragBend=-1;repaint();}
    void mouseDoubleClick(const juce::MouseEvent& event) override {if(!juce::Rectangle<float>(56,96,704,200).contains(event.position))return;if(nodeCount()>=32)return;if(owner.beforeChange)owner.beforeChange();setMotion("Shape",7);const float x=juce::jlimit(1.0f,99.0f,(event.position.x-56)/704*100);const float y=waveForY(event.position.y)*100;const int count=nodeCount();int insertion=count-1;for(int i=1;i<count;++i)if(nodeX(i)*100>x){insertion=i;break;}for(int i=count;i>insertion;--i)for(const auto* key:{"X","Y","Bend"})setMotion(juce::String(key)+juce::String(i),motionValue(juce::String(key)+juce::String(i-1)));setMotion("X"+juce::String(insertion),x);setMotion("Y"+juce::String(insertion),y);setMotion("Bend"+juce::String(insertion),0);setMotion("Count",(float)(count+1));selectedNode=insertion;update();}
    bool keyPressed(const juce::KeyPress& key) override {if((int)motionValue("Shape")!=7||selectedNode<0||selectedNode>=nodeCount())return false;const float step=key.getModifiers().isShiftDown()?10.0f:1.0f;if(key==juce::KeyPress::upKey||key==juce::KeyPress::downKey){if(owner.beforeChange)owner.beforeChange();setMotion("Y"+juce::String(selectedNode),juce::jlimit(-100.0f,100.0f,motionValue("Y"+juce::String(selectedNode))+(key==juce::KeyPress::upKey?step:-step)));update();return true;}if((key==juce::KeyPress::leftKey||key==juce::KeyPress::rightKey)&&selectedNode>0&&selectedNode<nodeCount()-1){if(owner.beforeChange)owner.beforeChange();setMotion("X"+juce::String(selectedNode),juce::jlimit(nodeX(selectedNode-1)*100+1,nodeX(selectedNode+1)*100-1,motionValue("X"+juce::String(selectedNode))+(key==juce::KeyPress::rightKey?step:-step)));update();return true;}return false;}
    void resized() override {for(int i=0;i<8;++i)shapes[(size_t)i]->setBounds(102+i*31,51,28,28);for(int i=0;i<7;++i)rates[(size_t)i].setBounds(405+i*34,51,33,28);rate.setBounds(405,51,232,28);targetChoice.setBounds(709,51,96,28);depth.setBounds(863,51,137,28);sync.setBounds(1008,51,49,28);follow.setBounds(1063,51,78,28);}
    std::vector<juce::String> gestureIds() const {if(dragNode>=0)return {"X"+juce::String(dragNode),"Y"+juce::String(dragNode)};if(dragBend>=0)return {"Bend"+juce::String(dragBend)};return {};}
    void setActual(const juce::String& id,float value){enableProfile();if(auto* parameter=processor.parameters.getParameter(motionId(id)))parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
    MotionView& owner;AlphaAudioProcessor& processor;int target=2,selectedNode=1,dragNode=-1,dragBend=-1;float low=0,high=100,rawX=0,rawY=0,rawBend=0;int cachedCount=4;std::array<alpha::CurvePoint,alpha::curveCapacity> cachedCurve{};juce::Point<float> lastPoint;
    std::array<std::unique_ptr<ShapeButton>,8> shapes;std::array<juce::TextButton,7> rates;GestureSlider rate,depth;juce::TextButton sync,follow;juce::ComboBox targetChoice;std::unique_ptr<SliderAttachment> rateAttachment,depthAttachment;std::array<float,164> history{};
};

MotionView::MotionView(AlphaAudioProcessor& p) : impl(std::make_unique<Impl>(*this, p)) { addAndMakeVisible(*impl); }
MotionView::~MotionView() = default;
void MotionView::setTarget(int target) { impl->setTarget(target); }
void MotionView::update() { impl->update(); }
void MotionView::paint(juce::Graphics&) {}
void MotionView::resized() { impl->setBounds(getLocalBounds()); }

struct PresetView::Impl final : juce::Component,juce::Timer {
    struct Card final : juce::Button {
        Card(Impl& o, int p) : juce::Button(o.processor.getProgramName(p)), owner(o), program(p) {
            setTooltip(AlphaAudioProcessor::presetDescription(program));
            onClick = [this] { owner.selected = program; owner.loadSelected(); };
        }
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            const bool focused = owner.selected == program;
            const bool current = owner.currentProgram() == program;
            const bool bright = over || down || (focused&&!current);
            g.setColour(bright ? light : screenPanel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(.8f), 10);
            if (current) { g.setColour(bright ? ink : light); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 10, 1.5f); }
            signature(g, { 12, 15, 44, 30 }, program);
            patina::text(g, owner.processor.getProgramName(program), { 68, 10, getWidth() - 80, 24 }, 13.5f, bright ? screen : light, juce::Justification::centredLeft, true);
            monoText(g, AlphaAudioProcessor::presetCategory(program).toLowerCase().replace(" / ",juce::String::fromUTF8(" · ")) + (current ? juce::String::fromUTF8(" · current") : ""), { 68, 32, getWidth() - 80, 17 }, 10.5f, bright ? juce::Colour(0xff4a4d4b) : screenMuted);
        }
        void mouseEnter(const juce::MouseEvent& e) override { juce::Button::mouseEnter(e); owner.hovered = program; owner.schedulePreview(program); owner.repaint(); }
        void mouseExit(const juce::MouseEvent& e) override { juce::Button::mouseExit(e); owner.hovered = -1; owner.cancelPreview(); owner.repaint(); }
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
        search.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        search.setColour(juce::TextEditor::textColourId, light);
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour(juce::TextEditor::focusedOutlineColourId, screenMuted);
        search.setColour(juce::TextEditor::highlightColourId, colours[4].withAlpha(.35f));
        search.setIndents(10, 6); addAndMakeVisible(search);
        search.onTextChange = [this] { refresh(); };
        search.onReturnKey = [this] { loadSelected(); };
        search.onEscapeKey = [this] { cancelPreview();if (owner.onClose) owner.onClose(); };
        categories.add("all");
        for (int i = 0; i < processor.getNumPrograms(); ++i) categories.addIfNotAlreadyThere(AlphaAudioProcessor::presetCategory(i).toLowerCase());
        for (int i = 0; i < categories.size(); ++i) {
            auto button = std::make_unique<juce::TextButton>(); pill(*button, categories[i]);
            button->onClick = [this, i] { category = i; refresh(); grabKeyboardFocus(); };
            addAndMakeVisible(*button); filters.push_back(std::move(button));
        }
        pill(more,"more");addAndMakeVisible(more);more.onClick=[this]{juce::PopupMenu menu;for(int i=1;i<categories.size();++i)menu.addItem(i,categories[i],true,category==i);auto safe=juce::Component::SafePointer<Impl>(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&more),[safe](int result){if(safe&&result>0){safe->category=result;safe->refresh();safe->grabKeyboardFocus();}});};
        close.setButtonText(juce::String::fromUTF8("×")); close.setTooltip("Close presets (Escape)"); close.getProperties().set("variant", "dark");
        close.onClick = [this] { cancelPreview();if (owner.onClose) owner.onClose(); }; addAndMakeVisible(close);
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
    ~Impl() override { stopTimer();if(previewActive)processor.setStateInformation(previewState.getData(),(int)previewState.getSize());viewport.setViewedComponent(nullptr, false); }
    int currentProgram() const{return previewActive?committedProgram:processor.getCurrentProgram();}
    void schedulePreview(int program){if(program==currentProgram())return;pendingPreview=program;startTimer(120);}
    void cancelPreview(){stopTimer();pendingPreview=-1;if(previewActive){processor.setStateInformation(previewState.getData(),(int)previewState.getSize());previewActive=false;previewState.reset();if(owner.onPreview)owner.onPreview();grid.repaint();repaint();}}
    void timerCallback() override {stopTimer();if(pendingPreview<0)return;if(!previewActive){committedProgram=processor.getCurrentProgram();processor.getStateInformation(previewState);previewActive=true;}const float amount=processor.value("character"),mix=processor.value("mix"),output=processor.value("output");processor.setCurrentProgram(pendingPreview);if(keepAmount.getToggleState())processor.setValue("character",amount);if(keepMix.getToggleState())processor.setValue("mix",mix);if(keepOutput.getToggleState())processor.setValue("output",output);if(owner.onPreview)owner.onPreview();grid.repaint();repaint();}

    void refresh() {
        cancelPreview();filtered.clear(); const auto query = search.getText().trim();
        for (int order = 0; order < processor.getNumPrograms(); ++order) {
            const int i=processor.getNumPrograms()>10?(order==0?10:order<=10?order-1:order):order;
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
        selected = currentProgram();
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
        cancelPreview();const float amount = processor.value("character"), mix = processor.value("mix"), output = processor.value("output");
        if (owner.onBeforeLoad) owner.onBeforeLoad();
        processor.setCurrentProgram(selected);
        if (keepAmount.getToggleState()) processor.setValue("character", amount);
        if (keepMix.getToggleState()) processor.setValue("mix", mix);
        if (keepOutput.getToggleState()) processor.setValue("output", output);
        grid.repaint(); repaint();
        if (owner.onLoad) owner.onLoad();
    }
    juce::Point<float> pointFor(int program) const {
        const auto values=AlphaAudioProcessor::presetSignature(program);float intensity=0;for(float value:values)intensity+=value;
        return {48+juce::jlimit(0.0f,1.0f,intensity/260)*310,326-juce::jlimit(0.0f,1.0f,AlphaAudioProcessor::presetMovement(program)*.01f)*230};
    }
    void paint(juce::Graphics& g) override {
        g.setColour(screenPanel);g.fillRoundedRectangle(194,14,230,28,7);
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
            if (program == currentProgram()) { g.setColour(light); g.drawEllipse(point.x - 12, point.y - 12, 24, 24, 1.5f); }
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
        if (key == juce::KeyPress::escapeKey) { cancelPreview();if (owner.onClose) owner.onClose(); return true; }
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
        int x=438;bool overflow=false;
        for(size_t i=0;i<filters.size();++i){const int width=23+(int)categories[(int)i].length()*7;const bool visible=x+width<=914;filters[i]->setVisible(visible);if(visible){filters[i]->setBounds(x,15,width,26);x+=width+4;}else overflow=true;}
        more.setVisible(overflow);more.setBounds(916,15,54,26);
        save.setBounds(982, 14, 48, 28); open.setBounds(1036, 14, 48, 28); close.setBounds(1108, 14, 28, 28);
        viewport.setBounds(408, 56, 728, 268); layoutGrid();
        keepAmount.setBounds(546, 337, 63, 24); keepMix.setBounds(615, 337, 44, 24); keepOutput.setBounds(665, 337, 62, 24);
    }
    PresetView& owner; AlphaAudioProcessor& processor;
    juce::TextEditor search; juce::StringArray categories;
    std::vector<std::unique_ptr<juce::TextButton>> filters;
    juce::TextButton close, save, open,more, keepAmount, keepMix, keepOutput;
    juce::Viewport viewport; Grid grid;
    std::vector<std::unique_ptr<Card>> cards;
    std::vector<int> filtered;
    int category = 0, selected = -1, hovered = -1,pendingPreview=-1,committedProgram=0;bool previewActive=false;juce::MemoryBlock previewState;
};

PresetView::PresetView(AlphaAudioProcessor& p) : impl(std::make_unique<Impl>(*this, p)) { addAndMakeVisible(*impl); }
PresetView::~PresetView() = default;
void PresetView::selectCurrent() { impl->selectCurrent(); }
void PresetView::endPreview(){impl->cancelPreview();}
bool PresetView::isPreviewing() const{return impl->previewActive;}
void PresetView::paint(juce::Graphics&) {}
void PresetView::resized() { impl->setBounds(getLocalBounds()); }
}
