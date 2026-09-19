#include "dw4/system_frame.hpp"
#include "dw4/application_frame.hpp"
#include "dw4/game_clock.hpp"
#include "dw4/serial_scheduler.hpp"

#include <bit>
#include <iostream>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& call,const char* reason) {
    ++checks;try {call();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
MessengerCallResult done() {return {true,0,{}};}
std::shared_ptr<SystemFrameState> system() {
    auto state=std::make_shared<SystemFrameState>();state->context=std::make_shared<ModelSceneContext>();
    state->context->world_time=16666667;return state;
}
std::shared_ptr<SystemFrameServices> healthy() {
    auto services=std::make_shared<SystemFrameServices>();services->sample_reset=done;
    services->storage=std::make_shared<SystemStorageServices>();
    services->storage->drive_status=[] {return SystemStorageValueResult{true,0,{}};};return services;
}
void defaults_and_raw_sample() {
    SystemFrameState initial;
    check(initial.reset_enabled==1 && initial.reset_now==0 && initial.reset_last==0 && initial.reset_release==0 &&
        initial.frame_time_f==1 && !initial.stage.next_stage && !initial.stage.boot_directory,
        "System reflected reset/timing/stage defaults changed.");
    for(const auto previous:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{255}})
        for(const auto sample:{0u,1u,0x100u,0x80000000u,0xffffffffu}) {
            auto state=system();state->reset_enabled=0;state->reset_now=previous;state->reset_last=73;
            auto services=healthy();services->sample_reset=[sample] {return MessengerCallResult{true,sample,{}};};
            SystemFrameInvocation frame(state);
            check(frame.resume(services).completed && state->reset_last==previous && state->reset_now==(sample!=0) &&
                state->reset_release==((previous&(previous^std::uint8_t(sample!=0)))!=0),
                "Original reset normalization or raw falling-edge expression changed.");
            check(state->frame_time==16666667 && state->frame_time_f==1 && state->saved_world_time==16666667,
                "Inactive system or reset-disabled state skipped the original timing consumer.");
        }
    auto state=system();state->reset_enabled=0;state->reset_release=255;state->reset_now=13;state->reset_last=27;
    auto services=healthy();services->sample_reset={};SystemFrameInvocation latched(state);
    check(latched.resume(services).completed && state->reset_release==255 && state->reset_now==13 && state->reset_last==27,
        "Latched release resampled or normalized retained raw bytes.");
}
void sample_prefix_and_live_fields() {
    auto state=system();state->reset_enabled=0;state->reset_now=9;auto services=healthy();unsigned samples{};bool ready{};
    services->sample_reset=[&] {
        ++samples;
        if(!ready) {state->reset_now=7;state->reset_last=13;return MessengerCallResult{false,0,"Prepared reset sample pending."};}
        return MessengerCallResult{true,0x100,{}};
    };
    SystemFrameInvocation frame(state);
    check(!frame.resume(services).completed && state->reset_now==7 && state->reset_last==13 && state->saved_world_time==0,
        "Pending reset sample lost the prior last-byte publication or advanced timing.");
    state->reset_now=17;services->sample_reset=[]()->MessengerCallResult {throw std::runtime_error("Replacement sample was called.");};ready=true;
    check(frame.resume(services).completed && samples==2 && state->reset_now==1 && state->reset_last==13 && state->reset_release==1,
        "Reset resumption reselected the callback, recopied current, or ignored the live last byte.");
    state=system();state->reset_now=1;state->reset_enabled=1;services=healthy();
    services->sample_reset=[&] {state->reset_enabled=0;state->reset_last=0;state->reset_release=255;return done();};
    SystemFrameInvocation mutated(state);
    check(mutated.resume(services).completed && state->reset_release==0 && state->reset_enabled==0,
        "Sampler field mutations were ignored or the reset latch was not overwritten in original order.");
    state=system();state->reset_now=21;services=healthy();services->sample_reset=[]()->MessengerCallResult {throw std::runtime_error("Sample failed.");};
    SystemFrameInvocation failed(state);
    rejects([&] {failed.resume(services);},"Sampling exception escaped the system fault contract.");
    check(state->reset_last==21 && state->reset_now==21 && state->reset_release==0 && state->saved_world_time==0,
        "Sample failure rolled back last or published later timing fields.");
}
void reset_order_and_early_return() {
    for(const auto disk:{0u,1u,0x100u,0x80000000u,0xffffffffu}) {
        auto state=system();state->context.reset();state->reset_enabled=0;state->reset_now=1;state->is_first_time=255;
        state->stage.next_stage=std::make_shared<const std::string>("pending.igb");
        auto services=std::make_shared<SystemFrameServices>();std::vector<unsigned> calls;
        services->sample_reset=[&] {calls.push_back(1);state->reset_enabled=255;return done();};
        services->set_black=[&](std::uint32_t value) {check(value==1,"Reset used another display-blank argument.");calls.push_back(2);state->reset_enabled=0;state->reset_release=0;return done();};
        services->flush_video=[&] {calls.push_back(3);return done();};
        services->wait_retrace=[&] {calls.push_back(4);return done();};
        services->check_disk=[&] {calls.push_back(5);return MessengerCallResult{true,disk,{}};};
        services->reset_system=[&](std::uint32_t kind,std::uint32_t code,std::uint32_t force) {
            calls.push_back(6);check(kind==(disk==0) && code==0 && force==0,"Reset arguments did not use the full disk return word.");return done();
        };
        SystemFrameInvocation frame(state);
        check(frame.resume(services).completed && calls==std::vector<unsigned>{1,2,3,4,5,6} && state->saved_world_time==0 &&
            state->is_first_time==255 && state->stage.next_stage,
            "Admitted reset reordered callbacks, rechecked mutated gates, or entered timing/stage/tail.");
        check(frame.resume(services).completed && calls.size()==6,"Completed reset invocation repeated its platform effects.");
    }
    auto state=system();state->reset_release=1;auto services=std::make_shared<SystemFrameServices>();
    unsigned black{},flush{},retrace{},disk{},reset{};bool display_ready{},reset_ready{};
    services->set_black=[&](std::uint32_t) {++black;return done();};
    services->flush_video=[&] {++flush;return done();};
    services->wait_retrace=[&] {++retrace;return MessengerCallResult{display_ready,0,{}};};
    services->check_disk=[&] {++disk;return MessengerCallResult{true,0x100,{}};};
    services->reset_system=[&](std::uint32_t kind,std::uint32_t,std::uint32_t) {
        ++reset;check(kind==0,"Pending reset did not retain the completed disk decision.");return MessengerCallResult{reset_ready,0,{}};
    };
    SystemFrameInvocation frame(state);
    check(!frame.resume(services).completed && black==1 && flush==1 && retrace==1 && disk==0,
        "Pending retrace failed to retain the completed display prefix.");
    display_ready=true;check(!frame.resume(services).completed && black==1 && flush==1 && retrace==2 && disk==1 && reset==1,
        "Reset resumption repeated earlier display effects.");
    services->check_disk=[] {return done();};reset_ready=true;
    check(frame.resume(services).completed && disk==1 && reset==2,"Pending reset resampled disk or replayed preceding operations.");
}
void actual_stage_and_storage_suspension() {
    auto state=system();state->is_active=2;state->is_first_time=255;
    state->stage.next_stage=std::make_shared<const std::string>("prepared.igb");
    auto services=healthy();unsigned samples{},polls{};services->sample_reset=[&] {++samples;return done();};
    services->storage->drive_status=[&] {++polls;return SystemStorageValueResult{true,0,{}};};
    SystemFrameInvocation frame(state);
    check(!frame.resume(services).completed && state->saved_world_time==16666667 && state->frame_time_f==1 && samples==1 && polls==0,
        "Missing actual stage operation skipped the timing prefix or reached storage early.");
    state->context->world_time=500000001;
    auto pool=std::make_shared<ModelSceneData>();auto directory=std::make_shared<SystemStageDirectory>();directory->loading=1;
    services->stage=std::make_shared<SystemStageServices>();services->stage->loader=std::make_shared<SystemStageLoader>();
    services->stage->use_object_pool=1;services->stage->string_pool=pool;
    std::vector<SystemStageServiceKind> operations;
    services->stage->factory=[&](const SystemStageRequest& request) {
        auto operation=std::make_shared<SystemStageOperation>();operation->object_result=pool;operation->directory_result=directory;
        operation->resume=[&,kind=request.kind] {operations.push_back(kind);return done();};return operation;
    };
    check(frame.resume(services).completed && samples==1 && polls==1 && state->saved_world_time==16666667 && state->frame_time_f==1 &&
        !state->stage.next_stage && state->stage.boot_directory==directory && state->is_first_time==0,
        "Stage resumption replayed timing/sample or failed the live any-nonzero active tail.");
    check(operations==std::vector<SystemStageServiceKind>{SystemStageServiceKind::object_pool,SystemStageServiceKind::object_pool,
        SystemStageServiceKind::prepare_loading,SystemStageServiceKind::load_directory},
        "Main update bypassed actual stage request operations or completed the new directory immediately.");
    state=system();services=healthy();samples=polls=0;bool ready{};
    services->sample_reset=[&] {++samples;return done();};
    services->storage->drive_status=[&] {
        ++polls;state->is_active=2;state->is_first_time=255;
        return SystemStorageValueResult{ready,0,"Prepared storage status pending."};
    };
    SystemFrameInvocation storage(state);
    check(!storage.resume(services).completed && samples==1 && polls==1 && state->saved_world_time==16666667 && state->is_first_time==255,
        "Pending actual storage helper lost timing prefix or ran the final gate early.");
    state->context->world_time=100;ready=true;
    services->storage->drive_status=[]()->SystemStorageValueResult {throw std::runtime_error("Replacement storage poll was called.");};
    check(storage.resume(services).completed && samples==1 && polls==2 && state->saved_world_time==16666667 && state->is_first_time==0,
        "Storage resumption changed its selected provider, repeated timing, or ignored live tail fields.");
}
void actual_helper_tail_and_faults() {
    for(const auto active:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{128},std::uint8_t{255}})
        for(const auto phase:{-1,0,4,255}) {
            auto state=system();state->is_active=active;state->is_first_time=255;state->is_frame_skip=173;state->storage.error_phase=phase;
            auto services=healthy();SystemFrameInvocation frame(state);
            check(frame.resume(services).completed && state->is_first_time==(active!=0 && phase<=0 ? 0:255) && state->is_frame_skip==173,
                "Actual helper return or final any-nonzero active/first-time gate changed.");
        }
    auto state=system();state->context.reset();state->reset_enabled=0;auto services=healthy();unsigned samples{};
    services->sample_reset=[&] {++samples;return done();};SystemFrameInvocation absent_context(state);
    rejects([&] {absent_context.resume(services);},"Inactive system bypassed its reached missing timing context.");
    check(samples==1 && state->frame_time==0,"Missing context fault occurred before sampling or after timing writes.");
    state=system();services=healthy();services->sample_reset=[] {return MessengerCallResult{false,0,{}};};
    SystemFrameInvocation first(state);check(!first.resume(services).completed,"Prepared main operation did not suspend.");
    SystemFrameInvocation overlap(state);rejects([&] {overlap.resume(services);},"Concurrent system update was accepted.");
    check(!state->faulted(),"Overlap rejection invalidated the existing system owner.");
    rejects([&] {first.resume(healthy());},"Pending system changed its captured service owner.");
    state=system();services=healthy();SystemFrameInvocation recursive(state);
    services->sample_reset=[&] {try {recursive.resume(services);}catch(const SystemFrameError&) {}return done();};
    rejects([&] {recursive.resume(services);},"Caught recursive system entry escaped the outer fault check.");
    state=system();{SystemFrameInvocation abandoned(state);check(!abandoned.resume().completed,"Missing reset service completed silently.");}
    SystemFrameInvocation after_abandon(state);rejects([&] {after_abandon.resume(healthy());},"Abandoned system prefix restarted.");
}
void application_scheduler_connection() {
    unsigned clock_samples{},reset_samples{};bool ready{};
    auto clock=std::make_shared<NativeGameClock>([&] {return std::uint64_t(++clock_samples)*16666667;});clock->start();
    auto application=std::make_shared<ApplicationFrameState>(clock);initialize_application_frame_factor(*application);
    struct BindingCleanup {
        std::shared_ptr<ApplicationFrameState> owner;
        ~BindingCleanup() {owner->scheduler={};owner->dispatcher={};}
    } cleanup{application}; // Break the deliberately prepared registry/context cycle on every exit.
    application->dispatcher=done;
    auto state=std::make_shared<SystemFrameState>();state->context=application;state->is_active=2;state->is_first_time=1;
    auto services=healthy();services->sample_reset=[&] {++reset_samples;return MessengerCallResult{ready,0,{}};};
    auto scheduler=std::make_shared<SerialSchedulerState>();
    scheduler->registered->entries->append(std::make_shared<SerialSchedulerManager>(make_system_frame_update(state,services)));
    application->scheduler=make_serial_scheduler_update(scheduler);
    ApplicationFrameInvocation first(application);
    check(!first.resume().completed && clock_samples==2 && reset_samples==1 && state->saved_world_time==0,
        "Scheduled pending system changed the already published application time prefix.");
    ready=true;check(first.resume().completed && clock_samples==2 && reset_samples==2 && state->frame_time_f==1 &&
        state->saved_world_time==application->world_time && state->is_first_time==0 && scheduler->cache->count==0,
        "Actual application/scheduler/system resumption resampled time or skipped cleanup/tail.");
    ApplicationFrameInvocation second(application);
    check(second.resume().completed && clock_samples==3 && reset_samples==3 && state->frame_time_f==1,
        "Reusable scheduled system binding did not produce the next actual update.");
}
} // namespace
int main() {
    try {
        defaults_and_raw_sample();sample_prefix_and_live_fields();reset_order_and_early_return();
        actual_stage_and_storage_suspension();actual_helper_tail_and_faults();application_scheduler_connection();
        std::cout<<"Passed "<<checks<<" native system frame checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"System frame test failed: "<<error.what()<<'\n';return 1;}
}
