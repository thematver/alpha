#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>

namespace {
int failures=0;
class TestPlayHead final : public juce::AudioPlayHead {
public:
    TestPlayHead(int numerator,int denominator) : top(numerator),bottom(denominator) {}
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;info.setBpm(120);info.setTimeSignature(TimeSignature{top,bottom});return info;
    }
private:
    int top,bottom;
};
void check(bool ok,const char* label) {std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;}
void fill(juce::AudioBuffer<float>& b,int offset,double rate=48000) {
    for(int c=0;c<b.getNumChannels();++c)for(int n=0;n<b.getNumSamples();++n)
        b.setSample(c,n,.2f*std::sin(static_cast<float>((offset+n)*juce::MathConstants<double>::twoPi*(c?437:220)/rate)));
}
juce::Button* findButton(juce::Component& parent,const juce::String& text,bool partial=false) {
    for(auto* child:parent.getChildren()) {
        if(auto* button=dynamic_cast<juce::Button*>(child))
            if(button->getButtonText().equalsIgnoreCase(text)
                ||(partial&&button->getButtonText().containsIgnoreCase(text))
                ||button->getProperties().getWithDefault("icon","").toString()==text)return button;
        if(auto* found=findButton(*child,text,partial))return found;
    }
    return nullptr;
}
juce::Component* findComponent(juce::Component& parent,const juce::String& id) {
    if(parent.getComponentID()==id)return &parent;
    for(auto* child:parent.getChildren())if(auto* found=findComponent(*child,id))return found;
    return nullptr;
}
void snapshot(juce::AudioProcessorEditor& editor,const juce::File& file) {
    auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.5f);
    auto stream=file.createOutputStream();juce::PNGImageFormat png;
    if(stream!=nullptr) {stream->setPosition(0);stream->truncate();}
    check(stream!=nullptr&&png.writeImageToStream(image,*stream),"native editor snapshot");
}
}

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    AlphaAudioProcessor p;
    check(p.getTotalNumInputChannels()==2&&p.getTotalNumOutputChannels()==2,"default stereo input/output");
    auto mono=p.getBusesLayout();mono.inputBuses.set(0,juce::AudioChannelSet::mono());mono.outputBuses.set(0,juce::AudioChannelSet::mono());
    check(p.isBusesLayoutSupported(mono),"matched mono supported");
    auto mismatch=mono;mismatch.outputBuses.set(0,juce::AudioChannelSet::stereo());
    check(!p.isBusesLayoutSupported(mismatch),"mismatched buses rejected");
    std::set<juce::String> ids;
    bool uniqueIds=true;
    for(auto* param:p.getParameters()) {
        auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
        uniqueIds &= identified!=nullptr&&ids.insert(identified->paramID).second;
    }
    check(uniqueIds,"unique stable automation parameter IDs");
    check(ids.size()==1445,"1445 unique engine, focus, curve and global automation parameters");

    const juce::StringArray originalIds {
        "a_tape","a_tapeTone","a_tapeDetail","a_wobble","a_wobbleTone","a_wobbleDetail","a_noise","a_noiseTone","a_noiseDetail","a_drive","a_driveTone","a_driveDetail","a_space","a_spaceTone","a_spaceDetail",
        "b_tape","b_tapeTone","b_tapeDetail","b_wobble","b_wobbleTone","b_wobbleDetail","b_noise","b_noiseTone","b_noiseDetail","b_drive","b_driveTone","b_driveDetail","b_space","b_spaceTone","b_spaceDetail",
        "tapeOn","tapeMode","modTape","wobbleOn","wobbleMode","modWobble","noiseOn","noiseMode","modNoise","driveOn","driveMode","modDrive","spaceOn","spaceMode","modSpace",
        "character","mix","morph","output","lfoRate","lfoSync","lfoDivision","lfoShape","lfoDepth",
        "a_grit","a_gritTone","a_gritDetail","b_grit","b_gritTone","b_gritDetail","gritOn","gritMode","modGrit","bypass","delta","autoGain"
    };
    bool originalOrder=true;
    for(int i=0;i<originalIds.size();++i)
        originalOrder &= dynamic_cast<juce::AudioProcessorParameterWithID*>(p.getParameters()[i])->paramID==originalIds[i];
    check(originalOrder,"original automation IDs retain their parameter indices");
    bool auVersionOrder=true;
    for(int i=0;i<p.getParameters().size();++i)auVersionOrder &= p.getParameters()[i]->getVersionHint()==(i<66?1:2);
    check(auVersionOrder,"AU version hints retain original 66 Logic automation positions");
    auto* legacyDrive=p.parameters.getParameter("driveMode");
    check(legacyDrive&&std::abs(legacyDrive->convertFrom0to1(.5f)-1)<.001f&&std::abs(legacyDrive->convertFrom0to1(1)-2)<.001f,"legacy normalized Tube and Fold automation remain unchanged");
    p.setModuleMode(3,5);check(p.effectiveMode(3)==5,"extended Heat Tape model selects independent appended control");p.setModuleMode(3,1);
    check(p.getCurrentProgram()==10&&p.getProgramName(10)=="Sunday Tape Loop"&&p.value("a_grit")==12&&p.value("a_drive")==62&&p.value("autoGain")==1,"initial sound matches the reference Sunday Tape Loop");
    check(std::abs(p.getTailLengthSeconds()-35)<.001,"normal character sound reports its reverb tail");
    p.setValue("a_time",100);p.setValue("a_timeFourth",100);
    check(p.getTailLengthSeconds()>500&&p.getTailLengthSeconds()<=600,"maximum Time feedback reports a conservative long tail to the host");
    p.setValue("timeOn",0);check(std::abs(p.getTailLengthSeconds()-35)<.001,"disabled Time removes its feedback tail from host metadata");p.setCurrentProgram(10);
    bool unitsRoundTrip=true;
    for(int m=0;m<alpha::moduleCount;++m)for(int field=1;field<5;++field)for(float v:{0.0f,20.0f,50.0f,80.0f,100.0f}) {
        const auto printed=p.parameterValueText(m,field,v);
        const float expected=m==7&&field==1?20.0f*std::round(v/20.0f):v;
        const float parsed=p.parameterValueFromText(m,field,printed);
        if(std::abs(parsed-expected)>=1.1f)std::cout<<"UNIT "<<m<<'/'<<field<<' '<<v<<" -> "<<printed<<" -> "<<parsed<<'\n';
        unitsRoundTrip &= std::abs(parsed-expected)<1.1f;
    }
    check(unitsRoundTrip,"all character controls round trip their printed physical units");
    check(std::abs(p.parameterValueFromText(0,4,"1.50:1")-16.6667f)<.01f&&std::abs(p.parameterValueFromText(9,1,"2.4k")-69.306f)<.1f,"ratio denominator and shorthand kilohertz parse correctly");

    p.setCurrentProgram(3);p.setValue("a_tape",63.25f);p.setValue("b_space",83.5f);p.setValue("morph",37.4f);p.setValue("modWobble",-22);
    p.setValue("a_grit",56);p.setValue("b_gritDetail",81);p.setValue("modGrit",-36);p.setValue("delta",1);p.setValue("autoGain",1);
    p.setValue("a_pitch",61);p.setValue("b_filterFourth",47);p.setValue("filterLo",237);p.setValue("filterHi",7451);
    p.setValue(p.motionId(9,"Enabled"),1);p.setValue(p.motionId(9,"Count"),32);p.setValue(p.motionId(9,"X31"),100);p.setValue(p.motionId(9,"Y31"),-73);p.setValue(p.motionId(9,"Bend17"),62);
    juce::MemoryBlock state;p.getStateInformation(state);
    p.setCurrentProgram(0);p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    check(std::abs(p.value("a_tape")-63.25f)<.02f&&std::abs(p.value("b_space")-83.5f)<.02f&&std::abs(p.value("morph")-37.4f)<.02f&&p.getCurrentProgram()==3,"state round trip preserves scene, morph and program");
    check(std::abs(p.value("modWobble")+22)<.02f,"state preserves negative modulation depth");
    check(std::abs(p.value("a_grit")-56)<.02f&&std::abs(p.value("b_gritDetail")-81)<.02f&&p.value("delta")==1&&p.value("autoGain")==1,"state preserves Grit and monitoring controls");
    check(std::abs(p.value("a_pitch")-61)<.02f&&std::abs(p.value("b_filterFourth")-47)<.02f&&p.value("filterLo")==237&&p.value("filterHi")==7451&&p.value(p.motionId(9,"Count"))==32&&std::abs(p.value(p.motionId(9,"Y31"))+73)<.01f&&std::abs(p.value(p.motionId(9,"Bend17"))-62)<.01f,"state preserves new engines, focus and all 32 motion nodes");
    auto legacy=p.parameters.copyState();
    for(int i=legacy.getNumChildren();--i>=0;) {
        const auto id=legacy.getChild(i).getProperty("id").toString();
        if(id.containsIgnoreCase("grit")||id=="bypass"||id=="delta"||id=="autoGain")legacy.removeChild(i,nullptr);
    }
    juce::MemoryBlock legacyData;juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),legacyData);
    p.setValue("bypass",1);p.setStateInformation(legacyData.getData(),static_cast<int>(legacyData.getSize()));
    check(p.value("a_grit")==0&&p.value("b_grit")==0&&p.value("a_gritTone")==50&&p.value("b_gritDetail")==35&&std::abs(p.value("modGrit"))<.0001f&&p.value("bypass")==0&&p.value("delta")==0&&p.value("autoGain")==0,"legacy states reset missing additive controls to neutral defaults");
    auto original66=p.parameters.copyState();original66.removeProperty("schemaVersion",nullptr);
    for(int i=original66.getNumChildren();--i>=0;)if(!originalIds.contains(original66.getChild(i).getProperty("id").toString()))original66.removeChild(i,nullptr);
    juce::MemoryBlock original66Data;juce::AudioProcessor::copyXmlToBinary(*original66.createXml(),original66Data);
    p.setValue("a_pitch",89);p.setValue("heatModel",3);p.setStateInformation(original66Data.getData(),(int)original66Data.getSize());
    check(original66.getNumChildren()==66&&p.value("a_pitch")==0&&p.value("heatModel")==0&&p.value("filterLo")==20&&p.value(p.motionId(9,"Enabled"))==0&&std::abs(p.value("a_tape")-63.25f)<.02f,"true original 66-parameter state resets every appended field and preserves stored values");
    p.copyScene(0,1);bool copied=true;
    for(int m=0;m<alpha::moduleCount;++m)for(int f=0;f<5;++f)copied&=p.value(p.sceneId(0,m,f))==p.value(p.sceneId(1,m,f));
    check(copied,"copy scene includes all five fields of all twelve engines");
    p.setValue("b_tape",12);p.setValue("morph",27);p.swapScenes();
    check(std::abs(p.value("a_tape")-12)<.02f&&std::abs(p.value("b_tape")-63.25f)<.02f&&std::abs(p.value("morph")-73)<.02f,"swap preserves audible morph position");
    const auto presetFile=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("alpha-test-"+juce::Uuid().toString()+".alphapreset");
    check(p.saveUserPreset(presetFile),"user preset save");p.setValue("a_tape",99);
    check(p.loadUserPreset(presetFile)&&std::abs(p.value("a_tape")-12)<.02f,"user preset load");presetFile.deleteFile();
    juce::MemoryBlock before;p.getStateInformation(before);const char invalid[]="invalid";p.setStateInformation(invalid,7);juce::MemoryBlock after;p.getStateInformation(after);
    check(before==after,"malformed state leaves settings intact");

    juce::MidiBuffer midi;juce::AudioBuffer<float> block(2,256);
    bool allFinite=true;double largest=0;double change=0;
    for(int preset=0;preset<p.getNumPrograms();++preset) {
        p.setCurrentProgram(preset);p.prepareToPlay(48000,256);
        for(int b=0;b<140;++b) {
            fill(block,b*256);const float dry=block.getSample(0,100);p.processBlock(block,midi);
            change+=std::abs(block.getSample(0,100)-dry);
            for(int c=0;c<2;++c)for(int n=0;n<256;++n) {const auto v=block.getSample(c,n);allFinite&=std::isfinite(v);largest=std::max(largest,static_cast<double>(std::abs(v)));}
        }
    }
    check(allFinite&&largest<2.0,"all factory presets render finite bounded stereo audio");
    check(change>1,"factory presets transform input audio");
    p.setValue("mix",0);p.setValue("output",0);
    for(int i=0;i<100;++i){fill(block,i*256);p.processBlock(block,midi);}
    fill(block,30000);juce::AudioBuffer<float> reference;reference.makeCopyOf(block);p.processBlock(block,midi);
    double error=0;for(int c=0;c<2;++c)for(int n=0;n<256;++n)error=std::max(error,static_cast<double>(std::abs(block.getSample(c,n)-reference.getSample(c,n))));
    check(error<1e-6,"zero mix passes dry audio without delay");
    juce::AudioBuffer<float> bypassReference;bypassReference.makeCopyOf(reference);p.processBlockBypassed(reference,midi);
    bool bypassExact=true;for(int c=0;c<2;++c)bypassExact&=std::memcmp(reference.getReadPointer(c),bypassReference.getReadPointer(c),256*sizeof(float))==0;
    check(bypassExact,"host bypass is exact passthrough");
    p.setValue("bypass",1);p.setValue("delta",1);p.setValue("autoGain",1);p.setValue("output",6);p.prepareToPlay(48000,256);
    fill(block,30000);reference.makeCopyOf(block);p.processBlock(block,midi);
    bool globalBypassExact=true;
    for(int c=0;c<2;++c)globalBypassExact&=std::memcmp(block.getReadPointer(c),reference.getReadPointer(c),256*sizeof(float))==0;
    check(globalBypassExact,"automatable global bypass returns exact dry and overrides monitoring");
    p.setValue("bypass",0);p.setValue("character",0);p.setValue("output",0);p.prepareToPlay(48000,256);
    fill(block,30000);p.processBlock(block,midi);
    check(block.getMagnitude(0,256)==0,"automatable delta monitor nulls a neutral chain");
    p.setCurrentProgram(0);p.setValue("character",100);p.setValue("mix",100);
    for(int m=0;m<alpha::moduleCount;++m) {p.setValue(p.sceneId(0,m),0);p.setValue("mod"+juce::String(p.moduleNames[static_cast<size_t>(m)]),0);}
    p.setValue("a_grit",100);p.setValue("a_gritTone",12);p.setValue("a_gritDetail",90);p.prepareToPlay(48000,256);
    fill(block,30000);reference.makeCopyOf(block);p.processBlock(block,midi);
    float gritDifference=0;for(int n=0;n<256;++n)gritDifference+=std::abs(block.getSample(0,n)-reference.getSample(0,n));
    check(gritDifference>.1f,"sixth module scene controls reach the audio engine");
    p.setValue("gritOn",0);p.prepareToPlay(48000,256);fill(block,30000);reference.makeCopyOf(block);p.processBlock(block,midi);
    // JUCE's output range maps a requested 0 dB to a sub-micro-dB
    // rounding residual; allow that trim's single-ULP output difference.
    bool gritBypassed=true;for(int c=0;c<2;++c)for(int n=0;n<256;++n)
        gritBypassed&=std::abs(block.getSample(c,n)-reference.getSample(c,n))<.0000001f;
    check(gritBypassed,"Grit enable parameter bypasses the sixth module");
    p.setCurrentProgram(0);
    check(p.value("filterLo")==20&&p.value("filterHi")==20000&&p.value(p.motionId(9,"Count"))==4&&p.value(p.motionId(9,"Enabled"))==0,"factory sounds reset hidden motion profiles and focus bands");

    p.setBusesLayout(mono);p.prepareToPlay(44100,512);juce::AudioBuffer<float> monoBuffer(1,512);fill(monoBuffer,0,44100);p.processBlock(monoBuffer,midi);
    check(monoBuffer.getNumChannels()==1&&std::isfinite(monoBuffer.getSample(0,100)),"mono bus render");
    p.setBusesLayout(mismatch); // rejected, keeps previous valid bus layout
    p.setCurrentProgram(0);
    p.setBusesLayout(juce::AudioProcessor::BusesLayout{{juce::AudioChannelSet::stereo()},{juce::AudioChannelSet::stereo()}});
    p.setCurrentProgram(10);p.prepareToPlay(48000,256);for(int i=0;i<100;++i){fill(block,i*256);p.processBlock(block,midi);}
    bool syncFollowsMeter=true;
    for(auto signature: {std::pair<int,int>{3,4},{6,8},{7,8}}) {
        TestPlayHead playHead(signature.first,signature.second);p.setPlayHead(&playHead);
        p.setValue("lfoSync",1);p.setValue("lfoDivision",2);p.setValue("lfoShape",0);
        const auto quarterCycleSamples=juce::roundToInt(48000.0*.5*signature.first*4.0/signature.second*.25);
        juce::AudioBuffer<float> timingBlock(2,quarterCycleSamples);timingBlock.clear();p.prepareToPlay(48000,quarterCycleSamples);p.processBlock(timingBlock,midi);
        syncFollowsMeter &= std::abs(p.engine.getLfoValue()-1.0f)<.002f;p.setPlayHead(nullptr);
    }
    check(syncFollowsMeter,"bar sync follows 3/4, 6/8 and 7/8 host time signatures");
    p.prepareToPlay(48000,256);for(int i=0;i<100;++i){fill(block,i*256);p.processBlock(block,midi);}
    if(argc>1) {
        const juce::File directory{juce::String(argv[1])};directory.createDirectory();
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        check(editor!=nullptr&&editor->getWidth()>=900,"native editor construction");
        auto* initialTone=dynamic_cast<juce::Slider*>(findComponent(*editor,"a_driveTone"));
        check(initialTone&&std::abs(initialTone->getDoubleClickReturnValue()-p.value("a_driveTone"))<.02f,"initial character reset uses the loaded preset baseline");
        snapshot(*editor,directory.getChildFile("alpha-sound.png"));
        for(auto tab: {"MOTION","PRESETS"})if(auto* button=findButton(*editor,tab)) {
            if(button->onClick)button->onClick();snapshot(*editor,directory.getChildFile(juce::String("alpha-")+juce::String(tab).toLowerCase()+".png"));
        }
        if(auto* button=findButton(*editor,"PRESETS"))if(button->onClick)button->onClick();
        if(auto* button=findButton(*editor,"shapers"))if(button->onClick)button->onClick();
        if(auto* button=findButton(*editor,"PITCH"))if(button->onClick)button->onClick();
        snapshot(*editor,directory.getChildFile("alpha-shapers.png"));
        auto* transpose=dynamic_cast<juce::Slider*>(findComponent(*editor,"a_pitchTone"));
        if(transpose)transpose->setValue(75,juce::sendNotificationSync);
        check(transpose&&std::abs(p.value("a_pitchTone")-75)<.02f,"shaper bank binds the actual Pitch transpose parameter");
        if(auto* button=findButton(*editor,"core"))if(button->onClick)button->onClick();
        // Exercise the actual native components and their APVTS attachments.
        // Capture references first so these edits do not alter the design QA images.
        if(auto* button=findButton(*editor,"GRIT"))if(button->onClick)button->onClick();
        auto* gritA=dynamic_cast<juce::Slider*>(findComponent(*editor,"a_gritTone"));
        if(gritA) {if(gritA->onDragStart)gritA->onDragStart();gritA->setValue(34,juce::sendNotificationSync);}
        check(gritA!=nullptr&&std::abs(p.value("a_gritTone")-34)<.02f,"Grit selection binds its A-scene character slider");
        if(auto* button=findButton(*editor,"B"))if(button->onClick)button->onClick();
        auto* gritB=dynamic_cast<juce::Slider*>(findComponent(*editor,"b_gritTone"));
        if(gritB) {if(gritB->onDragStart)gritB->onDragStart();gritB->setValue(62,juce::sendNotificationSync);}
        check(gritB!=nullptr&&std::abs(p.value("b_gritTone")-62)<.02f&&std::abs(p.value("a_gritTone")-34)<.02f,"scene B rebinds character controls and preserves scene A");
        if(auto* button=findButton(*editor,"MAIN"))if(button->onClick)button->onClick();
        auto* flux=dynamic_cast<juce::Slider*>(findComponent(*editor,"flux"));
        if(flux) {if(flux->onDragStart)flux->onDragStart();flux->setValue(37,juce::sendNotificationSync);}
        check(flux!=nullptr&&std::abs(p.value("flux")-37)<.02f&&findComponent(*editor,"mainTone")&&findComponent(*editor,"mainWidth"),"Main binds Flux, Tone, Width and Output as designed");
        const auto deltaBefore=p.value("delta");
        auto* deltaButton=findButton(*editor,"delta",true);
        if(deltaButton&&deltaButton->onClick)deltaButton->onClick();
        check(deltaButton!=nullptr&&std::abs(p.value("delta")-(deltaBefore>.5f?0.0f:1.0f))<.01f,"Delta button toggles the processor monitor parameter");
        auto* undoButton=findButton(*editor,"undo");
        if(undoButton&&undoButton->onClick)undoButton->onClick();
        check(undoButton!=nullptr&&std::abs(p.value("delta")-deltaBefore)<.01f&&std::abs(p.value("flux")-37)<.02f,"editor undo restores the prior processor state");
        auto* character=dynamic_cast<juce::Slider*>(findComponent(*editor,"character"));
        auto* mix=dynamic_cast<juce::Slider*>(findComponent(*editor,"mix"));
        if(character)character->setValue(64,juce::sendNotificationSync);
        if(mix)mix->setValue(72,juce::sendNotificationSync);
        check(character!=nullptr&&mix!=nullptr&&std::abs(p.value("character")-64)<.02f&&std::abs(p.value("mix")-72)<.02f,"global Character and Mix controls update their host parameters");
        if(auto* button=findButton(*editor,"GRIT"))if(button->onClick)button->onClick();
        if(auto* button=findButton(*editor,"B"))if(button->onClick)button->onClick();
        if(auto* button=findButton(*editor,"next"))if(button->onClick)button->onClick();
        check(findComponent(*editor,"a_gritTone")!=nullptr&&findButton(*editor,"A")->getToggleState(),"loading a factory sound rebinds the audible A scene");
        if(undoButton&&undoButton->onClick)undoButton->onClick();
        check(findComponent(*editor,"b_gritTone")!=nullptr&&findButton(*editor,"B")->getToggleState(),"undo restores the previous editor scene with the audio state");
        p.setCurrentProgram(10);
        if(auto* button=findButton(*editor,"MAIN"))if(button->onClick)button->onClick();
        editor->setSize(900,600);snapshot(*editor,directory.getChildFile("alpha-75-percent.png"));
        editor->setSize(1800,1200);snapshot(*editor,directory.getChildFile("alpha-150-percent.png"));
    }
    std::cout<<"Result: "<<failures<<" failures\n";return failures?1:0;
}
