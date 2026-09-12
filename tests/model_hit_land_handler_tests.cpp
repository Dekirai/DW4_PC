#include "dw4/model_hit_land_handler.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_scene.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
struct Fixture {
    player_runtime_fixture::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> scene=std::make_shared<ActorSceneResource>(source.document(),"synthetic/hitland.igb",19);
    std::shared_ptr<ModelHitLandRam> ram=std::make_shared<ModelHitLandRam>();
    std::shared_ptr<ModelSceneManager> manager=std::make_shared<ModelSceneManager>();
    ActionEventRuntime events;NativeMessenger& messenger=events.messenger();
    Fixture() {
        messenger.select("HITLANDDELIVER","synthetic",0);
        ram->node=scene->mutable_node(source.nodes[0]);
        ram->manager=manager;
        ram->half_width=3;ram->up_range=4;ram->down_range=5;ram->check_flags=7;
        position({3,6,4});ram->previous_position={0,100,0};
    }
    void position(std::array<float,3> value) {
        auto matrix=ram->node->matrix_owner();for(unsigned i=0;i<3;++i)(*matrix)[12+i]=value[i];
    }
    std::array<float,3> position() const {
        const auto matrix=ram->node->matrix_owner();return {(*matrix)[12],(*matrix)[13],(*matrix)[14]};
    }
    void provider(Hook hook) {
        events.register_native_controller(ControllerObjectId::allocate_native(),{
            []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
            [hook=std::move(hook)]{return MessengerManager{hook,{}};}},"HITLANDMODEL",{},0);
    }
    MessengerCallResult run(ModelHitLandInvocation& call){return call.resume(messenger,events);}
};
void dispatch_and_adjust_gates() {
    Fixture unknown;unknown.ram->node.reset();ModelHitLandInvocation absent(unknown.ram,"UNKNOWN",unknown.manager);
    check(unknown.run(absent).completed && unknown.run(absent).value==0,"Unknown command consumed actual node or reported handled");
    ModelHitLandInvocation null(unknown.ram,{},unknown.manager);rejects([&]{unknown.run(null);},"Null command was accepted by nonempty original dictionary");
    check(null.faulted(),"Null command failure did not poison invocation");
    ModelHitLandInvocation inactive(unknown.ram,"HITFLOORADJUST",unknown.manager);
    check(unknown.run(inactive).completed && unknown.run(inactive).value==1,"Inactive floor adjustment consumed unused node");
    Fixture adjusted;adjusted.ram->land_position_ok=255;
    const float tiny=std::bit_cast<float>(std::uint32_t{1});adjusted.ram->land_position={tiny,-0.0f,9};
    const auto matrix=adjusted.ram->node->matrix_owner();(*matrix)[4]=std::numeric_limits<float>::quiet_NaN();
    ModelHitLandInvocation apply(adjusted.ram,"HITFLOORADJUST",adjusted.manager);
    check(adjusted.run(apply).completed && std::bit_cast<std::uint32_t>((*matrix)[12])==1 &&
        std::bit_cast<std::uint32_t>((*matrix)[13])==0x80000000 && (*matrix)[14]==9 && std::isnan((*matrix)[4]),
        "Floor adjustment failed scalar tiny/signed-zero preservation or validated unrelated matrix fields");
}
void equal_wall_consumes_existing_packet() {
    Fixture f;const auto nan=std::numeric_limits<float>::quiet_NaN();f.position({3,nan,4});
    f.ram->previous_position={3,99,4};f.ram->position={8,7,6};
    f.messenger.buffer().add_vector({10,11,12});f.messenger.buffer().add_bool(1);
    f.messenger.buffer().add_int(90);f.messenger.buffer().set_enabled_only(255);
    ModelHitLandInvocation call(f.ram,"HITWALL",f.manager);
    check(f.run(call).completed && f.ram->previous_position==std::array<float,3>{10,11,12} &&
        f.ram->position==std::array<float,3>{8,7,6} && std::isnan(f.position()[1]),
        "Equal X/Z wall path queried, validated unused Y, reset position or omitted existing vector result");
    check(f.messenger.buffer().booleans()==std::vector<std::uint8_t>({0,0,0}) &&
        f.messenger.buffer().integers()==std::vector<std::int32_t>{90} && f.messenger.buffer().vectors().size()==1 &&
        f.messenger.buffer().enabled_only()==255,"Equal wall response did not preserve other packet lists/gate");
}
void retained_wall_query_and_live_node() {
    Fixture f;ModelHitLandInvocation call(f.ram,"HITWALL",f.manager);
    check(!f.run(call).completed && f.messenger.buffer().vectors().empty(),"Missing query manager should retain captured position before selection");
    f.position({30,60,40});f.ram->half_width=-3;f.ram->position[0]=-0.0f;
    unsigned sends{};std::shared_ptr<const MessengerBuffer> packet;
    const auto old_node=f.ram->node;const auto old_matrix=old_node->matrix_owner();
    f.provider([&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++sends;
        check(delivery.message.command=="CHECKWALL","Wrong original wall query command");
        if(sends==1) {
            packet=live.buffer_owner();
            check(live.buffer().vectors()==std::vector<std::array<float,3>>({{3,6,4},{0.6f,0,0.8f}}) &&
                live.buffer().floats()==std::vector<float>({-3,4,5}) && live.buffer().integers()==std::vector<std::int32_t>({7,0}),
                "Wall query lost captured position, original normalization or live dimensions/flag");
            live.buffer().add_int(91);f.ram->node=f.scene->mutable_node(f.source.nodes[1]);f.ram->manager.reset();
            return MessengerCallResult{false,0,"Retained actual query"};
        }
        check(live.buffer_owner()==packet && live.buffer().integers()==std::vector<std::int32_t>({7,0,91}),
            "Wall retry recreated its nested query or repeated arguments");
        live.buffer().clear();live.buffer().add_bool(1);live.buffer().add_bool(98);live.buffer().add_vector({7,8,9});
        live.buffer().add_string("kept");return MessengerCallResult{true,1,{}};
    });
    check(!f.run(call).completed && f.ram->previous_position==std::array<float,3>{0,100,0},
        "Pending wall query published recipient response state too soon");
    check(f.run(call).completed && sends==2 && f.position()==std::array<float,3>{7,8,9} &&
        (*old_matrix)[12]==30 && f.ram->previous_position==std::array<float,3>{7,8,9} &&
        f.ram->position==std::array<float,3>{1,0,0},"Wall completion failed fresh node lookup or retained previous-position publication");
    check(f.messenger.buffer().booleans()==std::vector<std::uint8_t>({1,1,0}) &&
        f.messenger.buffer().strings()==std::vector<std::optional<std::string>>{"kept"},"Wall response overwritten unrelated query results");
    check(f.run(call).completed && sends==2,"Completed wall recipient repeated query");
}
void full_wall_response_conditions() {
    for(const auto hit:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{255}}) {
        Fixture f;f.ram->position[0]=std::numeric_limits<float>::quiet_NaN();
        f.provider([&](NativeMessenger& live,const MessengerDelivery& delivery) {
            check(delivery.message.command=="CHECKWALLFULL" && live.buffer().vectors()==
                std::vector<std::array<float,3>>({{3,6,4},{3,6,4}}) && live.buffer().integers().back()==1,
                "Full wall query normalized its second position or misread unordered previous flag");
            live.buffer().clear();live.buffer().add_bool(hit);live.buffer().add_vector({9,10,11});
            if(hit!=1)f.ram->node.reset();return MessengerCallResult{true,1,{}};
        });
        ModelHitLandInvocation call(f.ram,"HITWALLFULL",f.manager);
        check(f.run(call).completed && f.ram->position==std::array<float,3>{hit==1 ? 1.0f:0.0f,0,0} &&
            f.ram->previous_position==std::array<float,3>{9,10,11} &&
            f.messenger.buffer().booleans()==std::vector<std::uint8_t>({std::uint8_t(hit==1),std::uint8_t(hit==1),0}),
            "Full wall must require boolean exactly one for node movement");
    }
    Fixture missing_vector;missing_vector.provider([&](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().clear();live.buffer().add_bool(1);missing_vector.ram->node.reset();return MessengerCallResult{true,1,{}};
    });
    ModelHitLandInvocation no_vector(missing_vector.ram,"HITWALLFULL",missing_vector.manager);
    check(missing_vector.run(no_vector).completed && missing_vector.ram->position[0]==1 &&
        missing_vector.ram->previous_position==std::array<float,3>{0,100,0} &&
        missing_vector.messenger.buffer().booleans()==std::vector<std::uint8_t>({0,0,0}),
        "Wall without vector response consumed a node or fabricated movement");
}
void floor_results_and_adjustment() {
    for(const auto command:{"HITFLOORCHECK","HITFLOOR"}) {
        Fixture f;f.provider([&](NativeMessenger& live,const MessengerDelivery& delivery) {
            check(delivery.message.command=="CHECKLAND" && live.buffer().vectors()==std::vector<std::array<float,3>>{{3,6,4}} &&
                live.buffer().floats()==std::vector<float>({3,4,5}) && live.buffer().integers()==std::vector<std::int32_t>{7},
                "Floor query packet differs from original consumed arguments");
            live.buffer().clear();live.buffer().add_bool(255);live.buffer().add_bool(2);live.buffer().add_bool(3);
            live.buffer().add_vector({9,1,8});return MessengerCallResult{true,1,{}};
        });
        ModelHitLandInvocation call(f.ram,command,f.manager);
        check(f.run(call).completed && f.ram->land_position==std::array<float,3>{9,1,8} &&
            f.ram->previous_position==f.ram->land_position && f.ram->land_position_ok==3 &&
            f.messenger.buffer().booleans()==std::vector<std::uint8_t>({255,2}),"Floor query normalized response bytes or failed exact y=1 branch");
        check(f.position()==(std::string_view(command)=="HITFLOOR" ? std::array<float,3>{9,1,8}:std::array<float,3>{3,6,4}),
            "Floor check/combined adjustment used the wrong node-store behavior");
    }
    for(const auto y:{-2.0f,0.0f,std::numeric_limits<float>::quiet_NaN()}) {
        Fixture f;f.ram->land_position={80,81,82};f.ram->land_position_ok=9;
        f.provider([&](NativeMessenger& live,const MessengerDelivery&) {
            f.position({90,y,91});live.buffer().clear();
            live.buffer().add_bool(8);live.buffer().add_bool(9);live.buffer().add_bool(10);
            live.buffer().add_vector({7,0.5f,8});return MessengerCallResult{true,1,{}};
        });
        ModelHitLandInvocation call(f.ram,"HITFLOORCHECK",f.manager);
        check(f.run(call).completed && f.messenger.buffer().booleans()==std::vector<std::uint8_t>({1,std::uint8_t(y<0)}),
            "Low floor response did not read the fresh node Y with original ordered comparison");
        check(f.ram->land_position==(y<0 ? std::array<float,3>{7,0,8}:std::array<float,3>{80,81,82}) &&
            f.ram->land_position_ok==std::uint8_t(y<0),"Low floor branch changed the wrong retained land fields");
    }
    Fixture missing;missing.position({3,-2,4});missing.provider([](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().clear();live.buffer().add_bool(1);live.buffer().add_bool(1);live.buffer().add_vector({99,100,101});
        return MessengerCallResult{true,1,{}};
    });
    ModelHitLandInvocation fallback(missing.ram,"HITFLOOR",missing.manager);
    check(missing.run(fallback).completed && missing.position()==std::array<float,3>{0,0,0} &&
        missing.ram->land_position_ok==1,"Short floor boolean result must use original default vector before below-zero clamp");
}
void failure_prefixes_and_recursion() {
    Fixture lifetime;ModelHitLandInvocation lost(lifetime.ram,"HITWALL",lifetime.manager);
    check(!lifetime.run(lost).completed,"Missing query manager must pause before checking captured-owner loss");
    lifetime.manager.reset();
    rejects([&]{lifetime.run(lost);},"Expired captured invoking manager was replaced from another runtime owner");
    check(lost.faulted() && lifetime.messenger.buffer().vectors().empty(),"Captured manager loss published a query prefix");
    Fixture math;math.provider([](NativeMessenger&,const MessengerDelivery&){throw std::runtime_error("Unreached query");return MessengerCallResult{};});
    ModelHitLandInvocation invalid_math(math.ram,"HITWALL",math.manager);
    // Tiny distinct X reaches normalization after publishing its first vector.
    math.position({std::bit_cast<float>(std::uint32_t{1}),0,0});
    rejects([&]{math.run(invalid_math);},"Underflowed original normalization must stay a reached explicit boundary");
    check(invalid_math.faulted() && math.messenger.selected().category=="HITLANDMODEL" &&
        math.messenger.buffer().vectors().size()==1 && math.ram->position==std::array<float,3>{},
        "Normalization failure lost earlier packet prefix or published response state");
    Fixture extent;extent.ram->up_range=std::numeric_limits<float>::quiet_NaN();
    extent.provider([](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    ModelHitLandInvocation bad_extent(extent.ram,"HITWALLFULL",extent.manager);
    rejects([&]{extent.run(bad_extent);},"Reached nonfinite query dimension must reject");
    check(extent.messenger.buffer().vectors().size()==2 && extent.messenger.buffer().floats()==std::vector<float>{3},
        "Dimension failure discarded earlier vector/float appends");
    Fixture floor;floor.provider([&](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().clear();live.buffer().add_bool(1);live.buffer().add_bool(2);live.buffer().add_bool(1);
        live.buffer().add_vector({5,10,6});floor.ram->node.reset();return MessengerCallResult{true,1,{}};
    });
    ModelHitLandInvocation missing_node(floor.ram,"HITFLOOR",floor.manager);
    rejects([&]{floor.run(missing_node);},"Combined floor adjustment accepted its later missing node");
    check(floor.ram->land_position==std::array<float,3>{5,10,6} && floor.ram->land_position_ok==1 &&
        floor.messenger.buffer().booleans()==std::vector<std::uint8_t>({1,2}),"Later adjustment fault erased completed land/response stores");
    Fixture recursive;ModelHitLandInvocation outer(recursive.ram,"HITWALLFULL",recursive.manager);
    recursive.provider([&](NativeMessenger& live,const MessengerDelivery&) {
        try{recursive.run(outer);}catch(const std::exception&){}
        live.buffer().clear();live.buffer().add_bool(1);live.buffer().add_vector({8,9,10});return MessengerCallResult{true,1,{}};
    });
    rejects([&]{recursive.run(outer);},"Caught recursive recipient entry must poison the active continuation");
    check(outer.faulted() && recursive.ram->position==std::array<float,3>{} &&
        recursive.ram->previous_position==std::array<float,3>{0,100,0},"Caught recursion published uncertain response state");
}
}
int main() {
    try {
        dispatch_and_adjust_gates();equal_wall_consumes_existing_packet();retained_wall_query_and_live_node();
        full_wall_response_conditions();floor_results_and_adjustment();failure_prefixes_and_recursion();
        std::cout<<"HITLANDDELIVER handler checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"HITLANDDELIVER after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
