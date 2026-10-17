#include "AlphaDSP.h"
#include "MotionCurve.h"
#include "AuthenticCoreDSP.h"
#include "TimePitchDSP.h"
#include "ShaperDSP.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace alpha {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double tau = 2.0 * pi;
constexpr double invSqrt8 = 0.35355339059327376220;

float bounded(float v, float lo, float hi, float fallback = 0.0f) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}
double clean(double v) noexcept { return std::abs(v) < 1.0e-25 ? 0.0 : v; }
double slide(double current, double target, double coefficient) noexcept
{
    const auto difference = target - current;
    return std::abs(difference) < 1.0e-7 ? target : current + coefficient * difference;
}
double lowpassCoefficient(double hz, double sampleRate) noexcept
{
    return 1.0 - std::exp(-tau * std::clamp(hz, 5.0, sampleRate * 0.43) / sampleRate);
}
double logCosh(double x) noexcept
{
    const auto a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994530942;
}

struct Random {
    uint32_t state = 1;
    uint32_t next() noexcept
    {
        auto x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return state = x;
    }
    double bipolar() noexcept { return static_cast<double>(next()) / 2147483648.0 - 1.0; }
};

struct Delay {
    std::vector<double> samples;
    int position = 0;
    void prepare(int length) { samples.assign(static_cast<size_t>(std::max(8, length)), 0.0); position = 0; }
    void reset() noexcept { std::fill(samples.begin(), samples.end(), 0.0); position = 0; }
    void write(double v) noexcept
    {
        samples[static_cast<size_t>(position)] = clean(v);
        if (++position == static_cast<int>(samples.size())) position = 0;
    }
    // Cubic Hermite/Lagrange interpolation for feed-forward pitch modulation.
    // The feedback network intentionally continues to use the passive linear read.
    double readCubic(double delay) const noexcept {
        auto index=static_cast<double>(position)-std::clamp(delay,2.0,static_cast<double>(samples.size()-3));
        while(index<0)index+=samples.size();
        const auto base=static_cast<int>(index);const auto f=index-base;
        const auto at=[&](int i){const auto count=static_cast<int>(samples.size());return samples[static_cast<size_t>((i+count)%count)];};
        const auto ym=at(base-1),y0=at(base),y1=at(base+1),y2=at(base+2);
        // Four-point Lagrange has a flatter high-frequency passband than linear interpolation.
        return -f*(f-1)*(f-2)*ym/6+(f+1)*(f-1)*(f-2)*y0/2-(f+1)*f*(f-2)*y1/2+(f+1)*f*(f-1)*y2/6;
    }
    // Linear fractional reads are intentionally used in the feedback network:
    // their interpolation cannot add energy to an otherwise stable FDN.
    double read(double delay) const noexcept
    {
        auto index = static_cast<double>(position) - std::clamp(delay, 1.0, static_cast<double>(samples.size() - 2));
        if (index < 0.0) index += static_cast<double>(samples.size());
        const auto a = static_cast<int>(index);
        auto b = a + 1;
        if (b == static_cast<int>(samples.size())) b = 0;
        const auto fraction = index - a;
        return samples[static_cast<size_t>(a)] + fraction * (samples[static_cast<size_t>(b)] - samples[static_cast<size_t>(a)]);
    }
};

// Trapezoidal SVF topology, Andrew Simper/Cytomic's derivation:
// https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
// It keeps state bounded while the focus cutoffs move; no biquad coefficient
// interpolation can move an intermediate pole outside the unit circle.
struct FocusFilter {
    double a1=1,a2=0,a3=0,s1=0,s2=0;
    void set(double hz,double sr) noexcept {
        const auto g=std::tan(pi*std::clamp(hz,10.0,sr*.43)/sr);
        a1=1/(1+g*(g+std::sqrt(2.0)));a2=g*a1;a3=g*a2;
    }
    double tick(double x,bool highpass) noexcept {
        const auto v3=x-s2,v1=a1*s1+a2*v3,v2=s2+a2*s1+a3*v3;
        s1=clean(2*v1-s1);s2=clean(2*v2-s2);
        return highpass?x-std::sqrt(2.0)*v1-v2:v2;
    }
    double band(double x) noexcept {
        const auto v3=x-s2,v1=a1*s1+a2*v3,v2=s2+a2*s1+a3*v3;s1=clean(2*v1-s1);s2=clean(2*v2-s2);return std::sqrt(2.0)*v1;
    }
    void reset() noexcept {s1=s2=0;}
};
struct FocusBand {
    FocusFilter inLow,inHigh,outLow,outHigh;
    double blend=0,target=0,slew=.001;
    bool useLow=false,useHigh=false;
    void reset() noexcept {inLow.reset();inHigh.reset();outLow.reset();outHigh.reset();blend=target=0;}
    void set(double low,double high,double sr,double smoothing,bool initial) noexcept {
        low=std::clamp(low,20.0,19900.0);high=std::clamp(high,low+1,20000.0);
        const auto nextTarget=(low>20.01||high<19999.9)?1.0:0.0;
        if(target==0&&nextTarget>0&&blend==0){inLow.reset();inHigh.reset();outLow.reset();outHigh.reset();}
        target=nextTarget;slew=smoothing;useLow=low>20.01;useHigh=high<19999.9;
        if(target==0&&blend==0)return;
        inLow.set(low,sr);outLow.set(low,sr);inHigh.set(high,sr);outHigh.set(high,sr);
        if(initial)blend=target;
    }
    double input(double x) noexcept {
        blend=slide(blend,target,slew);if(blend==0)return x;
        const auto hp=inLow.tick(x,true);const auto value=useLow?hp:x;
        const auto lp=inHigh.tick(value,false);
        return x+blend*((useHigh?lp:value)-x);
    }
    double difference(double x) noexcept {
        if(blend==0)return x;
        const auto hp=outLow.tick(x,true);const auto value=useLow?hp:x;
        const auto lp=outHigh.tick(value,false);
        return x+blend*((useHigh?lp:value)-x);
    }
};

struct Allpass {
    Delay delay;
    double length = 1;
    double tick(double x) noexcept
    {
        const auto delayed = delay.read(length);
        const auto result = delayed - 0.55 * x;
        delay.write(x + 0.55 * result);
        return result;
    }
};

// First-order antiderivative antialiasing. Unlike evaluating the transfer
// function directly, this integrates it across the sample-to-sample interval.
// It considerably reduces aliasing without oversampling or audio-thread memory.
// ADAA reference: Bilbao/Esqueda/Parker/Valimaki, IEEE SPL 2017.
// https://aaltodoc.aalto.fi/items/470aab15-1702-4ccf-a148-24e6173079fb
// Diode transfer is the implicit Shockley diode + series-resistor equation,
// with independent positive/negative ideality voltages. Its primitive follows
// by integrating v * dx/dv; no device-specific circuit or GPL code is used.
struct Saturator {
    struct Reconstruction {
        double b0=1,b1=0,b2=0,a1=0,a2=0,s1=0,s2=0;
        double tick(double x) noexcept {const auto y=b0*x+s1;s1=clean(b1*x-a1*y+s2);s2=clean(b2*x-a2*y);return y;}
    };
    std::array<Reconstruction,3> reconstruction,interpolation;
    double previous=0,previousInput=0;
    void prepare(double sr) noexcept {
        constexpr std::array<double,3> q{.5176380902050415,.7071067811865476,1.9318516525781366};
        const auto omega=tau*(sr*.43)/(sr*4),cosine=std::cos(omega),sine=std::sin(omega);
        for(size_t i=0;i<3;++i){const auto alpha=sine/(2*q[i]),a0=1+alpha;auto& f=reconstruction[i];f.b0=(1-cosine)*.5/a0;f.b1=(1-cosine)/a0;f.b2=f.b0;f.a1=-2*cosine/a0;f.a2=(1-alpha)/a0;interpolation[i]=f;}
        reset();
    }
    void reset() noexcept {previous=previousInput=0;for(auto& filter:reconstruction)filter.s1=filter.s2=0;for(auto& filter:interpolation)filter.s1=filter.s2=0;}
    static double diode(double x,bool primitive) noexcept {
        const auto sign=x<0?-1.0:1.0,a=.025,n=x<0?.26:.18,u=std::abs(x);
        auto y=n*std::log1p(u/a);
        for(int iteration=0;iteration<8;++iteration){const auto e=std::exp(std::min(40.0,y/n));y-= (y+a*(e-1)-u)/(1+a/n*e);y=std::max(0.0,y);}
        if(!primitive)return sign*y;
        const auto e=std::exp(std::min(40.0,y/n));return y*y*.5+a*((y-n)*e+n);
    }
    static double evaluate(int mode,double x,bool primitive) noexcept {
        switch(mode){
            case 1:return primitive?logCosh(x):std::tanh(x);
            case 2:return primitive?-std::cos(x):std::sin(x);
            case 3:return diode(x,primitive);
            case 4:{constexpr double lower=-.45,upper=.7;if(!primitive)return std::clamp(x,lower,upper);return x>upper?upper*x-upper*upper*.5:x<lower?lower*x-lower*lower*.5:x*x*.5;}
            default:return primitive?std::hypot(1.0,x):x/std::hypot(1.0,x);
        }
    }
    double tick(double input,double bias,const std::array<double,6>& weights,double fuzzGate=1,double kneeP=3) noexcept {
        input=std::clamp(input,-1.0e6,1.0e6);double output=0;const auto slope=kneeP/3;
        // Four uniformly spaced input samples, ADAA at4x, then a genuine
        // sixth-order Butterworth lowpass before decimating. Averaging ADAA
        // substeps alone is algebraically the original1xintegral and would
        // not improve alias rejection; reconstruction filtering is essential.
        for(int subsample=1;subsample<=4;++subsample){
        auto x=previousInput+(input-previousInput)*subsample*.25;for(auto& filter:interpolation)x=filter.tick(x);const auto dx=x-previous;output=0;
        for(int mode=0;mode<5;++mode)if(weights[static_cast<size_t>(mode)]>1e-9){
            const auto offset=evaluate(mode,bias*slope,false)/slope;
            const auto value=std::abs(dx)>1e-5?(evaluate(mode,(x+bias)*slope,true)-evaluate(mode,(previous+bias)*slope,true))/(dx*slope*slope)-offset:evaluate(mode,((x+previous)*.5+bias)*slope,false)/slope-offset;
            output+=weights[static_cast<size_t>(mode)]*value*(mode==4?fuzzGate:1);
        }
        previous=x;for(auto& filter:reconstruction)output=filter.tick(output);
        }
        previousInput=input;return output;
    }
};

// Vinyl surface impulses pass through a bilinear RIAA playback section:
// (1+s318us)/[(1+s3180us)(1+s75us)], normalized at1kHz.
// https://www.analog.com/media/en/training-seminars/design-handbooks/P2%20Ch6_final.pdf
// This shapes synthesized groove disturbances; the original track is not
// mistakenly de-emphasized again, nor is a particular stylus/record claimed.
struct RiaaSurface {
    struct Section {double b0=1,b1=0,a1=0,x1=0,y1=0;double tick(double x) noexcept {const auto y=b0*x+b1*x1-a1*y1;x1=x;y1=clean(y);return y1;}};
    std::array<Section,2> filter;double gain=1;
    void prepare(double sr) noexcept {
        constexpr std::array<double,2> zero{.000318,0},pole{.003180,.000075};
        for(size_t i=0;i<2;++i){const auto denominator=1+2*sr*pole[i];filter[i].b0=(1+2*sr*zero[i])/denominator;filter[i].b1=(1-2*sr*zero[i])/denominator;filter[i].a1=(1-2*sr*pole[i])/denominator;}
        const auto omega=2*sr*std::tan(pi*1000/sr);
        gain=std::sqrt((1+std::pow(omega*.00318,2))*(1+std::pow(omega*.000075,2))/(1+std::pow(omega*.000318,2)));reset();
    }
    void reset() noexcept {for(auto& section:filter)section.x1=section.y1=0;}
    double tick(double x) noexcept {for(auto& section:filter)x=section.tick(x);return x*gain;}
};

struct Channel {
    Delay wobble;
    std::array<Allpass, 4> diffusion;
    Saturator tapeSaturation, driveSaturation;
    MagneticHysteresis tapeMagnet,heatMagnet;
    RiaaSurface vinylPlayback;
    Random random,dropoutRandom,gritRandom;
    FocusFilter tapeBumpFilter;
    double heatEnvelope=0,tapeEnvelope=0,noiseTilt=0,driveTilt=0;
    int dropoutHold=0;double dropoutTarget=0;
    double tapeLow = 0, tapeSub = 0, tapeHigh = 0;
    double driveLow = 0, driveDcIn = 0, driveDcOut = 0, spaceDc = 0;
    double tapeDcIn=0,tapeDcOut=0,gritDcIn=0,gritDcOut=0,gritSmooth=0,gritPre=0,dropout=0,mainLow=0;
    Delay preDelay;
    double noiseLow = 0, noisePink = 0, noiseColor = 0, crackle = 0, noiseDc = 0;
    double gritPhase = 0, gritHeld = 0;
    void reset(int c) noexcept
    {
        wobble.reset();preDelay.reset();
        for (auto& a : diffusion) a.delay.reset();
        tapeSaturation.reset();driveSaturation.reset();tapeMagnet.reset();heatMagnet.reset();vinylPlayback.reset();
        tapeLow = tapeSub = tapeHigh = driveLow = driveDcIn = driveDcOut = spaceDc = 0;
        noiseLow = noisePink = noiseColor = crackle = noiseDc = 0;
        gritPhase = gritHeld = 0;
        tapeDcIn=tapeDcOut=gritDcIn=gritDcOut=gritSmooth=gritPre=dropout=mainLow=0;
        random.state = c == 0 ? 0x5d112f71u : 0xa4b27c85u;
        dropoutRandom.state=c==0?0x493a712du:0x63b0a19fu;dropoutHold=0;dropoutTarget=0;gritRandom.state=c==0?0x517c9423u:0x918e7243u;tapeBumpFilter.reset();heatEnvelope=tapeEnvelope=noiseTilt=driveTilt=0;
    }
};

struct AtomicMotion {
    std::atomic<int> independent{0},followInput{0},shape{0},count{4};
    std::atomic<float> rate{.25f},hostPhase{-1};
    std::atomic<double> hostCyclesPerBeat{0};
    std::array<std::atomic<float>,curveCapacity*3> points;
    void store(const MotionSettings& s) noexcept {
        independent.store(s.independent,std::memory_order_relaxed);followInput.store(s.followInput,std::memory_order_relaxed);
        shape.store(std::clamp(s.shape,0,7),std::memory_order_relaxed);count.store(std::clamp(s.curveCount,2,curveCapacity),std::memory_order_relaxed);
        rate.store(bounded(s.rateHz,.01f,4000,.25f),std::memory_order_relaxed);hostPhase.store(bounded(s.phaseFromHost,-1,1,-1),std::memory_order_relaxed);
        hostCyclesPerBeat.store(std::isfinite(s.hostCyclesPerBeat)?std::clamp(s.hostCyclesPerBeat,0.0,240.0):0,std::memory_order_relaxed);
        for(size_t i=0;i<curveCapacity;++i){points[i*3].store(bounded(s.curve[i].x,0,1),std::memory_order_relaxed);points[i*3+1].store(bounded(s.curve[i].y,-1,1),std::memory_order_relaxed);points[i*3+2].store(bounded(s.curve[i].curve,-1,1),std::memory_order_relaxed);}
    }
    MotionSettings load() const noexcept {
        MotionSettings s;s.independent=independent.load(std::memory_order_relaxed)!=0;s.followInput=followInput.load(std::memory_order_relaxed)!=0;
        s.shape=shape.load(std::memory_order_relaxed);s.curveCount=count.load(std::memory_order_relaxed);s.rateHz=rate.load(std::memory_order_relaxed);s.phaseFromHost=hostPhase.load(std::memory_order_relaxed);
        s.hostCyclesPerBeat=hostCyclesPerBeat.load(std::memory_order_relaxed);
        for(size_t i=0;i<curveCapacity;++i)s.curve[i]={points[i*3].load(std::memory_order_relaxed),points[i*3+1].load(std::memory_order_relaxed),points[i*3+2].load(std::memory_order_relaxed)};
        std::sort(s.curve.begin(),s.curve.begin()+s.curveCount,[](const auto& a,const auto& b){return a.x<b.x;});return s;
    }
};
struct MotionState {
    double phase=0,random=0,previous=0,target=0,hostOffset=0;
    bool followed=false;
    Random source;
    double tick(double rate,double sr,bool advance=true) noexcept {
        if(!advance)return random;
        phase+=rate/sr;if(phase>=1){phase-=std::floor(phase);previous=target;target=source.bipolar();}
        random=slide(random,target,std::min(1.0,83.3333333333333/sr));return random;
    }
};

double shapeLfo(int shape,double phase,double random,double previous,double target,const std::array<CurvePoint,curveCapacity>& points,int count) noexcept {
    const auto sine=std::sin(tau*phase);
    switch(shape){case 1:return 1-4*std::abs(phase-.5);case 2:return std::tanh(sine*9);case 3:return random;
        case 4:return phase*2-1;case 5:return 1-phase*2;case 6:{const auto t=phase*phase*(3-2*phase);return previous+(target-previous)*t;}
        case 7:return evaluateMotionCurve(points,count,phase);default:return sine;}
}

struct AtomicSettings {
    std::array<std::atomic<float>, moduleCount * 10> scenes;
    std::array<std::atomic<float>, moduleCount> modulation, lowHz, highHz;
    std::array<std::atomic<int>, moduleCount> motionTarget;
    std::array<std::atomic<float>,curveCapacity*3> curve;
    std::array<AtomicMotion,moduleCount> motions;
    std::atomic<float> hostPhase{-1};
    std::atomic<double> hostPpq{0},hostTempo{120},hostCyclesPerBeat{0};
    std::atomic<int> hostPlaying{0},hostValid{0};
    std::atomic<int> curveCount {4};
    std::array<std::atomic<int>, moduleCount> enabled, modes;
    std::atomic<float> morph {0}, character {0.7f}, mix {1}, outputDb {0}, rate {0.25f}, depth {0.5f};
    std::atomic<float> flux {0}, mainTone {.5f}, mainWidth {.5f};
    std::atomic<uint32_t> fluxSeed{79581};
    std::atomic<int> shape {0}, bypass {0}, delta {0}, autoGain {0};

    void store(const Settings& s) noexcept
    {
        for (int i = 0; i < moduleCount; ++i) {
            const auto j = static_cast<size_t>(i);
            const std::array<const std::array<float,moduleCount>*,10> fields {
                &s.a.amount,&s.a.tone,&s.a.detail,&s.a.third,&s.a.fourth,
                &s.b.amount,&s.b.tone,&s.b.detail,&s.b.third,&s.b.fourth
            };
            for(size_t field=0;field<fields.size();++field)
                scenes[j+moduleCount*field].store(bounded((*fields[field])[j],0,1,field%5==0?0.0f:0.5f),std::memory_order_relaxed);
            lowHz[j].store(bounded(s.lowHz[j],20,20000,20),std::memory_order_relaxed);
            highHz[j].store(bounded(s.highHz[j],20,20000,20000),std::memory_order_relaxed);
            motionTarget[j].store(std::clamp(s.motionTarget[j],0,4),std::memory_order_relaxed);
            modulation[j].store(bounded(s.modulation[j], -1, 1), std::memory_order_relaxed);
            enabled[j].store(s.enabled[j] ? 1 : 0, std::memory_order_relaxed);
            modes[j].store(std::clamp(s.mode[j],0,i==3?5:2), std::memory_order_relaxed);
        }
        for(size_t i=0;i<moduleCount;++i)motions[i].store(s.motions[i]);
        hostPhase.store(bounded(s.phaseFromHost,-1,1,-1),std::memory_order_relaxed);
        hostCyclesPerBeat.store(std::isfinite(s.hostCyclesPerBeat)?std::clamp(s.hostCyclesPerBeat,0.0,240.0):0,std::memory_order_relaxed);
        hostPpq.store(std::isfinite(s.hostPpq)?std::clamp(s.hostPpq,-1.0e9,1.0e9):0,std::memory_order_relaxed);hostTempo.store(std::isfinite(s.hostTempoBpm)?std::clamp(s.hostTempoBpm,10.0,999.0):120,std::memory_order_relaxed);hostPlaying.store(s.hostPlaying,std::memory_order_relaxed);hostValid.store(s.hostPositionValid,std::memory_order_relaxed);
        morph.store(bounded(s.morph, 0, 1), std::memory_order_relaxed);
        character.store(bounded(s.character, 0, 1, 0.7f), std::memory_order_relaxed);
        mix.store(bounded(s.mix, 0, 1, 1), std::memory_order_relaxed);
        outputDb.store(bounded(s.outputDb, -24, 12), std::memory_order_relaxed);
        rate.store(bounded(s.lfoRateHz,0.01f,4000,0.25f), std::memory_order_relaxed);
        depth.store(bounded(s.lfoDepth, 0, 1), std::memory_order_relaxed);
        shape.store(std::clamp(s.lfoShape, 0, 7), std::memory_order_relaxed);
        flux.store(bounded(s.flux,0,1),std::memory_order_relaxed);fluxSeed.store(std::max(1u,s.fluxSeed),std::memory_order_relaxed);
        mainTone.store(bounded(s.mainTone,0,1,.5f),std::memory_order_relaxed);
        mainWidth.store(bounded(s.mainWidth,0,1,.5f),std::memory_order_relaxed);
        curveCount.store(std::clamp(s.curveCount,2,curveCapacity),std::memory_order_relaxed);
        for(size_t i=0;i<curveCapacity;++i) {
            curve[i*3].store(bounded(s.customCurve[i].x,0,1,static_cast<float>(i)/7),std::memory_order_relaxed);
            curve[i*3+1].store(bounded(s.customCurve[i].y,-1,1),std::memory_order_relaxed);
            curve[i*3+2].store(bounded(s.customCurve[i].curve,-1,1),std::memory_order_relaxed);
        }
        bypass.store(s.bypass ? 1 : 0, std::memory_order_relaxed);
        delta.store(s.delta ? 1 : 0, std::memory_order_relaxed);
        autoGain.store(s.autoGain ? 1 : 0, std::memory_order_relaxed);
    }
    Settings load() const noexcept
    {
        Settings s;
        for (int i = 0; i < moduleCount; ++i) {
            const auto j = static_cast<size_t>(i);
            const std::array<std::array<float,moduleCount>*,10> fields {
                &s.a.amount,&s.a.tone,&s.a.detail,&s.a.third,&s.a.fourth,
                &s.b.amount,&s.b.tone,&s.b.detail,&s.b.third,&s.b.fourth
            };
            for(size_t field=0;field<fields.size();++field)(*fields[field])[j]=scenes[j+moduleCount*field].load(std::memory_order_relaxed);
            s.lowHz[j]=lowHz[j].load(std::memory_order_relaxed);s.highHz[j]=highHz[j].load(std::memory_order_relaxed);
            s.motionTarget[j]=motionTarget[j].load(std::memory_order_relaxed);
            s.modulation[j] = modulation[j].load(std::memory_order_relaxed);
            s.enabled[j] = enabled[j].load(std::memory_order_relaxed) != 0;
            s.mode[j] = modes[j].load(std::memory_order_relaxed);
        }
        for(size_t i=0;i<moduleCount;++i)s.motions[i]=motions[i].load();
        s.phaseFromHost=hostPhase.load(std::memory_order_relaxed);s.hostPpq=hostPpq.load(std::memory_order_relaxed);s.hostTempoBpm=hostTempo.load(std::memory_order_relaxed);s.hostPlaying=hostPlaying.load(std::memory_order_relaxed)!=0;s.hostPositionValid=hostValid.load(std::memory_order_relaxed)!=0;
        s.hostCyclesPerBeat=hostCyclesPerBeat.load(std::memory_order_relaxed);
        s.morph = morph.load(std::memory_order_relaxed);
        s.character = character.load(std::memory_order_relaxed);
        s.mix = mix.load(std::memory_order_relaxed);
        s.outputDb = outputDb.load(std::memory_order_relaxed);
        s.lfoRateHz = rate.load(std::memory_order_relaxed);
        s.lfoDepth = depth.load(std::memory_order_relaxed);
        s.lfoShape = shape.load(std::memory_order_relaxed);
        s.flux=flux.load(std::memory_order_relaxed);s.fluxSeed=fluxSeed.load(std::memory_order_relaxed);s.mainTone=mainTone.load(std::memory_order_relaxed);s.mainWidth=mainWidth.load(std::memory_order_relaxed);
        s.curveCount=curveCount.load(std::memory_order_relaxed);
        for(size_t i=0;i<curveCapacity;++i)s.customCurve[i]={curve[i*3].load(std::memory_order_relaxed),curve[i*3+1].load(std::memory_order_relaxed),curve[i*3+2].load(std::memory_order_relaxed)};
        std::sort(s.customCurve.begin(),s.customCurve.begin()+s.curveCount,[](const auto& a,const auto& b){return a.x<b.x;});
        s.bypass = bypass.load(std::memory_order_relaxed) != 0;
        s.delta = delta.load(std::memory_order_relaxed) != 0;
        s.autoGain = autoGain.load(std::memory_order_relaxed) != 0;
        return s;
    }
};
static_assert(std::atomic<float>::is_always_lock_free);
}

struct Engine::Impl {
    AtomicSettings target;
    Settings current;
    Scene sound;
    std::array<double, moduleCount> active = filled<double,moduleCount>(1);
    std::array<std::array<double, 6>, moduleCount> modeWeights {};
    std::array<double, 8> shapeWeights {1, 0, 0, 0, 0, 0, 0, 0};
    std::array<Channel, 2> channel;
    std::array<std::array<FocusBand,2>,moduleCount> focus;
    TimePitchDSP timePitch;
    ShaperDSP shaper;
    PlateReverb plate;
    double bloomSwell=.3;
    uint32_t appliedFluxSeed=0;
    std::array<Delay, 8> reverb;
    std::array<double, 8> reverbLow {}, delaySamples {}, delayTargets {}, feedback {};
    std::array<std::atomic<float>, Engine::scopeSize> scope;
    std::array<std::atomic<float>, Engine::analyserSize> analyser;
    std::atomic<unsigned> scopeWrite {0}, analyserWrite {0};
    std::atomic<float> inputMeter {0}, outputMeter {0}, lfoMeter {0},phaseMeter{0};
    std::array<std::atomic<float>,moduleCount> moduleLfoMeter,modulePhaseMeter;
    std::array<MotionState,moduleCount> motionState;
    double followerFast=0,followerSlow=0;int followerHold=0;
    double sampleRate = 48000, smoothing = 0.001, switchSmoothing = 0.001;
    double phase = 0, wowPhase = 0, flutterPhase = 0, wowAuxPhase = 0, flutterAuxPhase = 0, humPhase = 0;
    double irregularity = 0, randomLfo = 0, randomLfoTarget = 0, previousRandomLfo = 0;
    std::array<double,moduleCount> fluxMotion {}, fluxTarget {};
    double fluxClock = 0, mainLowCoefficient = .1;
    double envelope = 0, inputPeak = 0, outputPeak = 0;
    double bypassBlend = 0, deltaBlend = 0, autoGainBlend = 0;
    double dryEnergy = 0, wetEnergy = 0, gainCompensation = 1, energySmoothing = 0.0001;
    double tapeLp = 0.4, tapeHp = 0.004, tapeBass = 0.01, driveLp = 0.4;
    double noiseLp = 0.2, noisePinkLp = 0.02, noiseDcLp = 0.01;
    double roomDamping = 0.4, roomInputHp = 0.01, envelopeAttack = 0.01, envelopeRelease = 0.001, meterRelease = 0.9999;
    double wowRate = 0.3, flutterRate = 7, crackleDecay = 0.99, dcCoefficient = 0.995;
    unsigned controlCounter = 0, scopeCounter = 0, scopeInterval = 12;
    Random modulationRandom,fluxRandom;
    bool prepared = false, initial = true;

    Impl()
    {
        target.store(Settings {});
        for(size_t i=0;i<moduleCount;++i){moduleLfoMeter[i].store(0);modulePhaseMeter[i].store(0);motionState[i].source.state=0x3a47c291u+static_cast<uint32_t>(i)*7919u;}
        for (auto& v : scope) v.store(0, std::memory_order_relaxed);
        for (auto& v : analyser) v.store(0, std::memory_order_relaxed);
    }
    static Scene interpolate(const Settings& s) noexcept
    {
        Scene result;
        for (size_t i = 0; i < moduleCount; ++i) {
            result.amount[i] = s.a.amount[i] + s.morph * (s.b.amount[i] - s.a.amount[i]);
            result.tone[i] = s.a.tone[i] + s.morph * (s.b.tone[i] - s.a.tone[i]);
            result.detail[i] = s.a.detail[i] + s.morph * (s.b.detail[i] - s.a.detail[i]);
            result.third[i] = s.a.third[i] + s.morph * (s.b.third[i] - s.a.third[i]);
            result.fourth[i] = s.a.fourth[i] + s.morph * (s.b.fourth[i] - s.a.fourth[i]);
        }
        return result;
    }
    void snap(const Settings& s, const Scene& interpolated) noexcept
    {
        current = s;
        sound = interpolated;
        bypassBlend = s.bypass ? 1.0 : 0.0;
        deltaBlend = s.delta ? 1.0 : 0.0;
        autoGainBlend = s.autoGain ? 1.0 : 0.0;
        for (size_t i = 0; i < moduleCount; ++i) {
            active[i] = s.enabled[i] ? 1.0 : 0.0;
            for (int m = 0; m < 6; ++m) modeWeights[i][static_cast<size_t>(m)] = s.mode[i] == m ? 1.0 : 0.0;
        }
        for (int m = 0; m < 8; ++m) shapeWeights[static_cast<size_t>(m)] = s.lfoShape == m ? 1.0 : 0.0;
    }
    void smooth(const Settings& s, const Scene& interpolated) noexcept
    {
        auto parameter = [this](float& now, float desired) {
            const auto next = static_cast<float>(slide(now, desired, smoothing));
            // Reach the actual target once float precision can no longer
            // represent a smoothing step (particularly important for Mix=1).
            now = next == now ? desired : next;
        };
        for (size_t i = 0; i < moduleCount; ++i) {
            // Smooth the sound itself. Independently smoothing A, B and Morph
            // creates an audible detour when a user swaps the scene labels.
            // Interpolation before smoothing makes that operation invariant,
            // including during an earlier parameter or morph transition.
            parameter(sound.amount[i], interpolated.amount[i]);
            parameter(sound.tone[i], interpolated.tone[i]);
            parameter(sound.detail[i], interpolated.detail[i]);
            parameter(sound.third[i], interpolated.third[i]);parameter(sound.fourth[i], interpolated.fourth[i]);
            parameter(current.lowHz[i],s.lowHz[i]);parameter(current.highHz[i],s.highHz[i]);
            parameter(current.modulation[i], s.modulation[i]);
            active[i] = slide(active[i], s.enabled[i] ? 1.0 : 0.0, switchSmoothing);
            for (int m = 0; m < 6; ++m) {
                auto& weight = modeWeights[i][static_cast<size_t>(m)];
                weight = slide(weight, s.mode[i] == m ? 1.0 : 0.0, switchSmoothing);
            }
        }
        parameter(current.character, s.character);
        parameter(current.flux,s.flux);parameter(current.mainTone,s.mainTone);parameter(current.mainWidth,s.mainWidth);
        parameter(current.mix, s.mix); parameter(current.outputDb, s.outputDb);
        parameter(current.lfoRateHz, s.lfoRateHz); parameter(current.lfoDepth, s.lfoDepth);
        bypassBlend = slide(bypassBlend, s.bypass ? 1.0 : 0.0, switchSmoothing);
        deltaBlend = slide(deltaBlend, s.delta ? 1.0 : 0.0, switchSmoothing);
        autoGainBlend = slide(autoGainBlend, s.autoGain ? 1.0 : 0.0, switchSmoothing);
        for (int m = 0; m < 8; ++m)
            shapeWeights[static_cast<size_t>(m)] = slide(shapeWeights[static_cast<size_t>(m)], s.lfoShape == m ? 1.0 : 0.0, switchSmoothing);
    }
    void coefficients(const std::array<double,moduleCount>& tone,const std::array<double,moduleCount>& detail,const std::array<double,moduleCount>& third,const std::array<double,moduleCount>& fourth) noexcept
    {
        const auto& tape = modeWeights[0];
        const auto tapeBrightness = 0.78 * tape[0] + 1.14 * tape[1] + 0.60 * tape[2];
        tapeLp=lowpassCoefficient((19000.0-15500.0*tone[0])*tapeBrightness,sampleRate);
        tapeHp=lowpassCoefficient(18+50*tone[0]+20*tape[2],sampleRate);
        tapeBass = lowpassCoefficient(95.0, sampleRate);
        driveLp=lowpassCoefficient(18000,sampleRate);
        noiseLp=lowpassCoefficient(18000,sampleRate);
        wowRate=.05*std::pow(400.0,tone[1]);
        flutterRate=std::clamp(wowRate*12,4.0,60.0);
        envelopeRelease = 1.0 - std::exp(-1.0 / (sampleRate * (0.08 + 0.32 * third[2])));
        const auto& room = modeWeights[4];
        const auto scale = (.5*std::pow(4.0,tone[4])) * (room[0] + 0.72 * room[1] + 1.22 * room[2]);
        const auto rt60 = (.2*std::pow(100.0,detail[4])) * (0.72 * room[0] + room[1] + 1.30 * room[2]);
        constexpr std::array<double, 8> times {0.0297, 0.0371, 0.0411, 0.0437, 0.0531, 0.0617, 0.0713, 0.0799};
        for (size_t i = 0; i < 8; ++i) {
            const auto seconds = times[i] * scale;
            delayTargets[i] = seconds * sampleRate;
            if (delaySamples[i] == 0) delaySamples[i] = delayTargets[i];
            feedback[i] = std::pow(0.001, delaySamples[i] / (sampleRate * rt60));
        }
        roomDamping=lowpassCoefficient(1000*std::pow(16.0,third[4]),sampleRate);
        (void)fourth;
    }
    double nextLfo(const Settings& desired) noexcept
    {
        const auto synced=desired.hostPositionValid&&desired.phaseFromHost>=0;
        const auto advance=!synced||desired.hostPlaying;
        const auto rate=synced&&desired.hostCyclesPerBeat>0?desired.hostTempoBpm/60*desired.hostCyclesPerBeat:(synced?desired.lfoRateHz:current.lfoRateHz);
        if(advance)phase+=rate/sampleRate;
        if(phase>=1){phase-=std::floor(phase);previousRandomLfo=randomLfoTarget;randomLfoTarget=modulationRandom.bipolar();}
        if(advance)randomLfo=slide(randomLfo,randomLfoTarget,smoothing*3);
        double result=0;
        for(int shape=0;shape<8;++shape)if(shapeWeights[static_cast<size_t>(shape)]>1e-7)
            result+=shapeWeights[static_cast<size_t>(shape)]*shapeLfo(shape,phase,randomLfo,previousRandomLfo,randomLfoTarget,desired.customCurve,desired.curveCount);
        return std::clamp(result,-1.0,1.0);
    }

};

Engine::Engine() : impl(new Impl) {}
Engine::~Engine() { delete impl; }

void Engine::prepare(double sr, int maxBlockSize, int)
{
    auto& p = *impl;
    p.sampleRate = std::isfinite(sr) ? std::clamp(sr, 8000.0, 384000.0) : 48000.0;
    p.smoothing = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.018));
    p.switchSmoothing = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.012));
    p.energySmoothing = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.35));
    p.envelopeAttack = 1.0 - std::exp(-1.0 / (p.sampleRate * 0.003));
    p.meterRelease = std::exp(-1.0 / (p.sampleRate * 0.55));
    p.noisePinkLp = lowpassCoefficient(120.0, p.sampleRate);
    p.noiseDcLp = lowpassCoefficient(28.0, p.sampleRate);
    p.roomInputHp = lowpassCoefficient(65.0, p.sampleRate);
    p.crackleDecay = std::exp(-1.0 / (p.sampleRate * 0.0014));
    p.dcCoefficient = std::exp(-tau * 18.0 / p.sampleRate);
    p.scopeInterval = static_cast<unsigned>(std::max(1.0, std::round(p.sampleRate / 4000.0)));
    constexpr std::array<double, 4> diffusionTimes {0.0047, 0.0063, 0.0091, 0.0127};
    for (size_t c = 0; c < 2; ++c) {
        p.channel[c].tapeSaturation.prepare(p.sampleRate);p.channel[c].driveSaturation.prepare(p.sampleRate);p.channel[c].vinylPlayback.prepare(p.sampleRate);
        p.channel[c].wobble.prepare(static_cast<int>(std::ceil(p.sampleRate * 0.04)) + 8);
        p.channel[c].preDelay.prepare(static_cast<int>(std::ceil(p.sampleRate*.25))+8);p.channel[c].tapeBumpFilter.set(95,p.sampleRate);
        for (size_t i = 0; i < 4; ++i) {
            auto& diffuser = p.channel[c].diffusion[i];
            diffuser.length = std::round(diffusionTimes[i] * p.sampleRate * (c == 0 ? 1.0 : 1.083));
            diffuser.delay.prepare(static_cast<int>(diffuser.length) + 4);
        }
    }
    for (auto& delay : p.reverb) delay.prepare(static_cast<int>(std::ceil(p.sampleRate * 0.23)) + 8);
    p.timePitch.prepare(p.sampleRate,maxBlockSize);p.shaper.prepare(p.sampleRate);p.plate.prepare(p.sampleRate);
    p.mainLowCoefficient=lowpassCoefficient(1000,p.sampleRate);
    p.prepared = true;
    reset();
}

void Engine::reset() noexcept
{
    auto& p = *impl;
    for (int c = 0; c < 2; ++c) p.channel[static_cast<size_t>(c)].reset(c);
    for (auto& delay : p.reverb) delay.reset();
    p.reverbLow.fill(0);p.timePitch.reset();p.shaper.reset();p.plate.reset();p.bloomSwell=.3;p.appliedFluxSeed=0;
    for(auto& module:p.focus)for(auto& focus:module)focus.reset();
    p.fluxMotion.fill(0);p.fluxTarget.fill(0);p.fluxClock=0;
    p.delaySamples.fill(0);
    p.delayTargets.fill(0);
    p.phase = p.wowPhase = p.flutterPhase = p.wowAuxPhase = p.flutterAuxPhase = p.humPhase = 0;
    p.irregularity = p.randomLfo = p.randomLfoTarget = p.previousRandomLfo = 0;
    p.envelope = p.inputPeak = p.outputPeak = 0;
    p.dryEnergy = p.wetEnergy = 0; p.gainCompensation = 1;
    p.modulationRandom.state = 0x1a704c3du;p.fluxRandom.state=0x937a81c3u;
    for(size_t i=0;i<moduleCount;++i){p.fluxTarget[i]=p.fluxRandom.bipolar();p.motionState[i]={};p.motionState[i].source.state=0x3a47c291u+static_cast<uint32_t>(i)*7919u;p.moduleLfoMeter[i].store(0);p.modulePhaseMeter[i].store(0);}
    p.followerFast=p.followerSlow=0;p.followerHold=0;p.phaseMeter.store(0);
    p.controlCounter = p.scopeCounter = 0;
    p.scopeWrite.store(0, std::memory_order_release);
    p.analyserWrite.store(0, std::memory_order_release);
    for (auto& v : p.scope) v.store(0, std::memory_order_relaxed);
    for (auto& v : p.analyser) v.store(0, std::memory_order_relaxed);
    p.inputMeter.store(0, std::memory_order_relaxed);
    p.outputMeter.store(0, std::memory_order_relaxed);
    p.lfoMeter.store(0, std::memory_order_relaxed);
    p.initial = true;
}

void Engine::setSettings(const Settings& s) noexcept { impl->target.store(s); }

void Engine::process(float* const* buffers, int channels, int samples) noexcept
{
    auto& p = *impl;
    if (!p.prepared || buffers == nullptr || channels <= 0 || samples <= 0) return;
    const auto channelCount = std::min(channels, 2);
    for (int c = 0; c < channelCount; ++c) if (buffers[c] == nullptr) return;
    const auto desired = p.target.load();
    const auto desiredSound = Impl::interpolate(desired);
    const bool initial=p.initial;
    if(p.initial){p.snap(desired,desiredSound);p.initial=false;}
    if(desired.phaseFromHost>=0){const auto anchored=desired.hostPositionValid&&desired.hostCyclesPerBeat>0?desired.hostPpq*desired.hostCyclesPerBeat:desired.phaseFromHost;p.phase=anchored-std::floor(anchored);}
    for(size_t i=0;i<moduleCount;++i){
        auto& state=p.motionState[i];const auto& motion=desired.motions[i];
        if(motion.followInput!=state.followed){state.hostOffset=0;state.followed=motion.followInput;}
        if(motion.phaseFromHost>=0){const auto hostPhase=desired.hostPositionValid&&motion.hostCyclesPerBeat>0?desired.hostPpq*motion.hostCyclesPerBeat:motion.phaseFromHost;const auto anchored=hostPhase+(motion.followInput?state.hostOffset:0);state.phase=anchored-std::floor(anchored);}
    }
    if(p.appliedFluxSeed!=desired.fluxSeed){p.appliedFluxSeed=desired.fluxSeed;p.fluxRandom.state=desired.fluxSeed;for(auto& value:p.fluxTarget)value=p.fluxRandom.bipolar();}
    double lfo = 0;
    for (int sample = 0; sample < samples; ++sample) {
        p.smooth(desired, desiredSound);
        lfo=p.nextLfo(desired);
        std::array<double,2> dry {},signal {},diffused {},plateInput {};
        double peak=0;
        for(int c=0;c<channelCount;++c){const auto input=buffers[c][sample];dry[static_cast<size_t>(c)]=std::isfinite(input)?input:0;peak=std::max(peak,std::abs(dry[static_cast<size_t>(c)]));}
        if(channelCount==1)dry[1]=dry[0];
        p.followerFast=slide(p.followerFast,peak,1/(p.sampleRate*.010));p.followerSlow=slide(p.followerSlow,peak,1/(p.sampleRate*.100));
        if(p.followerHold>0)--p.followerHold;
        const bool transient=p.followerHold==0&&p.followerFast>std::max(.002,p.followerSlow*2)&&peak>.005;
        if(transient)p.followerHold=static_cast<int>(p.sampleRate*.050);
        p.fluxClock+=.35/p.sampleRate;
        if(p.fluxClock>=1){p.fluxClock-=1;for(auto& value:p.fluxTarget)value=p.fluxRandom.bipolar();}
        std::array<double,moduleCount> amount,tone,detail,third,fourth,moduleLfo;
        for(size_t i=0;i<moduleCount;++i){
            auto& motion=p.motionState[i];const auto& settings=desired.motions[i];
            if(settings.independent){
                const auto synced=desired.hostPositionValid&&settings.phaseFromHost>=0;
                const auto advance=!synced||desired.hostPlaying;
                const auto rateHz=synced&&settings.hostCyclesPerBeat>0?desired.hostTempoBpm/60*settings.hostCyclesPerBeat:settings.rateHz;
                if(transient&&settings.followInput&&advance){
                    if(synced)motion.hostOffset=-(desired.hostPpq*(settings.hostCyclesPerBeat>0?settings.hostCyclesPerBeat:rateHz/(desired.hostTempoBpm/60))+sample*rateHz/p.sampleRate);
                    motion.phase=0;
                }
                motion.tick(rateHz,p.sampleRate,advance);
                moduleLfo[i]=shapeLfo(settings.shape,motion.phase,motion.random,motion.previous,motion.target,settings.curve,settings.curveCount);
            }else{motion.phase=p.phase;moduleLfo[i]=lfo;}
            p.fluxMotion[i]=slide(p.fluxMotion[i],p.fluxTarget[i],1/(p.sampleRate*.8));
            const auto flux=i<6?p.current.flux*p.fluxMotion[i]:0;
            const auto modulation=moduleLfo[i]*p.current.modulation[i]*p.current.lfoDepth;
            const auto target=desired.motionTarget[i];
            const auto base=std::clamp(static_cast<double>(p.sound.amount[i])*(1+flux*.08),0.0,1.0);
            amount[i]=std::clamp(target==0?base*(1+modulation):base,0.0,1.0)*p.current.character*p.active[i];
            tone[i]=std::clamp(p.sound.tone[i]+flux*.025+(target==1?modulation:0),0.0,1.0);
            detail[i]=std::clamp(p.sound.detail[i]+(target==2?modulation:0),0.0,1.0);
            third[i]=std::clamp(p.sound.third[i]+(target==3?modulation:0),0.0,1.0);
            fourth[i]=std::clamp(p.sound.fourth[i]+(target==4?modulation:0),0.0,1.0);
        }
        if((p.controlCounter++&15u)==0){
            p.coefficients(tone,detail,third,fourth);
            for(size_t i=0;i<moduleCount;++i)for(auto& band:p.focus[i])band.set(p.current.lowHz[i],p.current.highHz[i],p.sampleRate,p.smoothing,initial&&sample==0);
        }
        const auto envelopeCoefficient = peak > p.envelope ? p.envelopeAttack : p.envelopeRelease;
        p.envelope = clean(p.envelope + envelopeCoefficient * (std::min(peak, 1.0e6) - p.envelope));
        p.inputPeak = std::max(peak, p.inputPeak * p.meterRelease);
        const auto irregularRandom = p.modulationRandom.bipolar();
        p.irregularity = clean(p.irregularity + (2.0 / p.sampleRate) * (irregularRandom - p.irregularity));
        p.wowPhase += p.wowRate / p.sampleRate;
        p.flutterPhase += p.flutterRate / p.sampleRate;
        p.wowAuxPhase += 2.713 * p.wowRate / p.sampleRate;
        p.flutterAuxPhase += 1.731 * p.flutterRate / p.sampleRate;
        p.humPhase+=50.0/p.sampleRate;
        if (p.wowPhase >= 1.0) p.wowPhase -= 1.0;
        if (p.flutterPhase >= 1.0) p.flutterPhase -= 1.0;
        if (p.wowAuxPhase >= 1.0) p.wowAuxPhase -= 1.0;
        if (p.flutterAuxPhase >= 1.0) p.flutterAuxPhase -= 1.0;
        if (p.humPhase >= 1.0) p.humPhase -= 1.0;
        const auto wowMode=p.modeWeights[1][0]+p.modeWeights[1][1];
        const auto flutterMode=p.modeWeights[1][0]+p.modeWeights[1][2];
        const auto wobbleMix=std::min(1.0,amount[1]*3.5);
        const auto noiseGate=std::clamp((p.envelope-.0002)*100,0.0,1.0)*(1-third[2]+third[2]*std::sqrt(std::min(1.0,p.envelope*4)));
        std::array<double,2> rawNoise {};
        // Tape and transport are stereo-linked in time; random modulation is
        // smoothed rather than independently stepping each sample or channel.
        for(int c=0;c<channelCount;++c){
            const auto j=static_cast<size_t>(c);auto& state=p.channel[j];auto x=dry[j];
            auto& tapeFocus=p.focus[0][j];const auto tapeFull=x;const auto tapeInput=tapeFocus.input(x);
            const auto guarded=std::clamp(tapeInput,-1.0e6,1.0e6);
            state.tapeHigh=clean(state.tapeHigh+p.tapeHp*(guarded-state.tapeHigh));
            state.tapeLow=clean(state.tapeLow+p.tapeLp*(guarded-state.tapeHigh-state.tapeLow));
            state.tapeSub=clean(state.tapeSub+p.tapeBass*(state.tapeLow-state.tapeSub));
            const auto tapeGain=1+amount[0]*3;
            state.tapeEnvelope=slide(state.tapeEnvelope,std::abs(state.tapeLow),1/(p.sampleRate*(std::abs(state.tapeLow)>state.tapeEnvelope?.002:.080)));
            const auto ratio=1+3*fourth[0];
            const auto compression=state.tapeEnvelope>.125?std::pow(state.tapeEnvelope/.125,1/ratio-1):1;
            const auto bump=state.tapeBumpFilter.band(state.tapeLow)*(std::pow(10.0,6*detail[0]/20)-1);
            const auto shaped=(state.tapeLow+bump)*tapeGain*compression;
            const std::array<double,6> tapeShape {.2,.8,0,0,0,0};
            const auto staticWet=state.tapeSaturation.tick(shaped,0,tapeShape);
            const auto& tapeMode=p.modeWeights[0];
            const auto magnetic=amount[0]>1e-8?state.tapeMagnet.process(shaped,.35*tapeMode[0]+.18*tapeMode[1]+.6*tapeMode[2],.55*tapeMode[0]+.8*tapeMode[1]+.4*tapeMode[2]):0;
            auto tapeWet=(staticWet*.65+magnetic*.35)/(1+amount[0]*1.5);
            // Random tape defects are smooth finite attenuation events,
            // rather than periodically muting the signal at a sine threshold.
            if(state.dropoutRandom.next()<static_cast<uint32_t>((.5*third[0])*4294967296.0/p.sampleRate)){
                state.dropoutTarget=third[0]*(.25+.55*std::abs(state.dropoutRandom.bipolar()));
                state.dropoutHold=static_cast<int>(p.sampleRate*(.003+.025*third[0]*std::abs(state.dropoutRandom.bipolar())));
            }
            if(state.dropoutHold>0)--state.dropoutHold;else state.dropoutTarget=0;
            state.dropout=slide(state.dropout,state.dropoutTarget,1/(p.sampleRate*(state.dropoutTarget>state.dropout?.001:.030)));
            tapeWet*=1-std::min(.85,state.dropout);
            const auto tapeDc=clean(tapeWet-state.tapeDcIn+p.dcCoefficient*state.tapeDcOut);state.tapeDcIn=tapeWet;state.tapeDcOut=tapeDc;
            x=tapeFull+tapeFocus.difference(amount[0]*(tapeDc-tapeInput));

            auto& driftFocus=p.focus[1][j];const auto driftFull=x;const auto driftInput=driftFocus.input(x);state.wobble.write(driftInput);
            const auto spread=fourth[1]*2.5;
            const auto sineWow=std::sin(tau*p.wowPhase+c*.055*spread);
            const auto triangleWow=1-4*std::abs(p.wowPhase-.5);
            const auto wow=sineWow*(1-detail[1]*.35)+triangleWow*detail[1]*.35+detail[1]*(.19*std::sin(tau*p.wowAuxPhase+c*.19*spread)+p.irregularity*15);
            const auto flutter=std::sin(tau*p.flutterPhase+.7+c*.12*spread)+detail[1]*.25*std::sin(tau*p.flutterAuxPhase);
            // Soft-bound combined wow/flutter excursion before the read head can
            // hit its causal floor; hard delay clamping would create pitch corners.
            const auto excursion=amount[1]*(.0038*wowMode*wow+.0021*third[1]*flutterMode*flutter);
            const auto delay=p.sampleRate*(.006+.005*std::tanh(excursion/.005));
            const auto wobbleWet=state.wobble.readCubic(delay);
            x=driftFull+driftFocus.difference(wobbleMix*(wobbleWet-driftInput));signal[j]=x;

            const auto white=state.random.bipolar();state.noiseLow=clean(state.noiseLow+p.noiseLp*(white-state.noiseLow));
            state.noisePink=clean(state.noisePink+p.noisePinkLp*(white-state.noisePink));
            const auto event=state.random.next();
            if(event<static_cast<uint32_t>((.5+tone[2]*24)*(4294967296.0/p.sampleRate)))state.crackle+=white*(.3+tone[2]*2.5);
            state.crackle=clean(state.crackle*std::exp(-1/(p.sampleRate*(.0003+tone[2]*.005))));
            const auto vinyl=state.noisePink*4+state.vinylPlayback.tick(state.noiseLow*.17+state.crackle);
            const auto hiss=white*(.85-tone[2]*.6)+state.noiseLow*(.15+tone[2]*.6);
            const auto mechanical=state.noisePink*3+state.noiseLow*(.15+detail[2]*.35)+tone[2]*state.crackle*.15+.1*std::sin(tau*p.humPhase)+.035*std::sin(2*tau*p.humPhase+.3)+.018*std::sin(3*tau*p.humPhase+.7);
            const auto& mode=p.modeWeights[2];const auto noise=mode[0]*vinyl+mode[1]*hiss+mode[2]*mechanical;
            state.noiseColor=clean(state.noiseColor+p.noiseLp*(noise-state.noiseColor));
            state.noiseTilt=clean(state.noiseTilt+p.mainLowCoefficient*(state.noiseColor-state.noiseTilt));
            const auto noiseGain=std::pow(10.0,(detail[2]-.5)*12/20);
            const auto tilted=state.noiseTilt/noiseGain+(state.noiseColor-state.noiseTilt)*noiseGain;
            state.noiseDc=clean(state.noiseDc+p.noiseDcLp*(tilted-state.noiseDc));
            rawNoise[j]=(tilted-state.noiseDc)*(.075*amount[2]*amount[2]*noiseGate);
        }
        if(channelCount==1){signal[1]=signal[0];rawNoise[1]=rawNoise[0];}
        const auto noiseMid=(rawNoise[0]+rawNoise[1])*.5,noiseSide=(rawNoise[0]-rawNoise[1])*.5*fourth[2]*1.42857142857143;
        for(int c=0;c<channelCount;++c){
            const auto j=static_cast<size_t>(c);auto& state=p.channel[j];auto x=signal[j];
            auto& dustFocus=p.focus[2][j];(void)dustFocus.input(x);x+=dustFocus.difference(noiseMid+(c==0?noiseSide:-noiseSide));
            auto& heatFocus=p.focus[3][j];const auto heatFull=x,heatInput=heatFocus.input(x);
            const auto gain=1+24*amount[3]*amount[3];
            state.heatEnvelope=slide(state.heatEnvelope,std::abs(heatInput),1/(p.sampleRate*(std::abs(heatInput)>state.heatEnvelope?.002:.080)));
            const auto sag=std::pow(10.0,-9*fourth[3]*std::min(1.0,state.heatEnvelope*2)/20);
            const auto bias=(detail[3]-.5)*.9;
            auto nonlinear=state.driveSaturation.tick(heatInput*gain*sag,bias,p.modeWeights[3],std::clamp((p.envelope-.00005)*1000,0.0,1.0),1.5+third[3]*6.5);
            if(p.modeWeights[3][5]>1e-8)nonlinear+=p.modeWeights[3][5]*state.heatMagnet.process((heatInput*gain*sag+bias)*(1.5+third[3]*6.5)/3,.3,.65)/((1.5+third[3]*6.5)/3);
            const auto compensated=nonlinear/(1+.45*std::sqrt(gain-1));
            state.driveLow=clean(state.driveLow+p.driveLp*(compensated-state.driveLow));
            state.driveTilt=clean(state.driveTilt+p.mainLowCoefficient*(state.driveLow-state.driveTilt));
            const auto heatToneGain=std::pow(10.0,(tone[3]-.5)*12/20);
            const auto heatTone=state.driveTilt/heatToneGain+(state.driveLow-state.driveTilt)*heatToneGain;
            const auto dc=clean(heatTone-state.driveDcIn+p.dcCoefficient*state.driveDcOut);state.driveDcIn=heatTone;state.driveDcOut=dc;
            x=heatFull+heatFocus.difference(amount[3]*(dc-heatInput));

            auto& gritFocus=p.focus[5][j];const auto gritFull=x,gritInput=std::clamp(gritFocus.input(x),-1.0e6,1.0e6);
            const auto rate=std::min(p.sampleRate,48000*std::pow(1.0/48,detail[5]));const auto rateDivisor=p.sampleRate/rate;const auto bits=16*std::pow(.125,tone[5]);
            const auto antiAlias=lowpassCoefficient(p.sampleRate/(rateDivisor*2.5),p.sampleRate);
            state.gritPre=clean(state.gritPre+antiAlias*(gritInput-state.gritPre));
            state.gritPhase-=1;
            if(state.gritPhase<=0){state.gritPhase+=std::max(1.0,rateDivisor*(1+third[5]*state.gritRandom.bipolar()*.25));state.gritHeld=gritInput+(state.gritPre-gritInput)*fourth[5];}
            const auto quantize=[](double input,double bits){const auto levels=std::exp2(bits-1);return std::round(std::clamp(input,-1.0,1-1/levels)*levels)/levels;};
            const auto crushed=quantize(gritInput*.65+state.gritHeld*.35,bits);
            const auto decimated=quantize(state.gritHeld,std::min(24.0,bits+4));
            // ITU-T G.711 mu-law companding law; deliberately variable word
            // length, not a claim of bit-exact telephony framing/packet loss.
            const auto compressed=std::copysign(std::log1p(255*std::abs(state.gritHeld))/std::log(256.0),state.gritHeld);
            const auto code=quantize(compressed,std::clamp(bits,2.0,12.0));
            const auto digital=std::copysign(std::expm1(std::min(40.0,std::abs(code)*std::log(256.0)))/255,code);
            const auto& mode=p.modeWeights[5];const auto grit=mode[0]*crushed+mode[1]*decimated+mode[2]*digital;
            state.gritSmooth=clean(state.gritSmooth+antiAlias*(grit-state.gritSmooth));
            const auto reconstruction=grit+(state.gritSmooth-grit)*fourth[5];
            const auto gritDc=clean(reconstruction-state.gritDcIn+p.dcCoefficient*state.gritDcOut);state.gritDcIn=reconstruction;state.gritDcOut=gritDc;
            x=gritFull+gritFocus.difference(amount[5]*(gritDc-gritInput));signal[j]=x;
        }
        if(channelCount==1)signal[1]=signal[0];
        // Each processor receives a band and returns a processed difference;
        // the original full-band signal remains phase-exact when neutral.
        for(int module=6;module<moduleCount;++module){
            std::array<double,2> bandInput;const auto before=signal;
            for(size_t c=0;c<2;++c)bandInput[c]=p.focus[static_cast<size_t>(module)][c].input(signal[c]);
            const std::array<double,4> controls {tone[static_cast<size_t>(module)],detail[static_cast<size_t>(module)],third[static_cast<size_t>(module)],fourth[static_cast<size_t>(module)]};
            const auto& motion=desired.motions[static_cast<size_t>(module)];
            const auto anchored=(motion.independent?motion.phaseFromHost:desired.phaseFromHost)>=0;
            const auto hostCyclesPerBeat=motion.independent?motion.hostCyclesPerBeat:desired.hostCyclesPerBeat;
            const auto rateHz=desired.hostPositionValid&&anchored&&hostCyclesPerBeat>0?desired.hostTempoBpm/60*hostCyclesPerBeat:(motion.independent?motion.rateHz:(desired.hostPositionValid&&anchored?desired.lfoRateHz:p.current.lfoRateHz));
            const auto followOffset=desired.motions[static_cast<size_t>(module)].independent&&desired.motions[static_cast<size_t>(module)].followInput?p.motionState[static_cast<size_t>(module)].hostOffset:0;
            const auto absolutePhase=desired.hostPositionValid&&anchored?desired.hostPpq*(hostCyclesPerBeat>0?hostCyclesPerBeat:rateHz/(desired.hostTempoBpm/60))+(desired.hostPlaying?sample*rateHz/p.sampleRate:0)+followOffset:std::numeric_limits<double>::quiet_NaN();
            const auto processed=module<8?p.timePitch.process(module-6,bandInput,amount[static_cast<size_t>(module)],controls,desired.mode[static_cast<size_t>(module)],rateHz,moduleLfo[static_cast<size_t>(module)],absolutePhase):p.shaper.process(module-8,bandInput,amount[static_cast<size_t>(module)],controls,desired.mode[static_cast<size_t>(module)],moduleLfo[static_cast<size_t>(module)],p.motionState[static_cast<size_t>(module)].phase);
            for(size_t c=0;c<2;++c)signal[c]=before[c]+p.focus[static_cast<size_t>(module)][c].difference(processed[c]-bandInput[c]);
        }
        if(channelCount==1)signal[0]=signal[1]=(signal[0]+signal[1])*.5;
        for(int c=0;c<channelCount;++c){
            const auto j=static_cast<size_t>(c);auto& state=p.channel[j];
            auto spaceInput=std::clamp(p.focus[4][j].input(signal[j]),-1.0e6,1.0e6);
            state.preDelay.write(spaceInput);spaceInput=state.preDelay.read(1+fourth[4]*fourth[4]*.25*p.sampleRate);
            state.spaceDc=clean(state.spaceDc+p.roomInputHp*(spaceInput-state.spaceDc));auto diffusion=spaceInput-state.spaceDc;plateInput[j]=diffusion;
            for(auto& allpass:state.diffusion)diffusion=allpass.tick(diffusion);diffused[j]=diffusion;
        }
        if(channelCount==1){diffused[1]=diffused[0];plateInput[1]=plateInput[0];}
        const auto plate=p.plate.process(plateInput,tone[4],detail[4],third[4]);
        p.bloomSwell=slide(p.bloomSwell,peak>.001?.3:1,1/(p.sampleRate*.12));
        std::array<double, 8> taps, matrix;
        for (size_t i = 0; i < 8; ++i) {
            // Room-size and mode changes slew the read heads continuously.
            // This makes a gentle pitch glide instead of jumping between taps.
            p.delaySamples[i] += std::clamp(p.delayTargets[i] - p.delaySamples[i], -0.3, 0.3);
            taps[i] = p.reverb[i].read(p.delaySamples[i]);
            p.reverbLow[i] = clean(p.reverbLow[i] + p.roomDamping * (taps[i] - p.reverbLow[i]));
            matrix[i] = p.reverbLow[i];
        }
        // The normalized Hadamard transform is orthogonal. Each delay's
        // RT60-derived feedback is strictly below one, including Bloom mode.
        for (size_t width = 1; width < 8; width *= 2) {
            for (size_t i = 0; i < 8; i += width * 2) {
                for (size_t j = 0; j < width; ++j) {
                    const auto a = matrix[i + j], b = matrix[i + j + width];
                    matrix[i + j] = a + b; matrix[i + j + width] = a - b;
                }
            }
        }
        constexpr std::array<double, 8> signsL {1, -1, 1, 1, -1, 1, -1, -1};
        constexpr std::array<double, 8> signsR {1, 1, -1, 1, 1, -1, 1, -1};
        double roomL = 0, roomR = 0;
        for (size_t i = 0; i < 8; ++i) {
            const auto injection = (diffused[0] * signsL[i] + diffused[1] * signsR[i]) * 0.18;
            p.reverb[i].write(injection + matrix[i] * invSqrt8 * p.feedback[i]);
            roomL += taps[i] * signsR[i];
            roomR += taps[i] * signsL[i];
        }
        const auto outputGain = std::pow(10.0, p.current.outputDb * 0.05);
        std::array<double, 2> wet {};
        double dryPower = 0, wetPower = 0;
        for (int c = 0; c < channelCount; ++c) {
            const auto j = static_cast<size_t>(c);
            const auto room=(channelCount==1?(roomL+roomR)*.5:(c==0?roomL:roomR))*invSqrt8;
            const auto plateWet=channelCount==1?(plate[0]+plate[1])*.5:plate[j];
            const auto space=room*(p.modeWeights[4][0]+p.modeWeights[4][2]*p.bloomSwell*1.35)+plateWet*p.modeWeights[4][1];
            wet[j]=signal[j]+p.focus[4][j].difference(amount[4]*space*.72);
            auto& state=p.channel[j];state.mainLow=clean(state.mainLow+p.mainLowCoefficient*(wet[j]-state.mainLow));
            if(std::abs(p.current.mainTone-.5f)>1e-7){const auto tilt=std::exp2((p.current.mainTone-.5)*1.5);wet[j]=state.mainLow/tilt+(wet[j]-state.mainLow)*tilt;}
            dryPower += dry[j] * dry[j]; wetPower += wet[j] * wet[j];
        }
        if(channelCount==2&&std::abs(p.current.mainWidth-.5f)>1e-7){const auto mid=(wet[0]+wet[1])*.5,side=(wet[0]-wet[1])*p.current.mainWidth;wet[0]=mid+side;wet[1]=mid-side;}
        wetPower=0;for(int c=0;c<channelCount;++c)wetPower+=wet[static_cast<size_t>(c)]*wet[static_cast<size_t>(c)];
        p.dryEnergy += p.energySmoothing * (dryPower / channelCount - p.dryEnergy);
        p.wetEnergy += p.energySmoothing * (wetPower / channelCount - p.wetEnergy);
        // Linked stereo RMS compensation is bounded and slewed; silence never
        // asks the gain follower to amplify a decaying reverb or noise floor.
        const auto compensation = p.dryEnergy > 1.0e-10 && p.wetEnergy > 1.0e-10
            ? std::clamp(std::sqrt(p.dryEnergy / p.wetEnergy), 0.25, 4.0) : 1.0;
        p.gainCompensation = slide(p.gainCompensation, compensation, p.smoothing);
        const auto wetGain = 1.0 + p.autoGainBlend * (p.gainCompensation - 1.0);
        double outputPeak = 0, scopeSample = 0;
        for (int c = 0; c < channelCount; ++c) {
            const auto j = static_cast<size_t>(c);
            // Both neutral paths take the actual dry sample; there is no
            // conversion through a filter, delayed copy, or crossfade law.
            const auto mixed = p.current.mix == 0.0f ? dry[j] : dry[j] + p.current.mix * (wet[j] * wetGain - dry[j]);
            const auto monitored = (mixed - p.deltaBlend * dry[j]) * outputGain;
            auto output = p.bypassBlend == 1.0 ? dry[j] : monitored + p.bypassBlend * (dry[j] - monitored);
            if (!std::isfinite(output)) output = 0;
            output = std::clamp(output, -static_cast<double>(std::numeric_limits<float>::max()), static_cast<double>(std::numeric_limits<float>::max()));
            buffers[c][sample] = static_cast<float>(output);
            outputPeak = std::max(outputPeak, std::abs(output));
            scopeSample += output;
        }
        p.outputPeak = std::max(outputPeak, p.outputPeak * p.meterRelease);
        const auto analyserIndex = p.analyserWrite.load(std::memory_order_relaxed);
        p.analyser[analyserIndex].store(static_cast<float>(scopeSample / channelCount), std::memory_order_relaxed);
        p.analyserWrite.store((analyserIndex + 1) % Engine::analyserSize, std::memory_order_release);
        if (++p.scopeCounter >= p.scopeInterval) {
            p.scopeCounter = 0;
            const auto index = p.scopeWrite.load(std::memory_order_relaxed);
            p.scope[index].store(static_cast<float>(scopeSample / channelCount), std::memory_order_relaxed);
            p.scopeWrite.store((index + 1) % Engine::scopeSize, std::memory_order_release);
        }
    }
    p.inputMeter.store(static_cast<float>(std::min(p.inputPeak, static_cast<double>(std::numeric_limits<float>::max()))), std::memory_order_relaxed);
    p.outputMeter.store(static_cast<float>(std::min(p.outputPeak, static_cast<double>(std::numeric_limits<float>::max()))), std::memory_order_relaxed);
    p.lfoMeter.store(static_cast<float>(lfo),std::memory_order_relaxed);p.phaseMeter.store(static_cast<float>(p.phase),std::memory_order_relaxed);
    for(size_t i=0;i<moduleCount;++i){const auto& motion=p.motionState[i];const auto& settings=desired.motions[i];const auto value=settings.independent?shapeLfo(settings.shape,motion.phase,motion.random,motion.previous,motion.target,settings.curve,settings.curveCount):lfo;p.moduleLfoMeter[i].store(static_cast<float>(value),std::memory_order_relaxed);p.modulePhaseMeter[i].store(static_cast<float>(motion.phase),std::memory_order_relaxed);}
}

float Engine::getLfoValue(int module) const noexcept {return module>=0&&module<moduleCount?impl->moduleLfoMeter[static_cast<size_t>(module)].load(std::memory_order_relaxed):impl->lfoMeter.load(std::memory_order_relaxed);}
float Engine::getLfoPhase(int module) const noexcept {return module>=0&&module<moduleCount?impl->modulePhaseMeter[static_cast<size_t>(module)].load(std::memory_order_relaxed):impl->phaseMeter.load(std::memory_order_relaxed);}
float Engine::getInputPeak() const noexcept { return impl->inputMeter.load(std::memory_order_relaxed); }
float Engine::getOutputPeak() const noexcept { return impl->outputMeter.load(std::memory_order_relaxed); }
void Engine::copyScope(std::array<float, scopeSize>& destination) const noexcept
{
    const auto start = impl->scopeWrite.load(std::memory_order_acquire);
    for (unsigned i = 0; i < scopeSize; ++i)
        destination[i] = impl->scope[(start + i) % scopeSize].load(std::memory_order_relaxed);
}
void Engine::copyAnalyser(std::array<float, analyserSize>& destination) const noexcept
{
    const auto start = impl->analyserWrite.load(std::memory_order_acquire);
    for (unsigned i = 0; i < analyserSize; ++i)
        destination[i] = impl->analyser[(start + i) % analyserSize].load(std::memory_order_relaxed);
}

}
