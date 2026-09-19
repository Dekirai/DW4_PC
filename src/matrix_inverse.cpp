#include "dw4/matrix_inverse.hpp"

#include <cmath>
#include <limits>

namespace dw4 {
namespace {
constexpr float minimum = std::numeric_limits<float>::min();

float finite(float value) {
    if (!std::isfinite(value))
        throw AnimationMathError("Original matrix inverse exceeds its finite arithmetic profile");
    return value;
}

float product(float a, float b) {
    const float rounded = finite(a * b);
    const float magnitude = std::abs(rounded);
    // Binary32 products are exact in binary64. The original NI profile tests
    // tininess before rounding, including results that round up to min-normal.
    if (magnitude < minimum || (magnitude == minimum &&
        std::abs(static_cast<double>(a) * b) < minimum))
        return std::copysign(0.f, rounded);
    return rounded;
}

float fused(float a, float b, float addend) {
    const float rounded = finite(std::fma(a, b, addend));
    const float magnitude = std::abs(rounded);
    if (magnitude < minimum) return std::copysign(0.f, rounded);
    if (magnitude != minimum) return rounded;

    // Retain the low part when an exact binary32 product plus addend lies
    // just below min-normal but a binary64 addition rounds to that boundary.
    const double multiplied = static_cast<double>(a) * b;
    const double added = multiplied + addend;
    const double virtual_addend = added - multiplied;
    const double error = (multiplied - (added - virtual_addend)) +
        (static_cast<double>(addend) - virtual_addend);
    if (std::abs(added) < minimum || (std::abs(added) == minimum &&
        ((added > 0 && error < 0) || (added < 0 && error > 0))))
        return std::copysign(0.f, rounded);
    return rounded;
}

float fused_difference(float a, float b, float subtrahend) {
    return fused(a, b, -subtrahend);
}

float reciprocal(float determinant) {
    const float rounded = finite(1.f / determinant);
    if (std::abs(rounded) < minimum || (std::abs(rounded) == minimum &&
        std::abs(1. / static_cast<double>(determinant)) < minimum))
        return std::copysign(0.f, rounded);
    return rounded;
}

// The names identify component pairs in two consecutive four-float vectors.
// Treating the flat column-major matrix this way retains the original memory
// layout without changing multiplication convention or transposing at entry.
struct Minors {
    float xy, xz, xw, yz, yw, zw;
};

Minors minors(const float* upper, const float* lower) {
    const auto minor = [&](std::size_t first, std::size_t second) {
        return fused_difference(upper[first], lower[second],
            product(lower[first], upper[second]));
    };
    return {minor(0, 1), minor(0, 2), minor(0, 3),
        minor(1, 2), minor(1, 3), minor(2, 3)};
}

std::array<float, 4> cofactors(const float* vector, const Minors& minor) {
    // Each three-term determinant rounds the subtracted product first, then
    // performs one fused subtraction and one fused addition. Do not replace
    // this with separately rounded products, a generic determinant, or LU.
    return {
        fused(vector[3], minor.yz, fused_difference(vector[1], minor.zw,
            product(vector[2], minor.yw))),
        -fused(vector[3], minor.xz, fused_difference(vector[0], minor.zw,
            product(vector[2], minor.xw))),
        fused(vector[3], minor.xy, fused_difference(vector[0], minor.yw,
            product(vector[1], minor.xw))),
        -fused(vector[2], minor.xy, fused_difference(vector[0], minor.yz,
            product(vector[1], minor.xz)))
    };
}
} // namespace

bool original_matrix_inverse(const AnimationMatrix& source, AnimationMatrix& destination) {
    for (float value : source) finite(value);

    // 8012AA04 writes only its local adjugate, never the caller's destination.
    // Capture every source-dependent value before publishing, including when
    // source and destination are the same object.
    const auto bottom = minors(source.data() + 8, source.data() + 12);
    const auto first = cofactors(source.data() + 4, bottom);
    const auto second = cofactors(source.data(), bottom);
    const auto third = cofactors(source.data(), minors(source.data() + 4, source.data() + 12));
    const auto fourth = cofactors(source.data(), minors(source.data() + 4, source.data() + 8));
    AnimationMatrix adjugate{};
    for (std::size_t i = 0; i < 4; ++i) {
        adjugate[4 * i] = first[i];
        adjugate[4 * i + 1] = -second[i];
        adjugate[4 * i + 2] = third[i];
        adjugate[4 * i + 3] = -fourth[i];
    }

    // 8012AD50 repeats these same cofactor expressions. The last instruction
    // is fnmsubs: negate AFTER rounding the fused subtraction, including zero.
    float determinant = fused_difference(source[0], first[0], product(source[1], -first[1]));
    determinant = fused(source[2], first[2], determinant);
    determinant = -fused_difference(source[3], -first[3], determinant);
    if (std::abs(determinant) < minimum) return false;

    const float scale = reciprocal(determinant);
    for (std::size_t i = 0; i < destination.size(); ++i)
        destination[i] = product(scale, adjugate[i]);
    return true;
}

} // namespace dw4
