#include "TimePitchDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr double sr = 48000, pi = 3.14159265358979323846;
int failures = 0;
void check(bool result, const char* name) { std::cout << (result ? "PASS " : "FAIL ") << name << '\n'; if (!result) ++failures; }
double tone(int sample, double hz) { return .35 * std::sin(2*pi*hz*sample/sr); }
double spectralPower(const std::vector<double>& samples, double hz) {
    const double coefficient = 2 * std::cos(2*pi*hz/sr); double a=0,b=0;
    for (size_t i=0;i<samples.size();++i) {
        const double window = .5-.5*std::cos(2*pi*(double)i/(double)(samples.size()-1));
        const double next=samples[i]*window+coefficient*a-b;b=a;a=next;
    }
    return a*a+b*b-coefficient*a*b;
}
double peakFrequency(const std::vector<double>& samples, double expected) {
    double best=expected,power=-1;
    for(double hz=expected-18;hz<=expected+18;hz+=.25){double p=spectralPower(samples,hz);if(p>power){power=p;best=hz;}}
    return best;
}
void transparent() {
    alpha::TimePitchDSP dsp; dsp.prepare(sr,512); bool neutral=true,bypass=true;
    for(int i=0;i<48000;++i){std::array<double,2> x {tone(i,443),tone(i,731)};
        neutral &= dsp.process(0,x,1,{.5,.5,.5,0},0,.5)==x;
        neutral &= dsp.process(1,x,1,{.5,.5,.5,0},0,.5)==x;
    }
    for(int i=0;i<12000;++i){std::array<double,2> x {tone(i,317),tone(i,611)};
        bypass &= dsp.process(0,x,0,{1,.9,.9,.5},2,.5)==x;
        bypass &= dsp.process(1,x,0,{.8,.2,.9,.5},2,.5)==x;
    }
    check(neutral,"both neutral modes are bit-exact, zero-latency identity");
    check(bypass,"amount zero is bit-exact despite nonneutral controls");
}
void transposition() {
    for(double semitone: {-12.0,7.0,12.0}){
        alpha::TimePitchDSP dsp;dsp.prepare(sr,64);std::vector<double> output;
        for(int i=0;i<144000;++i){auto y=dsp.process(0,{tone(i,440),tone(i,440)},1,{.5+semitone/24,.5,.5,0},1,.5);if(i>=96000)output.push_back(y[0]);}
        const double expected=440*std::exp2(semitone/12),peak=peakFrequency(output,expected);
        std::cout<<"pitch "<<semitone<<" st: "<<peak<<" Hz, expected "<<expected<<" Hz\n";
        check(std::abs(peak-expected)<.5,"pitch shift changes frequency independently of stream duration");
        check(spectralPower(output,expected)>spectralPower(output,440)*2,"shifted output dominates unshifted fundamental");
    }
}
void silenceStereoAndBounds() {
    bool silence=true,stereo=true,finite=true;
    alpha::TimePitchDSP dsp;dsp.prepare(sr,257);
    for(int mode=0;mode<3;++mode){dsp.reset();for(int i=0;i<24000;++i){auto p=dsp.process(0,{0,0},1,{.9,.3,.6,.7},mode,2);auto t=dsp.process(1,{0,0},1,{.1,.1,.1,1},mode,2);silence&=p[0]==0&&p[1]==0&&t[0]==0&&t[1]==0;}}
    dsp.reset();
    for(int i=0;i<192000;++i){const double x=tone(i,443);auto p=dsp.process(0,{x,-x},1,{.77,.4,.5,0},1,.5);auto t=dsp.process(1,{x,-x},1,{.3,.8,.7,.8},i/64000,.5);stereo&=std::abs(p[0]+p[1])<1e-12&&std::abs(t[0]+t[1])<1e-12;for(double v:t)finite&=std::isfinite(v)&&std::abs(v)<16;}
    check(silence,"all modes preserve digital silence after reset");check(stereo,"stereo stays coherent and retains opposite polarity");check(finite,"feedback remains finite and bounded");
    dsp.reset();bool clean=true;for(int i=0;i<12000;++i){auto y=dsp.process(1,{0,0},1,{1,0,1,1},2,.03);clean&=y[0]==0&&y[1]==0;}check(clean,"O(1) reset exposes no old audio");
}
void blockInvariance() {
    alpha::TimePitchDSP a,b;a.prepare(sr,17);b.prepare(sr,1024);bool exact=true;
    for(int i=0;i<96000;++i){const std::array<double,2>x{tone(i,329),tone(i,521)};const int mode=i/32000;std::array<double,4> c{.2+.6*(double)i/96000,.17+.7*(double)i/96000,.4,.3};exact&=a.process(0,x,.8,c,mode,.7)==b.process(0,x,.8,c,mode,.7);exact&=a.process(1,x,.8,c,mode,.7)==b.process(1,x,.8,c,mode,.7);}
    check(exact,"processing is bit-identical across maximum block sizes");
}
void automationAndReverse() {
    alpha::TimePitchDSP dsp;dsp.prepare(sr,256);double previous=0,maxJump=0;bool finite=true;
    for(int i=0;i<144000;++i){const int section=i/12000;std::array<double,4> c{section%2?.9:.1,section%2?.95:.05,section%2?.9:.1,.4};auto y=dsp.process(0,{tone(i,220),tone(i,330)},1,c,section%3,.5);if(i>6000)maxJump=std::max(maxJump,std::abs(y[0]-previous));previous=y[0];finite&=std::isfinite(y[0]);}
    std::cout<<"pitch automation max adjacent-sample change "<<maxJump<<'\n';check(finite&&maxJump<.2,"pitch/grain/mode automation avoids discontinuous clicks");
    dsp.reset();int negativeRun=0,longest=0;previous=0;
    for(int i=0;i<96000;++i){double x=.3*std::fmod((double)i/24000,1.0);auto y=dsp.process(1,{x,x},1,{0,.5,.6,0},1,2);if(i>48000&&y[0]-previous<-1e-6){++negativeRun;longest=std::max(longest,negativeRun);}else negativeRun=0;previous=y[0];}
    check(longest>100,"reverse reads captured audio backwards for sustained runs");
}
void boundedTail() {
    alpha::TimePitchDSP dsp;dsp.prepare(sr,128);double tail=0;
    for(int i=0;i<96000;++i){double x=i<48000?tone(i,440):0;auto y=dsp.process(0,{x,x},1,{1,1,.5,0},1,.5);if(i>72000)tail=std::max(tail,std::abs(y[0]));}
    check(tail<1e-10,"pitch keeps streaming duration and has a bounded grain tail");
}
void modeControlsAndAliasing() {
    for(int mode=0;mode<3;++mode){
        alpha::TimePitchDSP dsp;dsp.prepare(sr,128);std::vector<double> output;
        const double requested=mode==2?8:6.7;
        const double semitones=mode==0?7:mode==2?12:6.7;
        for(int i=0;i<96000;++i){auto y=dsp.process(0,{tone(i,440),tone(i,440)},1,{.5+requested/24,.5,.5,0},mode,.5);if(i>=48000)output.push_back(y[0]);}
        const double expected=440*std::exp2(semitones/12),peak=peakFrequency(output,expected);
        check(std::abs(peak-expected)<.5,mode==0?"Chromatic snaps to semitones":mode==1?"Smooth retains fractional semitone shifts":"Octave snaps to octave intervals");
    }
    alpha::TimePitchDSP high,low;high.prepare(sr,128);low.prepare(sr,128);double highEnergy=0,lowEnergy=0;
    for(int i=0;i<96000;++i){auto h=high.process(0,{tone(i,17000),tone(i,17000)},1,{1,.5,.5,0},1,.5);auto l=low.process(0,{tone(i,1000),tone(i,1000)},1,{1,.5,.5,0},1,.5);if(i>48000){highEnergy+=h[0]*h[0];lowEnergy+=l[0]*l[0];}}
    const double rejection=10*std::log10(highEnergy/lowEnergy);std::cout<<"17kHz up-octave alias rejection vs1kHz: "<<rejection<<" dB\n";
    check(rejection<-18,"upsampling reads attenuate above-band content before resampling");
    alpha::TimePitchDSP stop;stop.prepare(sr,128);double stopEnergy=0;
    for(int i=0;i<96000;++i){auto y=stop.process(1,{tone(i,443),tone(i,443)},1,{.4,i<24000?.5:0,.5,0},0,1);if(i>72000)stopEnergy+=y[0]*y[0];}
    check(stopEnergy<1e-9,"Slip speed zero comes to a silent tape stop");
}
void repeatAndTimeAutomation() {
    alpha::TimePitchDSP dsp;dsp.prepare(sr,128);int clusters=0,since=48000;double max=0;
    for(int i=0;i<144000;++i){double input=(i==24000)?.5:0;auto y=dsp.process(1,{input,input},1,{0,1,.7,.8},2,1);max=std::max(max,std::abs(y[0]));if(std::abs(y[0])>.005){if(since>400)++clusters;since=0;}else ++since;}
    std::cout<<"repeat impulse produced "<<clusters<<" separated bursts\n";check(clusters>=4&&max<1,"Repeat produces multiple bounded rhythmic recurrences");
    dsp.reset();double previous=0,maxJump=0;int jumpSample=0;bool finite=true;
    for(int i=0;i<192000;++i){const int section=i/16000;std::array<double,4> c{section%2?.8:.1,section%2?.9:.2,section%2?.9:.1,.35};auto y=dsp.process(1,{tone(i,220),tone(i,330)},1,c,section%3,section%2?.7:1.4);if(i>10000&&std::abs(y[0]-previous)>maxJump){maxJump=std::abs(y[0]-previous);jumpSample=i;}previous=y[0];finite&=std::isfinite(y[0]);}
    std::cout<<"time automation max adjacent-sample change "<<maxJump<<" at sample "<<jumpSample<<'\n';check(finite&&maxJump<.3,"Time mode, speed, clock and length automation is continuous");
}
void hostClockAndSeek() {
    alpha::TimePitchDSP a,b;a.prepare(sr,64);b.prepare(sr,1024);
    double beforeDifference=0,afterDifference=0,previous=0,maxJump=0;
    constexpr int seek=96000;
    const std::array<double,4> controls{1,.8,.6,0}; // Four motion cycles per slice.
    for(int i=0;i<240000;++i){
        const double advancing=(double)i*4/sr;
        const double phaseA=i<seek?8+advancing:21.37+(double)(i-seek)*4/sr;
        const double phaseB=i<seek?9+advancing:21.37+(double)(i-seek)*4/sr;
        const std::array<double,2> input{tone(i,443)+.08*std::sin(i*.023),tone(i,661)};
        const auto x=a.process(1,input,1,controls,2,4,0,phaseA);
        const auto y=b.process(1,input,1,controls,2,4,0,phaseB);
        if(i>48000&&i<seek)beforeDifference=std::max(beforeDifference,std::abs(x[0]-y[0]));
        if(i>seek+48000)afterDifference=std::max(afterDifference,std::abs(x[0]-y[0]));
        if(i>seek-32&&i<seek+4800)maxJump=std::max(maxJump,std::abs(x[0]-previous));previous=x[0];
    }
    std::cout<<"host seek convergence "<<afterDifference<<", max adjacent change "<<maxJump<<'\n';
    check(beforeDifference>.01,"four-cycle Repeat distinguishes absolute host bar phase");
    check(afterDifference<1e-8,"seek reanchors Repeat deterministically despite different prior bar phases");
    check(maxJump<.12,"transport seek reset crossfades without a waveform discontinuity");
    alpha::TimePitchDSP absolute,wrapped;absolute.prepare(sr,128);wrapped.prepare(sr,128);bool same=true;
    for(int i=0;i<96000;++i){const double phase=(double)i*2/sr;std::array<double,2>input{tone(i,220),tone(i,330)};
        const auto x=absolute.process(1,input,1,{.4,.7,.5,0},2,2,0,phase);
        const auto y=wrapped.process(1,input,1,{.4,.7,.5,0},2,2,0,phase-std::floor(phase));
        same&=std::abs(x[0]-y[0])<1e-8&&std::abs(x[1]-y[1])<1e-8;
    }
    check(same,"wrapped host clock crosses ordinary cycle boundaries without false seeks");
}
void preRollAndFastClock() {
    alpha::TimePitchDSP negative,positive;negative.prepare(sr,64);positive.prepare(sr,511);
    bool equivalent=true,finite=true;double previous=0,maxJump=0;
    for(int i=0;i<192000;++i) {
        const double phase=i<96000?-8.25+(double)i*4/sr:-21.37+(double)(i-96000)*4/sr;
        const std::array<double,2> input{tone(i,443)+.08*std::sin(i*.023),tone(i,661)};
        const auto a=negative.process(1,input,1,{1,.8,.6,0},2,4,0,phase);
        const auto b=positive.process(1,input,1,{1,.8,.6,0},2,4,0,phase+40);
        for(int channel=0;channel<2;++channel) {equivalent&=std::abs(a[(size_t)channel]-b[(size_t)channel])<1e-8;finite&=std::isfinite(a[(size_t)channel]);}
        if(i>95968&&i<100800)maxJump=std::max(maxJump,std::abs(a[0]-previous));previous=a[0];
    }
    check(equivalent&&finite,"negative pre-roll and backward seeks retain absolute bar phase across timeline zero");
    check(maxJump<.12,"negative transport seek crossfades without a waveform discontinuity");
    alpha::TimePitchDSP host,free;host.prepare(sr,17);free.prepare(sr,1024);bool aligned=true;
    constexpr double rate=999.0/60*4;
    for(int i=0;i<144000;++i) {
        const std::array<double,2> input{tone(i,317),tone(i,443)};
        const auto a=host.process(1,input,1,{1,.8,.6,0},2,rate,0,(double)i*rate/sr);
        const auto b=free.process(1,input,1,{1,.8,.6,0},2,rate);
        aligned&=std::isfinite(a[0])&&std::isfinite(a[1])&&std::abs(a[0]-b[0])<1e-8&&std::abs(a[1]-b[1])<1e-8;
    }
    check(aligned,"999 BPM sixteenth clock matches its free-running rate without hidden clamping");
    alpha::TimePitchDSP slowHost,slowFree;slowHost.prepare(sr,17);slowFree.prepare(sr,1024);bool slowFinite=true;double slowError=0,slowMaximum=0;
    constexpr double slowRate=10.0/60/16;
    for(int i=0;i<768000;++i) {
        const std::array<double,2> input{tone(i,317),tone(i,443)};
        const auto a=slowHost.process(1,input,1,{1,.8,.6,0},2,slowRate,0,(double)i*slowRate/sr);
        const auto b=slowFree.process(1,input,1,{1,.8,.6,0},2,slowRate);
        for(size_t channel=0;channel<2;++channel){const double error=a[channel]-b[channel];slowFinite&=std::isfinite(a[channel]);slowError+=error*error;slowMaximum=std::max(slowMaximum,std::abs(error));}
    }
    const double slowRms=std::sqrt(slowError/(2*768000));
    std::cout<<"10 BPM host/free error RMS "<<slowRms<<", maximum "<<slowMaximum<<'\n';
    // Accumulated free phase can cross an exact four-second capture boundary
    // one sample after absolute PPQ; allow that sampling precision, not drift.
    check(slowFinite&&slowRms<.01&&slowMaximum<.04,"10 BPM four-bar clock respects the four-second slice cap within sample capture precision");
}
void longFeedbackTail() {
    // The lowest supported rate preserves the same physical window/feedback
    // durations while keeping this nine-minute stress render inexpensive.
    constexpr int tailRate=8000,seconds=540,window=5*tailRate;
    for(int mode=0;mode<3;++mode) {
        alpha::TimePitchDSP dsp;dsp.prepare(tailRate,257);
        double middleEnergy=0,lateEnergy=0,endEnergy=0,peak=0;bool finite=true;
        for(int i=0;i<seconds*tailRate;++i) {
            const double input=i<tailRate?.3*std::sin(2*pi*443*i/tailRate):0;
            const auto output=dsp.process(1,{input,input},1,{1,.5,.5,1},mode,1,0,(double)i/tailRate);
            for(double sample:output) {finite&=std::isfinite(sample);peak=std::max(peak,std::abs(sample));}
            const double energy=output[0]*output[0];
            if(i>=35*tailRate&&i<40*tailRate)middleEnergy+=energy;
            if(i>=175*tailRate&&i<180*tailRate)lateEnergy+=energy;
            if(i>=535*tailRate)endEnergy+=energy;
        }
        const double middle=std::sqrt(middleEnergy/window),late=std::sqrt(lateEnergy/window),end=std::sqrt(endEnergy/window);
        const auto decibels=[](double value){return 20*std::log10(std::max(1e-30,value));};
        std::cout<<"Time mode "<<mode<<" max-feedback RMS at35/175/535s "<<decibels(middle)<<" / "<<decibels(late)<<" / "<<decibels(end)<<" dBFS, peak "<<peak<<'\n';
        check(finite&&peak<4,"four-second maximum-feedback tail remains finite and bounded for nine minutes");
        check(middle>1e-4,"maximum-feedback Time remains audible beyond a35-second host tail");
        check(late<middle*.01,"maximum-feedback Time energy decays by at least40dB by180seconds");
        check(end<middle*.001,"maximum-feedback Time has decayed by at least60dB by540seconds");
    }
}
}
int main(){transparent();transposition();silenceStereoAndBounds();blockInvariance();automationAndReverse();boundedTail();modeControlsAndAliasing();repeatAndTimeAutomation();hostClockAndSeek();preRollAndFastClock();longFeedbackTail();std::cout<<failures<<" failures\n";return failures?EXIT_FAILURE:EXIT_SUCCESS;}
