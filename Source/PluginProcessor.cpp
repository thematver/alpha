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
      {42,22,9,16,18}, {68,48,24,32,35}, {43,32,38,45,35}, {32,40,80,30,34}, {0,28,0,0,12}, {0,0,0,0,0}, 70,100,.25f,50,0,2 },
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
    return juce::String(scene == 0 ? "a_" : "b_") + moduleIds[static_cast<size_t>(module)] + (parameter == 1 ? "Tone" : parameter == 2 ? "Detail" : "");
}
juce::StringArray AlphaAudioProcessor::modeNames(int module) {
    switch(module) {
        case 0:return {"Cassette","Reel-to-reel","VHS"};
        case 1:return {"Wow + flutter","Wow","Flutter"};
        case 2:return {"Vinyl","Hiss","Mechanical"};
        case 3:return {"Soft clip","Tube","Fold"};
        case 4:return {"Room","Plate","Bloom"};
        default:return {"Crush","Decimate","Digital"};
    }
}
juce::String AlphaAudioProcessor::toneName(int module) {
    static const char* names[]{"Tone","Rate","Color","Tone","Size","Bits"}; return names[module];
}
juce::String AlphaAudioProcessor::detailName(int module) {
    static const char* names[]{"Age","Drift","Follow","Shape","Decay","Rate"}; return names[module];
}

juce::AudioProcessorValueTreeState::ParameterLayout AlphaAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto addPercent = [&](juce::String id,juce::String name,float def) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(0,100,.01f),def,percentAttributes()));
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
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Mode",1},name+" Model",modeNames(m),0));
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
    return layout;
}

AlphaAudioProcessor::AlphaAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      parameters(*this,nullptr,"AlphaState",createLayout()) {
    for(int s=0;s<2;++s) for(int m=0;m<alpha::moduleCount;++m) for(int p=0;p<3;++p)
        sceneValues[static_cast<size_t>(s)][static_cast<size_t>(m*3+p)]=parameters.getRawParameterValue(sceneId(s,m,p));
    for(int m=0;m<alpha::moduleCount;++m) {
        auto id=juce::String(moduleIds[static_cast<size_t>(m)]),name=juce::String(moduleNames[static_cast<size_t>(m)]);
        onValues[static_cast<size_t>(m)]=parameters.getRawParameterValue(id+"On");
        modeValues[static_cast<size_t>(m)]=parameters.getRawParameterValue(id+"Mode");
        modValues[static_cast<size_t>(m)]=parameters.getRawParameterValue("mod"+name);
    }
    morphValue=parameters.getRawParameterValue("morph"); characterValue=parameters.getRawParameterValue("character");
    mixValue=parameters.getRawParameterValue("mix"); outputValue=parameters.getRawParameterValue("output");
    rateValue=parameters.getRawParameterValue("lfoRate"); syncValue=parameters.getRawParameterValue("lfoSync");
    divisionValue=parameters.getRawParameterValue("lfoDivision"); shapeValue=parameters.getRawParameterValue("lfoShape");
    depthValue=parameters.getRawParameterValue("lfoDepth");
    bypassValue=parameters.getRawParameterValue("bypass");deltaValue=parameters.getRawParameterValue("delta");autoGainValue=parameters.getRawParameterValue("autoGain");
}

void AlphaAudioProcessor::prepareToPlay(double sampleRate,int blockSize) { engine.prepare(sampleRate,blockSize,getTotalNumOutputChannels()); }
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
    for(int s=0;s<2;++s) {
        auto& scene=s==0?settings.a:settings.b;
        for(size_t m=0;m<alpha::moduleCount;++m) {
            scene.amount[m]=sceneValues[static_cast<size_t>(s)][m*3]->load(std::memory_order_relaxed)*.01f;
            scene.tone[m]=sceneValues[static_cast<size_t>(s)][m*3+1]->load(std::memory_order_relaxed)*.01f;
            scene.detail[m]=sceneValues[static_cast<size_t>(s)][m*3+2]->load(std::memory_order_relaxed)*.01f;
        }
    }
    for(size_t m=0;m<alpha::moduleCount;++m) {
        settings.enabled[m]=onValues[m]->load(std::memory_order_relaxed)>.5f;
        settings.mode[m]=juce::roundToInt(modeValues[m]->load(std::memory_order_relaxed));
        settings.modulation[m]=modValues[m]->load(std::memory_order_relaxed)*.01f;
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
    double bpm=120,beatsPerBar=4;
    if(auto* playHead=getPlayHead()) if(auto info=playHead->getPosition()) {
        if(auto hostBpm=info->getBpm()) if(std::isfinite(*hostBpm)&&*hostBpm>0) bpm=juce::jlimit(10.0,999.0,*hostBpm);
        if(auto signature=info->getTimeSignature()) if(signature->numerator>0&&signature->denominator>0)
            beatsPerBar=static_cast<double>(signature->numerator)*4.0/static_cast<double>(signature->denominator);
    }
    tempo.store(bpm,std::memory_order_relaxed);
    if(syncValue->load(std::memory_order_relaxed)>.5f) {
        const std::array<double,7> beats{beatsPerBar*4,beatsPerBar*2,beatsPerBar,2,1,.5,.25};
        int div=juce::jlimit(0,6,juce::roundToInt(divisionValue->load(std::memory_order_relaxed)));
        settings.lfoRateHz=static_cast<float>((bpm/60.0)/beats[static_cast<size_t>(div)]);
    } else settings.lfoRateHz=rateValue->load(std::memory_order_relaxed);
    if((epochBefore&1u)==0 && epochBefore==parameterEpoch.load(std::memory_order_acquire)) coherentSettings=settings;
    engine.setSettings(coherentSettings);
    engine.process(buffer.getArrayOfWritePointers(),buffer.getNumChannels(),buffer.getNumSamples());
}
void AlphaAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) {}
juce::AudioProcessorEditor* AlphaAudioProcessor::createEditor() { return new AlphaAudioProcessorEditor(*this); }
int AlphaAudioProcessor::getNumPrograms() { return static_cast<int>(presets.size()); }
const juce::String AlphaAudioProcessor::getProgramName(int i) { return juce::isPositiveAndBelow(i,getNumPrograms())?presets[static_cast<size_t>(i)].name:juce::String(); }
juce::String AlphaAudioProcessor::presetDescription(int i) { return juce::isPositiveAndBelow(i,static_cast<int>(presets.size()))?presets[static_cast<size_t>(i)].description:juce::String(); }
juce::String AlphaAudioProcessor::presetCategory(int i) { return juce::isPositiveAndBelow(i,static_cast<int>(presets.size()))?presets[static_cast<size_t>(i)].category:juce::String(); }
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
    const auto& p=presets[static_cast<size_t>(i)];
    for(int m=0;m<alpha::moduleCount;++m) {
        auto j=static_cast<size_t>(m);
        setValue(sceneId(0,m),m==5?0.0f:p.a[j]);setValue(sceneId(1,m),m==5?0.0f:p.b[j]);
        for(int s=0;s<2;++s) {setValue(sceneId(s,m,1),m==5?50.0f:p.tone[j]);setValue(sceneId(s,m,2),m==5?35.0f:p.detail[j]);}
        setValue(juce::String(moduleIds[j])+"On",1);setValue(juce::String(moduleIds[j])+"Mode",m==5?0.0f:static_cast<float>(p.modes[j]));
        setValue("mod"+juce::String(moduleNames[j]),m==5?0.0f:p.routes[j]);
    }
    setValue("morph",0);setValue("character",p.character);setValue("mix",p.mix);setValue("output",0);
    setValue("lfoRate",p.rate);setValue("lfoSync",1);setValue("lfoDivision",static_cast<float>(p.division));
    setValue("lfoDepth",p.depth);setValue("lfoShape",static_cast<float>(p.shape));
    setValue("bypass",0);setValue("delta",0);setValue("autoGain",0);
    currentProgram.store(i); updateHostDisplay(ChangeDetails().withProgramChanged(true));
}
void AlphaAudioProcessor::copyScene(int from,int to) {
    if(from<0||from>1||to<0||to>1)return;
    ParameterTransaction transaction(parameterTransactionLock,parameterEpoch);
    for(int m=0;m<alpha::moduleCount;++m)for(int p=0;p<3;++p)setValue(sceneId(to,m,p),value(sceneId(from,m,p)));
}
void AlphaAudioProcessor::swapScenes() {
    ParameterTransaction transaction(parameterTransactionLock,parameterEpoch);
    for(int m=0;m<alpha::moduleCount;++m)for(int p=0;p<3;++p) {
        auto a=sceneId(0,m,p),b=sceneId(1,m,p);const float va=value(a),vb=value(b);setValue(a,vb);setValue(b,va);
    }
    setValue("morph",100-value("morph"));
}
void AlphaAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    const juce::ScopedLock lock(parameterTransactionLock);
    auto state=parameters.copyState();state.setProperty("factoryProgram",currentProgram.load(),nullptr);
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
