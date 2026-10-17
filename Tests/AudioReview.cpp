#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
struct ReviewClock final : juce::AudioPlayHead {
    double ppq=0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;p.setBpm(120);p.setPpqPosition(ppq);p.setIsPlaying(true);p.setTimeSignature(TimeSignature{4,4});return p;
    }
};
struct Metrics {
    double rms=0,peak=0,dc=0,correlation=0,step=0;
};
Metrics measure(const juce::AudioBuffer<float>& b,int frames) {
    Metrics m;double l2=0,r2=0,lr=0,sum=0;
    for(int i=0;i<frames;++i) {
        const double l=b.getSample(0,i),r=b.getSample(1,i);
        if(!std::isfinite(l)||!std::isfinite(r))throw std::runtime_error("non-finite audio");
        l2+=l*l;r2+=r*r;lr+=l*r;sum+=l+r;m.peak=std::max({m.peak,std::abs(l),std::abs(r)});
        if(i>0)m.step=std::max({m.step,std::abs(l-b.getSample(0,i-1)),std::abs(r-b.getSample(1,i-1))});
    }
    m.rms=std::sqrt((l2+r2)/(2*frames));m.dc=sum/(2*frames);m.correlation=lr/std::sqrt(std::max(1e-30,l2*r2));return m;
}
double db(double x){return 20*std::log10(std::max(1e-12,x));}
bool write(const juce::File& f,const juce::AudioBuffer<float>& b,double sr) {
    f.deleteFile();auto out=f.createOutputStream();if(!out)return false;
    juce::WavAudioFormat wav;
    auto writer=std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(out.get(),sr,2,24,{},0));
    if(!writer)return false;out.release();return writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples());
}
juce::String slug(juce::String s){return s.toLowerCase().replaceCharacters(" /+", "---").retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789-");}
}

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc!=3){std::cerr<<"Usage: AlphaAudioReview original.wav output-directory\n";return 2;}
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    auto reader=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(juce::File(argv[1])));
    if(!reader||reader->numChannels<1||reader->lengthInSamples<1){std::cerr<<"Cannot read source\n";return 2;}
    const double sr=reader->sampleRate;const int frames=static_cast<int>(reader->lengthInSamples),tail=static_cast<int>(sr*4),warm=static_cast<int>(sr*2);
    juce::AudioBuffer<float> dry(2,frames);reader->read(&dry,0,frames,0,true,true);
    if(reader->numChannels==1)dry.copyFrom(1,0,dry,0,0,frames);
    const auto dryMetrics=measure(dry,frames);
    const juce::File directory(argv[2]);directory.createDirectory();
    if(!write(directory.getChildFile("00-dry.wav"),dry,sr))return 2;
    std::ostringstream csv;csv<<"kind,engine,mode,rms_dbfs,peak_dbfs,dc,correlation,max_adjacent_step,match_gain_db,render_ms,audio_seconds\n"<<std::fixed<<std::setprecision(6);
    int failures=0,count=0;double largest=0;
    auto render=[&](AlphaAudioProcessor& p,const juce::String& kind,const juce::String& engine,const juce::String& mode,const juce::String& filename) {
        ReviewClock clock;p.setPlayHead(&clock);p.prepareToPlay(sr,256);juce::MidiBuffer midi;
        juce::AudioBuffer<float> result(2,frames+tail),block(2,256);const double start=juce::Time::getMillisecondCounterHiRes();
        for(int offset=-warm;offset<frames+tail;offset+=256) {
            const int n=std::min(256,frames+tail-offset);block.setSize(2,n,false,false,true);block.clear();
            for(int i=0;i<n;++i) {
                const int pos=offset+i;
                if(pos<frames)for(int c=0;c<2;++c)block.setSample(c,i,dry.getSample(c,(pos%frames+frames)%frames));
            }
            clock.ppq=static_cast<double>(offset+warm)/sr*2;p.processBlock(block,midi);
            if(offset+n>0){const int from=std::max(0,-offset),dest=std::max(0,offset);for(int c=0;c<2;++c)result.copyFrom(c,dest,block,c,from,n-from);}
        }
        const double elapsed=juce::Time::getMillisecondCounterHiRes()-start;p.setPlayHead(nullptr);
        const auto metrics=measure(result,frames),all=measure(result,frames+tail);largest=std::max(largest,all.peak);
        if(all.peak>8||std::abs(metrics.dc)>.02)++failures;
        // Global, linked, offline RMS matching. Peak cap avoids clipping the
        // 24-bit listening file; the CSV records the actual applied gain.
        const double gain=std::min(dryMetrics.rms/std::max(1e-12,metrics.rms),std::pow(10.0,-1.0/20)/std::max(1e-12,all.peak));
        result.applyGain(static_cast<float>(gain));
        if(!write(directory.getChildFile(filename+".wav"),result,sr))++failures;
        csv<<kind<<','<<engine<<','<<mode<<','<<db(metrics.rms)<<','<<db(all.peak)<<','<<metrics.dc<<','<<metrics.correlation<<','<<metrics.step<<','<<db(gain)<<','<<elapsed<<','<<(frames+warm+tail)/sr<<'\n';
        std::cout<<"RENDER "<<filename<<" peak="<<db(all.peak)<<"dBFS gain="<<db(gain)<<"dB\n";++count;
    };
    try {
        for(int m=0;m<alpha::moduleCount;++m)for(int mode=0;mode<AlphaAudioProcessor::modeNames(m).size();++mode) {
            AlphaAudioProcessor p;p.setCurrentProgram(0);p.setValue("character",100);p.setValue("mix",100);p.setValue("lfoDepth",100);
            for(int j=0;j<alpha::moduleCount;++j){p.setValue(p.sceneId(0,j),j==m?70:0);p.setValue("mod"+juce::String(p.moduleNames[(size_t)j]),0);}
            p.setModuleMode(m,mode);
            if(m==6){p.setValue(p.sceneId(0,m,1),79.1667f);p.setValue(p.sceneId(0,m,4),25);}
            if(m==7){p.setValue(p.sceneId(0,m,1),40);p.setValue(p.sceneId(0,m,2),35);p.setValue(p.sceneId(0,m,4),18);}
            if(m==8){p.setValue(p.sceneId(0,m,2),15);p.setValue(p.motionId(m,"Enabled"),1);p.setValue(p.motionId(m,"Division"),4);p.setValue(p.motionId(m,"Shape"),7);}
            if(m==9){p.setValue(p.sceneId(0,m,1),58);p.setValue(p.sceneId(0,m,2),25);p.setValue(p.sceneId(0,m,3),25);}
            if(m==10){p.setValue(p.sceneId(0,m,1),72);p.setValue(p.sceneId(0,m,2),80);}
            if(m==11){p.setValue(p.sceneId(0,m,1),80);p.setValue(p.sceneId(0,m,2),35);p.setValue(p.sceneId(0,m,4),35);}
            const juce::String engine=p.moduleNames[(size_t)m],name=p.modeNames(m)[mode];
            render(p,"mode",engine,name,"mode-"+slug(engine)+"-"+slug(name));
        }
        for(int i=0;i<17;++i){AlphaAudioProcessor p;p.setCurrentProgram(i);render(p,"preset","ALL",p.getProgramName(i),"preset-"+juce::String(i).paddedLeft('0',2)+"-"+slug(p.getProgramName(i)));}
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    directory.getChildFile("metrics.csv").replaceWithText(csv.str());
    std::cout<<"Result: "<<count<<" renders, "<<failures<<" failures, maximum raw peak "<<largest<<'\n';return failures?1:0;
}
