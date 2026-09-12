#include "dw4/model_subcommand_update.hpp"
#include "dw4/model_player_work.hpp"
#include "dw4/model_scene.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid actual subcommand operation completed.");
}
struct Fixture {
    std::shared_ptr<ModelSceneManager> manager=std::make_shared<ModelSceneManager>();
    std::shared_ptr<ModelSceneInfo> parent=std::make_shared<ModelSceneInfo>();
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSceneOne> one=std::make_shared<ModelSceneOne>();
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    std::shared_ptr<ModelSubcommandUpdateServices> services=std::make_shared<ModelSubcommandUpdateServices>();
    ModelSubcommandQueue queue{work->subcommands};
    Fixture() {
        manager->system=std::make_shared<ModelSceneSystem>();manager->system->frame_time_f=1;
        parent->name="source";ram->number=3;ram->parent=parent;ram->manager=manager;
        one->name="ONE";one->work=work;ensure_model_data(*ram)->append(one);
    }
};
ModelEffectSubcommand effect(bool retained=false) {
    return {0,"effect","SQ_OPEN",{1,2,3},{4,5,6},retained,true};
}
void idle_and_reached_gates() {
    auto ram=std::make_shared<ModelSceneRam>();auto wrapper=std::make_shared<ModelSubcommandWrapper>();
    ModelSubcommandUpdateInvocation idle(ram,wrapper);
    check(idle.resume().complete() && idle.completed() && !ram->data,
        "Empty actual wrapper must not read manager/system or allocate RAM data.");
    check(idle.resume().complete(),"Completed invocation must not start another frame pass.");
    rejects([&]{ModelSubcommandUpdateInvocation invalid(ram,{});});
    rejects([&]{ModelSubcommandUpdateInvocation invalid({},wrapper);});

    Fixture delayed;delayed.ram->data.reset();delayed.ram->parent.reset();delayed.queue.append_sound(3,"later");
    ModelSubcommandUpdateInvocation first(delayed.ram,delayed.work->subcommands);
    check(first.resume().complete() && delayed.queue.entries()[0].elapsed==1 && !delayed.ram->data,
        "Delayed sound must only consume actual frame delta, not parent or ONE.");
    delayed.manager->system->frame_time_f=2;
    ModelSubcommandUpdateInvocation second(delayed.ram,delayed.work->subcommands);
    check(second.resume().stop==ModelSubcommandStop::sound_play_required && delayed.queue.entries()[0].started,
        "Exact elapsed/delay equality must publish started before a missing sound provider.");
    check(!second.captured_scope().parent && !second.captured_scope().one && !second.captured_scope().work,
        "Sound start must not invent parent/ONE/work captures.");
    delayed.manager->system.reset();
    delayed.services->play_sound=[](const auto&,const auto&){return ModelSoundPlayResult{true,0,0};};
    check(second.resume(delayed.services).complete() && delayed.queue.entries().empty(),
        "Resuming sound must not read or advance the removed system delta again.");

    Fixture null_system;null_system.queue.append_sound(0,"sound");null_system.manager->system.reset();
    ModelSubcommandUpdateInvocation invalid(null_system.ram,null_system.work->subcommands);
    rejects([&]{invalid.resume();});
    check(invalid.faulted() && null_system.queue.entries()[0].elapsed==0 && !null_system.queue.entries()[0].started,
        "Missing system must fault at the reached delta read before elapsed publication.");
}
void per_entry_manager_and_original_count() {
    Fixture f;f.queue.append_sound(0,"first");f.queue.append_sound(0,"second");
    auto replacement=std::make_shared<ModelSceneManager>();replacement->system=std::make_shared<ModelSceneSystem>();
    replacement->system->frame_time_f=4;std::vector<std::string> calls;
    const auto first_owner=f.queue.entry_owner(0),second_owner=f.queue.entry_owner(1);
    f.services->play_sound=[&](const auto& scope,const auto& name) {
        calls.push_back(*name);
        if(*name=="first") {
            check(scope.manager.lock()==f.manager,"First sound must retain its own manager capture.");
            f.ram->manager=replacement;f.queue.append_sound(0,"appended");
        } else check(scope.manager.lock()==replacement,"Next entry must use the RAM's newly installed manager.");
        return ModelSoundPlayResult{true,0,0};
    };
    ModelSubcommandUpdateInvocation call(f.ram,f.work->subcommands);
    check(call.resume(f.services).complete() && calls==std::vector<std::string>{"first","second"},
        "Actual pass must preserve the original count and visit each original entry once.");
    check(first_owner->elapsed==1 && second_owner->elapsed==4,
        "Each entry must read its own current manager/system frame delta.");
    check(f.queue.entries().size()==1 && f.queue.entries()[0].elapsed==0 && !f.queue.entries()[0].started,
        "Callback append must wait for the next original wrapper pass.");
    replacement->system->frame_time_f=-3;
    ModelSubcommandUpdateInvocation negative(f.ram,f.work->subcommands);
    check(negative.resume().complete() && f.queue.entries()[0].elapsed==-3,
        "Actual system delta must preserve negative finite values.");
}
void effect_read_order_and_retained_packet() {
    Fixture none;none.queue.append_effect(effect());none.ram->parent.reset();none.ram->data.reset();
    none.services->find_effect_space=[&](const auto& scope,const auto& name) {
        check(name=="effect" && !scope.parent && !scope.one && !scope.work && !none.ram->data,
            "FIND_SPACE must precede source-name dereference and current ONE preparation.");
        return ModelEffectSpaceResult{true,{}};
    };
    ModelSubcommandUpdateInvocation absent(none.ram,none.work->subcommands);
    check(absent.resume(none.services).complete() && none.queue.entries().empty() && !none.ram->data,
        "Completed no-space result must remove effect without ONE or parent reads.");

    Fixture f;f.queue.append_effect(effect());const auto wrapper=f.work->subcommands;
    auto next_parent=std::make_shared<ModelSceneInfo>();next_parent->name="new-parent";
    auto next_one=std::make_shared<ModelSceneOne>();next_one->name="ONE";next_one->work=make_model_player_work();
    next_one->work->hit_parent_name="attachment";next_one->work->hit_parent_ram=17;
    auto next_manager=std::make_shared<ModelSceneManager>();next_manager->system=std::make_shared<ModelSceneSystem>();
    unsigned finds{},generates{};
    f.services->find_effect_space=[&](const auto& scope,const auto&) {
        ++finds;check(scope.parent==f.parent && scope.manager.lock()==f.manager && !scope.one,
            "Effect start must capture manager then parent before FIND_SPACE.");
        f.parent->name="renamed-captured";f.ram->parent=next_parent;f.ram->number=9;f.ram->manager=next_manager;
        f.ram->data=std::make_shared<ModelSceneList>();f.ram->data->append(next_one);
        f.work->subcommands=std::make_shared<ModelSubcommandWrapper>();
        return ModelEffectSpaceResult{true,6};
    };
    f.services->generate_effect=[&](const auto& scope,const ModelEffectGenerateRequest& request) {
        ++generates;
        check(scope.manager.lock()==f.manager && scope.parent==f.parent && scope.one==next_one && scope.work==next_one->work,
            "GENERATION must retain old manager/parent but resolve fresh post-FIND_SPACE ONE/work.");
        check(request.ram_no==6 && request.source==ModelSubcommandOwner{"renamed-captured",9} &&
            request.attachment==ModelSubcommandOwner{"attachment",17},"Actual effect packet used an eager frame binding.");
        check(request.model_name=="effect" && request.sequence_name=="SQ_OPEN" && request.offset==std::array<float,3>{1,2,3} &&
            request.rotation==std::array<float,3>{4,5,6} && request.position_lock,"Actual generation payload changed.");
        if(generates==1) {
            f.ram->number=22;f.parent->name="later";next_one->work->hit_parent_name="later-attachment";return false;
        }
        return true;
    };
    ModelSubcommandUpdateInvocation call(f.ram,wrapper);
    check(call.resume(f.services).stop==ModelSubcommandStop::effect_generate_required,
        "Unavailable generation must retain the completed find and packet phases.");
    check(f.queue.entries()[0].elapsed==1 && f.queue.entries()[0].started &&
        std::get<ModelEffectSubcommandState>(f.queue.entries()[0].value).ram_no==6,
        "Elapsed/start/selected RAM prefixes must remain published while generation waits.");
    check(call.resume().complete() && finds==1 && generates==2 && f.queue.entries().empty() &&
        call.captured_wrapper()==wrapper && f.work->subcommands!=wrapper,
        "Retry must retain original wrapper, manager, source packet and completed FIND_SPACE.");
}
void fresh_effects_and_checked_prefix() {
    Fixture f;f.queue.append_effect(effect());f.queue.append_effect(effect());
    unsigned finds{},generates{};auto next=make_model_player_work();next->hit_parent_name="";next->hit_parent_ram=31;
    f.services->find_effect_space=[&](const auto&,const auto&) {++finds;return ModelEffectSpaceResult{true,4};};
    f.services->generate_effect=[&](const auto& scope,const auto& request) {
        ++generates;
        if(generates==1) {
            check(scope.work==f.work && request.attachment==ModelSubcommandOwner{"source",3},
                "Null attachment must use the freshly read captured parent/RAM fallback.");
            f.one->work=next;f.manager->system->frame_time_f=2;
        } else check(scope.work==next && request.attachment==ModelSubcommandOwner{"",31},
            "Each effect must freshly resolve work; a non-null empty attachment stays empty.");
        return true;
    };
    ModelSubcommandUpdateInvocation call(f.ram,f.work->subcommands);
    check(call.resume(f.services).complete() && finds==2 && generates==2,"Actual effects must not share a prepared work capture.");

    Fixture invalid;invalid.queue.append_sound(0,"first");invalid.queue.append_sound(0,"overflow");
    const auto entry=invalid.queue.entry_owner(0);unsigned sounds{};
    invalid.services->play_sound=[&](const auto&,const auto&) {
        ++sounds;invalid.manager->system->frame_time_f=std::numeric_limits<float>::infinity();
        return ModelSoundPlayResult{true,0,0};
    };
    ModelSubcommandUpdateInvocation checked(invalid.ram,invalid.work->subcommands);
    rejects([&]{checked.resume(invalid.services);});
    check(sounds==1 && entry->elapsed==1 && entry->started && invalid.queue.entries().size()==1 &&
        invalid.queue.entries()[0].elapsed==0 && checked.faulted(),
        "A later checked nonfinite boundary must preserve earlier committed entry effects/removal.");

    Fixture no_work;no_work.queue.append_effect(effect());no_work.one->work.reset();
    no_work.services->find_effect_space=[](const auto&,const auto&){return ModelEffectSpaceResult{true,7};};
    ModelSubcommandUpdateInvocation missing(no_work.ram,no_work.work->subcommands);
    rejects([&]{missing.resume(no_work.services);});
    check(missing.faulted() && no_work.queue.entries()[0].started &&
        std::get<ModelEffectSubcommandState>(no_work.queue.entries()[0].value).ram_no==7,
        "Null fresh effect work must fail only after the actual start and selected-RAM prefixes.");
}
void hitbox_scope_and_expiry() {
    Fixture f;f.queue.append_hitbox({1,0,2,{1,2,3},{4,5,6}});unsigned calls{};
    f.services->check_hitbox=[&](const auto& scope,const auto& request) {
        ++calls;check(scope.parent==f.parent && scope.one==f.one && scope.work==f.work && scope.manager.lock()==f.manager,
            "Actual hitbox check must capture its own current parent/ONE/manager/work.");
        check(request.context.source.model_name.empty() && !request.context.attachment,
            "Actual HITBOX must leave late source/attachment reads to the real geometry/dispatch provider.");
        return calls!=1;
    };
    ModelSubcommandUpdateInvocation start(f.ram,f.work->subcommands);
    check(start.resume(f.services).stop==ModelSubcommandStop::hitbox_check_required,
        "Incomplete actual hitbox operation must retain its provider continuation.");
    rejects([&]{f.queue.cleanup();});
    check(!start.faulted(),"An invoked incomplete hitbox must reject cleanup without poisoning its retained operation.");
    check(start.resume().complete() && f.queue.entries()[0].elapsed==1 && calls==2,
        "Hitbox retry must neither advance time nor re-resolve its captured helper scope.");
    f.ram->data.reset();
    ModelSubcommandUpdateInvocation expired(f.ram,f.work->subcommands);
    check(expired.resume().complete() && f.queue.entries().empty() && !f.ram->data,
        "Expired hitbox update must remove before another ONE or check-provider access.");

    Fixture malformed;malformed.queue.append_hitbox({1,0,10,{},{}});malformed.ram->data.reset();
    ModelSubcommandUpdateInvocation invalid(malformed.ram,malformed.work->subcommands);
    rejects([&]{invalid.resume();});
    check(invalid.faulted() && malformed.ram->data && malformed.queue.entries()[0].started,
        "Missing hitbox ONE must retain ensure-list and started prefixes at the reached check.");

    Fixture unavailable;unavailable.queue.append_hitbox({1,0,10,{},{}});
    ModelSubcommandUpdateInvocation no_provider(unavailable.ram,unavailable.work->subcommands);
    check(no_provider.resume().stop==ModelSubcommandStop::hitbox_check_required &&
        no_provider.captured_scope().one==unavailable.one && unavailable.queue.entries()[0].started,
        "Absent hitbox provider must still retain the original ONE/started prefix.");
    check(unavailable.queue.cleanup().complete() && unavailable.queue.entries().empty(),
        "A missing hitbox provider must not claim an invoked partial operation that prohibits cleanup.");
    rejects([&]{no_provider.resume();});
}
void continuation_fault_and_lifetime() {
    Fixture f;f.queue.append_sound(0,"sound");ModelSubcommandUpdateInvocation call(f.ram,f.work->subcommands);
    check(call.resume(f.services).stop==ModelSubcommandStop::sound_play_required,"Absent provider must suspend.");
    auto replacement=std::make_shared<ModelSubcommandUpdateServices>();
    rejects([&]{call.resume(replacement);});check(call.faulted(),"Replacing a retained provider owner must fault the endpoint.");

    Fixture recursion;recursion.queue.append_sound(0,"sound");ModelSubcommandUpdateInvocation recursive(recursion.ram,recursion.work->subcommands);
    recursion.services->play_sound=[&](const auto&,const auto&) {
        rejects([&]{recursive.resume();});return ModelSoundPlayResult{true,42,4};
    };
    rejects([&]{recursive.resume(recursion.services);});
    check(recursive.faulted() && recursion.queue.faulted() && recursion.queue.entries()[0].started &&
        std::get<ModelSoundSubcommandState>(recursion.queue.entries()[0].value).handle==0,
        "Caught recursion must poison both invocation and wrapper before service result publication.");
    rejects([&]{recursion.queue.resume();});

    Fixture facade;facade.queue.append_sound(0,"sound");ModelSubcommandUpdateInvocation outer(facade.ram,facade.work->subcommands);
    facade.services->play_sound=[&](const auto&,const auto&) {
        rejects([&]{facade.queue.resume();});return ModelSoundPlayResult{true,0,0};
    };
    rejects([&]{outer.resume(facade.services);});
    check(outer.faulted() && facade.queue.faulted() && facade.queue.entries().size()==1,
        "Caught recursion through another facade must not silently finish an actual pass.");

    Fixture interrupted;interrupted.queue.append_sound(0,"sound");
    ModelSubcommandUpdateInvocation old(interrupted.ram,interrupted.work->subcommands);old.resume();
    check(interrupted.queue.cleanup().complete(),"Original cleanup may explicitly interrupt an unfinished update.");
    rejects([&]{old.resume();});check(old.faulted(),"An interrupted update must not claim its original continuation completed.");

    std::weak_ptr<ModelSceneRam> weak_ram;std::weak_ptr<ModelPlayerWork> weak_work;
    auto retained=std::make_shared<ModelSubcommandWrapper>();
    {
        Fixture lifetime;lifetime.work->subcommands=retained;ModelSubcommandQueue queue(retained);queue.append_effect(effect());
        lifetime.services->find_effect_space=[](const auto&,const auto&){return ModelEffectSpaceResult{true,8};};
        weak_ram=lifetime.ram;weak_work=lifetime.work;
        ModelSubcommandUpdateInvocation pending(lifetime.ram,retained);
        check(pending.resume(lifetime.services).stop==ModelSubcommandStop::effect_generate_required,
            "Lifetime fixture must retain actual work at a generation pause.");
    }
    check(weak_ram.expired() && weak_work.expired(),"Wrapper access binding must not strongly retain its containing RAM/work.");
    ModelSubcommandQueue orphan(retained);rejects([&]{orphan.resume();});
    check(orphan.faulted(),"A pass whose actual invocation was abandoned must reject stale access and retain fault state.");
}
}
int main() {
    try {
        idle_and_reached_gates();per_entry_manager_and_original_count();effect_read_order_and_retained_packet();
        fresh_effects_and_checked_prefix();hitbox_scope_and_expiry();continuation_fault_and_lifetime();
        std::cout<<checks<<" actual model subcommand update checks passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
