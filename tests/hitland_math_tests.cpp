#include "dw4/hitland_math.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
std::uint32_t word(float value) {return std::bit_cast<std::uint32_t>(value);}
float value(std::uint32_t bits) {return std::bit_cast<float>(bits);}
template<class F> void rejects(F&& operation,const char* message) {
    ++checks;try {operation();}catch(const CollisionMathError&) {return;}
    throw std::runtime_error(message);
}
void directions() {
    check(original_hitland_wall_direction({7,19,2},{4,-99,-2})==std::array<float,3>{.6f,0,.8f},
        "The direction uses the captured X/Z displacement and literal positive-zero Y");
    for(const auto axis:{0u,2u})for(const auto sign:{-1.f,1.f}) {
        std::array<float,3> current{};current[axis]=sign*4;
        auto expected=current;expected[axis]=sign;
        check(original_hitland_wall_direction(current,{0,0,0})==expected,"Cardinal displacement is normalized");
    }
    auto diagonal=original_hitland_wall_direction({1,0,1},{0,0,0});
    check(word(diagonal[0])==0x3f3504f3 && word(diagonal[2])==0x3f3504f3,
        "Length rounds before the separate reciprocal and component products");
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    check(original_hitland_wall_direction({3,nan,4},{0,nan,0})==std::array<float,3>{.6f,0,.8f},
        "Unused Y coordinates are never consumed or validated");
    const auto zero=original_hitland_wall_direction({value(0x80000000),nan,4},{0,nan,0});
    check(word(zero[0])==0x80000000 && word(zero[1])==0,
        "Subtraction/product preserve negative X zero while authored Y remains positive zero");
    const auto tiny=original_hitland_wall_direction({value(1),0,4},{0,0,0});
    check(word(tiny[0])==0 && tiny[2]==1,"Tiny scalar displacement flushes before squared length accumulation");
    const auto negative_tiny=original_hitland_wall_direction({value(0x80000001),0,4},{0,0,0});
    check(word(negative_tiny[0])==0x80000000,"Flushed negative displacement retains its zero sign");
    check(original_hitland_wall_direction({0x1p-63f,0,0},{0,0,0})[0]==1,
        "Exactly normal squared length survives NI and normalizes a small displacement");
    rejects([] {original_hitland_wall_direction({0,0,0},{0,0,0});},"Zero length is an explicit numeric boundary");
    rejects([] {original_hitland_wall_direction({0x1p-64f,0,0},{0,0,0});},"Underflowed squared length is rejected");
    rejects([] {original_hitland_wall_direction({0x1p100f,0,0},{0,0,0});},"Overflowed squared length is rejected");
    rejects([] {original_hitland_wall_direction({std::numeric_limits<float>::max(),0,0},
        {-std::numeric_limits<float>::max(),0,0});},"Overflowed displacement is rejected");
    for(const float invalid:{nan,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        for(const auto axis:{0u,2u}) {
            std::array<float,3> bad{3,0,4};bad[axis]=invalid;
            rejects([&] {original_hitland_wall_direction(bad,{0,0,0});},"Reached current X/Z must be finite");
            rejects([&] {original_hitland_wall_direction({0,0,0},bad);},"Reached previous X/Z must be finite");
        }
    }
}
void bounds() {
    const CollisionBox box{{-1,-2,-3},{1,2,3}};
    check(original_hitland_xz_overlap(box,box),"A box overlaps itself");
    check(original_hitland_xz_overlap(box,{{1,100,3},{2,101,4}}),"Touching X/Z bounds are inclusive and Y separation is ignored");
    check(!original_hitland_xz_overlap(box,{{2,0,0},{3,0,1}}),"Model maximum X rejects separated query minimum X");
    check(!original_hitland_xz_overlap(box,{{-3,0,0},{-2,0,1}}),"Model minimum X rejects separated query maximum X");
    check(!original_hitland_xz_overlap(box,{{0,0,4},{1,0,5}}),"Model maximum Z rejects separated query minimum Z");
    check(!original_hitland_xz_overlap(box,{{0,0,-5},{1,0,-4}}),"Model minimum Z rejects separated query maximum Z");
    check(original_hitland_xz_overlap({{1,0,1},{-1,0,-1}},{{-2,0,-2},{2,0,2}}),
        "Finite inverted bounds retain the original four tests without an emptiness predicate");
    auto ignored=box;ignored.minimum[1]=ignored.maximum[1]=std::numeric_limits<float>::quiet_NaN();
    check(original_hitland_xz_overlap(ignored,ignored),"Nonfinite Y is ignored by the X/Z-only helper");
    for(const auto axis:{0u,2u})for(const auto minimum:{false,true}) {
        auto bad=box;(minimum?bad.minimum:bad.maximum)[axis]=std::numeric_limits<float>::quiet_NaN();
        check(!original_hitland_xz_overlap(bad,box),"An unordered reached model bound rejects overlap");
        check(!original_hitland_xz_overlap(box,bad),"An unordered reached query bound rejects overlap");
    }
    auto infinite=box;infinite.minimum[0]=-std::numeric_limits<float>::infinity();
    infinite.maximum[0]=std::numeric_limits<float>::infinity();
    check(original_hitland_xz_overlap(infinite,box),"Ordered infinite bounds are not rejected by invented validation");
}
}
int main() {
    try {directions();bounds();std::cout<<"HITLAND math checks: "<<checks<<'\n';return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
