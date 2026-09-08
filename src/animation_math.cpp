#include "dw4/animation_math.hpp"

#include <cmath>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) { throw AnimationMathError(reason); }

void finite_quaternion(const std::array<float,4>& value) {
    for (const auto component : value)
        if (!std::isfinite(component)) fail("Animation quaternion contains a non-finite component");
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
} // namespace dw4
