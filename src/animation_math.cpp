#include "dw4/animation_math.hpp"

#include <cmath>
#include <limits>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) { throw AnimationMathError(reason); }

void finite_quaternion(const std::array<float,4>& value) {
    for (const auto component : value)
        if (!std::isfinite(component)) fail("Animation quaternion contains a non-finite component");
}

float matrix_product(float a,float b) {
    const float rounded=a*b;
    const float magnitude=std::abs(rounded),minimum=std::numeric_limits<float>::min();
    if (magnitude<minimum) return std::copysign(0.0f,rounded);
    // A binary32 product is exact in binary64. Gekko NI mode tests tininess
    // before rounding, including products that would round up to min-normal.
    if (magnitude==minimum && std::abs(static_cast<double>(a)*static_cast<double>(b))<minimum)
        return std::copysign(0.0f,rounded);
    return rounded;
}

float matrix_fused_sum(float a,float b,float addend) {
    const float rounded=std::fma(a,b,addend);
    const float magnitude=std::abs(rounded),minimum=std::numeric_limits<float>::min();
    if (magnitude<minimum) return std::copysign(0.0f,rounded);
    if (magnitude!=minimum) return rounded;

    // Only the min-normal boundary needs more than the correctly rounded
    // float FMA. TwoSum retains the exact low part of product + addend even
    // when a tiny product is lost by a double addition at that boundary.
    const double product=static_cast<double>(a)*static_cast<double>(b);
    const double sum=product+static_cast<double>(addend);
    const double virtual_addend=sum-product;
    const double error=(product-(sum-virtual_addend))+
        (static_cast<double>(addend)-virtual_addend);
    if (std::abs(sum)<minimum || (std::abs(sum)==minimum &&
        ((sum>0 && error<0) || (sum<0 && error>0))))
        return std::copysign(0.0f,rounded);
    return rounded;
}

std::array<float,2> polynomial_weights(float dot, float time) {
    const float square = dot*dot, cube = square*dot;
    const float time_square = time*time, time_cube = time*time_square;
    // Recovered bicubic coefficients from the original scalar routine. Keep
    // its operation order and fused multiply/adds; algebraic rearrangement
    // would change the single-precision approximation, including endpoints.
    float a = (0.932855904f*square)*time;
    a = std::fma(-1.67769766f*dot,time,a);
    a = std::fma(-(0.301663756f*cube),time,a);
    a = std::fma(0.0465055406f,time,a);
    a = std::fma(2.26251626f*dot,time_square,a);
    a = std::fma(-(1.13500059f*square),time_square,a);
    a = std::fma(0.355032355f*cube,time_square,a);
    a = std::fma(-1.48254812f,time_square,a);
    a = std::fma(-(0.584818542f*dot),time_cube,a);
    a = std::fma(0.202144668f*square,time_cube,a);
    a = std::fma(-(0.0533686206f*cube),time_cube,a);
    a = std::fma(0.436042517f,time_cube,a);
    a = std::fma(0.0039327261f,dot,a);
    a = std::fma(-0.00403263187f,square,a);
    a = std::fma(0.00146585365f,cube,a);
    a = 0.99863404f+a;

    float b = (0.730710685f*square)*time;
    b = std::fma(-1.09287894f*dot,time,b);
    b = std::fma(-(0.248294786f*cube),time,b);
    b = std::fma(1.61046302f,time,b);
    b = std::fma(0.508060098f*dot,time_square,b);
    b = std::fma(-(0.528565109f*square),time_square,b);
    b = std::fma(0.194925562f*cube,time_square,b);
    b = std::fma(-0.17442055f,time_square,b);
    b = std::fma(0.58481884f*dot,time_cube,b);
    b = std::fma(-(0.202145636f*square),time_cube,b);
    b = std::fma(0.0533692576f*cube,time_cube,b);
    b = std::fma(-0.436042488f,time_cube,b);
    b = std::fma(0.00393273169f,dot,b);
    b = std::fma(-0.0040326342f,square,b);
    b = std::fma(0.00146585039f,cube,b);
    b -= 0.00136594765f;
    return {a,b};
}
} // namespace

std::array<float,4> original_interpolate_rotation(const std::array<float,4>& a,
    const std::array<float,4>& b, float blend) {
    finite_quaternion(a); finite_quaternion(b);
    if (!std::isfinite(blend) || blend < 0 || blend > 1)
        fail("Animation rotation blend must be finite and within [0,1]");
    float dot = a[1]*b[1];
    dot = std::fma(a[0],b[0],dot);
    dot = std::fma(a[2],b[2],dot);
    dot = std::fma(a[3],b[3],dot);
    if (!std::isfinite(dot)) fail("Animation quaternion dot product overflow");
    auto end = b;
    if (dot < 0) {
        dot = -dot;
        for (auto& component : end) component = -component;
    }
    const auto weights = 1-dot > 0.08f ? polynomial_weights(dot,blend) :
        std::array<float,2>{1-blend,blend};
    std::array<float,4> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const float second = weights[1]*end[i];
        result[i] = std::fma(weights[0],a[i],second);
    }
    finite_quaternion(result);
    return result;
}

AnimationMatrix original_matrix_multiply(const AnimationMatrix& left, const AnimationMatrix& right) {
    for (auto value:left) if (!std::isfinite(value)) fail("Animation matrix contains a non-finite component");
    for (auto value:right) if (!std::isfinite(value)) fail("Animation matrix contains a non-finite component");
    AnimationMatrix result{};
    for (std::size_t column=0;column<4;++column) for (std::size_t row=0;row<4;++row) {
        float value=matrix_product(left[row],right[column*4]);
        for (std::size_t k=1;k<4;++k) value=matrix_fused_sum(left[k*4+row],right[column*4+k],value);
        if (!std::isfinite(value)) fail("Animation matrix arithmetic overflow");
        result[column*4+row]=value;
    }
    return result;
}

void original_rotation_matrix(const std::array<float,4>& q, AnimationMatrix& matrix) {
    finite_quaternion(q);
    for (auto value : matrix) if (!std::isfinite(value)) fail("Animation matrix contains a non-finite component");
    const float y2=2*q[1], z2=2*q[2], x2=2*q[0];
    const float yy=y2*q[1], zz=z2*q[2], xx=x2*q[0];
    const float zw=z2*q[3], yx=y2*q[0], yw=y2*q[3], zx=z2*q[0];
    const float xw=x2*q[3], zy=z2*q[1];
    auto result=matrix;
    result[0]=1-(yy+zz); result[1]=yx-zw; result[2]=zx+yw;
    result[4]=yx+zw; result[5]=1-(xx+zz); result[6]=zy-xw;
    result[8]=zx-yw; result[9]=zy+xw; result[10]=1-(xx+yy);
    for (auto value : result) if (!std::isfinite(value)) fail("Animation matrix arithmetic overflow");
    matrix=result;
}
std::array<float,4> original_matrix_rotation(const AnimationMatrix& m) {
    for (auto value : m) if (!std::isfinite(value)) fail("Animation matrix contains a non-finite component");
    const float xy=m[0]+m[5], trace=m[10]+xy;
    std::array<float,4> result{};
    const auto root=[](float value) {
        // The original positive-float frsqrte + three double Newton steps
        // round to this float square root. Nonpositive inputs skip those steps.
        if (!std::isfinite(value)) fail("Animation snapshot trace overflow");
        return value>0 ? std::sqrt(value) : value;
    };
    if (trace>=0) {
        const float length=root(1+trace), scale=0.5f/length;
        result={scale*(m[9]-m[6]),scale*(m[2]-m[8]),scale*(m[4]-m[1]),0.5f*length};
    } else {
        std::size_t axis=m[5]>m[0] ? 1 : 0;
        if (m[10]>m[axis*5]) axis=2;
        if (axis==0) {
            const float length=root(m[15]+(m[0]-(m[5]+m[10]))), scale=0.5f/length;
            result={0.5f*length,scale*(m[1]+m[4]),scale*(m[8]+m[2]),scale*(m[9]-m[6])};
        } else if (axis==1) {
            const float length=root(m[15]+(m[5]-(m[10]+m[0]))), scale=0.5f/length;
            result={scale*(m[1]+m[4]),0.5f*length,scale*(m[6]+m[9]),scale*(m[2]-m[8])};
        } else {
            const float length=root(m[15]+(m[10]-xy)), scale=0.5f/length;
            result={scale*(m[8]+m[2]),scale*(m[6]+m[9]),0.5f*length,scale*(m[4]-m[1])};
        }
    }
    finite_quaternion(result);
    return result;
}
} // namespace dw4
