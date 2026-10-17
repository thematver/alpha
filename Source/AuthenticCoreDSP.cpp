#include "AuthenticCoreDSP.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace alpha {
namespace {
constexpr double pi=3.14159265358979323846;
double clean(double x) noexcept {return std::abs(x)<1e-25?0:x;}
struct Line {
    std::vector<double> data;size_t write=0;
    void prepare(size_t count){data.assign(std::max<size_t>(count,8),0);write=0;}
    void reset() noexcept {std::fill(data.begin(),data.end(),0);write=0;}
    double read(double delay) const noexcept {
        auto at=static_cast<double>(write)-std::clamp(delay,1.0,static_cast<double>(data.size()-2));
        if(at<0)at+=data.size();const auto i=static_cast<size_t>(at),j=(i+1)%data.size();const auto f=at-static_cast<double>(i);
        return data[i]+f*(data[j]-data[i]);
    }
    void push(double x) noexcept {data[write]=clean(x);if(++write==data.size())write=0;}
};
struct Diffuser {
    Line line;
    double tick(double x,double delay,double gain) noexcept {
        const auto delayed=line.read(delay);const auto y=delayed-gain*x;line.push(x+gain*y);return y;
    }
};
}

// Differential Jiles-Atherton equation and Langevin limit, independently
// derived from Chowdhury, DAFx-2019, section 3.2 (not copied CHOW/GPL code).
// https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf
// Integration is in field H with four RK4 substeps. Effective reversibility
// represents a biased operating loop; no ultrasonic bias oscillator is claimed.
void MagneticHysteresis::reset() noexcept {m=previousH=0;}
double MagneticHysteresis::process(double input,double coercivity,double reversibility) noexcept {
    const auto h=std::clamp(input,-6.0,6.0),dh=(h-previousH)*.25;
    const auto direction=dh>=0?1.0:-1.0;
    const auto k=.16+.5*std::clamp(coercivity,0.0,1.0),c=.35+.5*std::clamp(reversibility,0.0,1.0);
    constexpr double a=.65,alpha=.018;
    const auto derivative=[&](double field,double magnetization){
        const auto q=(field+alpha*magnetization)/a;
        double man,slope;
        if(std::abs(q)<.01){const auto q2=q*q;man=q*(1.0/3-q2/45+2*q2*q2/945);slope=1.0/3-q2/15+2*q2*q2/189;}
        else{const auto t=std::tanh(q);man=1/t-1/q;slope=1/(q*q)+1-1/(t*t);}
        const auto delta=man-magnetization;
        const auto irreversible=direction*delta>0?(1-c)*delta/((1-c)*direction*k-alpha*delta):0;
        const auto reversible=(c/a)*slope;
        return std::clamp((irreversible+reversible)/(1-alpha*reversible),0.0,4.0);
    };
    auto field=previousH;
    for(int step=0;step<4;++step){
        const auto k1=derivative(field,m),k2=derivative(field+dh*.5,m+dh*.5*k1);
        const auto k3=derivative(field+dh*.5,m+dh*.5*k2),k4=derivative(field+dh,m+dh*k3);
        m=std::clamp(m+dh*(k1+2*k2+2*k3+k4)/6,-1.0,1.0);field+=dh;
    }
    previousH=h;return clean(m*(3*a));
}

struct PlateReverb::Impl {
    std::array<Diffuser,4> input;
    std::array<Diffuser,2> modulated,second;
    std::array<Line,2> firstDelay,lastDelay;
    std::array<double,2> feedback {},low {};
    double sr=48000,phase=0,size=1,bandwidth=0,bw=.5,damp=.5;
    unsigned counter=0;std::array<double,2> gain{.5,.5};
};
PlateReverb::PlateReverb():impl(std::make_unique<Impl>()){}
PlateReverb::~PlateReverb()=default;
void PlateReverb::prepare(double sampleRate){
    auto& p=*impl;p.sr=std::clamp(sampleRate,8000.0,384000.0);p.bw=1-std::exp(-2*pi*16000/p.sr);
    for(auto& ap:p.input)ap.line.prepare(static_cast<size_t>(p.sr*.04)+8);
    for(auto& ap:p.modulated)ap.line.prepare(static_cast<size_t>(p.sr*.09)+8);
    for(auto& ap:p.second)ap.line.prepare(static_cast<size_t>(p.sr*.22)+8);
    for(auto& line:p.firstDelay)line.prepare(static_cast<size_t>(p.sr*.36)+8);
    for(auto& line:p.lastDelay)line.prepare(static_cast<size_t>(p.sr*.32)+8);
    reset();
}
void PlateReverb::reset() noexcept {
    auto& p=*impl;for(auto& ap:p.input)ap.line.reset();for(auto& ap:p.modulated)ap.line.reset();for(auto& ap:p.second)ap.line.reset();
    for(auto& line:p.firstDelay)line.reset();for(auto& line:p.lastDelay)line.reset();p.feedback.fill(0);p.low.fill(0);p.phase=0;p.size=1;p.bandwidth=0;p.counter=0;
}
// Dattorro, JAES 45(9), 1997, Fig.1 / Tables1-2:
// https://freeverb3-vst.sourceforge.io/doc/EffectDesignPart1.pdf
// Four input lattices -> two cross-coupled, modulated allpass/delay/damping
// tanks, with distributed signed stereo taps. Room/Bloom use separate FDNs.
std::array<double,2> PlateReverb::process(std::array<double,2> in,double size,double decay,double damping) noexcept {
    auto& p=*impl;const auto desired=.5*std::pow(4.0,std::clamp(size,0.0,1.0));
    const auto maximumSizeStep=.3*29761/(p.sr*4453);p.size+=std::clamp(desired-p.size,-maximumSizeStep,maximumSizeStep);const auto scale=p.sr/29761*p.size;
    p.phase+=.73/p.sr;if(p.phase>=1)p.phase-=1;
    auto x=(in[0]+in[1])*.25;
    p.bandwidth=clean(p.bandwidth+p.bw*(x-p.bandwidth));x=p.bandwidth;
    constexpr std::array<double,4> lengths{142,107,379,277};
    for(size_t i=0;i<4;++i)x=p.input[i].tick(x,lengths[i]*scale,i<2?.75:.625);
    constexpr std::array<double,2> ap1{672,908},d1{4453,4217},ap2{1800,2656},d2{3720,3163};
    if((p.counter++&15u)==0){
        const auto rt60=.2*std::pow(100.0,decay);
        const auto dampHz=1000*std::pow(16.0,std::clamp(damping,0.0,1.0));p.damp=1-std::exp(-2*pi*dampHz/p.sr);
        for(size_t c=0;c<2;++c){const auto seconds=(ap1[c]+d1[c]+ap2[c]+d2[c])*p.size/29761;p.gain[c]=std::pow(.001,seconds/rt60);}
    }
    const auto old=p.feedback;
    for(size_t c=0;c<2;++c){
        auto y=x+old[1-c]*p.gain[c];
        y=p.modulated[c].tick(y,ap1[c]*scale+8*p.sr/29761*std::sin(2*pi*p.phase+c*pi*.5),-.7);
        p.firstDelay[c].push(y);y=p.firstDelay[c].read(d1[c]*scale);
        p.low[c]=clean(p.low[c]+p.damp*(y-p.low[c]));y=p.second[c].tick(p.low[c],ap2[c]*scale,.5);
        p.lastDelay[c].push(y);p.feedback[c]=p.lastDelay[c].read(d2[c]*scale);
    }
    const auto tap=[&](const Line& line,double t){return line.read(t*scale);};
    const auto left=tap(p.firstDelay[1],266)+tap(p.firstDelay[1],2974)-tap(p.second[1].line,1913)+tap(p.lastDelay[1],1996)-tap(p.firstDelay[0],1990)-tap(p.second[0].line,187)-tap(p.lastDelay[0],1066);
    const auto right=tap(p.firstDelay[0],353)+tap(p.firstDelay[0],3627)-tap(p.second[0].line,1228)+tap(p.lastDelay[0],2673)-tap(p.firstDelay[1],2111)-tap(p.second[1].line,335)-tap(p.lastDelay[1],121);
    return {clean(left*.6),clean(right*.6)};
}
}
