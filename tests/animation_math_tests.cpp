#include "dw4/animation_math.hpp"

#include <array>
#include <bit>
#include <cstdint>
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

void matrices() {
    using dw4::AnimationMatrix;
    AnimationMatrix matrix{}; matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
    matrix[3]=7; matrix[7]=8; matrix[11]=9; matrix[12]=10; matrix[13]=11; matrix[14]=12;
    const auto saved=matrix;
    dw4::original_rotation_matrix({0.5f,0.5f,0.5f,0.5f},matrix);
    const AnimationMatrix expected{0,0,1,7, 1,0,0,8, 0,1,0,9, 10,11,12,1};
    check(matrix==expected,"Forward conversion preserves all seven untouched affine/translation fields");
    check(dw4::original_matrix_rotation(matrix)==Quaternion{0.5f,0.5f,0.5f,0.5f},"Trace-zero snapshot branch");
    matrix=saved;
    rejects([&]{dw4::original_rotation_matrix({std::numeric_limits<float>::max(),1,1,1},matrix);},"Reject forward arithmetic overflow");
    check(matrix==saved,"Failed matrix conversion must be atomic");
    matrix[15]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{dw4::original_matrix_rotation(matrix);},"Reject non-finite snapshot input");
    matrix={}; matrix[0]=1; matrix[5]=matrix[10]=-1; matrix[15]=-3;
    rejects([&]{dw4::original_matrix_rotation(matrix);},"Reject a zero radicand producing non-finite quaternion components");
    // Synthetic matrices evaluated by the original 8012B8A8 instructions.
    // Cover all branches, ties, m15 dependence and the nonpositive-root path.
    struct Case { std::array<float,3> diagonal; float affine; std::array<std::uint32_t,4> expected; };
    const Case cases[]{
        {{{0,0,0}},1,{{0x3ec00000u,0xbf400000u,0x3ec00000u,0x3f000000u}}},
        {{{0,0,0}},2,{{0x3ec00000u,0xbf400000u,0x3ec00000u,0x3f000000u}}},
        {{{0,0,0}},-100,{{0x3ec00000u,0xbf400000u,0x3ec00000u,0x3f000000u}}},
        {{{-1,-1,-1}},1,{{0x3f3504f3u,0x3f1e6455u,0x3f87c3b6u,0x3e87c3b6u}}},
        {{{-1,-1,-1}},2,{{0x3f5db3d7u,0x3f015393u,0x3f5db3d7u,0x3e5db3d7u}}},
        {{{-1,-1,-1}},-100,{{0xc2460000u,0xbc10cedeu,0xbc783e10u,0xbb783e10u}}},
        {{{1,-1,-1}},1,{{0x3f800000u,0x3ee00000u,0x3f400000u,0x3e400000u}}},
        {{{1,-1,-1}},2,{{0x3f8f1bbdu,0x3ec85a08u,0x3f2bbae2u,0x3e2bbae2u}}},
        {{{1,-1,-1}},-100,{{0xc2420000u,0xbc13cb37u,0xbc7d5c5eu,0xbb7d5c5eu}}},
        {{{-1,1,-1}},1,{{0x3ee00000u,0x3f800000u,0x3f880000u,0xbec00000u}}},
        {{{-1,1,-1}},2,{{0x3ec85a08u,0x3f8f1bbdu,0x3f7348c1u,0xbeabbae2u}}},
        {{{-1,1,-1}},-100,{{0xbc13cb37u,0xc2420000u,0xbcb376c3u,0x3bfd5c5eu}}},
        {{{-1,-1,1}},1,{{0x3f400000u,0x3f880000u,0x3f800000u,0x3e400000u}}},
        {{{-1,-1,1}},2,{{0x3f2bbae2u,0x3f7348c1u,0x3f8f1bbdu,0x3e2bbae2u}}},
        {{{-1,-1,1}},-100,{{0xbc7d5c5eu,0xbcb376c3u,0xc2420000u,0xbb7d5c5eu}}},
        {{{-3,-2,-1}},1,{{0x3f2bbae2u,0x3f7348c1u,0x3f8f1bbdu,0x3e2bbae2u}}},
        {{{-3,-2,-1}},2,{{0x3f1cc470u,0x3f5e164au,0x3f9cc471u,0x3e1cc470u}}},
        {{{-3,-2,-1}},-100,{{0xbc800000u,0xbcb55556u,0xc2400000u,0xbb800000u}}},
        {{{-2,-1,-3}},1,{{0x3ec85a08u,0x3f8f1bbdu,0x3f7348c1u,0xbeabbae2u}}},
        {{{-2,-1,-3}},2,{{0x3eb6e52eu,0x3f9cc471u,0x3f5e164au,0xbe9cc470u}}},
        {{{-2,-1,-3}},-100,{{0xbc155556u,0xc2400000u,0xbcb55556u,0x3c000000u}}},
        {{{-1,-3,-2}},1,{{0x3f8f1bbdu,0x3ec85a08u,0x3f2bbae2u,0x3e2bbae2u}}},
        {{{-1,-3,-2}},2,{{0x3f9cc471u,0x3eb6e52eu,0x3f1cc470u,0x3e1cc470u}}},
        {{{-1,-3,-2}},-100,{{0xc2400000u,0xbc155556u,0xbc800000u,0xbb800000u}}},
    };
    for (const auto& value:cases) {
        for (std::size_t i=0;i<16;++i) matrix[i]=0.25f*static_cast<float>(i+1);
        matrix[0]=value.diagonal[0]; matrix[5]=value.diagonal[1]; matrix[10]=value.diagonal[2]; matrix[15]=value.affine;
        const auto q=dw4::original_matrix_rotation(matrix);
        for (std::size_t i=0;i<4;++i) check(std::bit_cast<std::uint32_t>(q[i])==value.expected[i],"Original snapshot branch and float rounding");
    }
}

void rotation_matrix_ni() {
    // Synthetic vectors evaluated by exact integer arithmetic through the
    // original scalar instruction words, using startup NI=1/RN=00.
    struct Case {
        std::array<std::uint32_t,4> rotation;
        std::array<std::uint32_t,9> expected;
        const char* reason;
    };
    const Case cases[]{
        {{0,0,0x00400000u,0x3f7fffffu},
            {0x3f800000u,0,0,0,0x3f800000u,0,0,0,0x3f800000u},
            "Scalar product flush precedes rounding up to min-normal"},
        {{0x3f800000u,0x80000000u,0x80400000u,0x3f7fffffu},
            {0x3f800000u,0,0x80800000u,0x80000000u,0xbf800000u,0xbfffffffu,0x80800000u,0x3fffffffu,0xbf800000u},
            "Scalar underflow and later sums preserve the original signed zeros"},
        {{0x5f000000u,0,0,1},
            {0x3f800000u,0,0,0,0xff000000u,0x95000000u,0,0x15000000u,0xff000000u},
            "Scalar loads retain a subnormal W component which produces normal cross terms"},
        {{1,0x5f000000u,0,0},
            {0xff000000u,0x15000000u,0,0x15000000u,0x3f800000u,0,0,0,0xff000000u},
            "An input remains usable after its separately doubled value underflows"},
        {{1,0,0,0x5f000000u},
            {0x3f800000u,0,0,0,0x3f800000u,0,0,0,0x3f800000u},
            "Doubling underflow is flushed before multiplication by a large W"},
        {{0x3f000000u,0x00800001u,0x00800000u,0x3f000000u},
            {0x3f800000u,0,0x01000000u,0x01000000u,0x3f000000u,0xbf000000u,0x80000000u,0x3f000000u,0x3f000000u},
            "Subtraction of normal cross terms flushes tiny positive and negative residuals"},
        {{0x3f000000u,0x00800001u,0x80800000u,0x3f000000u},
            {0x3f800000u,0x01000000u,0,0,0x3f000000u,0xbf000000u,0x81000000u,0x3f000000u,0x3f000000u},
            "Addition of opposite normal cross terms flushes its tiny result"},
    };
    constexpr std::array<std::size_t,9> written{0,1,2,4,5,6,8,9,10};
    constexpr std::array<std::size_t,7> untouched{3,7,11,12,13,14,15};
    constexpr std::array<std::uint32_t,7> preserved{
        0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,0x00800000u,0xff7fffffu};
    dw4::AnimationMatrix initial{};
    for (std::size_t i=0;i<initial.size();++i) initial[i]=static_cast<float>(i+1);
    for (std::size_t i=0;i<untouched.size();++i) initial[untouched[i]]=std::bit_cast<float>(preserved[i]);
    for (const auto& value:cases) {
        Quaternion rotation{};
        for (std::size_t i=0;i<rotation.size();++i) rotation[i]=std::bit_cast<float>(value.rotation[i]);
        auto matrix=initial;
        dw4::original_rotation_matrix(rotation,matrix);
        for (std::size_t i=0;i<written.size();++i)
            check(std::bit_cast<std::uint32_t>(matrix[written[i]])==value.expected[i],value.reason);
        for (std::size_t i=0;i<untouched.size();++i)
            check(std::bit_cast<std::uint32_t>(matrix[untouched[i]])==preserved[i],
                "Quaternion conversion preserves every untouched word including subnormals and negative zero");
    }
    const auto same_words=[](const dw4::AnimationMatrix& a,const dw4::AnimationMatrix& b) {
        for (std::size_t i=0;i<a.size();++i)
            if (std::bit_cast<std::uint32_t>(a[i])!=std::bit_cast<std::uint32_t>(b[i])) return false;
        return true;
    };
    for (std::size_t i=0;i<4;++i) for (const auto invalid:{
        std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        Quaternion rotation{0,0,0,1};rotation[i]=invalid;auto matrix=initial;
        rejects([&]{dw4::original_rotation_matrix(rotation,matrix);},"Reject each nonfinite quaternion input");
        check(same_words(matrix,initial),"Invalid quaternion input leaves all matrix words unchanged");
    }
    for (std::size_t i=0;i<initial.size();++i) {
        auto matrix=initial;matrix[i]=std::numeric_limits<float>::quiet_NaN();const auto saved=matrix;
        rejects([&]{dw4::original_rotation_matrix({0,0,0,1},matrix);},"Reject nonfinite existing matrix fields even if they would be overwritten");
        check(same_words(matrix,saved),"Invalid matrix input leaves all matrix words unchanged");
    }
    auto matrix=initial;
    rejects([&]{dw4::original_rotation_matrix({std::numeric_limits<float>::max(),0,0,0},matrix);},
        "Reject finite quaternion input whose scalar arithmetic overflows");
    check(same_words(matrix,initial),"Scalar arithmetic failure preserves the complete existing matrix");
}

void skin_points() {
    using dw4::AnimationMatrix;
    using dw4::original_skin_point;
    const AnimationMatrix matrix{1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16};
    check(original_skin_point(matrix,{2,3,4})==std::array<float,3>{66,76,86},
        "CPU skinning uses the selected column-major matrix and returns XYZ without dividing by W");
    AnimationMatrix discarded{};discarded[0]=discarded[5]=discarded[10]=discarded[15]=1;
    discarded[3]=discarded[7]=std::numeric_limits<float>::max();
    check(original_skin_point(discarded,{2,3,4})==std::array<float,3>{2,3,4},
        "Overflow in the discarded fourth lane does not invalidate finite skin coordinates");
    const auto first_word=[](const std::array<float,4>& row,const std::array<float,3>& point) {
        AnimationMatrix value{};
        for (std::size_t i=0;i<row.size();++i) value[4*i]=row[i];
        return std::bit_cast<std::uint32_t>(original_skin_point(value,point)[0]);
    };
    const auto word=[](std::uint32_t value) { return std::bit_cast<float>(value); };
    const auto above_one=word(0x3f800001u),below_one=word(0x3f7ffffeu);
    check(first_word({above_one,-1,0,0},{below_one,1,0})==0,
        "Skinning rounds its initial X product before Y accumulation");
    check(first_word({-1,above_one,0,0},{1,below_one,0})==0xa8800000u,
        "Skinning fuses the Y term with the rounded X product");
    check(first_word({-1,0,above_one,0},{1,0,below_one})==0xa8800000u,
        "Skinning fuses the Z term with accumulated XY");
    check(first_word({0,0,above_one,-1},{0,0,below_one})==0,
        "Translation is added after the Z term has rounded, without fusion");
    check(first_word({1e20f,-1e20f,3,4},{1,1,1})==std::bit_cast<std::uint32_t>(7.0f),
        "CPU skinning preserves X, Y, Z, translation cancellation order");
    const auto minimum=std::numeric_limits<float>::min(),tiny=std::numeric_limits<float>::denorm_min();
    check(first_word({minimum,0,0,0},{word(0x3f7fffffu),1,1})==0,
        "Skinning applies NI before initial-product rounding up to min-normal");
    check(first_word({word(0x7e800000u),0,0,0},{tiny,0,0})==0x34000000u,
        "Skin point loads preserve a subnormal coordinate that produces a normal result");
    check(first_word({-minimum,-0.0f,-0.0f,-0.0f},{word(0x3f7fffffu),1,1})==0x80000000u,
        "Skinning retains negative zero after NI underflow and translation addition");
    check(first_word({word(0x00800001u),0,0,-minimum},{1,0,0})==0,
        "Separate translation addition flushes a tiny cancellation result");
    check(first_word({0,0,0,tiny},{0,0,0})==0,
        "A subnormal translation is loaded intact then flushed by the final arithmetic operation");
    for (std::size_t row=0;row<3;++row) {
        AnimationMatrix overflow{};overflow[row]=std::numeric_limits<float>::max();
        rejects([&]{original_skin_point(overflow,{2,0,0});},"Reject overflow in each returned skin coordinate");
    }
    for (const auto invalid:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        for (std::size_t i=0;i<matrix.size();++i) {
            auto bad=matrix;bad[i]=invalid;
            rejects([&]{original_skin_point(bad,{0,0,0});},"Reject every nonfinite skin matrix input even in the discarded row");
        }
        for (std::size_t i=0;i<3;++i) {
            std::array<float,3> bad{};bad[i]=invalid;
            rejects([&]{original_skin_point(matrix,bad);},"Reject every nonfinite skin point coordinate");
        }
    }
}

void matrix_products() {
    using dw4::AnimationMatrix;
    using dw4::original_matrix_multiply;
    // Synthetic inputs evaluated through the original 80128690 paired-single
    // instructions, with independently rounded binary32 products and FMAs.
    const AnimationMatrix left{1,2,0,0, 3,1,0,0, 0,0,1,0, 5,7,11,1};
    const AnimationMatrix right{0,1,0,0, -1,0,0,0, 0,0,1,0, 2,4,9,1};
    check(original_matrix_multiply(left,right)==AnimationMatrix{3,1,0,0, -1,-2,0,0, 0,0,1,0, 19,15,20,1},
        "Matrix multiplication uses column-major left times right");
    check(original_matrix_multiply(right,left)==AnimationMatrix{-2,1,0,0, -1,3,0,0, 0,0,1,0, -5,9,20,1},
        "Noncommuting matrix inputs preserve their requested order");
    AnimationMatrix full_left{},full_right{};
    for (std::size_t i=0;i<16;++i) { full_left[i]=static_cast<float>(i+1);full_right[i]=static_cast<float>(i+17); }
    check(original_matrix_multiply(full_left,full_right)==AnimationMatrix{
        538,612,686,760, 650,740,830,920, 762,868,974,1080, 874,996,1118,1240},
        "Full matrix multiplication evaluates all sixteen entries without an affine shortcut");
    const auto from_word=[](std::uint32_t value) { return std::bit_cast<float>(value); };
    const auto dot=[](const std::array<float,4>& a,const std::array<float,4>& b) {
        AnimationMatrix first{},second{};
        for (std::size_t i=0;i<4;++i) { first[i*4]=a[i];second[i]=b[i]; }
        return original_matrix_multiply(first,second)[0];
    };
    const float above_one=from_word(0x3f800001u),below_one=from_word(0x3f7ffffeu);
    check(std::bit_cast<std::uint32_t>(dot({above_one,-1,0,0},{below_one,1,0,0}))==0,
        "The first matrix product rounds before later fused accumulation");
    check(std::bit_cast<std::uint32_t>(dot({-1,above_one,0,0},{1,below_one,0,0}))==0xa8800000u,
        "Fused matrix terms preserve residuals lost by separate multiply and add");
    check(dot({1e20f,-1e20f,3,4},{1,1,1,1})==7,
        "Matrix accumulation keeps original k0, k1, k2, k3 cancellation order");
    const auto large=std::numeric_limits<float>::max();
    check(dot({large,-large,0,0},{1,2,0,0})==-large,
        "A fused term remains valid when its unrounded product exceeds float range");
    const auto minimum=std::numeric_limits<float>::min(),tiny=std::numeric_limits<float>::denorm_min();
    const auto before_one=from_word(0x3f7fffffu);
    check(dot({minimum,0,0,0},{before_one,1,1,1})==0,
        "NI flush precedes initial-product rounding up to the smallest normal float");
    check(std::bit_cast<std::uint32_t>(dot({-minimum,-0.0f,-0.0f,-0.0f},{before_one,1,1,1}))==0x80000000u,
        "NI underflow preserves the negative zero sign");
    check(dot({tiny,0,0,0},{from_word(0x7e800000u),1,1,1})==from_word(0x34000000u),
        "Paired loads preserve subnormal inputs which multiply to normal results");
    check(dot({0,tiny,0,0},{1,from_word(0x7e800000u),1,1})==from_word(0x34000000u),
        "Fused terms also retain subnormal multiplicands");
    check(dot({minimum,tiny,0,0},{1,-tiny,1,1})==0,
        "NI detects a fused result below min-normal even when double rounding hides the deficit");
    check(std::bit_cast<std::uint32_t>(dot({-minimum,tiny,-0.0f,-0.0f},{1,tiny,1,1}))==0x80000000u,
        "The negative fused min-normal boundary flushes toward signed zero");
    check(dot({minimum,minimum,minimum,0},{0.5f,0.5f,1,1})==minimum,
        "NI flushes every intermediate result before the following fused term");
    check(dot({minimum,tiny,0,0},{1,tiny,1,1})==minimum,
        "A fused exact result above min-normal remains normal at the same rounded word");
    rejects([&]{dot({large,-large,0,0},{2,2,0,0});},
        "Reject the original initial-product overflow even when an exact wider sum cancels");
    rejects([&]{dot({large,large,large,large},{1,1,1,1});},"Reject finite-input matrix accumulation overflow");
    AnimationMatrix zeros{},ones{};zeros.fill(-0.0f);ones.fill(1);
    for (auto value:original_matrix_multiply(zeros,ones))
        check(std::bit_cast<std::uint32_t>(value)==0x80000000u,"Matrix multiplication preserves negative zero products and sums");
    for (const auto invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        for (std::size_t i=0;i<16;++i) {
            auto bad=left;bad[i]=invalid;
            rejects([&]{original_matrix_multiply(bad,zeros);},"Reject every non-finite left component even with a zero right matrix");
            bad=right;bad[i]=invalid;
            rejects([&]{original_matrix_multiply(zeros,bad);},"Reject every non-finite right component even with a zero left matrix");
        }
    }
}

int main() {
    try {
        matrices(); rotation_matrix_ni(); matrix_products(); skin_points();
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
