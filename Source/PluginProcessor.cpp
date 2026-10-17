#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace {
struct ParameterTransaction {
    juce::ScopedLock lock;
    std::atomic<uint32_t>& epoch;
    ParameterTransaction(juce::CriticalSection& mutex,std::atomic<uint32_t>& value) : lock(mutex),epoch(value) { epoch.fetch_add(1,std::memory_order_acq_rel); }
    ~ParameterTransaction() { epoch.fetch_add(1,std::memory_order_release); }
};
struct FactoryPreset {
    const char* name;
    const char* category;
    const char* description;
    std::array<float,5> a, b, tone, detail, routes;
    std::array<int,5> modes;
    float character, mix, rate, depth;
    int shape, division;
};
const std::array<FactoryPreset, 10> presets {{
    { "Pocket Cassette", "Warm", "Rounded tape, a wandering capstan and a little dust. Made for keys, guitars and imperfect loops.",
      {42,22,9,16,18}, {68,48,24,32,35}, {43,32,38,45,35}, {32,40,80,30,34}, {0,28,0,0,12}, {0,0,1,0,0}, 70,100,.25f,50,0,2 },
    { "Soft Focus", "Warm", "Gentle tape density and a short, soft room. A quiet lift for a bus or a clean instrument.",
      {30,5,0,8,12}, {55,15,3,18,28}, {57,22,45,50,30}, {15,24,90,20,18}, {5,0,0,0,6}, {1,0,1,0,0}, 65,100,.12f,25,0,1 },
    { "Loose Capstan", "Moving", "Slow uneven pitch movement for sustained notes. Scene B lets the machine drift further.",
      {35,52,4,10,20}, {58,82,9,21,40}, {36,15,35,42,40}, {40,68,85,25,42}, {0,45,0,0,12}, {0,1,1,0,1}, 80,85,.12f,65,0,0 },
    { "Drum Room 04", "Warm", "Thickened transients and a close room. Keep scene A tight; push B for a more crowded space.",
      {28,0,2,35,24}, {48,5,7,57,42}, {60,40,48,56,20}, {18,20,95,40,15}, {0,0,0,10,8}, {1,2,2,1,0}, 70,80,.5f,30,1,3 },
    { "Night Bus", "Warm", "Dark harmonic weight with a restrained tail. For low-lit chords and drums that need some glue.",
      {55,10,3,30,22}, {75,28,8,50,38}, {25,22,25,32,35}, {50,30,85,38,32}, {8,14,0,8,10}, {1,0,1,1,1}, 75,100,.2f,35,0,2 },
    { "Bent Chorus", "Moving", "A moving stereo silhouette. Flutter and a small plate turn a static pad into a living part.",
      {20,55,0,8,35}, {40,85,5,24,60}, {58,62,50,58,50}, {18,35,90,22,52}, {0,60,0,0,24}, {1,2,1,0,1}, 75,65,.5f,65,0,3 },
    { "Paper Cone", "Broken", "Narrow tape and a folded edge. Pull the mix back for grit that sits around the original.",
      {62,15,12,42,8}, {85,35,28,75,18}, {22,35,30,40,18}, {72,55,85,68,15}, {8,15,0,22,0}, {2,0,2,2,0}, 80,70,.8f,45,1,4 },
    { "Last Repeat", "Spacious", "A blurred, slowly moving tail. Morph toward B to dissolve chords into the room.",
      {28,18,3,12,55}, {55,48,12,25,90}, {35,18,40,40,74}, {35,65,80,32,80}, {10,24,0,0,48}, {0,1,1,0,2}, 80,90,.12f,60,0,0 },
    { "Bad Reception", "Broken", "Grain, damaged pitch and soft foldback. Small amounts add texture; scene B starts to unravel.",
      {48,38,22,35,14}, {82,75,48,68,40}, {35,48,58,44,32}, {65,85,70,62,44}, {15,42,28,20,12}, {2,0,2,2,0}, 82,78,.7f,60,3,4 },
    { "Open Water", "Spacious", "A wide, weightless room with a slow pulse. Leave space between notes and let the tail breathe.",
      {15,20,0,5,62}, {35,45,5,20,95}, {58,20,48,55,82}, {20,50,95,20,90}, {0,24,0,0,55}, {1,1,1,0,2}, 75,85,.1f,60,0,0 }
}};
juce::AudioParameterFloatAttributes percentAttributes() {
    return juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction([](float v,int){return juce::String(juce::roundToInt(v));});
}
}

juce::String AlphaAudioProcessor::sceneId(int scene,int module,int parameter) {
    static const char* suffixes[]{"","Tone","Detail","Third","Fourth"};
    return juce::String(scene == 0 ? "a_" : "b_") + moduleIds[(size_t)juce::jlimit(0,alpha::moduleCount-1,module)] + suffixes[juce::jlimit(0,4,parameter)];
}
juce::String AlphaAudioProcessor::motionId(int module,const juce::String& suffix) {
    return juce::String(moduleIds[(size_t)juce::jlimit(0,alpha::moduleCount-1,module)])+"Motion"+suffix;
}
juce::StringArray AlphaAudioProcessor::modeNames(int module) {
    switch(module) {
        case 0:return {"Cassette","Reel-to-reel","VHS"};
        case 1:return {"Wow + flutter","Wow","Flutter"};
        case 2:return {"Vinyl","Hiss","Mechanical"};
        case 3:return {"Soft clip","Tube","Fold","Diode","Fuzz","Tape"};
        case 4:return {"Room","Plate","Bloom"};
        case 5:return {"Crush","Decimate","Digital"};
        case 6:return {"Chromatic","Smooth","Octave"};
        case 7:return {"Slip","Reverse","Repeat"};
        case 8:return {"Pump","Tremolo","Gate"};
        case 9:return {"Lowpass","Bandpass","Highpass"};
        case 10:return {"Balance","Auto pan","Rotate"};
        default:return {"Mid / side","Haas","Mono"};
    }
}
juce::String AlphaAudioProcessor::toneName(int module) {
    static const char* names[]{"Age","Rate","Grain","Tone","Size","Bits","Transpose","Length","Curve","Cutoff","Position","Width"}; return names[juce::jlimit(0,11,module)];
}
int AlphaAudioProcessor::effectiveMode(int module) const noexcept {
    const auto j=(size_t)juce::jlimit(0,alpha::moduleCount-1,module);
    const int legacy=juce::roundToInt(modeValues[j]->load(std::memory_order_relaxed));
    const int extended=module==3?juce::roundToInt(heatModelValue->load(std::memory_order_relaxed)):0;
    return extended>0?extended+2:legacy;
}
void AlphaAudioProcessor::setModuleMode(int module,int mode) {
    if(module==3){if(mode<3){setValue("driveMode",(float)juce::jlimit(0,2,mode));setValue("heatModel",0);}else setValue("heatModel",(float)juce::jlimit(1,3,mode-2));}
    else setValue(juce::String(moduleIds[(size_t)juce::jlimit(0,alpha::moduleCount-1,module)])+"Mode",(float)mode);
}
juce::String AlphaAudioProcessor::detailName(int module) {
    static const char* names[]{"Bump","Shape","Tone","Bias","Decay","Rate","Grain","Speed","Floor","Resonance","Depth","Bass mono"}; return names[juce::jlimit(0,11,module)];
}
juce::String AlphaAudioProcessor::thirdName(int module) {
    static const char* names[]{"Dropouts","Flutter","Follow","Knee","Damp","Jitter","Fine","Smooth","Attack","Motion","Phase","Motion"}; return names[juce::jlimit(0,11,module)];
}
juce::String AlphaAudioProcessor::fourthName(int module) {
    static const char* names[]{"Squash","Spread","Width","Sag","Predelay","Smooth","Spread","Feedback","Release","Drive","Smooth","Delay"}; return names[juce::jlimit(0,11,module)];
}
juce::String AlphaAudioProcessor::parameterValueText(int m,int field,float normalized) {
    const double v=juce::jlimit(0.0,1.0,(double)normalized*.01);
    const auto number=[](double value,int places=1){return places==0?juce::String(juce::roundToInt(value)):juce::String(std::abs(value)<.00005?0.0:value,places);};
    const auto hz=[&](double f){return f>=1000?number(f/1000,f>=10000?1:2)+" kHz":number(f,f<1?3:f<20?2:1)+" Hz";};
    if(m==0){if(field==2)return number(v*6)+" dB";if(field==3)return number(v*30)+" /min";if(field==4)return number(1+v*3,2)+":1";}
    if(m==1&&field==1)return hz(.05*std::pow(400.0,v));
    if((m==2&&field==2)||(m==3&&field==1)){const auto db=(v-.5)*12;return (db>.00005?juce::String("+"):juce::String())+number(db)+" dB";}
    if(m==4){if(field==1)return number(.5*std::pow(4.0,v),2)+" x";if(field==2)return number(.2*std::pow(100.0,v),2)+" s";if(field==3)return hz(1000*std::pow(16.0,v));if(field==4)return number(250*v*v)+" ms";}
    if(m==5){if(field==1)return number(16*std::pow(.125,v),1)+" bit";if(field==2)return hz(48000*std::pow(1.0/48,v));}
    if(m==6){if(field==1)return number((v-.5)*24)+" st";if(field==2)return number(24+96*v)+" ms";if(field==3)return number((v-.5)*100)+" ct";if(field==4)return number(v*20)+" ct";}
    if(m==7){if(field==1){static const char* lengths[]{"1/8 cycle","1/4 cycle","1/2 cycle","1 cycle","2 cycles","4 cycles"};return lengths[juce::jlimit(0,5,static_cast<int>(std::floor(v*5+.5)))];}if(field==2)return number(v*2,2)+" x";if(field==3)return number(v*100,0)+"%";if(field==4)return number(v*85,0)+"%";}
    if(m==8){if(field==1)return number(.25*std::pow(16.0,v),2)+" x";if(field==2)return v<1e-6?"-inf dB":number(-60+60*v)+" dB";if(field==3)return number(.2+99.8*v*v)+" ms";if(field==4)return number(5+995*v*v)+" ms";}
    if(m==9){if(field==1)return hz(20*std::pow(1000.0,v));if(field==2)return "Q "+number(.5+11.5*v*v,2);if(field==4)return number(v*24)+" dB";}
    if(m==10){if(field==1)return std::abs(v-.5)<.005?"centre":number(std::abs(v*2-1)*100,0)+(v<.5?" L":" R");if(field==3)return number(v*360,0)+juce::String::fromUTF8("°");if(field==4)return number(.2+199.8*v*v)+" ms";}
    if(m==11){if(field==1)return number(v*200,0)+"%";if(field==2)return v<1e-6?"off":hz(20*std::pow(25.0,v));if(field==4)return number(v*30)+" ms";}
    return number(v*100,0)+"%";
}
float AlphaAudioProcessor::parameterValueFromText(int m,int field,const juce::String& input) {
    auto text=input.trim().toLowerCase().replaceCharacter((juce::juce_wchar)0x2212,'-').replaceCharacter(',','.');
    // A ratio's denominator is a unit, not another digit in its numerator.
    const auto numeric=(m==0&&field==4)?text.upToFirstOccurrenceOf(":",false,false):text;
    double x=numeric.retainCharacters("-0123456789.").getDoubleValue();
    if(text.contains("khz")||text.endsWith("k"))x*=1000;
    double v=x*.01;
    if(m==0){if(field==2)v=x/6;if(field==3)v=x/30;if(field==4)v=(x-1)/3;}
    if(m==1&&field==1)v=std::log(juce::jmax(.05,x)/.05)/std::log(400.0);
    if((m==2&&field==2)||(m==3&&field==1))v=.5+x/12;
    if(m==4){if(field==1)v=std::log(juce::jmax(.5,x)/.5)/std::log(4.0);if(field==2)v=std::log(juce::jmax(.2,x)/.2)/std::log(100.0);if(field==3)v=std::log(juce::jmax(1000.0,x)/1000)/std::log(16.0);if(field==4)v=std::sqrt(juce::jmax(0.0,x)/250);}
    if(m==5){if(field==1)v=std::log(juce::jmax(2.0,x)/16)/std::log(.125);if(field==2)v=std::log(juce::jmax(1000.0,x)/48000)/std::log(1.0/48);}
    if(m==6){if(field==1)v=.5+x/24;if(field==2)v=(x-24)/96;if(field==3)v=.5+x/100;if(field==4)v=x/20;}
    if(m==7){if(field==1){const juce::StringArray lengths{"1/8 cycle","1/4 cycle","1/2 cycle","1 cycle","2 cycles","4 cycles"};const int index=lengths.indexOf(text);v=index>=0?index*.2:x*.01;}if(field==2)v=x/2;if(field==3)v=x*.01;if(field==4)v=x/85;}
    if(m==8){if(field==1)v=std::log(juce::jmax(.25,x)/.25)/std::log(16.0);if(field==2)v=text.contains("inf")?0:(x+60)/60;if(field==3)v=std::sqrt(juce::jmax(0.0,x-.2)/99.8);if(field==4)v=std::sqrt(juce::jmax(0.0,x-5)/995);}
    if(m==9){if(field==1)v=std::log(juce::jmax(20.0,x)/20)/std::log(1000.0);if(field==2)v=std::sqrt(juce::jmax(0.0,x-.5)/11.5);if(field==4)v=x/24;}
    if(m==10){if(field==1)v=text=="centre"?.5:.5+(text.contains("l")?-1:1)*x/200;if(field==3)v=x/360;if(field==4)v=std::sqrt(juce::jmax(0.0,x-.2)/199.8);}
    if(m==11){if(field==1)v=x/200;if(field==2)v=text=="off"?0:std::log(juce::jmax(20.0,x)/20)/std::log(25.0);if(field==4)v=x/30;}
    return (float)(juce::jlimit(0.0,1.0,std::isfinite(v)?v:0.0)*100);
}

juce::AudioProcessorValueTreeState::ParameterLayout AlphaAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    int versionHint=1;
    const auto addPercent = [&](juce::String id,juce::String name,float def) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,versionHint},name,juce::NormalisableRange<float>(0,100,.01f),def,percentAttributes()));
    };
    for(int scene=0;scene<2;++scene) for(int m=0;m<5;++m) {
        const auto prefix=juce::String(scene==0?"A ":"B ")+moduleNames[static_cast<size_t>(m)];
        addPercent(sceneId(scene,m,0),prefix+" Amount",scene==0?presets[0].a[static_cast<size_t>(m)]:presets[0].b[static_cast<size_t>(m)]);
        addPercent(sceneId(scene,m,1),prefix+" "+toneName(m),presets[0].tone[static_cast<size_t>(m)]);
        addPercent(sceneId(scene,m,2),prefix+" "+detailName(m),presets[0].detail[static_cast<size_t>(m)]);
    }
    for(int m=0;m<5;++m) {
        auto id=juce::String(moduleIds[static_cast<size_t>(m)]), name=juce::String(moduleNames[static_cast<size_t>(m)]);
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"On",1},name+" On",true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Mode",1},name+" Model",m==3?juce::StringArray{"Soft clip","Tube","Fold"}:modeNames(m),0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mod"+name,1},name+" Modulation",juce::NormalisableRange<float>(-100,100,.01f),presets[0].routes[static_cast<size_t>(m)],percentAttributes()));
    }
    addPercent("character","Character",70); addPercent("mix","Mix",100); addPercent("morph","Scene Morph",0);
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"Output",juce::NormalisableRange<float>(-18,12,.01f),0,juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"lfoRate",1},"Motion Rate",juce::NormalisableRange<float>(.03f,12.0f,.001f,.35f),.25f,juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"lfoSync",1},"Motion Tempo Sync",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"lfoDivision",1},"Motion Division",juce::StringArray{"4 bars","2 bars","1 bar","1/2","1/4","1/8","1/16"},2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"lfoShape",1},"Motion Shape",juce::StringArray{"Sine","Triangle","Square","Random"},0));
    addPercent("lfoDepth","Motion Depth",50);
    // Append new controls after the original 54 parameters. Hosts that persist
    // parameter indices as well as IDs can still address older sessions.
    for(int scene=0;scene<2;++scene) {
        const auto prefix=juce::String(scene==0?"A Grit ":"B Grit ");
        addPercent(sceneId(scene,5),prefix+"Amount",0);
        addPercent(sceneId(scene,5,1),prefix+"Bits",50);
        addPercent(sceneId(scene,5,2),prefix+"Rate",35);
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"gritOn",1},"Grit On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"gritMode",1},"Grit Model",modeNames(5),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"modGrit",1},"Grit Modulation",juce::NormalisableRange<float>(-100,100,.01f),0,percentAttributes()));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Global Bypass",false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"delta",1},"Delta Monitor",false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"autoGain",1},"Auto Gain",false));
    versionHint=2;
    const alpha::Scene defaults;
    const auto fieldName=[](int m,int f){return f==0?juce::String("Amount"):f==1?toneName(m):f==2?detailName(m):f==3?thirdName(m):fourthName(m);};
    const auto fieldDefault=[&](int m,int f){const size_t j=(size_t)m;return 100*(f==0?defaults.amount[j]:f==1?defaults.tone[j]:f==2?defaults.detail[j]:f==3?defaults.third[j]:defaults.fourth[j]);};
    for(int m=6;m<alpha::moduleCount;++m) {
        for(int scene=0;scene<2;++scene)for(int f=0;f<5;++f)addPercent(sceneId(scene,m,f),juce::String(scene==0?"A ":"B ")+moduleNames[(size_t)m]+" "+fieldName(m,f),fieldDefault(m,f));
        const auto id=juce::String(moduleIds[(size_t)m]),name=juce::String(moduleNames[(size_t)m]);
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"On",2},name+" On",true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Mode",2},name+" Model",modeNames(m),0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mod"+name,2},name+" Modulation",juce::NormalisableRange<float>(-100,100,.01f),0,percentAttributes()));
    }
    for(int m=0;m<6;++m)for(int scene=0;scene<2;++scene)for(int f=3;f<5;++f)addPercent(sceneId(scene,m,f),juce::String(scene==0?"A ":"B ")+moduleNames[(size_t)m]+" "+fieldName(m,f),fieldDefault(m,f));
    for(int m=0;m<alpha::moduleCount;++m) {
        const auto id=juce::String(moduleIds[(size_t)m]),name=juce::String(moduleNames[(size_t)m]);
        for(const auto* edge:{"Lo","Hi"})layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id+edge,2},name+" Focus "+edge,juce::NormalisableRange<float>(20,20000,1,.25f),juce::String(edge)=="Lo"?20:20000,juce::AudioParameterFloatAttributes().withLabel("Hz")));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"MotionTarget",2},name+" Motion Target",juce::StringArray{"Amount",toneName(m),detailName(m),thirdName(m),fourthName(m)},0));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{motionId(m,"Enabled"),2},name+" Independent Motion",false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{motionId(m,"Shape"),2},name+" Motion Shape",juce::StringArray{"Sine","Triangle","Square","Random","Ramp up","Ramp down","Smooth random","Custom"},0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{motionId(m,"Rate"),2},name+" Motion Rate",juce::NormalisableRange<float>(.05f,20.0f,.001f,.35f),.25f,juce::AudioParameterFloatAttributes().withLabel("Hz")));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{motionId(m,"Sync"),2},name+" Motion Sync",true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{motionId(m,"Division"),2},name+" Motion Division",juce::StringArray{"4 bars","2 bars","1 bar","1/2","1/4","1/8","1/16"},2));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{motionId(m,"Follow"),2},name+" Motion Follow Input",false));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{motionId(m,"Count"),2},name+" Motion Nodes",2,alpha::curveCapacity,4));
        const alpha::MotionSettings motion;
        for(int n=0;n<alpha::curveCapacity;++n)for(int axis=0;axis<3;++axis) {
            const auto suffix=juce::String(axis==0?"X":axis==1?"Y":"Bend")+juce::String(n);
            const auto point=motion.curve[(size_t)n];const float value=100*(axis==0?point.x:axis==1?point.y:point.curve);
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{motionId(m,suffix),2},name+" Curve "+suffix,juce::NormalisableRange<float>(axis==0?0.0f:-100.0f,100.0f,.001f),value,percentAttributes()));
        }
    }
    addPercent("flux","Flux",0);addPercent("mainTone","Main Tone",50);addPercent("mainWidth","Main Width",50);
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"fluxSeed",2},"Flux Seed",1,16777215,79581));
    // Retain the three-choice legacy parameter's normalized automation. New
    // transfers use an appended override; zero delegates to driveMode.
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"heatModel",2},"Heat Extended Model",juce::StringArray{"Legacy drive model","Diode","Fuzz","Tape"},0));
    return layout;
}

AlphaAudioProcessor::AlphaAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      parameters(*this,nullptr,"AlphaState",createLayout()) {
    for(int s=0;s<2;++s) for(int m=0;m<alpha::moduleCount;++m) for(int p=0;p<5;++p)
        sceneValues[static_cast<size_t>(s)][static_cast<size_t>(m*5+p)]=parameters.getRawParameterValue(sceneId(s,m,p));
    for(int m=0;m<alpha::moduleCount;++m) {
        auto id=juce::String(moduleIds[static_cast<size_t>(m)]),name=juce::String(moduleNames[static_cast<size_t>(m)]);
        onValues[static_cast<size_t>(m)]=parameters.getRawParameterValue(id+"On");
        modeValues[static_cast<size_t>(m)]=parameters.getRawParameterValue(id+"Mode");
        modValues[static_cast<size_t>(m)]=parameters.getRawParameterValue("mod"+name);
        lowValues[(size_t)m]=parameters.getRawParameterValue(id+"Lo");highValues[(size_t)m]=parameters.getRawParameterValue(id+"Hi");targetValues[(size_t)m]=parameters.getRawParameterValue(id+"MotionTarget");
        auto& motion=motionValues[(size_t)m];
        motion.independent=parameters.getRawParameterValue(motionId(m,"Enabled"));motion.rate=parameters.getRawParameterValue(motionId(m,"Rate"));
        motion.sync=parameters.getRawParameterValue(motionId(m,"Sync"));motion.division=parameters.getRawParameterValue(motionId(m,"Division"));
        motion.shape=parameters.getRawParameterValue(motionId(m,"Shape"));motion.follow=parameters.getRawParameterValue(motionId(m,"Follow"));motion.count=parameters.getRawParameterValue(motionId(m,"Count"));
        for(int n=0;n<alpha::curveCapacity;++n)for(int axis=0;axis<3;++axis)motion.curve[(size_t)n][(size_t)axis]=parameters.getRawParameterValue(motionId(m,juce::String(axis==0?"X":axis==1?"Y":"Bend")+juce::String(n)));
    }
    heatModelValue=parameters.getRawParameterValue("heatModel");
    morphValue=parameters.getRawParameterValue("morph"); characterValue=parameters.getRawParameterValue("character");
    mixValue=parameters.getRawParameterValue("mix"); outputValue=parameters.getRawParameterValue("output");
    rateValue=parameters.getRawParameterValue("lfoRate"); syncValue=parameters.getRawParameterValue("lfoSync");
    divisionValue=parameters.getRawParameterValue("lfoDivision"); shapeValue=parameters.getRawParameterValue("lfoShape");
    depthValue=parameters.getRawParameterValue("lfoDepth");
    bypassValue=parameters.getRawParameterValue("bypass");deltaValue=parameters.getRawParameterValue("delta");autoGainValue=parameters.getRawParameterValue("autoGain");
    fluxValue=parameters.getRawParameterValue("flux");mainToneValue=parameters.getRawParameterValue("mainTone");mainWidthValue=parameters.getRawParameterValue("mainWidth");fluxSeedValue=parameters.getRawParameterValue("fluxSeed");
    setCurrentProgram(10);
}

void AlphaAudioProcessor::prepareToPlay(double sampleRate,int blockSize) { engine.prepare(sampleRate,blockSize,getTotalNumOutputChannels()); }
double AlphaAudioProcessor::getTailLengthSeconds() const {
    if(onValues[7]==nullptr||onValues[7]->load(std::memory_order_relaxed)<.5f)return 35;
    const auto amount=std::max(sceneValues[0][7*5]->load(std::memory_order_relaxed),sceneValues[1][7*5]->load(std::memory_order_relaxed));
    if(amount<=0)return 35;
    double feedback=std::max(sceneValues[0][7*5+4]->load(std::memory_order_relaxed),sceneValues[1][7*5+4]->load(std::memory_order_relaxed))*.01;
    if(targetValues[7]->load(std::memory_order_relaxed)>3.5f)feedback+=std::abs(modValues[7]->load(std::memory_order_relaxed))*.01*depthValue->load(std::memory_order_relaxed)*.01;
    feedback=.85*std::clamp(feedback,0.0,1.0);
    if(feedback<=0)return 35;
    // Bound by the entire allocated history, rather than only the four-second
    // slice: Repeat's captured heads can read older material. Include a 60 dB
    // feedback decay plus downstream reverb and splice/history padding.
    return std::min(600.0,43+12.1*std::log(.001)/std::log(feedback));
}
void AlphaAudioProcessor::releaseResources() {}
void AlphaAudioProcessor::reset() { engine.reset(); }
bool AlphaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    auto out=layout.getMainOutputChannelSet();
    return (out==juce::AudioChannelSet::mono()||out==juce::AudioChannelSet::stereo()) && out==layout.getMainInputChannelSet();
}
void AlphaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    const auto epochBefore=parameterEpoch.load(std::memory_order_acquire);
    alpha::Settings settings;
    std::array<MotionTransport,alpha::moduleCount+1> transport;
    for(int s=0;s<2;++s) {
        auto& scene=s==0?settings.a:settings.b;
        for(size_t m=0;m<alpha::moduleCount;++m) {
            scene.amount[m]=sceneValues[static_cast<size_t>(s)][m*5]->load(std::memory_order_relaxed)*.01f;
            scene.tone[m]=sceneValues[static_cast<size_t>(s)][m*5+1]->load(std::memory_order_relaxed)*.01f;
            scene.detail[m]=sceneValues[static_cast<size_t>(s)][m*5+2]->load(std::memory_order_relaxed)*.01f;
            scene.third[m]=sceneValues[(size_t)s][m*5+3]->load(std::memory_order_relaxed)*.01f;scene.fourth[m]=sceneValues[(size_t)s][m*5+4]->load(std::memory_order_relaxed)*.01f;
        }
    }
    for(size_t m=0;m<alpha::moduleCount;++m) {
        settings.enabled[m]=onValues[m]->load(std::memory_order_relaxed)>.5f;
        settings.mode[m]=effectiveMode((int)m);
        settings.modulation[m]=modValues[m]->load(std::memory_order_relaxed)*.01f;
        settings.lowHz[m]=lowValues[m]->load(std::memory_order_relaxed);settings.highHz[m]=highValues[m]->load(std::memory_order_relaxed);settings.motionTarget[m]=juce::roundToInt(targetValues[m]->load(std::memory_order_relaxed));
    }
    settings.morph=morphValue->load(std::memory_order_relaxed)*.01f;
    settings.character=characterValue->load(std::memory_order_relaxed)*.01f;
    settings.mix=mixValue->load(std::memory_order_relaxed)*.01f;
    settings.outputDb=outputValue->load(std::memory_order_relaxed);
    settings.lfoDepth=depthValue->load(std::memory_order_relaxed)*.01f;
    settings.lfoShape=juce::roundToInt(shapeValue->load(std::memory_order_relaxed));
    settings.bypass=bypassValue->load(std::memory_order_relaxed)>.5f;
    settings.delta=deltaValue->load(std::memory_order_relaxed)>.5f;
    settings.autoGain=autoGainValue->load(std::memory_order_relaxed)>.5f;
    settings.flux=fluxValue->load(std::memory_order_relaxed)*.01f;settings.mainTone=mainToneValue->load(std::memory_order_relaxed)*.01f;settings.mainWidth=mainWidthValue->load(std::memory_order_relaxed)*.01f;settings.fluxSeed=(uint32_t)juce::roundToInt(fluxSeedValue->load(std::memory_order_relaxed));
    double bpm=120,beatsPerBar=4;
    if(auto* playHead=getPlayHead()) if(auto info=playHead->getPosition()) {
        settings.hostPlaying=info->getIsPlaying();if(auto ppq=info->getPpqPosition())if(std::isfinite(*ppq)){settings.hostPpq=*ppq;settings.hostPositionValid=true;}
        if(auto hostBpm=info->getBpm()) if(std::isfinite(*hostBpm)&&*hostBpm>0) bpm=juce::jlimit(10.0,999.0,*hostBpm);
        if(auto signature=info->getTimeSignature()) if(signature->numerator>0&&signature->denominator>0)
            beatsPerBar=static_cast<double>(signature->numerator)*4.0/static_cast<double>(signature->denominator);
    }
    settings.hostTempoBpm=bpm;tempo.store(bpm,std::memory_order_relaxed);
    const std::array<double,7> beats{beatsPerBar*4,beatsPerBar*2,beatsPerBar,2,1,.5,.25};
    for(int m=0;m<alpha::moduleCount;++m){auto& motion=settings.motions[(size_t)m];const auto& ptr=motionValues[(size_t)m];
        motion.independent=ptr.independent->load(std::memory_order_relaxed)>.5f;motion.followInput=ptr.follow->load(std::memory_order_relaxed)>.5f;
        motion.shape=juce::roundToInt(ptr.shape->load(std::memory_order_relaxed));motion.curveCount=juce::jlimit(2,alpha::curveCapacity,juce::roundToInt(ptr.count->load(std::memory_order_relaxed)));
        const int division=juce::jlimit(0,6,juce::roundToInt(ptr.division->load(std::memory_order_relaxed)));const bool sync=ptr.sync->load(std::memory_order_relaxed)>.5f;
        transport[(size_t)m]={sync,division};motion.rateHz=ptr.rate->load(std::memory_order_relaxed);
        for(int n=0;n<alpha::curveCapacity;++n){auto& point=motion.curve[(size_t)n];point.x=ptr.curve[(size_t)n][0]->load(std::memory_order_relaxed)*.01f;point.y=ptr.curve[(size_t)n][1]->load(std::memory_order_relaxed)*.01f;point.curve=ptr.curve[(size_t)n][2]->load(std::memory_order_relaxed)*.01f;}
    }
    transport[(size_t)alpha::moduleCount]={syncValue->load(std::memory_order_relaxed)>.5f,juce::jlimit(0,6,juce::roundToInt(divisionValue->load(std::memory_order_relaxed)))};
    settings.lfoRateHz=rateValue->load(std::memory_order_relaxed);
    const auto hostPpq=settings.hostPpq;const auto hostPositionValid=settings.hostPositionValid;const auto hostPlaying=settings.hostPlaying;
    if((epochBefore&1u)==0 && epochBefore==parameterEpoch.load(std::memory_order_acquire)) {coherentSettings=settings;coherentTransport=transport;}
    // A state/preset transaction holds only sound controls. Host time must stay
    // current on every callback, including while an incomplete edit is held.
    settings=coherentSettings;settings.hostPpq=hostPpq;settings.hostPositionValid=hostPositionValid;settings.hostPlaying=hostPlaying;settings.hostTempoBpm=bpm;
    const auto hostPhase=[&](int division){const double phase=hostPpq/beats[(size_t)division];return hostPositionValid?(float)(phase-std::floor(phase)):-1.0f;};
    for(size_t m=0;m<alpha::moduleCount;++m)if(coherentTransport[m].sync) {
        settings.motions[m].rateHz=(float)((bpm/60)/beats[(size_t)coherentTransport[m].division]);settings.motions[m].phaseFromHost=hostPhase(coherentTransport[m].division);
        settings.motions[m].hostCyclesPerBeat=1.0/beats[(size_t)coherentTransport[m].division];
    }
    const auto& globalTransport=coherentTransport[(size_t)alpha::moduleCount];
    if(globalTransport.sync) {settings.lfoRateHz=(float)((bpm/60)/beats[(size_t)globalTransport.division]);settings.phaseFromHost=hostPhase(globalTransport.division);settings.hostCyclesPerBeat=1.0/beats[(size_t)globalTransport.division];}
    engine.setSettings(settings);
    engine.process(buffer.getArrayOfWritePointers(),buffer.getNumChannels(),buffer.getNumSamples());
}
void AlphaAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) {}
juce::AudioProcessorEditor* AlphaAudioProcessor::createEditor() { return new AlphaAudioProcessorEditor(*this); }
namespace {
struct ExtraPreset {const char* name;const char* category;const char* description;};
const std::array<ExtraPreset,7> extraPresets {{{"Sunday Tape Loop","lo-fi / keys","Warm, focused tube colour, vinyl grain and a moving plate. The complete reference sound."},
{"Silver Doubler","Pitch","A wide, softly detuned double with a little plate. Scene B moves up a fifth."},
{"Spliced Memory","Time","A tempo-locked reverse slice, softened at the splice and held inside the original loop."},
{"Afterbeat","Rhythm","A smooth quarter-note pump with a gentle floor. The stereo image and bass stay anchored."},
{"Velvet Sweep","Filter","A resonant lowpass moving slowly through an otherwise warm, open loop."},
{"Orbit","Stereo","Constant-power movement with a restrained stereo spread and a quiet room."},
{"Open Frame","Stereo","A mono-safe mid/side lift with low frequencies anchored at the centre."}}};
}
int AlphaAudioProcessor::getNumPrograms() { return (int)(presets.size()+extraPresets.size()); }
const juce::String AlphaAudioProcessor::getProgramName(int i) {
    if(juce::isPositiveAndBelow(i,(int)presets.size()))return presets[(size_t)i].name;
    const int j=i-(int)presets.size();return juce::isPositiveAndBelow(j,(int)extraPresets.size())?extraPresets[(size_t)j].name:juce::String();
}
juce::String AlphaAudioProcessor::presetDescription(int i) {
    if(juce::isPositiveAndBelow(i,(int)presets.size()))return presets[(size_t)i].description;
    const int j=i-(int)presets.size();return juce::isPositiveAndBelow(j,(int)extraPresets.size())?extraPresets[(size_t)j].description:juce::String();
}
juce::String AlphaAudioProcessor::presetCategory(int i) {
    if(juce::isPositiveAndBelow(i,(int)presets.size()))return presets[(size_t)i].category;
    const int j=i-(int)presets.size();return juce::isPositiveAndBelow(j,(int)extraPresets.size())?extraPresets[(size_t)j].category:juce::String();
}
std::array<float,alpha::moduleCount> AlphaAudioProcessor::presetSignature(int i) {
    std::array<float,alpha::moduleCount> result{};
    if(juce::isPositiveAndBelow(i,(int)presets.size())){for(size_t m=0;m<5;++m)result[m]=presets[(size_t)i].a[m];return result;}
    result={22,30,52,62,44,12,0,0,0,0,0,0};
    if(i>10){result={20,5,2,12,12,0,0,0,0,0,0,0};const int module=i==11?6:i==12?7:i==13?8:i==14?9:i==15?10:11;result[(size_t)module]=i==11?36:i==12?48:i==13?75:i==14?72:70;}
    return result;
}
float AlphaAudioProcessor::presetMovement(int i) {
    if(juce::isPositiveAndBelow(i,(int)presets.size())){const auto& p=presets[(size_t)i];float v=0;for(float x:p.routes)v=juce::jmax(v,std::abs(x)*p.depth*.01f);return v;}
    return i==10?42.0f:i==11?15.0f:i==12?75.0f:i==13?68.0f:i==14?55.0f:i==15?60.0f:20.0f;
}
void AlphaAudioProcessor::setValue(const juce::String& id,float actual) {
    if(auto* parameter=parameters.getParameter(id)) {
        parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->convertTo0to1(actual)); parameter->endChangeGesture();
    }
}
float AlphaAudioProcessor::value(const juce::String& id) const {
    if(auto* v=parameters.getRawParameterValue(id))return v->load(std::memory_order_relaxed);
    return 0;
}
void AlphaAudioProcessor::setCurrentProgram(int i) {
    if(!juce::isPositiveAndBelow(i,getNumPrograms()))return;
    ParameterTransaction transaction(parameterTransactionLock,parameterEpoch);
    // Reset the entire sound, including independent motion curves. A factory
    // preset cannot inherit hidden routing or transport controls from the last.
    for(auto* parameter:getParameters())if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(parameter))ranged->setValueNotifyingHost(ranged->getDefaultValue());
    const alpha::Scene defaults;const auto signature=presetSignature(i);
    for(int m=0;m<alpha::moduleCount;++m) {
        const auto j=(size_t)m;
        for(int scene=0;scene<2;++scene){
            setValue(sceneId(scene,m),signature[j]);
            setValue(sceneId(scene,m,1),defaults.tone[j]*100);setValue(sceneId(scene,m,2),defaults.detail[j]*100);
            setValue(sceneId(scene,m,3),defaults.third[j]*100);setValue(sceneId(scene,m,4),defaults.fourth[j]*100);
        }
        setValue(juce::String(moduleIds[j])+"On",1);setValue(juce::String(moduleIds[j])+"Mode",0);setValue("mod"+juce::String(moduleNames[j]),0);
    }
    if(i<(int)presets.size()) {
        const auto& preset=presets[(size_t)i];
        for(int m=0;m<5;++m){const auto j=(size_t)m;setValue(sceneId(0,m),preset.a[j]);setValue(sceneId(1,m),preset.b[j]);
            // The historic programs retain their IDs and character balances;
            // new physical controls use the current model's sensible defaults.
            for(int scene=0;scene<2;++scene){if(m==0)setValue(sceneId(scene,m,1),preset.detail[j]);if(m==1)setValue(sceneId(scene,m,1),100*std::log((.08+1.12*std::pow(preset.tone[j]*.01,2))/.05)/std::log(400.0));if(m==2){setValue(sceneId(scene,m,2),preset.tone[j]);setValue(sceneId(scene,m,3),preset.detail[j]);}if(m==3){setValue(sceneId(scene,m,1),preset.tone[j]);setValue(sceneId(scene,m,3),preset.detail[j]);}if(m==4){setValue(sceneId(scene,m,1),preset.tone[j]);setValue(sceneId(scene,m,2),preset.detail[j]);}}
            setValue(juce::String(moduleIds[j])+"Mode",(float)preset.modes[j]);setValue("mod"+juce::String(moduleNames[j]),preset.routes[j]);}
        setValue("character",preset.character);setValue("mix",preset.mix);setValue("lfoRate",preset.rate);setValue("lfoSync",1);setValue("lfoDivision",(float)preset.division);setValue("lfoDepth",preset.depth);setValue("lfoShape",(float)preset.shape);
    } else {
        setValue("character",100);setValue("mix",100);setValue("autoGain",1);setValue("lfoDepth",100);
        if(i==10){
            const std::array<float,6> lows{20,160,2500,40,350,1800},highs{20000,20000,16000,2400,12000,9000};
            for(int m=0;m<6;++m){setValue(juce::String(moduleIds[(size_t)m])+"Lo",lows[(size_t)m]);setValue(juce::String(moduleIds[(size_t)m])+"Hi",highs[(size_t)m]);}
            setValue("driveMode",1);setValue("spaceMode",1);setValue("wobbleMode",1);
            for(int scene=0;scene<2;++scene){setValue(sceneId(scene,3,1),51.6667f);setValue(sceneId(scene,3,2),35);setValue(sceneId(scene,3,3),40);setValue(sceneId(scene,3,4),60);}
            setValue("driveMotionTarget",4);setValue("modDrive",25);setValue(motionId(3,"Enabled"),1);setValue(motionId(3,"Shape"),7);setValue(motionId(3,"Division"),4);
        } else {
            const int module=i==11?6:i==12?7:i==13?8:i==14?9:i==15?10:11;
            setValue(motionId(module,"Enabled"),1);setValue(motionId(module,"Division"),i==13?4:2);
            if(i==11){for(int scene=0;scene<2;++scene){setValue(sceneId(scene,6,1),scene==0?50:79.1667f);setValue(sceneId(scene,6,3),58);setValue(sceneId(scene,6,4),32);}setValue("pitchMode",1);}
            if(i==12){setValue("timeMode",1);for(int scene=0;scene<2;++scene)setValue(sceneId(scene,7,1),25);}
            if(i==13){setValue(motionId(8,"Shape"),7);for(int scene=0;scene<2;++scene){setValue(sceneId(scene,8,1),55);setValue(sceneId(scene,8,2),22);}}
            if(i==14){setValue("filterMotionTarget",1);setValue("modFilter",28);for(int scene=0;scene<2;++scene){setValue(sceneId(scene,9,1),52);setValue(sceneId(scene,9,2),32);}}
            if(i==15){setValue("panMode",1);for(int scene=0;scene<2;++scene)setValue(sceneId(scene,10,2),65);}
            if(i==16){for(int scene=0;scene<2;++scene){setValue(sceneId(scene,11,1),72);setValue(sceneId(scene,11,2),38);}setValue("widthMode",0);}
        }
    }
    setValue("morph",0);setValue("output",0);setValue("bypass",0);setValue("delta",0);
    currentProgram.store(i);updateHostDisplay(ChangeDetails().withProgramChanged(true));
}
void AlphaAudioProcessor::copyScene(int from,int to) {
    if(from<0||from>1||to<0||to>1)return;
    ParameterTransaction transaction(parameterTransactionLock,parameterEpoch);
    for(int m=0;m<alpha::moduleCount;++m)for(int p=0;p<5;++p)setValue(sceneId(to,m,p),value(sceneId(from,m,p)));
}
void AlphaAudioProcessor::swapScenes() {
    ParameterTransaction transaction(parameterTransactionLock,parameterEpoch);
    for(int m=0;m<alpha::moduleCount;++m)for(int p=0;p<5;++p) {
        auto a=sceneId(0,m,p),b=sceneId(1,m,p);const float va=value(a),vb=value(b);setValue(a,vb);setValue(b,va);
    }
    setValue("morph",100-value("morph"));
}
void AlphaAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    const juce::ScopedLock lock(parameterTransactionLock);
    auto state=parameters.copyState();state.setProperty("factoryProgram",currentProgram.load(),nullptr);state.setProperty("schemaVersion",2,nullptr);
    if(auto xml=state.createXml())copyXmlToBinary(*xml,dest);
}
void AlphaAudioProcessor::setStateInformation(const void* data,int size) {
    if(auto xml=getXmlFromBinary(data,size)) if(xml->hasTagName(parameters.state.getType())) {
        auto state=juce::ValueTree::fromXml(*xml);
        if(state.isValid()) {
            // Older alpha presets have no Grit or monitor parameters. Seed any
            // missing parameter with its declared default rather than the value
            // left behind by the previously loaded sound.
            for(auto* parameter:getParameters()) if(auto* identified=dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
                if(!state.getChildWithProperty("id",identified->paramID).isValid()) {
                    juce::ValueTree child("PARAM");
                    child.setProperty("id",identified->paramID,nullptr);
                    child.setProperty("value",identified->convertFrom0to1(identified->getDefaultValue()),nullptr);
                    state.appendChild(child,nullptr);
                }
            }
            ParameterTransaction transaction(parameterTransactionLock,parameterEpoch);
            currentProgram.store(juce::jlimit(0,getNumPrograms()-1,static_cast<int>(state.getProperty("factoryProgram",0))));
            parameters.replaceState(state);
        }
    }
}
juce::File AlphaAudioProcessor::userPresetDirectory() const {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Alpha Audio/Presets");
}
bool AlphaAudioProcessor::saveUserPreset(const juce::File& file) {
    juce::MemoryBlock data;getStateInformation(data);return file.replaceWithData(data.getData(),data.getSize());
}
bool AlphaAudioProcessor::loadUserPreset(const juce::File& file) {
    juce::MemoryBlock data;if(!file.loadFileAsData(data)||data.getSize()>1024*1024)return false;
    auto xml=getXmlFromBinary(data.getData(),static_cast<int>(data.getSize()));
    if(!xml||!xml->hasTagName(parameters.state.getType()))return false;
    setStateInformation(data.getData(),static_cast<int>(data.getSize()));
    updateHostDisplay(ChangeDetails().withProgramChanged(true));return true;
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AlphaAudioProcessor(); }
