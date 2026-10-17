#include "ShaperDSP.h"
#include <algorithm>
#include <cmath>

namespace alpha {
namespace {
constexpr double pi=3.14159265358979323846;
double bounded(double x,double lo,double hi,double fallback=0) noexcept {return std::isfinite(x)?std::clamp(x,lo,hi):fallback;}
double lowpass(double hz,double sr) noexcept {return 1-std::exp(-2*pi*std::clamp(hz,1.0,sr*.45)/sr);}
double logCosh(double x) noexcept {const auto a=std::abs(x);return a+std::log1p(std::exp(-2*a))-std::log(2.0);}
double antiAliasedDrive(double x,double gain,double& previous) noexcept {
    const auto u=x*gain,delta=u-previous;
    const auto y=std::abs(delta)>1e-6?(logCosh(u)-logCosh(previous))/delta:std::tanh((u+previous)*.5);
    previous=u;return y/std::tanh(gain);
}
}
void ShaperDSP::prepare(double sr) {
    rate=bounded(sr,8000,384000,48000);
    widthDelay.assign(static_cast<size_t>(std::ceil(rate*.035))+8,0);
    reset();
}
void ShaperDSP::reset() noexcept {
    filters={};panWave.fill(0);std::fill(widthDelay.begin(),widthDelay.end(),0);writePosition=0;
    volumeGain=1;panPosition=0;sideLow=0;filterCounter=0;
    filterG=.1;filterK=1;filterA1=1/(1+filterG*(filterG+filterK));filterA2=filterG*filterA1;filterA3=filterG*filterA2;
    panSmooth=1-std::exp(-1/(rate*.018));widthBassCoefficient=0;
    initialized.fill(false);modeWeights={};controlSmoothing=1-std::exp(-1/(rate*.005));modeSmoothing=1-std::exp(-1/(rate*.020));
}
double ShaperDSP::delayRead(double samples) const noexcept {
    if(widthDelay.empty())return 0;
    const auto n=static_cast<int>(widthDelay.size());
    auto position=static_cast<double>(writePosition)-std::clamp(samples,1.0,static_cast<double>(n-4));
    if(position<0)position+=n;
    const auto i=static_cast<int>(position);const auto f=position-i;
    const auto at=[&](int offset){return widthDelay[static_cast<size_t>((i+offset+n)%n)];};
    // Third-order Lagrange interpolation, in the feed-forward path only.
    return at(-1)*(-f*(f-1)*(f-2)/6)+at(0)*((f+1)*(f-1)*(f-2)/2)
         +at(1)*(-(f+1)*f*(f-2)/2)+at(2)*((f+1)*f*(f-1)/6);
}
ShaperDSP::Stereo ShaperDSP::process(int index,Stereo input,double amount,Controls c,int mode,double lfo,double phase) noexcept {
    for(auto& x:input)x=bounded(x,-1e6,1e6);
    for(auto& x:c)x=bounded(x,0,1,.5);
    const auto slot=static_cast<size_t>(std::clamp(index,0,3));
    if(!initialized[slot]){smoothedControls[slot]=c;modeWeights[slot][(size_t)std::clamp(mode,0,2)]=1;initialized[slot]=true;}
    else for(size_t i=0;i<4;++i){smoothedControls[slot][i]+=controlSmoothing*(c[i]-smoothedControls[slot][i]);c[i]=smoothedControls[slot][i];}
    amount=bounded(amount,0,1);lfo=bounded(lfo,-1,1);mode=std::clamp(mode,0,2);
    auto& weights=modeWeights[slot];for(size_t i=0;i<3;++i)weights[i]+=modeSmoothing*((mode==(int)i?1.0:0.0)-weights[i]);
    auto wet=input;
    if(index==0) {
        // VCA envelopes: amplitude multiplication has no spectral aliasing of
        // its own; audio-rate edges are explicitly slewed by attack/release.
        const auto curve=.25*std::pow(16.0,c[0]);
        const auto phase=.5+.5*lfo;
        const auto floor=c[1]<1e-6?0.0:std::pow(10.0,(-60+60*c[1])/20);
        const auto pump=std::pow(std::clamp(phase,0.0,1.0),curve);
        const auto tremolo=std::pow(.5+.5*std::sin(pi*(phase-.5)),curve);
        const auto gate=1/(1+std::exp(-std::clamp((phase-.5)*(8+curve*24),-40.0,40.0)));
        const auto open=weights[0]*pump+weights[1]*tremolo+weights[2]*gate;
        const auto desired=floor+(1-floor)*open;
        const auto attack=.2+99.8*c[2]*c[2],release=5+995*c[3]*c[3];
        const auto duckMilliseconds=desired<volumeGain?attack:release;
        const auto gateMilliseconds=desired>volumeGain?attack:release;
        const auto milliseconds=weights[0]*duckMilliseconds+(weights[1]+weights[2])*gateMilliseconds;
        const auto coefficient=1-std::exp(-1/(rate*milliseconds*.001));
        volumeGain+=coefficient*(desired-volumeGain);
        for(auto& x:wet)x*=volumeGain;
    } else if(index==1) {
        // Zavalishin's TPT state-variable topology (trapezoidal integrators).
        // The same core is documented in JUCE StateVariableTPTFilter; unlike
        // the explicit Chamberlin update it remains stable near Nyquist.
        if((filterCounter++&31u)==0) {
            const auto hz=std::clamp(20*std::pow(1000.0,std::clamp(c[0]+lfo*c[2]*.5,0.0,1.0)),20.0,rate*.43);
            const auto q=.5+11.5*c[1]*c[1];
            filterG=std::tan(pi*hz/rate);filterK=1/q;
            filterA1=1/(1+filterG*(filterG+filterK));filterA2=filterG*filterA1;filterA3=filterG*filterA2;
        }
        for(size_t ch=0;ch<2;++ch) {
            auto& state=filters[ch];
            const auto gain=std::pow(10.0,c[3]*24/20);
            const auto nonlinear=antiAliasedDrive(input[ch],gain,state.previousDrive);
            const auto x=input[ch]+std::clamp(c[3]*4,0.0,1.0)*(nonlinear-input[ch]);
            const auto v3=x-state.integrator2;
            const auto v1=filterA1*state.integrator1+filterA2*v3;
            const auto v2=state.integrator2+filterA2*state.integrator1+filterA3*v3;
            state.integrator1=2*v1-state.integrator1;state.integrator2=2*v2-state.integrator2;
            if(std::abs(state.integrator1)<1e-25)state.integrator1=0;
            if(std::abs(state.integrator2)<1e-25)state.integrator2=0;
            // Unity peak bandpass scaling prevents resonance from being a
            // disguised level boost. LP/HP retain their genuine resonant peak.
            wet[ch]=weights[0]*v2+weights[1]*filterK*v1+weights[2]*(x-filterK*v1-v2);
        }
    } else if(index==2) {
        double shifted=lfo;
        if(phase>=0){const auto current=phase-std::floor(phase);panWave[(size_t)(current*512)%512]=lfo;
            const auto shiftedPhase=current-c[2]-std::floor(current-c[2]);const auto position=shiftedPhase*512;const auto index=(size_t)position%512;
            if(c[2]>1e-6)shifted=panWave[index]+(position-std::floor(position))*(panWave[(index+1)%512]-panWave[index]);}
        const auto target=std::clamp(c[0]*2-1+weights[1]*shifted*c[1],-1.0,1.0);
        const auto ms=.2+199.8*c[3]*c[3];panSmooth=1-std::exp(-1/(rate*ms*.001));
        panPosition+=panSmooth*(target-panPosition);
        const Stereo balance{input[0]*(panPosition>0?1-panPosition:1),input[1]*(panPosition<0?1+panPosition:1)};
        const auto angle=(panPosition+1)*pi*.25;const Stereo pan{input[0]*std::sqrt(2.0)*std::cos(angle),input[1]*std::sqrt(2.0)*std::sin(angle)};
        const auto rotation=panPosition*pi*.25;const Stereo rotated{input[0]*std::cos(rotation)-input[1]*std::sin(rotation),input[0]*std::sin(rotation)+input[1]*std::cos(rotation)};
        for(size_t ch=0;ch<2;++ch)wet[ch]=weights[0]*balance[ch]+weights[1]*pan[ch]+weights[2]*rotated[ch];
    } else if(index==3) {
        const auto mid=.5*(input[0]+input[1]),side=.5*(input[0]-input[1]);
        if(!widthDelay.empty())widthDelay[writePosition]=mid;
        auto width=std::clamp(c[0]*2+lfo*c[2],0.0,2.0);
        auto processedSide=(weights[0]+weights[1])*side*width;
        processedSide+=weights[1]*(delayRead(1)-delayRead(1+c[3]*.030*rate))*.5*width;
        const auto cutoff=c[1]<1e-6?0.0:20*std::pow(25.0,c[1]);
        widthBassCoefficient=lowpass(cutoff==0?20:cutoff,rate);
        sideLow+=widthBassCoefficient*(processedSide-sideLow);
        processedSide-=sideLow*std::clamp(c[1]*100,0.0,1.0);
        wet={mid+processedSide,mid-processedSide};
        if(!widthDelay.empty()&&++writePosition==widthDelay.size())writePosition=0;
    }
    if(amount==0)return input;
    for(size_t ch=0;ch<2;++ch){const auto output=input[ch]+amount*(wet[ch]-input[ch]);wet[ch]=std::isfinite(output)?std::clamp(output,-1e8,1e8):0;}
    return wet;
}
}
