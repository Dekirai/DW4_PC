#include "dw4/model_placement.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
int checks{};
void check(bool condition,const char* message) {
    ++checks;
    if(!condition)throw std::runtime_error(message);
}
template<class F> void rejects(F&& operation,const char* message) {
    bool caught=false;
    try { operation(); } catch(const ModelPlacementError&) { caught=true; }
    check(caught,message);
}
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float word(std::uint32_t value) { return std::bit_cast<float>(value); }
AnimationMatrix matrix() {
    AnimationMatrix value;
    for(std::size_t i=0;i<value.size();++i)value[i]=static_cast<float>(i+2);
    return value;
}
bool same(const AnimationMatrix& a,const AnimationMatrix& b) {
    for(std::size_t i=0;i<a.size();++i)if(bits(a[i])!=bits(b[i]))return false;
    return true;
}
auto owner(AnimationMatrix value=matrix()) {
    return make_model_ctrl_node2_placement(std::make_shared<AnimationMatrix>(value));
}

void routing_and_copies() {
    MessengerBuffer empty;
    int reads=0;
    ModelPlacementInvocation nothing([&]()->std::shared_ptr<ModelPlacementNode> { ++reads;throw std::runtime_error("Unused node read."); });
    check(nothing.resume(empty).completed && nothing.completed() && !nothing.published() && reads==0,
        "Empty placement must complete without touching the actual node.");
    check(nothing.resume(empty).completed && reads==0,"Completed placement replayed.");
    ModelPlacementInvocation unavailable({});
    check(unavailable.resume(empty).completed,"An empty placement needs no node resolver.");

    auto node=owner();const auto original=*node->matrix;
    MessengerBuffer vectors;vectors.add_vector({100,200,300});vectors.add_matrix(AnimationMatrix{});
    ModelPlacementInvocation one([&] { ++reads;return node; });
    check(one.resume(vectors).completed && one.published() && reads==2,"Vector placement must read source then setter node.");
    auto expected=original;expected[12]=100;expected[13]=200;expected[14]=300;
    check(same(*node->matrix,expected),"Vector0 must replace only translation and take precedence over matrices.");

    MessengerBuffer many;many.add_vector({4,5,6});many.add_vector({90,45,30});many.add_vector({7,8,9});
    *node->matrix=original;
    ModelPlacementInvocation three([&] { return node; });three.resume(many);
    expected=original;expected[12]=4;expected[13]=5;expected[14]=6;
    check(same(*node->matrix,expected),"More than two vectors must skip the rotation branch.");

    auto target=owner();target->matrix.reset(); // The actual setter still owns its storage.
    AnimationMatrix supplied=matrix();supplied[0]=-20;supplied[15]=-3;
    auto observed=std::make_shared<AnimationMatrix>();
    target->set_matrix=[observed](const AnimationMatrix& value) { *observed=value;return MessengerCallResult{true,99,{}}; };
    MessengerBuffer matrices;matrices.add_matrix(supplied);matrices.add_matrix(AnimationMatrix{});
    int matrix_reads=0;
    ModelPlacementInvocation direct([&] { ++matrix_reads;return target; });
    const auto result=direct.resume(matrices);
    check(result.completed && result.value==1 && matrix_reads==1,"Matrix-only placement read the old node matrix or exposed the setter return.");
    check(same(*observed,supplied),"Matrix0 must replace directly without parent multiplication or normalization.");
}

void rotation_and_float_words() {
    auto node=owner();const auto original=*node->matrix;
    MessengerBuffer zero;zero.add_vector({-2,3,-4});zero.add_vector({-0.0f,0,-0.0f});
    ModelPlacementInvocation turn([&] { return node; });turn.resume(zero);
    auto expected=original;
    for(const std::size_t at:{0u,1u,2u,4u,5u,6u,8u,9u,10u})expected[at]=0;
    expected[0]=expected[5]=expected[10]=1;expected[2]=-0.0f;
    expected[12]=-2;expected[13]=3;expected[14]=-4;
    check(same(*node->matrix,expected),"Zero Euler branches must preserve the original bottom row and negated sine zero.");

    MessengerBuffer x;x.add_vector({1,2,3});x.add_vector({90,0,0});
    ModelPlacementInvocation right_angle([&] { return node; });right_angle.resume(x);
    check((*node->matrix)[0]==1 && std::abs((*node->matrix)[6]-1)<1e-6f && std::abs((*node->matrix)[9]+1)<1e-6f,
        "The degree conversion or positive X Euler orientation is incorrect.");
    check((*node->matrix)[3]==original[3] && (*node->matrix)[7]==original[7] &&
        (*node->matrix)[11]==original[11] && (*node->matrix)[15]==original[15],
        "Placement must not use the motion helper's bottom-row reset.");

    // Independent original-instruction output for vector1=(30,45,60), using
    // the documented host double trig boundary and original scalar rounding.
    MessengerBuffer combined;combined.add_vector({4,5,6});combined.add_vector({30,45,60});
    ModelPlacementInvocation mixed([&] { return node; });mixed.resume(combined);
    constexpr std::array<std::uint32_t,9> basis_words{
        1052050674,1058849905,3207922931,3205676739,1060977699,
        1052050675,1060977700,1049593709,1058849904};
    std::size_t ordinal=0;bool exact=true;
    for(const std::size_t at:{0u,1u,2u,4u,5u,6u,8u,9u,10u})
        exact=exact && bits((*node->matrix)[at])==basis_words[ordinal++];
    check(exact,"Combined Euler rotation changed the original degree product, trig narrowing or fused-operation order.");

    AnimationMatrix tiny=matrix();tiny[0]=word(1);tiny[3]=word(0x807fffff);tiny[15]=word(0x00800000);
    auto tiny_node=owner(tiny);AnimationMatrix before_set{};
    const auto original_setter=tiny_node->set_matrix;
    tiny_node->set_matrix=[&](const AnimationMatrix& value) { before_set=value;return original_setter(value); };
    MessengerBuffer packet;packet.add_vector({word(1),word(0x807fffff),word(0x00800000)});
    ModelPlacementInvocation copy([&] { return tiny_node; });copy.resume(packet);
    check(bits(before_set[0])==0 && bits(before_set[3])==0x80000000 && bits(before_set[15])==0x00800000,
        "The initial matrix PSQ copy must flush tiny values with sign, including untouched entries.");
    check(bits(before_set[12])==1 && bits(before_set[13])==0x807fffff,
        "Vector translation is a scalar copy before concrete publication.");
    check(bits((*tiny_node->matrix)[12])==0 && bits((*tiny_node->matrix)[13])==0x80000000 &&
        bits((*tiny_node->matrix)[14])==0x00800000,
        "The actual node2 setter must perform the final PSQ copy.");

    // Degree conversion underflows to signed zero before the Euler zero test.
    MessengerBuffer tiny_angles;tiny_angles.add_vector({0,0,0});tiny_angles.add_vector({word(1),word(0x80000001),word(0x007fffff)});
    ModelPlacementInvocation underflow([&] { return node; });underflow.resume(tiny_angles);
    check((*node->matrix)[0]==1 && (*node->matrix)[5]==1 && (*node->matrix)[10]==1 && bits((*node->matrix)[2])==0x80000000,
        "Degree products must use the original NI tininess/zero branches.");
}

void live_nodes_and_continuations() {
    auto first=owner(),second=owner(AnimationMatrix{});const auto first_before=*first->matrix;
    MessengerBuffer packet;packet.add_vector({7,8,9});
    int reads=0;
    ModelPlacementInvocation live([&] { return ++reads==1 ? first : second; });
    live.resume(packet);
    auto expected=first_before;expected[12]=7;expected[13]=8;expected[14]=9;
    check(same(*first->matrix,first_before) && same(*second->matrix,expected) && reads==2,
        "Publication must reload the live ONE node rather than retain the source node as target.");

    auto pending=owner();pending->set_matrix={};int pending_reads=0;
    ModelPlacementInvocation missing([&] { ++pending_reads;return pending; });
    check(!missing.resume(packet).completed && pending_reads==2 && !missing.faulted(),
        "Unknown concrete setter must stop after computing the candidate.");
    packet.clear();*pending->matrix=AnimationMatrix{};
    int calls=0;AnimationMatrix supplied{};
    pending->set_matrix=[&](const AnimationMatrix& value) {
        ++calls;supplied=value;
        return MessengerCallResult{calls==2,0,"Concrete setter is paused."};
    };
    check(!missing.resume(packet).completed && pending_reads==2 && calls==1,
        "A resumed setter must retain its original target and candidate despite packet replacement.");
    pending->set_matrix=[](const AnimationMatrix&) { throw std::runtime_error("Replacement setter must not run.");return MessengerCallResult{}; };
    check(missing.resume(packet).completed && missing.published() && calls==2 && pending_reads==2 && same(supplied,expected),
        "Retry must retain the concrete invocation callback instead of resolving a replacement.");
    check(missing.resume(packet).completed && calls==2,"Completion repeated a setter's side effects.");

    // The concrete setter retains the actual matrix, and the invocation keeps
    // its captured target alive until completion without an owner cycle.
    std::weak_ptr<ModelPlacementNode> weak;
    auto lifetime=owner();weak=lifetime;
    lifetime->set_matrix={};
    auto retained_matrix=lifetime->matrix;
    MessengerBuffer input;input.add_matrix(matrix());
    ModelPlacementInvocation retained([lifetime] { return lifetime; });
    check(!retained.resume(input).completed,"Missing setter unexpectedly completed.");
    lifetime.reset();
    check(!weak.expired(),"Suspended placement lost its actual setter-node owner.");
    auto captured=weak.lock();captured->set_matrix=[retained_matrix](const AnimationMatrix& value) {
        *retained_matrix=value;return MessengerCallResult{true,1,{}};
    };captured.reset();
    check(retained.resume(input).completed && weak.expired(),"Completed placement retained a node ownership cycle.");
}

void checked_failures() {
    rejects([] { make_model_ctrl_node2_placement({}); },"Concrete setter accepted a fabricated null matrix.");
    MessengerBuffer packet;packet.add_vector({4,5,6});
    auto node=owner();(*node->matrix)[12]=std::numeric_limits<float>::quiet_NaN();
    int setters=0;node->set_matrix=[&](const AnimationMatrix&) { ++setters;return MessengerCallResult{true,1,{}}; };
    ModelPlacementInvocation bad([&] { return node; });
    rejects([&] { bad.resume(packet); },"Non-finite initial copy was ignored because translation would be overwritten.");
    check(bad.faulted() && !bad.published() && setters==0,"Copy failure published a node or did not fault the invocation.");
    rejects([&] { bad.resume(packet); },"A faulted placement replayed its prefix.");

    ModelPlacementInvocation null_node([] { return std::shared_ptr<ModelPlacementNode>{}; });
    rejects([&] { null_node.resume(packet); },"Vector placement silently accepted a null actual node.");
    auto source=owner();int reads=0;
    ModelPlacementInvocation lost([&] { return ++reads==1 ? source : std::shared_ptr<ModelPlacementNode>{}; });
    rejects([&] { lost.resume(packet); },"Null publication target silently fell back to the earlier source node.");

    auto partial=owner();int calls=0;
    partial->set_matrix=[&](const AnimationMatrix&) -> MessengerCallResult {
        ++calls;(*partial->matrix)[0]=123;throw ModelPlacementError("Concrete setter failed after a visible effect.");
    };
    ModelPlacementInvocation failed([&] { return partial; });
    rejects([&] { failed.resume(packet); },"A concrete setter failure was acknowledged.");
    check(failed.faulted() && (*partial->matrix)[0]==123 && calls==1,"A setter failure lost its real partial effect.");
    rejects([&] { failed.resume(packet); },"A partially failed concrete setter was replayed.");
    check(calls==1,"Faulted retry invoked the setter again.");
}

void invocation_reentrancy() {
    MessengerBuffer packet;packet.add_vector({4,5,6});
    auto node=owner();int reads=0;
    ModelPlacementInvocation* active=nullptr;
    ModelPlacementInvocation resolver_recursion([&] {
        ++reads;
        active->resume(packet);
        return node;
    });active=&resolver_recursion;
    rejects([&] { active->resume(packet); },"Recursive source-node resolution was allowed.");
    check(active->faulted() && reads==1 && !active->published(),"Resolver recursion repeated its prefix or escaped terminal fault.");

    int calls=0;bool nested_rejected=false;
    node->set_matrix=[&](const AnimationMatrix&) {
        ++calls;(*node->matrix)[0]=999;
        // Swallowing an inner ownership error must not acknowledge the
        // partially mutated outer placement as completed.
        try { active->resume(packet); } catch(const ModelPlacementError&) { nested_rejected=true; }
        return MessengerCallResult{true,1,{}};
    };
    ModelPlacementInvocation setter_recursion([&] { return node; });active=&setter_recursion;
    rejects([&] { active->resume(packet); },"A concrete setter hid same-invocation recursion and completed.");
    check(active->faulted() && !active->published() && nested_rejected && calls==1 && (*node->matrix)[0]==999,
        "Setter recursion lost the original partial effect or failed to retain its terminal state.");
    rejects([&] { active->resume(packet); },"Recursive placement was replayable after fault.");
    check(calls==1,"Faulted recursion invoked the setter again.");

    auto inner_node=owner(),outer_node=owner();
    MessengerBuffer inner_packet;inner_packet.add_vector({30,40,50});
    ModelPlacementInvocation inner([&] { return inner_node; });
    const auto ordinary_setter=outer_node->set_matrix;
    outer_node->set_matrix=[&](const AnimationMatrix& value) {
        check(inner.resume(inner_packet).completed,"Distinct nested placement failed.");
        return ordinary_setter(value);
    };
    ModelPlacementInvocation outer([&] { return outer_node; });
    check(outer.resume(packet).completed && outer.published() && inner.published() &&
        (*outer_node->matrix)[12]==4 && (*inner_node->matrix)[12]==30,
        "The ownership guard must allow distinct nested invocation objects.");
}
}

int main() {
    try {
        routing_and_copies();rotation_and_float_words();live_nodes_and_continuations();checked_failures();invocation_reentrancy();
        std::cout<<"Model placement: "<<checks<<" checks passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Model placement failure: "<<error.what()<<'\n';return 1;
    }
}
