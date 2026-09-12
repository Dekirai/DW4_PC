#include "dw4/hitland_geometry.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw HitLandQueryError(reason);}
constexpr float minimum=std::numeric_limits<float>::min();
float finite(float value) {
    if(!std::isfinite(value))fail("Original HITLAND geometry exceeds its finite arithmetic profile.");
    return value;
}
float flushed(float value,double exact) {
    finite(value);
    return std::abs(value)<minimum || (std::abs(value)==minimum && std::abs(exact)<minimum)
        ? std::copysign(0.f,value):value;
}
float product(float a,float b) {return flushed(a*b,static_cast<double>(a)*b);}
float sum(float a,float b) {return flushed(a+b,static_cast<double>(a)+b);}
float difference(float a,float b) {return flushed(a-b,static_cast<double>(a)-b);}
float divide(float a,float b) {return flushed(a/b,static_cast<double>(a)/b);}
float fused(float a,float b,float c) {
    const float value=finite(std::fma(a,b,c));
    if(std::abs(value)<minimum)return std::copysign(0.f,value);
    if(std::abs(value)!=minimum)return value;
    const double multiplied=static_cast<double>(a)*b,added=multiplied+c,virtual_addend=added-multiplied;
    const double error=(multiplied-(added-virtual_addend))+(static_cast<double>(c)-virtual_addend);
    if(std::abs(added)<minimum || (std::abs(added)==minimum && ((added>0 && error<0) || (added<0 && error>0))))
        return std::copysign(0.f,value);
    return value;
}
}

void original_hitland_point(std::array<float,3>& output,const std::array<float,3>& input,
    const AnimationMatrix& matrix) {
    const float y=input[1],x=input[0];
    float rx=product(y,matrix[4]),ry=product(y,matrix[5]),w=product(y,matrix[7]);
    rx=fused(x,matrix[0],rx);
    float rz=product(y,matrix[6]);
    ry=fused(x,matrix[1],ry);w=fused(x,matrix[3],w);
    const float z=input[2];rz=fused(x,matrix[2],rz);
    rx=fused(z,matrix[8],rx);ry=fused(z,matrix[9],ry);
    w=fused(z,matrix[11],w);rz=fused(z,matrix[10],rz);
    rx=sum(matrix[12],rx);ry=sum(matrix[13],ry);
    w=sum(matrix[15],w);
    const float translation_z=matrix[14];
    output[0]=rx;rz=sum(translation_z,rz);output[1]=ry;output[2]=rz;
    if(w!=1.f) {
        constexpr float epsilon=std::bit_cast<float>(std::uint32_t{0x350637bd});
        if(std::abs(w)<epsilon)w=epsilon;
        const float reciprocal=divide(1.f,w);
        output[0]=product(output[0],reciprocal);
        output[1]=product(output[1],reciprocal);
        output[2]=product(output[2],reciprocal);
    }
}
float original_hitland_distance(const std::array<float,3>& origin,const std::array<float,3>& position) {
    const float y=difference(origin[1],position[1]),x=difference(origin[0],position[0]);
    const float yy=product(y,y),z=difference(origin[2],position[2]);
    const float squared=fused(z,z,fused(x,x,yy));
    return squared>0 ? std::sqrt(squared):squared;
}
void original_hitland_transform_segment(HitLandSegment& value,const AnimationMatrix& matrix) {
    const float length=value.length;
    std::array<float,3> endpoint{};
    endpoint[0]=fused(length,value.direction[0],value.origin[0]);
    endpoint[1]=fused(length,value.direction[1],value.origin[1]);
    endpoint[2]=fused(length,value.direction[2],value.origin[2]);
    original_hitland_point(value.origin,value.origin,matrix);
    original_hitland_point(endpoint,endpoint,matrix);
    value.direction[0]=difference(endpoint[0],value.origin[0]);
    value.direction[1]=difference(endpoint[1],value.origin[1]);
    value.direction[2]=difference(endpoint[2],value.origin[2]);
    const float xx=product(value.direction[0],value.direction[0]);
    const float yy=product(value.direction[1],value.direction[1]);
    const float zz=product(value.direction[2],value.direction[2]);
    const float squared=sum(zz,sum(xx,yy));
    const float transformed_length=squared>0 ? std::sqrt(squared):squared;
    const float reciprocal=divide(1.f,transformed_length);
    value.direction[0]=product(value.direction[0],reciprocal);
    value.direction[1]=product(value.direction[1],reciprocal);
    value.direction[2]=product(value.direction[2],reciprocal);
    value.length=transformed_length;
}

} // namespace dw4
