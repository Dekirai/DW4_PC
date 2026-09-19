#include "dw4/player_input_math.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
constexpr AnimationMatrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
float value(std::uint32_t bits) {return std::bit_cast<float>(bits);}
std::uint32_t heading(const AnimationMatrix& matrix,float x,float y) {
    return std::bit_cast<std::uint32_t>(original_player_input_heading(matrix,x,y));
}
template<class F> void rejects(F operation) {
    ++checks;
    try {operation();}catch(const AnimationMathError&){return;}
    throw std::runtime_error("Nonfinite reached heading arithmetic was accepted.");
}
void original_goldens() {
    // Independently execute original vector 8012D434 and C690..C6B8 through
    // MSL atan2/atan. These fixtures contain numerical inputs/results only.
    struct Golden {std::uint32_t camera_x,camera_z,x,y,expected;};
    constexpr Golden cases[]{
        {0,0x3f800000,0,0xbf800000,0},
        {0,0x3f800000,0x3f800000,0,0x3fc90fdb},
        {0,0x3f800000,0,0x3f800000,0x40490fdb},
        {0,0x3f800000,0xbf800000,0,0xbfc90fdb},
        {0x3ea00000,0xbf400000,0x3ee66666,0xbf600000,0x404e322c},
        {0xbe000000,0xbf500000,0xbf400000,0x3ec00000,0xc0a0bf96},
        {0,0x3f800000,0x3f000000,0xbf800000,0x3eed6338},
        {0,0x3f800000,0x3f600000,0xbf800000,0x3f38053e},
        {0,0x3f800000,0x3fc00000,0xbf800000,0x3f7b985f},
        {0,0x3f800000,0x40400000,0xbf800000,0x3f9fe0bb},
    };
    for(const auto& row:cases) {
        auto matrix=identity;matrix[8]=value(row.camera_x);matrix[10]=value(row.camera_z);
        check(heading(matrix,value(row.x),value(row.y))==row.expected,
            "Player heading differs from an original instruction golden.");
    }
}
void tiny_and_signed_inputs() {
    for(const auto input:{1u,0x007fffffu,0x80000001u,0x807fffffu})
        check(heading(identity,value(input),-1)==0,
            "Original angle narrowing failed to flush a tiny result before the final sum.");
    check(heading(identity,value(0x00800000),-1)==0x00800000,
        "The minimum normal heading was flushed.");
    check(heading(identity,0.0f,0.0f)==0x40490fdb,
        "Positive pad Y zero was not negated before atan2.");
    check(heading(identity,0.0f,-0.0f)==0,
        "Negative pad Y zero did not reach the positive-X atan2 branch.");
    check(heading(identity,-0.0f,0.0f)==0xc0490fdb,
        "Negative pad X zero lost its negative pi quadrant.");
    auto matrix=identity;matrix[8]=value(0x007fffff);
    check(heading(matrix,0.0f,-1)==0,
        "A tiny forward-vector result was not flushed before atan2.");
}
void reached_profile() {
    constexpr std::array<std::size_t,9> reached{0,1,2,4,5,6,8,9,10};
    constexpr std::array<std::size_t,7> unused{3,7,11,12,13,14,15};
    const float nan=std::numeric_limits<float>::quiet_NaN();
    for(auto index:reached) {
        auto matrix=identity;matrix[index]=nan;
        rejects([&]{original_player_input_heading(matrix,0,-1);});
    }
    for(auto index:unused) {
        auto matrix=identity;matrix[index]=nan;
        check(heading(matrix,0,-1)==0,"Heading consumed an unused inverse-matrix field.");
    }
    rejects([&]{original_player_input_heading(identity,nan,-1);});
    rejects([&]{original_player_input_heading(identity,0,std::numeric_limits<float>::infinity());});
}
}
int main() {
    try {
        original_goldens();tiny_and_signed_inputs();reached_profile();
        std::cout<<"Player input math checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
