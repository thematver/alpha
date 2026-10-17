#include "ShaperDSP.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdlib>
namespace {
constexpr double pi=3.14159265358979323846;
int checks=0;
void check(bool v,const char* s){++checks;if(!v){std::cerr<<"FAIL "<<s<<'\n';std::exit(1);}}
double energy(int module,int mode,alpha::ShaperDSP::Controls controls,double frequency,double sr=48000) {
    alpha::ShaperDSP dsp;dsp.prepare(sr);double sum=0;
    for(int i=0;i<(int)sr;++i){const auto x=.2*std::sin(2*pi*frequency*i/sr);const auto y=dsp.process(module,{x,x},1,controls,mode,std::sin(2*pi*2*i/sr));if(i>(int)sr/2)sum+=y[0]*y[0];}
    return std::sqrt(sum/(sr/2));
}
}
int main(){
 for(double sr:{44100.,48000.,96000.}) {
  for(int module=0;module<4;++module)for(int mode=0;mode<3;++mode){
   alpha::ShaperDSP dsp;dsp.prepare(sr);bool exact=true,finite=true;double peak=0;
   for(int i=0;i<12000;++i){alpha::ShaperDSP::Stereo in{.2*std::sin(i*.17),.1*std::cos(i*.11)};auto out=dsp.process(module,in,0,{.5,.5,.5,.5},mode,std::sin(i*.02));exact&=out==in;}
   check(exact,"all twelve shaper modes have exact zero amount");dsp.reset();
   for(int i=0;i<12000;++i){auto out=dsp.process(module,{.1*std::sin(i*.17),.1*std::cos(i*.11)},1,{(i%2)?0.:1.,1,1,1},mode,std::sin(i*.012));for(auto x:out){finite&=std::isfinite(x);peak=std::max(peak,std::abs(x));}}
   check(finite&&peak<16,"high-Q automation remains finite within the resonant headroom bound");
  }
  const double cutoff=std::log(1000./20)/std::log(1000.);
  check(energy(1,0,{cutoff,.1,0,0},200,sr)>energy(1,0,{cutoff,.1,0,0},8000,sr)*15,"LP passes bass and rejects treble");
  check(energy(1,2,{cutoff,.1,0,0},8000,sr)>energy(1,2,{cutoff,.1,0,0},100,sr)*30,"HP passes treble and rejects bass");
  check(energy(1,1,{cutoff,.4,0,0},1000,sr)>energy(1,1,{cutoff,.4,0,0},100,sr)*5,"BP peaks around the specified cutoff");
 }
 alpha::ShaperDSP dsp;dsp.prepare(48000);bool monoPreserved=true,rotationEnergy=true,muted=true;
 for(int i=0;i<48000;++i){alpha::ShaperDSP::Stereo in{.17*std::sin(i*.021),.13*std::cos(i*.017)};auto w=dsp.process(3,in,1,{1,.5,.7,.5},1,std::sin(i*.001));monoPreserved&=std::abs(w[0]+w[1]-in[0]-in[1])<1e-12;auto p=dsp.process(2,in,1,{.8,0,0,0},2,0);rotationEnergy&=std::abs(p[0]*p[0]+p[1]*p[1]-in[0]*in[0]-in[1]*in[1])<1e-12;}
 check(monoPreserved,"MS and Haas widening preserve the mono sum");check(rotationEnergy,"pan rotation preserves stereo vector energy");
 dsp.reset();for(int i=0;i<96000;++i){auto y=dsp.process(0,{.2,.2},1,{.5,0,0,0},0,-1);if(i>48000)muted&=std::abs(y[0])<1e-12;}
 check(muted,"VCA closes completely at zero-floor closed envelope");
 dsp.reset();bool silence=true;for(int n=0;n<10000;++n)for(int module=0;module<4;++module){const auto y=dsp.process(module,{0,0},1,{.8,.8,.8,.8},2,.8);silence&=y[0]==0&&y[1]==0;}
 check(silence,"all shapers preserve silence after reset");
 for(int module=0;module<4;++module){
  dsp.reset();double previous=0,maxJump=0;
  for(int n=0;n<48000;++n){const double x=.2*std::sin(2*pi*220*n/48000);auto y=dsp.process(module,{x,x*.7},1,{.65,.2,.25,.3},(n/3000)%3,std::sin(2*pi*2*n/48000),(double)(n%24000)/24000);if(n>1000)maxJump=std::max(maxJump,std::abs(y[0]-previous));previous=y[0];}
  check(maxJump<.12,"mode changes crossfade without an added amplitude step");
 }
 const auto filterZero=energy(1,0,{.9,.1,0,0},220);
 const auto filterEpsilon=energy(1,0,{.9,.1,0,.00001},220);
 check(std::abs(filterEpsilon/filterZero-1)<.001,"filter drive transfer is continuous at zero");
 check(std::abs(energy(0,1,{0,0,0,0},220)-energy(0,1,{1,0,0,0},220))>.01,"Tremolo Curve changes its amplitude envelope");
 dsp.reset();for(int n=0;n<20000;++n)dsp.process(1,{.2,.2},1,{.5,1,0,1},0,0);
 double tail=0;for(int n=0;n<96000;++n){auto y=dsp.process(1,{0,0},1,{.5,1,0,1},0,0);if(n>90000)tail=std::max(tail,std::abs(y[0]));}
 check(tail<1e-8,"high-resonance filter energy decays after input stops");
 std::cout<<"PASS "<<checks<<" shaper checks\n";
}
