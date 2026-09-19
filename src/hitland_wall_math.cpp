#include "dw4/hitland_wall_math.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
constexpr float minimum=std::numeric_limits<float>::min();
[[noreturn]] void fail(const char* reason) {throw CollisionMathError(reason);}
float finite(float value) {
    if(!std::isfinite(value))fail("Original CHECKWALL exceeds its finite arithmetic profile.");
    return value;
}
float flushed(float rounded,double exact) {
    finite(rounded);
    if(std::abs(rounded)<minimum ||
        (std::abs(rounded)==minimum && std::abs(exact)<minimum))
        return std::copysign(0.0f,rounded);
    return rounded;
}
float product(float a,float b) {
    finite(a);finite(b);return flushed(a*b,static_cast<double>(a)*b);
}
float sum(float a,float b) {
    finite(a);finite(b);return flushed(a+b,static_cast<double>(a)+b);
}
float difference(float a,float b) {
    finite(a);finite(b);return flushed(a-b,static_cast<double>(a)-b);
}
float division(float a,float b) {
    finite(a);finite(b);
    if(b==0.0f)fail("Original CHECKWALL reaches a zero arithmetic divisor.");
    return flushed(a/b,static_cast<double>(a)/b);
}
float fused(float a,float b,float c) {
    finite(a);finite(b);finite(c);
    const float rounded=finite(std::fma(a,b,c));
    const auto magnitude=std::abs(rounded);
    if(magnitude<minimum)return std::copysign(0.0f,rounded);
    if(magnitude!=minimum)return rounded;
    // Exact binary32 product plus TwoSum residue retains a deficit that an
    // ordinary binary64 sum can lose at the pre-round minimum-normal boundary.
    const double multiplied=static_cast<double>(a)*b;
    const double added=multiplied+c;
    const double virtual_addend=added-multiplied;
    const double error=(multiplied-(added-virtual_addend))+(c-virtual_addend);
    if(std::abs(added)<minimum || (std::abs(added)==minimum &&
        ((added>0 && error<0) || (added<0 && error>0))))
        return std::copysign(0.0f,rounded);
    return rounded;
}
float squared_length(const std::array<float,3>& value) {
    const float xx=product(value[0],value[0]);
    const float yy=product(value[1],value[1]);
    const float zz=product(value[2],value[2]);
    return sum(zz,sum(xx,yy));
}
float positive_length(float squared) {
    if(!(finite(squared)>0.0f))
        fail("Original CHECKWALL requires a positive represented squared length.");
    // Original frsqrte/three binary64 Newton steps narrow to binary32. The
    // independent instruction sweep checks every significand of both exponent
    // parities and both range endpoints (33,554,432 cases). Power-of-four
    // scaling preserves that sequence across positive normal binary32 inputs;
    // all rounded results agree with sqrt. No nonfinite fallback is claimed.
    return std::sqrt(squared);
}
float lateral_cosine(float ratio) {
    // 802FF608..654 calls acos, rounds its result to binary32, then calls
    // cos and rounds again. The bounded inputs below were executed through
    // original MSL acos 800A2C34/8009F43C, its integer square root, and cos
    // 800A257C using independently rounded binary64 original instructions.
    // Each of these 17 inputs returns its input bits after BOTH narrowings.
    // This is a verified finite lookup, not an algebraic cos(acos(x)) rewrite
    // or a claim that host libm matches the original for arbitrary arguments.
    ratio=finite(ratio);
    if(ratio>1.0f)ratio=1.0f;
    else if(ratio<-1.0f)ratio=-1.0f;
    const auto bits=std::bit_cast<std::uint32_t>(ratio);
    if(bits>=0x3f7ffff0u && bits<=0x3f800000u)return ratio;
    fail("Original CHECKWALL reached an unverified rounded acos/cos input.");
}
}

AnimationMatrix original_hitland_wall_rotation() {
    // The reset angle is 0x3FC90FDB, not exact pi/2. Its original rounded
    // sine is 1 and cosine is 0xB33BBD2E. Preserve the Euler product zeros.
    constexpr float cosine=std::bit_cast<float>(std::uint32_t{0xb33bbd2e});
    return {cosine,-0.0f,-1.0f,0.0f,
        0.0f,1.0f,-0.0f,0.0f,
        1.0f,0.0f,cosine,0.0f,
        0.0f,0.0f,0.0f,1.0f};
}
float original_hitland_wall_radius(float width) {
    return product(std::bit_cast<float>(std::uint32_t{0x3fb5c28f}),width);
}
float original_hitland_wall_half_radius(float radius) {return product(radius,0.5f);}
float original_hitland_wall_twice_radius(float radius) {return sum(radius,radius);}
float original_hitland_wall_forward_offset(float radius) {return product(product(3.0f,radius),0.25f);}
float original_hitland_wall_narrow(float value) {return flushed(value,value);}

std::array<float,3> original_hitland_wall_height(const std::array<float,3>& position,float down) {
    return {position[0],sum(down,position[1]),position[2]};
}
std::array<float,3> original_hitland_wall_side(const std::array<float,3>& direction,
    const AnimationMatrix& rotation) {
    // 8012D434: rounded Y terms, fused X terms, fused Z terms. Translation,
    // homogeneous fields and unused source data are never validated.
    const float y=direction[1];
    float x=product(y,rotation[4]);
    const float source_x=direction[0];
    float transformed_y=product(y,rotation[5]);
    x=fused(source_x,rotation[0],x);
    const float z=direction[2];
    float transformed_z=product(y,rotation[6]);
    transformed_y=fused(source_x,rotation[1],transformed_y);
    x=fused(z,rotation[8],x);
    transformed_z=fused(source_x,rotation[2],transformed_z);
    transformed_y=fused(z,rotation[9],transformed_y);
    transformed_z=fused(z,rotation[10],transformed_z);
    const std::array<float,3> transformed{x,transformed_y,transformed_z};
    const float reciprocal=division(1.0f,positive_length(squared_length(transformed)));
    return {product(x,reciprocal),product(transformed_y,reciprocal),product(transformed_z,reciprocal)};
}
std::array<float,3> original_hitland_wall_offset(const std::array<float,3>& base,
    float scale,const std::array<float,3>& direction) {
    const float x=fused(scale,direction[0],0.0f);
    const float y=fused(scale,direction[1],0.0f);
    const float z=fused(scale,direction[2],0.0f);
    return {sum(base[0],x),sum(base[1],y),sum(base[2],z)};
}
std::array<float,3> original_hitland_wall_fused_offset(const std::array<float,3>& base,
    float scale,const std::array<float,3>& direction) {
    return {fused(scale,direction[0],base[0]),fused(scale,direction[1],base[1]),
        fused(scale,direction[2],base[2])};
}
std::array<float,3> original_hitland_wall_longitudinal(const std::array<float,3>& position,
    float doubled_radius,float distance,const std::array<float,3>& direction) {
    const float scale=-difference(doubled_radius,distance);
    return original_hitland_wall_fused_offset(position,scale,direction);
}
std::array<float,3> original_hitland_wall_lateral(const std::array<float,3>& position,
    float radius,float distance,const std::array<float,3>& normal) {
    const float squared=squared_length(normal);
    const float first=positive_length(squared);
    const float second=positive_length(squared_length(normal));
    const float ratio=division(squared,product(first,second));
    const float cosine=lateral_cosine(ratio);
    const float scale=product(difference(radius,distance),cosine);
    return original_hitland_wall_fused_offset(position,scale,normal);
}
void original_hitland_wall_extremum(float& destination,float source,std::uint32_t mode) {
    if(destination<source) {if(mode==0)destination=source;}
    else if(destination>source) {if(mode==1)destination=source;}
}
} // namespace dw4
