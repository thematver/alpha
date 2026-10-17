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
    check(ids.size()==66,"66 automatable parameters");

    const juce::StringArray originalIds {
        "a_tape","a_tapeTone","a_tapeDetail","a_wobble","a_wobbleTone","a_wobbleDetail","a_noise","a_noiseTone","a_noiseDetail","a_drive","a_driveTone","a_driveDetail","a_space","a_spaceTone","a_spaceDetail",
        "b_tape","b_tapeTone","b_tapeDetail","b_wobble","b_wobbleTone","b_wobbleDetail","b_noise","b_noiseTone","b_noiseDetail","b_drive","b_driveTone","b_driveDetail","b_space","b_spaceTone","b_spaceDetail",
        "tapeOn","tapeMode","modTape","wobbleOn","wobbleMode","modWobble","noiseOn","noiseMode","modNoise","driveOn","driveMode","modDrive","spaceOn","spaceMode","modSpace",
        "character","mix","morph","output","lfoRate","lfoSync","lfoDivision","lfoShape","lfoDepth"
    };
    bool originalOrder=true;
    for(int i=0;i<originalIds.size();++i)
        originalOrder &= dynamic_cast<juce::AudioProcessorParameterWithID*>(p.getParameters()[i])->paramID==originalIds[i];
    check(originalOrder,"original automation IDs retain their parameter indices");
    check(p.value("a_grit")==0&&p.value("b_grit")==0&&p.value("bypass")==0&&p.value("delta")==0&&p.value("autoGain")==0,"new controls default to the original sound");

    p.setCurrentProgram(3);p.setValue("a_tape",63.25f);p.setValue("b_space",83.5f);p.setValue("morph",37.4f);p.setValue("modWobble",-22);
    p.setValue("a_grit",56);p.setValue("b_gritDetail",81);p.setValue("modGrit",-36);p.setValue("delta",1);p.setValue("autoGain",1);
    juce::MemoryBlock state;p.getStateInformation(state);
    p.setCurrentProgram(0);p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    check(std::abs(p.value("a_tape")-63.25f)<.02f&&std::abs(p.value("b_space")-83.5f)<.02f&&std::abs(p.value("morph")-37.4f)<.02f&&p.getCurrentProgram()==3,"state round trip preserves scene, morph and program");
    check(std::abs(p.value("modWobble")+22)<.02f,"state preserves negative modulation depth");
    check(std::abs(p.value("a_grit")-56)<.02f&&std::abs(p.value("b_gritDetail")-81)<.02f&&p.value("delta")==1&&p.value("autoGain")==1,"state preserves Grit and monitoring controls");
    auto legacy=p.parameters.copyState();
    for(int i=legacy.getNumChildren();--i>=0;) {
        const auto id=legacy.getChild(i).getProperty("id").toString();
        if(id.containsIgnoreCase("grit")||id=="bypass"||id=="delta"||id=="autoGain")legacy.removeChild(i,nullptr);
    }
    juce::MemoryBlock legacyData;juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),legacyData);
    p.setValue("bypass",1);p.setStateInformation(legacyData.getData(),static_cast<int>(legacyData.getSize()));
    check(p.value("a_grit")==0&&p.value("b_grit")==0&&p.value("a_gritTone")==50&&p.value("b_gritDetail")==35&&std::abs(p.value("modGrit"))<.0001f&&p.value("bypass")==0&&p.value("delta")==0&&p.value("autoGain")==0,"legacy states reset missing additive controls to neutral defaults");
    p.copyScene(0,1);bool copied=true;
    for(int m=0;m<alpha::moduleCount;++m)for(int f=0;f<3;++f)copied&=p.value(p.sceneId(0,m,f))==p.value(p.sceneId(1,m,f));
    check(copied,"copy scene includes amount and detail parameters");
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

    p.setBusesLayout(mono);p.prepareToPlay(44100,512);juce::AudioBuffer<float> monoBuffer(1,512);fill(monoBuffer,0,44100);p.processBlock(monoBuffer,midi);
    check(monoBuffer.getNumChannels()==1&&std::isfinite(monoBuffer.getSample(0,100)),"mono bus render");
    p.setBusesLayout(mismatch); // rejected, keeps previous valid bus layout
    p.setCurrentProgram(0);
    p.setBusesLayout(juce::AudioProcessor::BusesLayout{{juce::AudioChannelSet::stereo()},{juce::AudioChannelSet::stereo()}});
    p.prepareToPlay(48000,256);for(int i=0;i<100;++i){fill(block,i*256);p.processBlock(block,midi);}
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
        snapshot(*editor,directory.getChildFile("alpha-sound.png"));
        for(auto tab: {"MOTION","PRESETS"})if(auto* button=findButton(*editor,tab)) {
            if(button->onClick)button->onClick();snapshot(*editor,directory.getChildFile(juce::String("alpha-")+juce::String(tab).toLowerCase()+".png"));
        }
        // Exercise the actual native components and their APVTS attachments.
        // Capture references first so these edits do not alter the design QA images.
        if(auto* button=findButton(*editor,"PRESETS"))if(button->onClick)button->onClick();
        if(auto* button=findButton(*editor,"GRIT"))if(button->onClick)button->onClick();
        auto* gritA=dynamic_cast<juce::Slider*>(findComponent(*editor,"a_gritTone"));
        if(gritA) {if(gritA->onDragStart)gritA->onDragStart();gritA->setValue(34,juce::sendNotificationSync);}
        check(gritA!=nullptr&&std::abs(p.value("a_gritTone")-34)<.02f,"Grit selection binds its A-scene character slider");
        if(auto* button=findButton(*editor,"B"))if(button->onClick)button->onClick();
        auto* gritB=dynamic_cast<juce::Slider*>(findComponent(*editor,"b_gritTone"));
        if(gritB) {if(gritB->onDragStart)gritB->onDragStart();gritB->setValue(62,juce::sendNotificationSync);}
        check(gritB!=nullptr&&std::abs(p.value("b_gritTone")-62)<.02f&&std::abs(p.value("a_gritTone")-34)<.02f,"scene B rebinds character controls and preserves scene A");
        if(auto* button=findButton(*editor,"MAIN"))if(button->onClick)button->onClick();
        auto* morph=dynamic_cast<juce::Slider*>(findComponent(*editor,"morph"));
        if(morph) {if(morph->onDragStart)morph->onDragStart();morph->setValue(37,juce::sendNotificationSync);}
        check(morph!=nullptr&&std::abs(p.value("morph")-37)<.02f,"Main morph control updates the shared scene parameter");
        const auto deltaBefore=p.value("delta");
        auto* deltaButton=findButton(*editor,"delta",true);
        if(deltaButton&&deltaButton->onClick)deltaButton->onClick();
        check(deltaButton!=nullptr&&std::abs(p.value("delta")-(deltaBefore>.5f?0.0f:1.0f))<.01f,"Delta button toggles the processor monitor parameter");
        auto* undoButton=findButton(*editor,"undo");
        if(undoButton&&undoButton->onClick)undoButton->onClick();
        check(undoButton!=nullptr&&std::abs(p.value("delta")-deltaBefore)<.01f&&std::abs(p.value("morph")-37)<.02f,"editor undo restores the prior processor state");
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
        p.setValue("lfoSync",1);
        if(auto* button=findButton(*editor,"MAIN"))if(button->onClick)button->onClick();
        auto* freeRate=dynamic_cast<juce::Slider*>(findComponent(*editor,"lfoRate"));
        check(freeRate!=nullptr&&!freeRate->isEnabled(),"Main free-rate control is disabled during host sync");
        editor->setSize(900,600);snapshot(*editor,directory.getChildFile("alpha-75-percent.png"));
        editor->setSize(1800,1200);snapshot(*editor,directory.getChildFile("alpha-150-percent.png"));
    }
    std::cout<<"Result: "<<failures<<" failures\n";return failures?1:0;
}
