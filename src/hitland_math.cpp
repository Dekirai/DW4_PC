#include "dw4/hitland_math.hpp"

#include <cmath>
#include <limits>

namespace dw4 {
namespace {
constexpr float minimum=std::numeric_limits<float>::min();
[[noreturn]] void fail() {throw CollisionMathError("Original HITLAND wall direction exceeds its finite nonzero arithmetic profile.");}
float difference(float a,float b) {
    if(!std::isfinite(a) || !std::isfinite(b))fail();
    const float result=a-b;
    return std::abs(result)<minimum ? std::copysign(0.0f,result) : result;
}
float product(float a,float b) {
    const float result=a*b;
    if(std::abs(result)<minimum || (std::abs(result)==minimum &&
        std::abs(static_cast<double>(a)*static_cast<double>(b))<minimum))
        return std::copysign(0.0f,result);
    return result;
}
float fused(float a,float b,float addend) {
    const float result=std::fma(a,b,addend);
    const float magnitude=std::abs(result);
    if(magnitude<minimum)return std::copysign(0.0f,result);
    if(magnitude!=minimum)return result;
    // The product is exact in binary64. TwoSum retains a tiny deficit that
    // a plain double sum can lose at the pre-round minimum-normal boundary.
    const double multiplied=static_cast<double>(a)*static_cast<double>(b);
    const double sum=multiplied+static_cast<double>(addend);
    const double virtual_addend=sum-multiplied;
    const double error=(multiplied-(sum-virtual_addend))+(static_cast<double>(addend)-virtual_addend);
    if(std::abs(sum)<minimum || (std::abs(sum)==minimum &&
        ((sum>0 && error<0) || (sum<0 && error>0))))return std::copysign(0.0f,result);
    return result;
}
}
std::array<float,3> original_hitland_wall_direction(
    const std::array<float,3>& current,const std::array<float,3>& previous) {
    const float x=difference(current[0],previous[0]);
    const float z=difference(current[2],previous[2]);
    const float y=0;
    const float squared=fused(z,z,fused(x,x,product(y,y)));
    if(!(squared>0) || !std::isfinite(squared))fail();
    // The positive binary32 length path uses frsqrte and three double Newton
    // corrections before frsp. Its rounded length agrees with binary32 sqrt
    // in the independently executed bounded original-instruction corpus.
    const float length=std::sqrt(squared);
    const float reciprocal=1.0f/length;
    const std::array<float,3> result{product(x,reciprocal),product(y,reciprocal),product(z,reciprocal)};
    for(const float value:result)if(!std::isfinite(value))fail();
    return result;
}
bool original_hitland_xz_overlap(const CollisionBox& model,const CollisionBox& query) {
    if(!(model.maximum[0]>=query.minimum[0]))return false;
    if(!(model.minimum[0]<=query.maximum[0]))return false;
    if(!(model.maximum[2]>=query.minimum[2]))return false;
    if(!(model.minimum[2]<=query.maximum[2]))return false;
    return true;
}
} // namespace dw4
