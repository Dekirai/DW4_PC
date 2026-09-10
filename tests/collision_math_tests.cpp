#include "dw4/collision_math.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool valid,const char* message) { ++checks; if (!valid) throw std::runtime_error(message); }
template<class F> void rejects(F&& operation,const char* message) {
    ++checks; try { operation(); } catch (const CollisionMathError&) { return; }
    throw std::runtime_error(message);
}
AnimationMatrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
float value(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t word(float component) { return std::bit_cast<std::uint32_t>(component); }
void ordinary() {
    check(original_hitbox_box(identity(),{0,4,7},{3,3,3})==CollisionBox{{-1.5f,2.5f,5.5f},{1.5f,5.5f,8.5f}},
        "Authored size is the full extent of each world axis");
    AnimationMatrix rotated{0,1,0,0,-1,0,0,0,0,0,1,0,10,-5,7,1};
    check(original_hitbox_box(rotated,{2,3,4},{2,4,6})==CollisionBox{{6,-5,8},{8,-1,14}},
        "The current matrix rotates/translates the offset before world extents are applied");
    auto scaled=identity(); scaled[0]=2; scaled[5]=3; scaled[10]=-4; scaled[12]=5;
    check(original_hitbox_box(scaled,{2,3,4},{2,4,6})==CollisionBox{{8,7,-19},{10,11,-13}},
        "Matrix scale affects the center but does not scale the authored extents");
    check(original_hitbox_box(identity(),{2,3,4},{-2,0,6})==CollisionBox{{3,3,1},{1,3,7}},
        "Negative and zero extents preserve the original unsorted bounds");
    auto matrix=identity(); matrix[15]=2;
    check(original_hitbox_box(matrix,{2,4,6},{2,2,2})==CollisionBox{{0,1,2},{2,3,4}},"Projective W divides the transformed offset");
    matrix[15]=-2;
    check(original_hitbox_box(matrix,{2,4,6},{2,2,2})==CollisionBox{{-2,-3,-4},{0,-1,-2}},"Ordinary negative W retains its sign");
    matrix=identity(); matrix[12]=3; matrix[15]=7;
    check(word(original_hitbox_box(matrix,{0,0,0},{0,0,0}).minimum[0])==0x3edb6db8,
        "The rounded reciprocal is multiplied separately instead of dividing XYZ directly");
    matrix=identity(); matrix[3]=1; matrix[7]=2; matrix[11]=3;
    check(original_hitbox_box(matrix,{1,1,1},{0,0,0}).minimum[0]==1.f/7.f,"Homogeneous W uses every projective row component");
}
void arithmetic_edges() {
    constexpr std::uint32_t epsilon_word=0x350637bd;
    auto matrix=identity(); matrix[12]=matrix[13]=matrix[14]=value(epsilon_word);
    for (const auto w:{0u,0x80000000u,epsilon_word-1,0x80000000u|(epsilon_word-1),epsilon_word}) {
        matrix[15]=value(w);
        check(original_hitbox_box(matrix,{0,0,0},{0,0,0})==CollisionBox{{1,1,1},{1,1,1}},
            "Small positive and negative W use the exact positive epsilon denominator");
    }
    matrix[15]=value(0x80000000u|epsilon_word);
    check(original_hitbox_box(matrix,{0,0,0},{0,0,0})==CollisionBox{{-1,-1,-1},{-1,-1,-1}},
        "Negative W at the exact epsilon boundary is not replaced");
    matrix=identity(); matrix[12]=3; matrix[7]=std::numeric_limits<float>::max();
    check(original_hitbox_box(matrix,{0,2,0},{2,4,6})==CollisionBox{{-1,-2,-3},{1,2,3}},
        "Computed infinite W may produce finite zero XYZ through the original reciprocal");
    matrix=identity(); matrix[15]=value(0x7e800001); matrix[12]=1;
    check(word(original_hitbox_box(matrix,{0,0,0},{0,0,0}).maximum[0])==0,
        "A tiny reciprocal flushes before multiplying transformed XYZ");
    matrix=identity(); matrix[0]=0x1p126f;
    check(original_hitbox_box(matrix,{value(1),0,0},{0,0,0}).maximum[0]==0x1p-23f,
        "Scalar loads retain subnormal offset inputs for later normal products");
    matrix=identity(); matrix[0]=value(1); matrix[4]=value(0x00800000); matrix[15]=value(epsilon_word);
    check(word(original_hitbox_box(matrix,{value(0x80000001),1,0},{0,0,0}).maximum[0])==0,
        "An exact fused deficit below minimum normal flushes before projective division can amplify it");
    matrix=identity(); matrix[12]=value(0x00800000);
    check(word(original_hitbox_box(matrix,{0,0,0},{0,0,0}).maximum[0])==0,
        "Each center half operation observes NI flushing rather than replacing center with the transformed point");
    matrix[12]=value(0x01000000);
    check(word(original_hitbox_box(matrix,{0,0,0},{0,0,0}).maximum[0])==0x01000000,
        "Exactly normal center halves survive NI arithmetic");
    matrix.fill(value(0x80000000)); matrix[15]=1;
    const auto zeros=original_hitbox_box(matrix,{1,1,1},{0,0,0});
    for (std::size_t i=0;i<3;++i) check(word(zeros.minimum[i])==0x80000000 && word(zeros.maximum[i])==0,
        "The first degenerate corner and ordered bound sums retain original signed-zero results");
    // The first Y product must round before X is fused, independently of the
    // CPU skin loop's X-first arithmetic.
    matrix=identity(); matrix[0]=value(0x3f800001); matrix[4]=-1;
    check(word(original_hitbox_box(matrix,{value(0x3f7ffffe),1,0},{0,0,0}).minimum[0])==0xa8800000,
        "Point accumulation fuses X only after the rounded Y product");
}
void invalid_values() {
    for (const auto invalid:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}) {
        for (std::size_t i=0;i<22;++i) {
            auto matrix=identity(); std::array<float,3> offset{},size{};
            if (i<16) matrix[i]=invalid; else if (i<19) offset[i-16]=invalid; else size[i-19]=invalid;
            rejects([&]{original_hitbox_box(matrix,offset,size);},"Every input word must be finite, including the homogeneous row");
        }
    }
    auto matrix=identity(); matrix[0]=std::numeric_limits<float>::max();
    rejects([&]{original_hitbox_box(matrix,{2,0,0},{0,0,0});},"Non-finite transformed XYZ is outside the usable box profile");
    matrix=identity(); matrix[12]=std::numeric_limits<float>::max();
    rejects([&]{original_hitbox_box(matrix,{0,0,0},{std::numeric_limits<float>::max(),0,0});},"Overflowing final bounds reject");
}
void general_boxes() {
    const CollisionBox local{{-1,-2,-3},{4,6,8}};
    AnimationMatrix rotated{0,2,0,0,-3,0,0,0,0,0,-4,0,10,-5,7,1};
    check(original_transform_box(rotated,local)==CollisionBox{{-8,-7,-25},{16,3,19}},
        "General box transform preserves rotated/scaled edge arithmetic");
    const auto distances=original_box_distances(local);
    check(distances==CollisionBoxDistances{{2.5f,4,5.5f},{2.5f,4,5.5f}},"Local distances are captured independently on each side");
    check(original_body_box(rotated,local,distances)==CollisionBox{{1.5f,-6,-8.5f},{6.5f,2,2.5f}},
        "Target body restores original local distances around the transformed box center");
    auto projective=identity();projective[15]=2;
    check(original_transform_box(projective,{{2,4,6},{4,8,10}})==CollisionBox{{1,2,3},{3,6,7}},
        "Only the minimum point receives homogeneous division; matrix edges remain undivided");
    const CollisionBox inverted_x{{4,-2,-3},{-1,6,8}};
    check(original_transform_box(rotated,inverted_x)==inverted_x,"Inverted X returns the complete original box before transform arithmetic");
    check(original_transform_box(identity(),{{-1,6,-3},{4,-2,8}})==local,
        "Inverted Y alone still executes the eight-corner transform");
    check(original_transform_box(identity(),{{-1,-2,8},{4,6,-3}})==local,
        "Inverted Z alone still executes the eight-corner transform");
    const CollisionBox adjacent{{1,1,1},{value(0x3f800001),value(0x3f800001),value(0x3f800001)}};
    check(original_box_center(adjacent)==std::array<float,3>{1,1,1},"Center uses rounded maximum half followed by fused minimum half");
    check(original_box_distances(adjacent)==CollisionBoxDistances{{0,0,0},{0x1p-23f,0x1p-23f,0x1p-23f}},
        "Rounded center can yield unequal minimum and maximum distances");
    const float tiny=value(0x007fffff);
    check(original_box_center({{tiny,tiny,tiny},{tiny,tiny,tiny}})==std::array<float,3>{0,0,0},
        "Center preserves startup NI half-operation flushing");
    auto shifted=identity();shifted[12]=value(0x00800000);
    check(word(original_body_box(shifted,{{0,0,0},{0,0,0}},{{tiny,0,0},{tiny,0,0}}).maximum[0])==0,
        "Body restoration preserves center flushing and explicitly rounds loaded distance inputs");
    shifted[12]=value(0x01000000);
    check(word(original_body_box(shifted,{{0,0,0},{0,0,0}},{{tiny,0,0},{tiny,0,0}}).maximum[0])==0x01000000,
        "Body frsp flushes a subnormal stored distance before adding it to a normal center");
    const auto signed_distances=original_box_distances({{0,0,0},{value(0x80000000),value(0x80000000),value(0x80000000)}});
    for(const auto component:signed_distances.maximum)check(word(component)==0x80000000,
        "Conditional absolute-distance negation retains negative zero");
    const float largest=std::numeric_limits<float>::max();
    const CollisionBox factory{{largest,largest,largest},{-largest,-largest,-largest}};
    check(original_transform_box(rotated,factory)==factory,"Factory inverted bounds survive the exact X early return");
    check(original_boxes_overlap({{-largest,-largest,-largest},{largest,largest,largest}},factory),
        "Inclusive overlap has no inferred inverted-box rejection");
    check(!original_boxes_overlap(local,factory),"Ordinary finite input misses the unrefreshed factory box");
    check(original_boxes_overlap(local,local),"A box overlaps itself");
    for(std::size_t axis=0;axis<3;++axis) {
        auto touching=local;touching.minimum[axis]=local.maximum[axis];touching.maximum[axis]+=10;
        check(original_boxes_overlap(local,touching) && original_boxes_overlap(touching,local),"Face contact is inclusive in both argument orders");
        touching.minimum[axis]+=1;
        check(!original_boxes_overlap(local,touching) && !original_boxes_overlap(touching,local),"Each pair of overlap comparisons can reject independently");
    }
    check(original_boxes_overlap(local,{{4,6,8},{9,10,11}}),"A shared corner counts as overlap");
    check(original_boxes_overlap({{0,0,0},{0,0,0}},{{value(0x80000000),0,0},{0,0,0}}),"Signed zero equality remains inclusive");
}
void invalid_general_boxes() {
    const CollisionBox box{{-1,-2,-3},{4,6,8}};
    for(const auto invalid:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}) {
        for(std::size_t at=0;at<6;++at) {
            auto bad=box;(at<3 ? bad.minimum[at] : bad.maximum[at-3])=invalid;
            rejects([&]{original_transform_box(identity(),bad);},"Transform requires finite input box words");
            rejects([&]{original_box_center(bad);},"Center requires finite input box words");
            rejects([&]{original_box_distances(bad);},"Distances require finite input box words");
            rejects([&]{original_boxes_overlap(bad,box);},"Overlap requires finite incoming box words");
            rejects([&]{original_boxes_overlap(box,bad);},"Overlap requires finite body box words");
            CollisionBoxDistances distances{};(at<3 ? distances.minimum[at] : distances.maximum[at-3])=invalid;
            rejects([&]{original_body_box(identity(),box,distances);},"Body restoration requires finite distance words");
        }
    }
    auto bad_matrix=identity();bad_matrix[15]=std::numeric_limits<float>::infinity();
    rejects([&]{original_transform_box(bad_matrix,{{1,0,0},{0,0,0}});},"Checked matrix validation applies even before inverted-X return");
    bad_matrix=identity();bad_matrix[0]=std::numeric_limits<float>::max();
    rejects([&]{original_transform_box(bad_matrix,{{0,0,0},{2,1,1}});},"Nonfinite transformed corner bounds reject");
    const float largest=std::numeric_limits<float>::max();
    check(original_box_distances({{largest,0,0},{-largest,0,0}})==CollisionBoxDistances{{largest,0,0},{largest,0,0}},
        "Extreme finite distances avoid an overflowing full endpoint subtraction");
    rejects([&]{original_body_box(identity(),{{largest,0,0},{largest,0,0}},{{0,0,0},{largest,0,0}});},
        "Unrepresentable reconstructed body bounds reject");
}
void box_oracle() {
    char operation{};
    while(std::cin>>operation) {
        const std::size_t count=operation=='T' ? 22 : operation=='B' ? 28 : operation=='O' ? 12 : 6;
        std::array<float,28> input{};
        for(std::size_t i=0;i<count;++i) {std::uint32_t bits{};if(!(std::cin>>bits))throw std::runtime_error("Incomplete box oracle input");input[i]=value(bits);}
        const auto box_at=[&](std::size_t at) {return CollisionBox{{input[at],input[at+1],input[at+2]},
            {input[at+3],input[at+4],input[at+5]}};};
        const auto print=[](const auto& values) {for(const auto component:values)std::cout<<word(component)<<' ';};
        try {
            if(operation=='O')std::cout<<(original_boxes_overlap(box_at(0),box_at(6)) ? 1 : 0);
            else if(operation=='C')print(original_box_center(box_at(0)));
            else if(operation=='D') {const auto result=original_box_distances(box_at(0));print(result.minimum);print(result.maximum);}
            else if(operation=='T' || operation=='B') {
                AnimationMatrix matrix{};for(std::size_t i=0;i<16;++i)matrix[i]=input[i];
                const auto distances=box_at(22);
                const auto result=operation=='T' ? original_transform_box(matrix,box_at(16)) :
                    original_body_box(matrix,box_at(16),{distances.minimum,distances.maximum});
                print(result.minimum);print(result.maximum);
            } else throw std::runtime_error("Unknown box oracle operation");
            std::cout<<'\n';
        }catch(const CollisionMathError&) {std::cout<<"ERROR\n";}
    }
}
void oracle() {
    std::uint32_t first{};
    while (std::cin>>first) {
        std::array<std::uint32_t,22> input{}; input[0]=first;
        for (std::size_t i=1;i<input.size();++i) if (!(std::cin>>input[i])) throw std::runtime_error("Incomplete oracle input");
        AnimationMatrix matrix{}; std::array<float,3> offset{},size{};
        for (std::size_t i=0;i<16;++i) matrix[i]=value(input[i]);
        for (std::size_t i=0;i<3;++i) { offset[i]=value(input[16+i]); size[i]=value(input[19+i]); }
        try {
            const auto box=original_hitbox_box(matrix,offset,size);
            for (const auto component:box.minimum) std::cout<<word(component)<<' ';
            for (std::size_t i=0;i<3;++i) std::cout<<word(box.maximum[i])<<(i==2 ? '\n' : ' ');
        } catch (const CollisionMathError&) { std::cout<<"ERROR\n"; }
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc==2 && std::string_view(argv[1])=="--oracle") { oracle(); return 0; }
        if (argc==2 && std::string_view(argv[1])=="--box-oracle") { box_oracle(); return 0; }
        ordinary(); arithmetic_edges(); invalid_values();general_boxes();invalid_general_boxes();
        std::cout<<checks<<" collision math checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Collision math failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
