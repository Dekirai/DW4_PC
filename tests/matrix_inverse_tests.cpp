#include "dw4/matrix_inverse.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks{};
void check(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

bool same_words(const dw4::AnimationMatrix& a, const dw4::AnimationMatrix& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) return false;
    return true;
}

dw4::AnimationMatrix identity() {
    dw4::AnimationMatrix result{};
    result[0] = result[5] = result[10] = result[15] = 1.f;
    return result;
}

void expect_inverse(const dw4::AnimationMatrix& source, const dw4::AnimationMatrix& expected) {
    const auto saved = source;
    auto actual = identity();
    check(dw4::original_matrix_inverse(source, actual), "Invertible matrix must succeed");
    check(actual == expected, "Exactly representable inverse components");
    check(same_words(source, saved), "Out-of-place inverse preserves source words");
    auto aliased = source;
    check(dw4::original_matrix_inverse(aliased, aliased), "In-place inverse must succeed");
    check(same_words(actual, aliased), "In-place inverse retains all result words and zero signs");
}

template<class F> void rejects(F&& operation) {
    bool rejected = false;
    try { operation(); }
    catch (const dw4::AnimationMathError&) { rejected = true; }
    check(rejected, "Unsupported non-finite arithmetic must report AnimationMathError");
}

void ordinary_matrices() {
    expect_inverse(identity(), identity());
    // Scale, shear, reflection and translation with an exactly representable
    // inverse; all sixteen components participate in the same general path.
    expect_inverse({2, 0, 0, 0, 1, 4, 0, 0, 0, 2, -8, 0, 6, -4, 16, 1},
        {0.5f, 0, 0, 0, -0.125f, 0.25f, 0, 0, -0.03125f, 0.0625f, -0.125f, 0, -3, 0, 2, 1});
    // This is not an affine 3x4 transform. Its last row/column exchange axes
    // and the homogeneous component; an affine shortcut gives a wrong result.
    expect_inverse({0, 1, 0, 0, 0, 0, -4, 0, 0, 0, 0, 8, 2, 0, 0, 0},
        {0, 0, 0, 0.5f, 1, 0, 0, 0, 0, -0.25f, 0, 0, 0, 0, 0.125f, 0});
}

void singular_and_threshold() {
    auto destination = identity();
    destination[2] = std::bit_cast<float>(std::uint32_t{0x7fc00123});
    destination[7] = -0.f;
    const auto saved = destination;
    check(!dw4::original_matrix_inverse({}, destination), "Zero matrix is singular");
    check(same_words(destination, saved), "Singular inverse does not read or write destination");
    auto duplicate = identity();
    for (std::size_t i = 0; i < 4; ++i) duplicate[4 + i] = duplicate[i];
    auto aliased = duplicate;
    check(!dw4::original_matrix_inverse(aliased, aliased), "Repeated columns are singular in place");
    check(same_words(aliased, duplicate), "Singular in-place source remains untouched");

    constexpr float minimum = std::numeric_limits<float>::min();
    for (float sign : {1.f, -1.f}) {
        auto boundary = identity();
        boundary[0] = sign * minimum;
        auto expected = identity();
        expected[0] = sign * 0x1p126f;
        expect_inverse(boundary, expected);

        boundary[0] = std::copysign(std::nextafter(minimum, 0.f), sign);
        destination = saved;
        check(!dw4::original_matrix_inverse(boundary, destination), "Below min-normal determinant fails");
        check(same_words(destination, saved), "Threshold failure preserves arbitrary destination words");
    }

    // The original reciprocal itself flushes to zero in NI mode. The routine
    // still reports success, so do not silently replace it with division of
    // each cofactor or a higher-precision inverse.
    auto large = identity();
    large[0] = std::numeric_limits<float>::max();
    destination = saved;
    check(dw4::original_matrix_inverse(large, destination), "Finite large determinant still succeeds");
    for (float value : destination) check(value == 0.f, "Reciprocal underflow reaches every output component");
}

void unsupported_profile() {
    const auto initial = identity();
    auto destination = initial;
    auto bad = initial;
    bad[7] = std::numeric_limits<float>::infinity();
    rejects([&] { dw4::original_matrix_inverse(bad, destination); });
    check(same_words(destination, initial), "Non-finite source leaves destination unchanged");
    bad[7] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { dw4::original_matrix_inverse(bad, destination); });
    check(same_words(destination, initial), "NaN source leaves destination unchanged");
    bad = initial;
    bad[0] = bad[5] = 0x1p100f;
    rejects([&] { dw4::original_matrix_inverse(bad, destination); });
    check(same_words(destination, initial), "Determinant overflow precedes destination writes");
}

void original_instruction_fixtures() {
    // Synthetic inputs evaluated independently through the original USA DOL
    // instructions, using exact integer rounding under startup NI=1/RN=00.
    // The general matrix exercises every cofactor and non-affine component;
    // expected words retain the original FMA and reciprocal rounding.
    const dw4::AnimationMatrix source{3, 1, -2, 1, 4, 5, 1, -1, 2, -3, 6, 2, -4, 1, 3, 7};
    constexpr std::array<std::uint32_t, 16> expected{
        0x3e3f737d, 0x3cafa4ab, 0x3da89e14, 0xbd413522,
        0xbda1977e, 0x3e1fd5d9, 0xbda51ac9, 0x3d67d95c,
        0xbe289e14, 0x3db16650, 0x3dbdb1d7, 0x3c1a90e8,
        0x3e413522, 0xbd413522, 0x3c9a90e8, 0x3dd4873f
    };
    auto destination = identity();
    check(dw4::original_matrix_inverse(source, destination), "Original general inverse fixture succeeds");
    for (std::size_t i = 0; i < destination.size(); ++i)
        check(std::bit_cast<std::uint32_t>(destination[i]) == expected[i], "Original general inverse result word");

    // Output element 14 overflows after fourteen completed stores. Earlier
    // results include negative zeros; element 15 retains its previous value.
    auto overflowing = identity();
    overflowing[10] = 0.5f;
    overflowing[14] = std::numeric_limits<float>::max();
    constexpr std::array<std::uint32_t, 16> prefix{
        0x3f800000, 0x80000000, 0, 0x80000000,
        0x80000000, 0x3f800000, 0x80000000, 0,
        0, 0x80000000, 0x40000000, 0x80000000,
        0x80000000, 0, 0x42e40000, 0x42e60000
    };
    for (std::size_t i = 0; i < destination.size(); ++i) destination[i] = 100.f + static_cast<float>(i);
    rejects([&] { dw4::original_matrix_inverse(overflowing, destination); });
    for (std::size_t i = 0; i < destination.size(); ++i)
        check(std::bit_cast<std::uint32_t>(destination[i]) == prefix[i], "Original partial destination store prefix");
}
} // namespace

int main() {
    try {
        ordinary_matrices();
        singular_and_threshold();
        unsupported_profile();
        original_instruction_fixtures();
        std::cout << checks << " matrix inverse checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Matrix inverse test failed: " << error.what() << '\n';
        return 1;
    }
}
