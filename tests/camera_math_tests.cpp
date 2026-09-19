#include "dw4/camera_math.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {
    ++checks;if(!value)throw std::runtime_error(reason);
}
float value(std::uint32_t word) {return std::bit_cast<float>(word);}
std::uint32_t word(float input) {return std::bit_cast<std::uint32_t>(input);}
template<std::size_t N> bool same(const std::array<float,N>& a,const std::array<float,N>& b) {
    for(std::size_t i=0;i<N;++i)if(word(a[i])!=word(b[i]))return false;
    return true;
}
template<class F> void rejects(F&& operation) {
    bool rejected{};
    try {operation();}catch(const AnimationMathError&) {rejected=true;}
    check(rejected,"Unsupported camera arithmetic did not reach its explicit boundary.");
}
AnimationMatrix identity() {
    AnimationMatrix result{};result[0]=result[5]=result[10]=result[15]=1.0f;return result;
}
void ordinary_and_unused_words() {
    const auto original=identity();AnimationMatrix destination;
    destination.fill(value(0x7fc00123));
    original_camera_view_inverse(original,destination);
    auto expected=original;expected[12]=expected[13]=expected[14]=-0.0f;
    check(same(destination,expected),"Camera identity lost the final negated-zero translation signs.");

    AnimationMatrix source{1,2,3,101,4,5,6,102,7,8,9,103,10,11,12,104};
    const auto saved=source;
    original_camera_view_inverse(source,destination);
    expected={1,4,7,0,2,5,8,0,3,6,9,0,-68,-167,-266,1};
    check(same(destination,expected),"Specialized camera inverse changed transpose or Y/X/Z dot order.");
    check(same(source,saved),"Disjoint camera publication mutated its source.");
    // The specialized routine does not compute the inverse of this scaled,
    // sheared, singular matrix or consume its homogeneous source entries.
    source[3]=value(0x7fc00123);source[7]=std::numeric_limits<float>::infinity();
    source[11]=-std::numeric_limits<float>::infinity();source[15]=value(0x7fc00456);
    original_camera_view_inverse(source,destination);
    check(same(destination,expected),"Ignored homogeneous words were read or validated.");
    original_camera_view_inverse(AnimationMatrix{},destination);
    expected={};expected[12]=expected[13]=expected[14]=-0.0f;expected[15]=1.0f;
    check(same(destination,expected),"Zero rotation incorrectly triggered a generic inverse singularity path.");
}
void original_alias_order() {
    AnimationMatrix matrix{1,2,3,101,4,5,6,102,7,8,9,103,10,11,12,104};
    original_camera_view_inverse(matrix,matrix);
    const AnimationMatrix expected{1,4,7,0,4,5,8,0,7,8,9,0,-138,-191,-266,1};
    check(same(matrix,expected),"In-place camera transpose hid the original overwritten source reads.");

    // Raw8012A860 goldens from the ordered 1..64 buffer. The +1 case also
    // overwrites a translation source word before it is captured.
    const AnimationMatrix before{21,25,29,0,22,26,30,0,23,27,31,0,-2246,-2654,-3062,1};
    const AnimationMatrix after{21,25,29,0,21,26,30,0,25,26,31,0,-1589,-1794,-2105,1};
    for(int displacement:{-1,1}) {
        std::array<float,64> memory;
        for(std::size_t i=0;i<memory.size();++i)memory[i]=static_cast<float>(i+1);
        const auto saved=memory;const std::size_t destination=20+displacement;
        original_camera_view_inverse(std::span<const float,16>(memory.data()+20,16),
            std::span<float,16>(memory.data()+destination,16));
        const auto& golden=displacement<0 ? before:after;
        bool matches=true,untouched=true;
        for(std::size_t i=0;i<16;++i)matches&=word(memory[destination+i])==word(golden[i]);
        for(std::size_t i=0;i<memory.size();++i)
            if(i<destination || i>=destination+16)untouched&=word(memory[i])==word(saved[i]);
        check(matches,"Partially overlapping camera inverse changed original load/store order.");
        check(untouched,"Camera inverse wrote outside its sixteen destination words.");
    }
}
void original_rounding_goldens() {
    AnimationMatrix source{},destination{};
    source[0]=value(0x3f7ffffe);source[1]=1.0f;
    source[12]=value(0x3f800001);source[13]=-1.0f;
    original_camera_view_inverse(source,destination);
    check(word(destination[12])==0x28800000,"Camera fused cancellation was replaced by separately rounded products.");

    source={};source[1]=value(0x3f7fffff);source[13]=value(0x00800000);
    original_camera_view_inverse(source,destination);
    check(word(destination[12])==0x80000000,"Tiny pre-round product rounded up to min-normal instead of NI zero.");

    // Raw results distinguish the exact tiny sum from double rounding to
    // min-normal: min-normal plus a product whose magnitude is only 2^-298.
    for(std::uint32_t sign:{0u,0x80000000u}) {
        source={};source[0]=1.0f;source[2]=value(1u^(0x80000000u^sign));
        source[12]=value(0x00800000u^sign);source[14]=value(1);
        original_camera_view_inverse(source,destination);
        check(word(destination[8])==(1u^(0x80000000u^sign)),"Pure matrix copies incorrectly flushed subnormal words.");
        check(word(destination[12])==(0x80000000u^sign),"Fused NI threshold or final negation changed the zero sign.");
    }
}
void reached_failure_prefixes() {
    auto source=identity();source[5]=std::numeric_limits<float>::max();
    source[12]=2.0f;source[13]=2.0f;source[14]=3.0f;
    AnimationMatrix destination;destination.fill(77.0f);
    rejects([&] {original_camera_view_inverse(source,destination);});
    auto expected=identity();expected[5]=std::numeric_limits<float>::max();
    expected[12]=-2.0f;expected[13]=expected[14]=77.0f;
    check(same(destination,expected),"Second translation overflow rolled back or advanced past the completed publication prefix.");

    source=identity();source[0]=std::numeric_limits<float>::infinity();
    destination.fill(77.0f);
    rejects([&] {original_camera_view_inverse(source,destination);});
    expected=identity();expected[0]=std::numeric_limits<float>::infinity();
    expected[12]=expected[13]=expected[14]=77.0f;
    check(same(destination,expected),"Nonfinite arithmetic boundary changed earlier copy/constant stores.");
}
}
int main() {
    try {
        ordinary_and_unused_words();original_alias_order();original_rounding_goldens();reached_failure_prefixes();
        std::cout<<"Passed "<<checks<<" native camera math checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Camera math test failed: "<<error.what()<<'\n';return 1;}
}
