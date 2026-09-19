#include "dw4/camera_math.hpp"

#include <cmath>
#include <limits>

namespace dw4 {
namespace {
constexpr float minimum=std::numeric_limits<float>::min();
float finite(float value) {
    if(!std::isfinite(value))
        throw AnimationMathError("Camera view inverse exceeds its finite arithmetic profile.");
    return value;
}
float product(float a,float b) {
    finite(a);finite(b);
    const float result=finite(a*b);
    const float magnitude=std::abs(result);
    if(magnitude<minimum || (magnitude==minimum &&
        std::abs(static_cast<double>(a)*b)<minimum))
        return std::copysign(0.0f,result);
    return result;
}
float fused(float a,float b,float addend) {
    finite(a);finite(b);finite(addend);
    const float result=finite(std::fma(a,b,addend));
    const float magnitude=std::abs(result);
    if(magnitude<minimum)return std::copysign(0.0f,result);
    if(magnitude!=minimum)return result;

    // The exact binary32 product fits in double. TwoSum retains the low part
    // when adding the float addend rounds a tiny exact result up to min-normal.
    const double multiplied=static_cast<double>(a)*b;
    const double added=multiplied+addend;
    const double virtual_addend=added-multiplied;
    const double error=(multiplied-(added-virtual_addend))+
        (static_cast<double>(addend)-virtual_addend);
    if(std::abs(added)<minimum || (std::abs(added)==minimum &&
        ((added>0 && error<0) || (added<0 && error>0))))
        return std::copysign(0.0f,result);
    return result;
}
}

void original_camera_view_inverse(std::span<const float,16> source,
    std::span<float,16> destination) {
    // These captures intentionally do not snapshot the matrix. The original
    // stores can change later source reads when the ranges overlap.
    float first=source[0];
    float second=source[4];
    destination[0]=first;
    first=source[8];
    destination[1]=second;
    second=source[1];
    destination[2]=first;
    first=source[5];
    destination[4]=second;
    second=source[9];
    destination[5]=first;
    first=source[2];
    destination[6]=second;
    second=source[6];
    destination[8]=first;
    first=source[10];
    destination[9]=second;
    destination[10]=first;

    destination[11]=0.0f;
    const float y=source[13];
    destination[7]=0.0f;
    const float x=source[12];
    destination[3]=0.0f;
    const float z=source[14];
    destination[15]=1.0f;

    for(std::size_t row=0;row<3;++row) {
        const float rotation_y=destination[4+row];
        const float rotation_x=destination[row];
        float value=product(y,rotation_y);
        const float rotation_z=destination[8+row];
        value=fused(x,rotation_x,value);
        // FNMADDS negates after the rounded/NI-flushed fused operation.
        // Moving the sign into its operands changes exact-zero signs.
        value=-fused(z,rotation_z,value);
        destination[12+row]=value;
    }
}
} // namespace dw4
