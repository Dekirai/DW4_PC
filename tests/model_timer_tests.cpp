#include "dw4/model_timer.hpp"
#include "dw4/model_scene.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const char* message) {
    ++checks;try {operation();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
bool same(const MotionTimer& a,const MotionTimer& b) {
    return std::bit_cast<std::uint32_t>(a.time_scale)==std::bit_cast<std::uint32_t>(b.time_scale) &&
        a.time_bias==b.time_bias && a.end_time==b.end_time && a.saved_world_time==b.saved_world_time &&
        a.previous_time==b.previous_time && std::bit_cast<std::uint32_t>(a.cached_frames)==std::bit_cast<std::uint32_t>(b.cached_frames);
}
std::shared_ptr<ModelSceneTimer> append_timer(const std::shared_ptr<ModelSceneRam>& ram,std::string name="KEIKATIMER") {
    auto timer=std::make_shared<ModelSceneTimer>();timer->name=std::move(name);ensure_model_data(*ram)->append(timer);return timer;
}
void binding_and_existence() {
    static_assert(!std::is_copy_constructible_v<ModelRamTimerAccess> && !std::is_move_constructible_v<ModelRamTimerAccess>);
    auto ram=std::make_shared<ModelSceneRam>();unsigned clocks{};
    const auto clock=[&](const ModelSceneRam& actual) {check(&actual==ram.get(),"Clock must receive the actual bound RAM.");++clocks;return std::uint64_t{12};};
    rejects([&]{ModelRamTimerAccess invalid({},clock);},"Null RAM binding must reject.");
    rejects([&]{ModelRamTimerAccess invalid(ram,{});},"Absent real context clock must reject at construction.");
    ModelRamTimerAccess access(ram,clock);
    check(!ram->data && access.ram_owner()==ram && !access.faulted(),"Binding must retain RAM without allocating timer data.");
    check(!access.exists("KEIKATIMER") && ram->data && ram->data->count==0 && clocks==0,
        "Existence must ensure only RAM data and must not read the context clock.");
    const auto list=ram->data;
    auto wrong=std::make_shared<ModelSceneData>();wrong->name="KEIKATIMER";list->append(wrong);
    const auto later=append_timer(ram);
    later->timer.time_scale=std::numeric_limits<float>::quiet_NaN();
    check(access.exists("KEIKATIMER") && clocks==0 && !access.faulted() && ram->data==list && list->count==2,
        "Untyped existence must count a wrong-type first match without touching a later timer.");
    check(!access.exists("keikatimer"),"Actual named existence must preserve exact case.");
    ram->data.reset();
    check(!access.exists("KEIKATIMER") && ram->data!=list && list->count==2 && clocks==0,
        "Each existence helper must resolve the current data-list slot.");
}
void actual_start_and_time() {
    auto ram=std::make_shared<ModelSceneRam>();unsigned clocks{};std::uint64_t world=64u<<18;
    std::shared_ptr<ModelSceneTimer> seen_at_clock;
    ModelRamTimerAccess access(ram,[&](const ModelSceneRam& actual) {
        ++clocks;check(&actual==ram.get(),"Timer start clock must use its actual RAM.");
        seen_at_clock=std::dynamic_pointer_cast<ModelSceneTimer>(find_model_named(actual.data,"KEIKATIMER"));
        check(bool(seen_at_clock),"New timer must already be named and appended before context time is read.");
        return world;
    });
    const auto timer=access.start("KEIKATIMER",-77);
    check(timer==seen_at_clock && timer==find_model_timer(*ram,"KEIKATIMER") && ram->data->count==1 &&
        timer->timer.time_scale==1 && timer->timer.time_bias==-static_cast<std::int64_t>(world) &&
        timer->timer.saved_world_time==world && timer->timer.previous_time==0 && timer->timer.cached_frames==1 &&
        timer->timer.end_time==-77 && clocks==1,"Actual start must initialize the named record at the clock site.");
    auto old_work=make_model_player_work(),new_work=make_model_player_work();
    auto one=std::make_shared<ModelSceneOne>();one->name="ONE";one->work=old_work;ram->data->append(one);one->work=new_work;
    check(access.exists("KEIKATIMER") && find_model_timer(*ram,"KEIKATIMER")==timer && clocks==1,
        "Work replacement must leave actual RAM named timers and clock demand unchanged.");
    timer->timer.time_scale=0.5f;timer->timer.previous_time=1234567;
    timer->timer.cached_frames=std::bit_cast<float>(0x7fc12345u);world=128u<<18;
    check(access.start("KEIKATIMER",901)==timer && ram->data->count==2 && clocks==2 &&
        timer->timer.time_scale==0.5f && timer->timer.previous_time==1234567 && timer->timer.cached_frames==1 &&
        timer->timer.end_time==901 && timer->timer.time_bias==-static_cast<std::int64_t>(64u<<18),
        "Restart must preserve actual scale/previous time and overwrite an unread nonfinite cache.");
    timer->timer.cached_frames=std::bit_cast<float>(0x7fc12345u);world=192u<<18;
    const auto before=timer->timer;const auto value=access.time("KEIKATIMER");
    check(value==static_cast<std::int64_t>(32u<<18) && same(before,timer->timer) && clocks==3,
        "Accumulated time must read current scale/bias only and leave every timer field unchanged.");
}
void helper_clock_scopes() {
    auto ram=std::make_shared<ModelSceneRam>();const auto timer=append_timer(ram);
    std::vector<std::uint64_t> timestamps{1u<<18,64u<<18,1u<<18,128u<<18,0};std::size_t clock_index{};
    ModelRamTimerAccess access(ram,[&](const ModelSceneRam&) {
        if(clock_index>=timestamps.size())throw std::runtime_error("Unexpected extra timer clock read.");
        return timestamps[clock_index++];
    });
    const auto first=access.frames("KEIKATIMER");
    check(clock_index==2 && first==static_cast<float>(64u<<18)/16666667.0f && timer->timer.previous_time==(64u<<18) &&
        timer->timer.saved_world_time==(1u<<18),"Changed frames must read W1 and a fresh W2, then save the earlier W1.");
    timer->timer.time_scale=std::numeric_limits<float>::quiet_NaN();
    timer->timer.cached_frames=std::bit_cast<float>(0x7fc12345u);
    const auto cached=access.frames("KEIKATIMER");
    check(clock_index==3 && std::bit_cast<std::uint32_t>(cached)==0x7fc12345u && !access.faulted(),
        "Matching timestamps must return the live cache without reading scale or calling accumulated time.");
    timer->timer.time_scale=1;timer->timer.time_bias=0;timer->timer.previous_time=0;
    const auto zero=access.frames("KEIKATIMER");
    check(clock_index==5 && std::bit_cast<std::uint32_t>(zero)==0 && timer->timer.previous_time==0 &&
        timer->timer.saved_world_time==(128u<<18),"Zero accumulated delta must publish positive zero and W1 while preserving previous time.");
    auto negative_ram=std::make_shared<ModelSceneRam>();const auto negative=append_timer(negative_ram);
    negative->timer.previous_time=4ll*16666667;
    ModelRamTimerAccess backwards(negative_ram,[](const ModelSceneRam&){return std::uint64_t{1};});
    check(backwards.frames("KEIKATIMER")==-4 && negative->timer.previous_time==0 && negative->timer.saved_world_time==1,
        "Original negative frame deltas must not be clamped to zero.");
}
void current_named_owner_and_lifetime() {
    auto ram=std::make_shared<ModelSceneRam>();const auto original=append_timer(ram);
    original->timer.time_bias=11;original->timer.saved_world_time=9;original->timer.cached_frames=0.25f;
    const auto original_before=original->timer;unsigned clocks{};
    auto access=std::make_shared<ModelRamTimerAccess>(ram,[&](const ModelSceneRam&){++clocks;return std::uint64_t{9};});
    check(access->frames("KEIKATIMER")==0.25f && clocks==1,"Initial timer access must use the current named record.");
    const auto old_data=ram->data;ram->data.reset();const auto replacement=append_timer(ram);
    replacement->timer.time_bias=27;replacement->timer.saved_world_time=9;replacement->timer.cached_frames=0.75f;
    check(access->time("KEIKATIMER")==27 && access->frames("KEIKATIMER")==0.75f && clocks==3 &&
        same(original_before,original->timer) && old_data->values()[0]==original,
        "Later helpers must resolve replacement RAM data without redirecting a retained old timer.");
    replacement->name="OLD";const auto renamed=append_timer(ram);renamed->timer.time_bias=-39;
    check(access->time("KEIKATIMER")==-39 && clocks==4,
        "Actual timer access must observe current name fields and ordered new records.");
    std::weak_ptr<ModelSceneRam> weak=ram;ram.reset();
    check(!weak.expired() && access->ram_owner()==weak.lock(),"Endpoint lifetime must retain the actual RAM independently of its caller.");
    access.reset();check(weak.expired(),"RAM/list/timer ownership must not create an endpoint retention cycle.");
}
void exact_failure_prefixes() {
    for(const auto method:{0,1,2}) {
        auto ram=std::make_shared<ModelSceneRam>();unsigned clocks{};
        auto wrong=std::make_shared<ModelSceneData>();wrong->name="KEIKATIMER";ensure_model_data(*ram)->append(wrong);
        const auto later=append_timer(ram);const auto initial=later->timer;
        ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++clocks;return std::uint64_t{9};});
        rejects([&]{if(method==0)access.start("KEIKATIMER");else if(method==1)access.time("KEIKATIMER");else access.frames("KEIKATIMER");},
            "Wrong-type first timer must remain a checked field-access boundary.");
        check(clocks==(method==2 ? 0u:1u) && access.faulted() && ram->data->count==2 && same(later->timer,initial),
            "Typed field failure must occur on the correct side of the clock read without touching a shadowed later timer.");
        rejects([&]{access.exists("KEIKATIMER");},"A failed timer endpoint must not silently restart its operation prefix.");
    }
    for(const auto method:{1,2}) {
        auto ram=std::make_shared<ModelSceneRam>();unsigned clocks{};
        ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++clocks;return std::uint64_t{9};});
        rejects([&]{if(method==1)access.time("MISSING");else access.frames("MISSING");},"Missing timer field query must reject.");
        check(ram->data && ram->data->count==0 && clocks==(method==1?1u:0u) && access.faulted(),
            "Missing timer queries must preserve list ensure and their original clock/type-check order.");
    }
    auto ram=std::make_shared<ModelSceneRam>();bool appended_before_failure{};
    ModelRamTimerAccess failed(ram,[&](const ModelSceneRam& actual)->std::uint64_t {
        const auto timer=std::dynamic_pointer_cast<ModelSceneTimer>(find_model_named(actual.data,"KEIKATIMER"));
        appended_before_failure=timer && same(timer->timer,MotionTimer{});
        throw std::runtime_error("Unavailable actual manager/context clock.");
    });
    rejects([&]{failed.start("KEIKATIMER",77);},"Unavailable clock must fail the started timer endpoint.");
    check(appended_before_failure && ram->data->count==1 && failed.faulted() &&
        same(find_model_timer(*ram,"KEIKATIMER")->timer,MotionTimer{}),
        "Clock failure must preserve the already appended default timer without publishing restart fields.");
    unsigned bad_scale_clocks{};const auto bad_scale=append_timer(ram,"BAD_SCALE");
    bad_scale->timer.time_scale=std::numeric_limits<float>::infinity();const auto bad_before=bad_scale->timer;
    ModelRamTimerAccess arithmetic(ram,[&](const ModelSceneRam&){++bad_scale_clocks;return std::uint64_t{9};});
    rejects([&]{arithmetic.start("BAD_SCALE",11);},"Unsupported scale arithmetic must remain checked.");
    check(bad_scale_clocks==1 && arithmetic.faulted() && same(bad_scale->timer,bad_before),
        "Bad scale must be read after context time but before restart field publication.");
}
void caught_reentry_and_second_clock_failure() {
    auto ram=std::make_shared<ModelSceneRam>();std::shared_ptr<ModelRamTimerAccess> access;unsigned clocks{};
    access=std::make_shared<ModelRamTimerAccess>(ram,[&](const ModelSceneRam&)->std::uint64_t {
        ++clocks;try {access->exists("KEIKATIMER");}catch(const std::exception&){}
        return 77;
    });
    rejects([&]{access->start("KEIKATIMER",66);},"Caught recursive timer failure must still abort the outer helper.");
    check(access->faulted() && clocks==1 && ram->data->count==1 && same(find_model_timer(*ram,"KEIKATIMER")->timer,MotionTimer{}),
        "Caught reentry must poison the endpoint without publishing after its completed allocation prefix.");
    auto second_ram=std::make_shared<ModelSceneRam>();const auto timer=append_timer(second_ram);timer->timer.previous_time=-7;
    const auto before=timer->timer;clocks=0;
    ModelRamTimerAccess second(second_ram,[&](const ModelSceneRam&)->std::uint64_t {
        if(++clocks==2)throw std::runtime_error("Nested original time clock is unavailable.");return 9;
    });
    rejects([&]{second.frames("KEIKATIMER");},"Second clock failure must not restart the outer frame helper.");
    check(clocks==2 && second.faulted() && same(before,timer->timer),
        "W2 failure must preserve the outer timer before its previous/cache/W1 publication.");
}
}
int main() {
    try {
        binding_and_existence();actual_start_and_time();helper_clock_scopes();current_named_owner_and_lifetime();
        exact_failure_prefixes();caught_reentry_and_second_clock_failure();
        std::cout<<"Model timer checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Model timer after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
