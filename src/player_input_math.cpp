#include "dw4/player_input_math.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dw4 {
namespace {
float finite(float value) {
    if(!std::isfinite(value))
        throw AnimationMathError("Player input heading requires finite reached arithmetic.");
    return value;
}
float narrow(double value) {
    if(!std::isfinite(value))
        throw AnimationMathError("Player input heading exceeds its finite arithmetic profile.");
    // FRSP and single arithmetic flush an exact tiny result before rounding,
    // including a value that would otherwise round up to the minimum normal.
    if(std::abs(value)<std::numeric_limits<float>::min())
        return std::signbit(value) ? -0.0f:0.0f;
    return finite(static_cast<float>(value));
}
std::uint32_t high_magnitude(double value) {
    return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(value)>>32)&0x7fffffffu;
}

// Finite-input reconstruction of MSL atan 800A21F8. Range reduction and
// polynomial operations follow the original binary64 rounding/FMA boundaries.
// The literals are the numerical coefficients, not copied instructions.
double arctangent(double value) {
    constexpr double high[]{0x1.dac670561bb4fp-2,0x1.921fb54442d18p-1,
        0x1.f730bd281f69bp-1,0x1.921fb54442d18p+0};
    constexpr double low[]{0x1.a2b7f222f65e2p-56,0x1.1a62633145c07p-55,
        0x1.007887af0cbbdp-56,0x1.1a62633145c07p-54};
    constexpr double coefficient[]{
        0x1.555555555550dp-2,-0x1.999999998ebc4p-3,
        0x1.24924920083ffp-3,-0x1.c71c6fe231671p-4,
        0x1.745cdc54c206ep-4,-0x1.3b0f2af749a6dp-4,
        0x1.10d66a0d03d51p-4,-0x1.dde2d52defd9ap-5,
        0x1.97b4b24760debp-5,-0x1.2b4442c6a6c2fp-5,
        0x1.0ad3ae322da11p-6};
    const auto magnitude=high_magnitude(value);
    if(magnitude>=0x44100000u) {
        if(std::signbit(value))return -high[3]-low[3];
        return high[3]+low[3];
    }
    int region=-1;
    double reduced=value;
    if(magnitude<0x3fdc0000u) {
        // The original huge+value comparison returns value for finite inputs
        // in this range. Its inexact exception flag is outside this profile.
        if(magnitude<0x3e200000u)return value;
    }else {
        const double absolute=std::abs(value);
        if(magnitude<0x3ff30000u) {
            if(magnitude<0x3fe60000u) {
                region=0;
                const double denominator=2.0+absolute;
                const double numerator=std::fma(2.0,absolute,-1.0);
                reduced=numerator/denominator;
            }else {
                region=1;
                const double numerator=absolute-1.0;
                const double denominator=1.0+absolute;
                reduced=numerator/denominator;
            }
        }else if(magnitude<0x40038000u) {
            region=2;
            const double numerator=absolute-1.5;
            const double denominator=std::fma(1.5,absolute,1.0);
            reduced=numerator/denominator;
        }else {
            region=3;reduced=-1.0/absolute;
        }
    }
    const double squared=reduced*reduced;
    const double fourth=squared*squared;
    double even=std::fma(fourth,coefficient[10],coefficient[8]);
    double odd=std::fma(fourth,coefficient[9],coefficient[7]);
    even=std::fma(fourth,even,coefficient[6]);
    odd=std::fma(fourth,odd,coefficient[5]);
    even=std::fma(fourth,even,coefficient[4]);
    odd=std::fma(fourth,odd,coefficient[3]);
    even=std::fma(fourth,even,coefficient[2]);
    odd=std::fma(fourth,odd,coefficient[1]);
    even=std::fma(fourth,even,coefficient[0]);
    const double odd_term=fourth*odd;
    const double even_term=squared*even;
    const double combined=even_term+odd_term;
    if(region<0)return -std::fma(reduced,combined,-reduced);
    const double correction=std::fma(reduced,combined,-low[region]);
    const double residual=correction-reduced;
    const double result=high[region]-residual;
    return std::signbit(value) ? -result:result;
}

// 800A2C74 -> 8009F8B0. Both inputs here are finite binary32 values promoted
// exactly to binary64. Their quotient cannot underflow/overflow binary64.
double angle(float y,float x) {
    constexpr double pi=0x1.921fb54442d18p+1;
    constexpr double half_pi=0x1.921fb54442d18p+0;
    constexpr double pi_tail=0x1.1a62633145c07p-53;
    finite(y);finite(x);
    if(x==1.0f)return arctangent(y);
    const bool negative_x=std::signbit(x),negative_y=std::signbit(y);
    if(y==0.0f) {
        if(!negative_x)return y;
        return negative_y ? -pi:pi;
    }
    if(x==0.0f)return negative_y ? -half_pi:half_pi;
    const auto exponent_distance=(static_cast<std::int32_t>(high_magnitude(y))-
        static_cast<std::int32_t>(high_magnitude(x)))>>20;
    double acute;
    if(exponent_distance>60)acute=half_pi;
    else if(negative_x && exponent_distance<-60)acute=0.0;
    else acute=arctangent(std::abs(static_cast<double>(y)/x));
    if(!negative_x)return negative_y ? -acute:acute;
    const double corrected=acute-pi_tail;
    return negative_y ? corrected-pi:pi-corrected;
}
}

float original_player_input_heading(const AnimationMatrix& inverse_camera,
    float left_x,float left_y) {
    // 8012D434 uses separate Y products, then fused X and Z terms. The
    // source vector is exactly (0,0,1); binary64 represents each exact fused
    // expression here, including a subnormal matrix value plus signed zero.
    float forward[3];
    for(std::size_t axis=0;axis<3;++axis) {
        const float y=narrow(0.0*finite(inverse_camera[4+axis]));
        const float xy=narrow(std::fma(0.0,static_cast<double>(finite(inverse_camera[axis])),static_cast<double>(y)));
        forward[axis]=narrow(std::fma(1.0,static_cast<double>(finite(inverse_camera[8+axis])),static_cast<double>(xy)));
    }
    const float camera_heading=narrow(angle(forward[0],forward[2]));
    const float pad_heading=narrow(angle(left_x,-left_y));
    return narrow(static_cast<double>(camera_heading)+pad_heading);
}
} // namespace dw4
