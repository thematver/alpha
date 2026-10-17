#pragma once
#include "AlphaDSP.h"
#include <algorithm>
#include <cmath>

namespace alpha {
// Shape-preserving cubic Hermite interpolation (Fritsch-Carlson/PCHIP).
// https://epubs.siam.org/doi/10.1137/0717021
// End slopes are zero for a smooth repeating pump. Snapshots sort points once
// per block; duplicate abscissae are skipped without division by zero.
inline double evaluateMotionCurve(const std::array<CurvePoint,curveCapacity>& points,int count,double phase) noexcept {
    count=std::clamp(count,0,curveCapacity);if(count==0)return 0;
    const auto y=[&](int i){const auto value=points[static_cast<size_t>(i)].y;return std::isfinite(value)?std::clamp(static_cast<double>(value),-1.0,1.0):0;};
    const auto x=[&](int i){const auto value=points[static_cast<size_t>(i)].x;return std::isfinite(value)?std::clamp(static_cast<double>(value),0.0,1.0):0;};
    phase=std::isfinite(phase)?std::clamp(phase,0.0,1.0):0;
    if(count==1||phase<=x(0))return y(0);if(phase>=x(count-1))return y(count-1);
    const auto tangent=[&](int i){
        if(i<=0||i>=count-1)return 0.0;
        const auto before=x(i)-x(i-1),after=x(i+1)-x(i);
        if(before<1e-6||after<1e-6)return 0.0;
        const auto previous=(y(i)-y(i-1))/before,next=(y(i+1)-y(i))/after;
        if(previous*next<=0)return 0.0;
        const auto w1=2*after+before,w2=after+2*before;
        return (w1+w2)/(w1/previous+w2/next);
    };
    for(int i=0;i<count-1;++i){
        const auto width=x(i+1)-x(i);if(width<1e-6||phase>x(i+1))continue;
        auto t=std::clamp((phase-x(i))/width,0.0,1.0);
        const auto bend=points[static_cast<size_t>(i)].curve;
        if(std::isfinite(bend)&&std::abs(bend)>1e-6)t=std::pow(t,std::exp2(std::clamp(static_cast<double>(bend),-1.0,1.0)*3));
        const auto t2=t*t,t3=t2*t;
        const auto value=(2*t3-3*t2+1)*y(i)+(t3-2*t2+t)*width*tangent(i)+(-2*t3+3*t2)*y(i+1)+(t3-t2)*width*tangent(i+1);
        return std::clamp(value,std::min(y(i),y(i+1)),std::max(y(i),y(i+1)));
    }
    return y(count-1);
}
}
