#include "dw4/animation_math.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Quaternion = std::array<float,4>;
int checks{};
void check(bool condition, const char* reason) {
    ++checks; if (!condition) throw std::runtime_error(reason);
}
void expected(const Quaternion& a, const Quaternion& b, float time, const Quaternion& value, const char* reason) {
    const auto actual = dw4::original_interpolate_rotation(a,b,time);
    for (std::size_t i = 0; i < value.size(); ++i)
        check(std::abs(actual[i]-value[i]) <= 2e-7f,reason);
}
template<class F> void rejects(F&& operation, const char* reason) {
    bool caught = false;
    try { operation(); } catch (const dw4::AnimationMathError&) { caught = true; }
    check(caught,reason);
}
}

int main() {
    try {
        // These small synthetic values were independently evaluated through a
        // bounded scalar interpreter of guest 0x8012be90. They are not game data.
        const Quaternion identity{0,0,0,1}, half_turn{0,0,1,0};
        expected(identity,identity,.5f,identity,"Identity interpolation");
        expected(identity,{0,0,0,-1},.5f,identity,"Shortest sign for equivalent quaternions");
        expected(identity,half_turn,.5f,{0,0,.705755115f,.705755115f},"Original polynomial midpoint is not normalized sine interpolation");
        expected(identity,half_turn,0,{0,0,-.00136594765f,.998634040f},"Original polynomial zero endpoint approximation is preserved");
        expected(identity,half_turn,1,{0,0,.998633981f,-.00136595964f},"Original polynomial final endpoint approximation is preserved");
        expected({.5f,.5f,.5f,.5f},{.1f,-.2f,.3f,.9f},.25f,
            {.433667094f,.345739692f,.492285371f,.668140173f},"Mixed components preserve original polynomial operation order");
        expected(identity,{0,0,0,.92f},.5f,{0,0,0,.960000038f},"Threshold near branch uses unnormalized linear interpolation");
        expected(identity,{0,0,0,.9199998f},.5f,{0,0,0,.981042624f},"Threshold outside branch uses the original polynomial");
        expected({0,0,0,2},{0,0,0,2},.5f,{0,0,0,2},"Finite nonunit inputs are not normalized");
        expected(identity,{0,0,-1,0},.5f,{0,0,-.705755115f,.705755115f},"Zero dot product preserves the input half-turn sign");
        for (const auto time : {-0.01f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
            rejects([&]{ dw4::original_interpolate_rotation(identity,half_turn,time); },"Reject invalid blend interval");
        for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            rejects([&]{ dw4::original_interpolate_rotation({invalid,0,0,1},identity,.5f); },"Reject invalid first quaternion");
            rejects([&]{ dw4::original_interpolate_rotation(identity,{0,invalid,0,1},.5f); },"Reject invalid second quaternion");
        }
        const auto large = std::numeric_limits<float>::max();
        rejects([&]{ dw4::original_interpolate_rotation({large,0,0,0},{large,0,0,0},.5f); },"Reject finite-input dot overflow");
        std::cout << checks << " animation math checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Animation math test failure: " << error.what() << '\n'; return 1;
    }
}
