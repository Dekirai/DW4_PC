#include "dw4/enbaya.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
int checks{};
void check(bool condition, const std::string& reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
bool near(float a, float b, float tolerance = 1e-5f) {
    return std::abs(a-b) <= tolerance * std::max(1.0f, std::abs(b));
}
template<class F> void rejects(F&& operation, const std::string& reason) {
    bool caught{};
    try { operation(); } catch (const dw4::EnbayaError&) { caught = true; }
    check(caught, reason);
}
void word(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at+i) = static_cast<std::uint8_t>(value >> (8*i));
}
void append_integer(Bytes& bytes, std::int64_t value, unsigned width) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (unsigned i = 0; i < width; ++i) bytes.push_back(static_cast<std::uint8_t>(bits >> (8*i)));
}

// Synthetic encoder helpers make independently chosen acceleration sequences.
// Channel arrays are header order; their physical order is explicitly separate.
struct Fixture {
    std::array<Bytes, 14> streams;
    std::array<unsigned, 14> packed_count{};
    std::uint32_t tracks{};
    float quantization{0.5f};
    float duration{2.5f};
    std::uint32_t rate{1};
    void packed(std::size_t stream, std::uint32_t value, unsigned width) {
        const auto index = packed_count[stream]++;
        const auto per_byte = 8 / width;
        if (index % per_byte == 0) streams[stream].push_back(0);
        streams[stream].back() |= static_cast<std::uint8_t>(value << (8-width*(index%per_byte+1)));
    }
    void initial(std::int32_t value) {
        if (!value) { packed(0,0,2); return; }
        const unsigned width = value >= -128 && value <= 127 ? 1
            : value >= -32768 && value <= 32767 ? 2 : 4;
        packed(0,width == 4 ? 3 : width,2);
        append_integer(streams[width == 4 ? 3 : width],value,width);
    }
    void track(std::array<std::int32_t,7> values, std::uint8_t flags) {
        ++tracks;
        for (const auto value : values) initial(value);
        streams[13].push_back(flags);
    }
    void delta(std::int32_t value) {
        if (value == 0 || value == 1 || value == -1) {
            packed(4, value < 0 ? 3 : static_cast<unsigned>(value), 2); return;
        }
        packed(4,2,2);
        constexpr std::array<std::int32_t,15> nibbles{8,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-9};
        const auto nibble = std::find(nibbles.begin(),nibbles.end(),value);
        if (nibble != nibbles.end()) {
            packed(5,static_cast<std::uint32_t>(nibble-nibbles.begin()+1),4);return;
        }
        packed(5,0,4);
        if ((value >= 9 && value <= 127) || (value >= -128 && value <= -10)) {
            append_integer(streams[6],value,1);return;
        }
        if (value >= 128 && value <= 135) {append_integer(streams[6],value-127,1);return;}
        if (value >= -136 && value <= -129) {append_integer(streams[6],value+128,1);return;}
        append_integer(streams[6],0,1);
        if (value >= -32768 && value <= 32767) {append_integer(streams[7],value,2);return;}
        append_integer(streams[7],0,2);append_integer(streams[8],value,4);
    }
    void state(std::uint32_t value) {
        if (!value) {packed(9,0,2);return;}
        const unsigned width = value <= 255 ? 1 : value <= 65535 ? 2 : 4;
        packed(9,width == 4 ? 3 : width,2);
        append_integer(streams[width == 4 ? 12 : 9+width],value,width);
    }
    Bytes bytes() const {
        Bytes result(80);
        word(result,0,0x002abf84);word(result,4,tracks);
        word(result,8,std::bit_cast<std::uint32_t>(quantization));
        word(result,12,std::bit_cast<std::uint32_t>(duration));word(result,16,rate);
        for (std::size_t i=0;i<streams.size();++i) word(result,20+i*4,static_cast<std::uint32_t>(streams[i].size()));
        // A nonzero guest word must remain opaque rather than being dereferenced.
        word(result,76,0xfedcba98);
        constexpr std::array<std::size_t,14> order{3,8,12,2,7,11,0,1,4,5,6,9,10,13};
        for (const auto i : order) result.insert(result.end(),streams[i].begin(),streams[i].end());
        return result;
    }
};
Fixture constant() {
    Fixture f; f.track({0,0,0,2,2,-4,6},0); f.state(100); return f;
}
dw4::EnbayaTransform sample(const dw4::EnbayaClip& clip, float time, std::size_t track = 0) {
    return dw4::sample_enbaya(clip,time).at(track);
}
void test_valid() {
    auto f = constant();
    f.track({0,0,0,-2,-128,32767,std::numeric_limits<std::int32_t>::min()},0);
    auto bytes = f.bytes();
    const auto clip = dw4::decode_enbaya(bytes);
    check(clip.track_count()==2 && clip.sample_rate()==1 && clip.frame_count()==4 && clip.duration()==2.5f,
        "Header and bounded frame metadata");
    bytes.assign(bytes.size(),0);
    auto value = sample(clip,1.25f);
    check(value.rotation==std::array<float,4>{0,0,0,1},"Owned constant identity quaternion");
    check(value.translation.x==1 && value.translation.y==-2 && value.translation.z==3,"Constant translations");
    value = sample(clip,0,1);
    check(value.rotation[3]==-1 && value.translation.x==-64 && value.translation.y==16383.5f
        && value.translation.z==-1073741824.0f,"Signed initial 8, 16 and 32-bit boundaries");
    check(sample(clip,-50).translation.x==1 && sample(clip,50).translation.x==1,"Finite time clamps");

    f = constant();f.streams[13][0]=16;f.delta(2);f.delta(-1);f.delta(0);
    const auto accelerated = dw4::decode_enbaya(f.bytes());
    check(near(sample(accelerated,.5f).translation.x,1.5f),"Linear translation midpoint");
    check(near(sample(accelerated,1).translation.x,2),"First integrated sample");
    check(near(sample(accelerated,2).translation.x,2.5f),"Packed deltas change velocity cumulatively");
    check(near(sample(accelerated,2.5f).translation.x,2.75f),"Partial end uses full sample-period denominator");
    check(near(sample(accelerated,5).translation.x,2.75f),"End time clamps without extrapolation");
    check(near(sample(accelerated,.5f).translation.x,1.5f),"Random backward sampling is deterministic");
    const auto original_middle=dw4::sample_enbaya_original(accelerated,.5f).at(0);
    const auto original_end=dw4::sample_enbaya_original(accelerated,50).at(0);
    check(near(original_middle.translation.x,1.5f) && near(original_end.translation.x,2.75f),
        "Original interpolation path shares clamping and partial-interval translation timing");
    check(dw4::sample_enbaya_original(accelerated,-50).at(0).translation.x==1,
        "Original interpolation path clamps negative finite time to raw initial frame");

    bool normalization_difference{};
    for (const auto rotation : std::array<std::array<std::int32_t,4>,4>{{
        {1,2,3,4},{1,1,1,4},{2,3,5,7},{231,-47,331,977}}}) {
        Fixture raw;raw.track({rotation[0],rotation[1],rotation[2],rotation[3],0,0,0},0);raw.state(100);
        const auto raw_clip=dw4::decode_enbaya(raw.bytes());
        std::array<float,4> expected;
        for (std::size_t i=0;i<4;++i)expected[i]=static_cast<float>(rotation[i])*raw.quantization;
        const auto reciprocal=1.0f/std::sqrt(expected[0]*expected[0]+expected[1]*expected[1]
            +expected[2]*expected[2]+expected[3]*expected[3]);
        for(auto& component:expected)component*=reciprocal;
        const auto original=dw4::sample_enbaya_original(raw_clip,0).at(0).rotation;
        check(original==expected,"Original time zero preserves the once-normalized integrated quaternion");
        normalization_difference |= original!=sample(raw_clip,0).rotation;
    }
    check(normalization_difference,"Reference comparison and original sampling have separate normalization behavior");

    f = constant();f.streams[13][0]=16;f.streams[9].clear();f.packed_count[9]=0;f.streams[10].clear();
    f.state(4);f.state(100);f.delta(2);
    const auto disabled = dw4::decode_enbaya(f.bytes());
    check(near(sample(disabled,2).translation.x,3),"State disables acceleration but preserves integrated velocity");

    f = constant();f.streams[9].clear();f.packed_count[9]=0;f.streams[10].clear();
    f.state(4);f.state(100);f.delta(2);f.delta(0);
    const auto enabled = dw4::decode_enbaya(f.bytes());
    check(near(sample(enabled,1).translation.x,1) && near(sample(enabled,2).translation.x,2),
        "State enables a component at the second interval");

    f = constant();f.duration=5;f.state(0); // Replace state with five tightly packed toggle values.
    f.streams[9].clear();f.packed_count[9]=0;f.streams[10].clear();
    f.state(0);f.state(0);f.state(0);f.state(0);f.state(0);f.state(100);
    for (unsigned i=0;i<20;++i) f.delta(0);
    check(dw4::decode_enbaya(f.bytes()).frame_count()==6,"Zero state runs advance and cross packed bytes");
    for (const auto run : {65535u,0xffffffffu}) {
        f=constant();f.streams[9].clear();f.packed_count[9]=0;f.streams[10].clear();f.state(run);
        check(sample(dw4::decode_enbaya(f.bytes()),1).translation.x==1,"Unsigned 16/32-bit state values");
    }

    f=constant();f.quantization=1;f.streams={};f.packed_count={};f.tracks=0;f.duration=1;
    f.track({0,0,0,1,0,0,0},12);f.state(100);f.delta(1);f.delta(-1);
    const auto turn=dw4::decode_enbaya(f.bytes());
    value=sample(turn,.5f);
    check(near(value.rotation[2],std::sqrt(.5f)) && near(value.rotation[3],std::sqrt(.5f)),"Spherical rotation midpoint");
    check(sample(turn,1).rotation==std::array<float,4>{0,0,1,0},"Quaternion component order is XYZW");
    f.quantization=.1f;f.streams={};f.packed_count={};f.tracks=0;
    f.track({0,0,0,10,0,0,0},4);f.state(100);f.delta(1);
    const auto small_turn=dw4::decode_enbaya(f.bytes());
    value=sample(small_turn,.5f);
    const auto norm=std::sqrt(value.rotation[2]*value.rotation[2]+value.rotation[3]*value.rotation[3]);
    check(norm<.999f && norm>.99f,"Near-angle interpolation retains reference unnormalized LERP");
    f.quantization=.5f;f.streams={};f.packed_count={};f.tracks=0;
    f.track({0,0,0,2,0,0,0},8);f.state(100);f.delta(-4);
    check(sample(dw4::decode_enbaya(f.bytes()),.5f).rotation[3]==1,"Equivalent opposite quaternion signs take short path");

    const std::vector<std::int32_t> deltas{0,1,-1,8,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-9,9,127,-10,-128,
        128,135,-129,-136,136,-137,32767,-32768,32768,-32769,std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::min()};
    f=constant();f.duration=static_cast<float>(deltas.size());f.streams[13][0]=16;
    // Enough unchanged-state components for the entire sequence.
    f.streams[9].clear();f.packed_count[9]=0;f.streams[10].clear();f.state(10000);
    for (const auto delta : deltas) f.delta(delta);
    const auto widths=dw4::decode_enbaya(f.bytes());
    float velocity{},position=1;
    for (std::size_t i=0;i<deltas.size();++i) {
        velocity+=static_cast<float>(deltas[i]);position+=velocity*.5f;
        check(near(sample(widths,static_cast<float>(i+1)).translation.x,position),
            "All signed delta selector/escape widths and packed boundaries");
    }
}
void test_malformed() {
    const auto valid=constant().bytes();
    for (std::size_t n=0;n<valid.size();++n) {
        auto truncated=valid;truncated.resize(n);
        rejects([&]{dw4::decode_enbaya(truncated);},"Every byte truncation rejects");
    }
    auto bad=valid;bad.push_back(0);
    rejects([&]{dw4::decode_enbaya(bad);},"Trailing unclaimed bytes reject");
    for (const auto pair : std::vector<std::pair<std::size_t,std::uint32_t>>{
        {0,0},{4,0},{4,1025},{8,0},{8,0x7f800000},{8,0x7fc00000},{8,0xbf000000},
        {8,0x40000000},{12,0},{12,0xbf800000},{12,0x7f800000},{12,0x7fc00000},
        {12,std::bit_cast<std::uint32_t>(601.0f)},{16,0},{16,601},{20,0xffffffff}}) {
        bad=valid;word(bad,pair.first,pair.second);
        rejects([&]{dw4::decode_enbaya(bad);},"Invalid header field rejects");
    }
    auto f=constant();f.streams[13][0]=128;
    rejects([&]{dw4::decode_enbaya(f.bytes());},"Unknown flag bit rejects");
    f=constant();f.streams[13].push_back(0);
    rejects([&]{dw4::decode_enbaya(f.bytes());},"Wrong flag count rejects");
    f=constant();f.streams[0][0]=0;
    rejects([&]{dw4::decode_enbaya(f.bytes());},"Zero initial quaternion rejects");
    f=constant();f.duration=1;f.streams[13][0]=8;f.delta(-2);
    rejects([&]{dw4::decode_enbaya(f.bytes());},"Zero integrated quaternion rejects");
    for (const auto stream : {2u,3u,7u,8u,11u,12u}) {
        f=constant();f.streams[stream].push_back(0);
        rejects([&]{dw4::decode_enbaya(f.bytes());},"Partial 16/32-bit stream rejects");
    }
    f=constant();f.duration=600;f.rate=600;
    for (unsigned i=1;i<1024;++i) f.track({0,0,0,2,0,0,0},0);
    rejects([&]{dw4::decode_enbaya(f.bytes());},"Decoded transform budget is checked before channel reads");
    f=constant();f.duration=1;f.streams[13][0]=16;f.delta(32768);
    // Each independently consumed substream must remain bounded by its own length.
    for (const auto stream : {0u,1u,4u,5u,6u,7u,8u,9u,10u}) {
        auto copy=f;copy.streams[stream].clear();
        rejects([&]{dw4::decode_enbaya(copy.bytes());},"Crossing an individual stream boundary rejects");
    }
    for (const auto initial : {32767,32768}) {
        f=constant();f.streams={};f.packed_count={};f.tracks=0;
        f.track({0,0,0,2,initial,0,0},0);f.state(100);
        f.streams[initial==32767?2:3].clear();
        rejects([&]{dw4::decode_enbaya(f.bytes());},"Missing wide initial value rejects");
    }
    for (const auto state : {65535u,0xffffffffu}) {
        f=constant();f.streams[9].clear();f.packed_count[9]=0;f.streams[10].clear();f.state(state);
        f.streams[state==65535u?11:12].clear();
        rejects([&]{dw4::decode_enbaya(f.bytes());},"Missing wide state value rejects");
    }
    f=constant();f.streams[9].clear();f.packed_count[9]=0;f.streams[10].clear();f.state(0);
    rejects([&]{dw4::decode_enbaya(f.bytes());},"Exhausted zero state runs reject instead of looping");
    const auto clip=dw4::decode_enbaya(valid);
    for (const auto time : {std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}) {
        rejects([&]{dw4::sample_enbaya(clip,time);},"Non-finite requested time rejects");
        rejects([&]{dw4::sample_enbaya_original(clip,time);},"Original sampler rejects non-finite requested time");
    }
    rejects([]{dw4::sample_enbaya(dw4::EnbayaClip{},0);},"Empty clip sampling rejects");
    rejects([]{dw4::sample_enbaya_original(dw4::EnbayaClip{},0);},"Empty original clip sampling rejects");
    Bytes oversized(16*1024*1024+1);
    rejects([&]{dw4::decode_enbaya(oversized);},"Input byte budget rejects before header reads");
}
} // namespace
int main() {
    try {test_valid();test_malformed();std::cout<<"Enbaya checks passed: "<<checks<<'\n';return 0;}
    catch(const std::exception& error) {std::cerr<<"Enbaya test failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
