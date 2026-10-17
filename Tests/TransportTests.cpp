#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iostream>

namespace {
int failures=0;
void check(bool condition,const char* description) {
    std::cout<<(condition?"PASS ":"FAIL ")<<description<<'\n';
    if(!condition)++failures;
}
double wrap(double phase) { return phase-std::floor(phase); }
struct PlayHead final : juce::AudioPlayHead {
    double ppq=6,bpm=120;
    int numerator=4,denominator=4;
    bool playing=true,valid=true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;info.setBpm(bpm);info.setTimeSignature(TimeSignature{numerator,denominator});info.setIsPlaying(playing);
        if(valid)info.setPpqPosition(ppq);
        return info;
    }
};
struct DuringPresetLoad final : juce::AudioProcessorValueTreeState::Listener {
    AlphaAudioProcessor& processor;
    PlayHead& playHead;
    juce::AudioBuffer<float> buffer{2,128};
    juce::MidiBuffer midi;
    bool armed=false,fired=false;
    explicit DuringPresetLoad(AlphaAudioProcessor& p,PlayHead& h):processor(p),playHead(h) {}
    void parameterChanged(const juce::String&,float) override {
        if(!armed||fired)return;
        fired=true;
        // Parameter listeners run synchronously inside the preset transaction.
        // Render several callbacks there to deterministically exercise the
        // rejected-snapshot path without racing another thread.
        playHead.bpm=90;playHead.numerator=3;playHead.denominator=4;
        playHead.ppq=7.5;playHead.playing=false;
        render();
        check(std::abs(processor.engine.getLfoPhase()-.5)<1e-5,"pending preset uses current stopped PPQ and new meter");
        check(std::abs(processor.engine.getLfoPhase(0)-.5)<1e-5,"pending preset preserves independent sync division");
        render();
        check(std::abs(processor.engine.getLfoPhase()-.5)<1e-5,"repeated stopped callbacks do not advance synced motion");
        playHead.ppq=7.75;playHead.playing=true;
        render();
        const double global=wrap(7.75/3+128*.5/48000);
        const double independent=wrap(7.75+128*1.5/48000);
        check(std::abs(processor.engine.getLfoPhase()-global)<1e-5,"pending preset refreshes PPQ and tempo on resume");
        check(std::abs(processor.engine.getLfoPhase(0)-independent)<1e-5,"independent motion advances at current host tempo during transaction");
        playHead.valid=false;
        render();
        check(std::abs(processor.engine.getLfoPhase()-wrap(global+128*.5/48000))<1e-5,"missing PPQ free-runs from current phase during transaction");
    }
    void render() {
        for(int channel=0;channel<2;++channel)for(int sample=0;sample<buffer.getNumSamples();++sample)
            buffer.setSample(channel,sample,(float)(.1*std::sin((sample+channel*71)*.09)));
        juce::AudioBuffer<float> expected;expected.makeCopyOf(buffer);
        processor.processBlock(buffer,midi);
        double maximum=0;
        for(int channel=0;channel<2;++channel)for(int sample=0;sample<buffer.getNumSamples();++sample)
            maximum=std::max(maximum,std::abs((double)buffer.getSample(channel,sample)-expected.getSample(channel,sample)));
        check(maximum<1e-7,"pending preset retains the prior complete neutral sound");
    }
};
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    AlphaAudioProcessor processor;
    PlayHead playHead;processor.setPlayHead(&playHead);
    for(int module=0;module<alpha::moduleCount;++module) {
        for(int scene=0;scene<2;++scene)processor.setValue(processor.sceneId(scene,module),0);
        processor.setValue("mod"+juce::String(processor.moduleNames[(size_t)module]),0);
    }
    processor.setValue("character",100);processor.setValue("mix",100);processor.setValue("output",0);
    processor.setValue("flux",0);processor.setValue("autoGain",0);processor.setValue("mainTone",50);processor.setValue("mainWidth",50);
    processor.setValue("lfoSync",1);processor.setValue("lfoDivision",2);
    processor.setValue(processor.motionId(0,"Enabled"),1);processor.setValue(processor.motionId(0,"Sync"),1);processor.setValue(processor.motionId(0,"Division"),4);
    processor.prepareToPlay(48000,128);
    juce::AudioBuffer<float> buffer(2,128);buffer.clear();juce::MidiBuffer midi;
    processor.processBlock(buffer,midi);
    DuringPresetLoad listener(processor,playHead);
    processor.parameters.addParameterListener("a_tape",&listener);
    listener.armed=true;processor.setCurrentProgram(0);
    processor.parameters.removeParameterListener("a_tape",&listener);
    processor.setPlayHead(nullptr);
    check(listener.fired,"preset load exercised a callback while its parameter epoch was incomplete");
    std::cout<<"Result: "<<failures<<" failures\n";
    return failures==0?0:1;
}
