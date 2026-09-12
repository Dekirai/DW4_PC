#include "dw4/model_subcommands.hpp"
#include "dw4/motion_timer.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    ++checks;try{action();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid subcommand operation accepted.");
}
ModelSubcommandContext context() {return {{"source",7},ModelSubcommandOwner{"attached",11}};}
ModelEffectSubcommand effect(bool sequence_dead=true) {
    return {0,"effect","sequence",{1,2,3},{4,5,6},sequence_dead,true};
}
void defaults_delay_and_results() {
    static_assert(!std::is_copy_constructible_v<ModelSubcommandQueue>);
    ModelSubcommandQueue queue;
    queue.append_sound(6,"footstep"); queue.append_sound(-1,std::nullopt);
    check(queue.entries().size()==2 && !queue.entries()[0].started && queue.entries()[0].elapsed==0,
        "Append must own separate unstarted entries with zero elapsed frames.");
    std::vector<std::optional<std::string>> names;
    ModelSubcommandServices services;
    services.play_sound=[&](const auto& name) {names.push_back(name);return ModelSoundPlayResult{true,0,4};};
    check(queue.update(3,context(),services).complete() && names.size()==1 && !names[0],
        "Negative-delay null sound starts first; unavailable and invalid handles differ.");
    check(queue.entries().size()==1 && queue.entries()[0].elapsed==3,"Positive delay remains pending.");
    queue.update(2.5f,context(),services);
    check(names.size()==1,"Subcommands must not round a fractional delay upward.");
    queue.update(0.5f,context(),services);
    check(names.size()==2 && names[1]=="footstep" && queue.entries().empty(),"Equality starts on the sixth cached frame.");
    queue.append_sound(6,"uncapped");queue.update(10,context(),services);
    check(names.size()==3 && names.back()=="uncapped" && queue.entries().empty(),
        "System frame delta must not acquire the separate motion timer's three-frame cap.");
    for(const auto result:{ModelSoundPlayResult{true,0,4},
                          ModelSoundPlayResult{true,std::numeric_limits<std::uint64_t>::max(),4},
                          ModelSoundPlayResult{true,0x100000002ULL,3}}) {
        queue.append_sound(0,""); services.play_sound=[&](const auto&){return result;};
        queue.update(0,context(),services);
        check(queue.entries().empty(),"Completed original sound failure/non-type4 must not retry.");
    }
}
void pending_order_and_cleanup() {
    ModelSubcommandQueue queue;
    const auto first=queue.append_sound(0,"first"),second=queue.append_sound(0,"second");
    const auto third=queue.append_sound(0,"third");
    std::vector<std::string> played; bool unavailable=true;
    ModelSubcommandServices services;
    services.play_sound=[&](const auto& name) {
        if(*name=="second" && unavailable) return ModelSoundPlayResult{};
        played.push_back(*name);return ModelSoundPlayResult{true,static_cast<std::uint64_t>(played.size()+100),4};
    };
    const auto pending=queue.update(1,context(),services);
    check(pending.stop==ModelSubcommandStop::sound_play_required && pending.entry_id==second && queue.pending(),
        "Unavailable sound must retain its exact pass position.");
    check(played==std::vector<std::string>{"first"} && queue.entries()[0].id==first &&
        queue.entries()[1].started && !queue.entries()[2].started,"Only the committed ordered prefix advances.");
    rejects([&]{queue.update(2,context(),services);});
    unavailable=false; queue.resume(services);
    check(played==std::vector<std::string>{"first","second","third"} && !queue.pending(),
        "Resume must not replay the earlier sound.");
    for(const auto& entry:queue.entries())check(entry.elapsed==1,"Resume must not add elapsed frames again.");
    queue.update(-0.5f,context(),services);
    check(played.size()==3 && queue.entries()[2].id==third && queue.entries()[2].elapsed==0.5f,
        "Retained sounds update without playback; negative delta is preserved.");
    std::vector<std::uint64_t> down;
    services.sound_volume_down=[&](std::uint64_t handle) {if(handle==102)return false;down.push_back(handle);return true;};
    const auto cleanup=queue.cleanup(services);
    check(cleanup.stop==ModelSubcommandStop::sound_volume_down_required && cleanup.entry_id==second &&
        down==std::vector<std::uint64_t>{101} && queue.entries().size()==3,"Cleanup retains list ownership until all virtual cleanup calls complete.");
    rejects([&]{queue.append_sound(0,"late");});
    services.sound_volume_down=[&](std::uint64_t handle){down.push_back(handle);return true;};
    queue.resume(services);
    check(down==std::vector<std::uint64_t>{101,102,103} && queue.entries().empty(),"Resume must not repeat volume-down effects.");
}
void effect_binding_and_interruption() {
    ModelSubcommandQueue queue;
    const auto id=queue.append_effect(effect());
    ModelSubcommandServices services; int searches{},generations{},erases{};
    services.find_effect_space=[&](const auto& name){++searches;check(name=="effect","Effect name routing.");return ModelEffectSpaceResult{true,13};};
    auto pending=queue.update(0,context(),services);
    check(pending.stop==ModelSubcommandStop::effect_generate_required && pending.entry_id==id && searches==1,
        "Selected effect RAM must persist across an unavailable generation service.");
    services.generate_effect=[&](const ModelEffectGenerateRequest& request) {
        ++generations;
        check(request.model_name=="effect" && request.sequence_name=="sequence" && request.ram_no==13,
            "Effect must generate the selected first-space RAM.");
        check(request.source==ModelSubcommandOwner{"source",7} && request.attachment==ModelSubcommandOwner{"attached",11},
            "Effect source and attachment identities must remain separate.");
        check(request.offset==std::array<float,3>{1,2,3} && request.rotation==std::array<float,3>{4,5,6} && request.position_lock,
            "Effect payload must preserve its transform and position lock.");
        return true;
    };
    queue.resume(services); queue.update(3,context(),services);
    check(searches==1 && generations==1 && queue.entries().size()==1,"Retained effect does not regenerate.");
    services.erase_effect=[&](const auto& name,std::int32_t ram){++erases;check(name=="effect" && ram==13,"Effect erasure target.");return true;};
    queue.cleanup(services);check(erases==1 && queue.entries().empty(),"Sequence-dead effect erases on interruption.");
    auto detached=effect(false);detached.model_name.reset();detached.sequence_name.reset();
    services.find_effect_space=[&](const auto& name){check(!name,"Null effect model must remain null.");return ModelEffectSpaceResult{true,3};};
    services.generate_effect=[&](const auto& request){check(!request.sequence_name && request.attachment==request.source,
        "Null sequence and unattached source fallback.");++generations;return true;};
    queue.append_effect(detached); queue.update(0,{{"source",7},{}},services);
    check(queue.entries().empty() && generations==2,"Non-sequence-dead generation leaves the queue immediately.");
    queue.cleanup(services); check(erases==1,"Detached effects must not be erased by sequence cleanup.");
    queue.append_effect(effect());services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{true,{}};};
    queue.update(0,context(),services);check(queue.entries().empty(),"Completed no-space result is terminal.");
    queue.append_effect(effect());services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{};};
    check(queue.update(0,context(),services).stop==ModelSubcommandStop::effect_space_required,"Unavailable space service remains pending.");
    queue.cleanup(services);check(queue.entries().empty() && erases==1,"Unselected effect can be cancelled without a manager erase.");
    queue.append_effect(effect());services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{true,8};};
    services.generate_effect={};queue.update(0,context(),services);
    services.erase_effect=[&](const auto&,std::int32_t ram){++erases;check(ram==8,"Interrupted selected effect cleanup target.");return true;};
    queue.cleanup(services);check(erases==2,"Original started/RAM cleanup guard applies after selection, before generation.");
}
void callbacks_moves_and_bounds() {
    ModelSubcommandQueue queue;queue.append_sound(0,"outer");
    ModelSubcommandServices services;int plays{};
    services.play_sound=[&](const auto&) {
        ++plays;queue.append_sound(0,"appended");
        rejects([&]{queue.cleanup(services);});rejects([&]{queue.resume(services);});
        rejects([&]{ModelSubcommandQueue moving(std::move(queue));});
        return ModelSoundPlayResult{true,0,4};
    };
    queue.update(1,context(),services);
    check(plays==1 && queue.entries().size()==1 && queue.entries()[0].elapsed==0 && !queue.faulted(),
        "Callback append waits for next original list-count snapshot; rejected recursion must not corrupt owner.");
    check(queue.update(1,context()).stop==ModelSubcommandStop::sound_play_required,"Missing service must be explicit.");
    ModelSubcommandQueue moved(std::move(queue));
    check(queue.entries().empty() && !queue.pending() && moved.pending(),"Move transfers the pending pass and ownership.");
    ModelSubcommandQueue occupied;occupied.append_sound(99,"owned");rejects([&]{occupied=std::move(moved);});
    check(moved.pending() && occupied.entries().size()==1,"Move assignment must not discard existing owned work.");
    occupied.cleanup();occupied=std::move(moved);check(occupied.pending() && !moved.pending(),"Clean destination accepts moved pass.");
    occupied.cleanup();check(occupied.entries().empty(),"Pending unplayed sound cancels without volume-down.");
    rejects([&]{occupied.append_sound(std::numeric_limits<float>::infinity(),"bad");});
    auto bad=effect();bad.rotation[1]=std::numeric_limits<float>::quiet_NaN();rejects([&]{occupied.append_effect(bad);});
    check(occupied.entries().empty(),"Malformed append must be atomic.");
    occupied.append_sound(std::numeric_limits<float>::max(),"far");occupied.update(std::numeric_limits<float>::max()/2,context());
    const float previous=occupied.entries()[0].elapsed;
    rejects([&]{occupied.update(std::numeric_limits<float>::max(),context());});
    check(!occupied.faulted() && occupied.entries()[0].elapsed==previous,"Numeric overflow rejection precedes mutation.");
    occupied.cleanup();occupied.append_sound(0,"uncertain");
    services.play_sound=[&](const auto&)->ModelSoundPlayResult {++plays;throw std::runtime_error("Manager failed after possible commit.");};
    rejects([&]{occupied.update(1,context(),services);});check(occupied.faulted(),"Uncertain callback outcome must fault the owner.");
    const int before=plays;rejects([&]{occupied.resume(services);});rejects([&]{occupied.cleanup(services);});
    check(plays==before,"A throwing service must never be replayed automatically.");
}

void live_effect_attachment_capture() {
    struct Work {
        std::shared_ptr<ModelSubcommandWrapper> subcommands=std::make_shared<ModelSubcommandWrapper>();
        std::optional<ModelSubcommandOwner> attachment=ModelSubcommandOwner{"initial",-1};
    };
    std::weak_ptr<Work> observed;
    {
        auto work=std::make_shared<Work>();observed=work;
        ModelSubcommandQueue queue(std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>>(work,&work->subcommands));
        auto prepared=context();unsigned reads{},finds{},generations{};
        prepared.attachment=ModelSubcommandOwner{std::string(1048577,'x'),0}; // Unused when the live resolver is present.
        prepared.resolve_attachment=[weak=std::weak_ptr<Work>(work),&reads] {
            ++reads;const auto live=weak.lock();if(!live)throw std::runtime_error("Missing captured work.");
            return live->attachment;
        };
        queue.append_sound(0,"first");queue.append_effect(effect(false));queue.append_effect(effect(false));
        ModelSubcommandServices services;
        services.play_sound=[&](const auto&){work->attachment=ModelSubcommandOwner{"sound mutation",3};return ModelSoundPlayResult{true,0,4};};
        services.find_effect_space=[&](const auto&) {
            ++finds;check(reads==finds-1,"Attachment was read before FIND_SPACE completed.");
            work->attachment=finds==1 ? std::optional<ModelSubcommandOwner>{{"",std::numeric_limits<std::int32_t>::min()}} : std::nullopt;
            return ModelEffectSpaceResult{true,19};
        };
        check(queue.update(2,prepared,services).stop==ModelSubcommandStop::effect_generate_required && reads==1 && finds==1,
            "Missing GENERATE did not retain the post-FIND_SPACE attachment capture.");
        work->attachment=ModelSubcommandOwner{"later",91};
        services.generate_effect=[&](const ModelEffectGenerateRequest& request) {
            ++generations;
            if(generations<=2)check(request.attachment==ModelSubcommandOwner{"",std::numeric_limits<std::int32_t>::min()},
                "A GENERATE retry re-read the live attachment or replaced a non-null empty name with its fallback.");
            else check(request.attachment==request.source,"A later entry failed to re-read the original null attachment fallback.");
            return generations!=1;
        };
        check(queue.resume(services).stop==ModelSubcommandStop::effect_generate_required && reads==1 && finds==1,
            "Unavailable GENERATE re-ran a completed context read or space selection.");
        work->attachment=ModelSubcommandOwner{"changed again",100};
        check(queue.resume(services).complete() && queue.entries().empty() && reads==2 && finds==2 && generations==3,
            "Effect packet capture leaked across entries or replayed the first effect's prefix.");
        queue.append_effect(effect(false));services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{true,{}};};
        queue.update(0,prepared,services);check(reads==2,"No-space effect performed a work attachment read absent from the original branch.");
        work.reset();check(!observed.expired(),"Queue slot stopped retaining its actual work.");
    }
    check(observed.expired(),"Retained effect context formed a work-to-wrapper ownership cycle.");
}
void original_timing_and_ni() {
    check(original_elapsed_frames(0)==0 && original_elapsed_frames(16666667)==1 &&
        original_elapsed_frames(-16666667)==-1,"System elapsed conversion preserves sign and nominal units.");
    check(original_system_frame_delta(100000002,0)==6,"System frame delta is uncapped.");
    check(original_system_frame_delta(2,std::numeric_limits<std::uint64_t>::max()-2)==original_elapsed_frames(5),
        "System timestamp subtraction wraps before signed conversion.");
    check(original_system_frame_delta(0,1)==original_elapsed_frames(-1),"Backwards world time remains negative.");
    const std::int64_t midpoint=(std::int64_t{1}<<62)+(std::int64_t{1}<<38)+1;
    check(std::bit_cast<std::uint32_t>(original_elapsed_frames(midpoint))==0x5280d959,
        "Original signed conversion must retain the intermediate binary64 rounding stage.");
    ModelSubcommandServices services;services.play_sound=[](const auto&){return ModelSoundPlayResult{true,1,4};};
    for(const auto row:{std::array<std::uint32_t,3>{0x00800000,0x807fffff,0x00000000},
                        std::array<std::uint32_t,3>{0x80800000,0x007fffff,0x80000000},
                        std::array<std::uint32_t,3>{0x00800000,0x00000001,0x00800001},
                        std::array<std::uint32_t,3>{0x80800000,0x80000001,0x80800001},
                        std::array<std::uint32_t,3>{0x00400000,0x00400000,0x00000000}}) {
        ModelSubcommandQueue queue;queue.append_sound(0,"numeric");
        queue.update(std::bit_cast<float>(row[0]),context(),services);
        queue.update(std::bit_cast<float>(row[1]),context(),services);
        check(std::bit_cast<std::uint32_t>(queue.entries()[0].elapsed)==row[2],
            "Scalar fadds must retain tiny inputs and flush each tiny result with its sign.");
        services.sound_volume_down=[](auto){return true;};queue.cleanup(services);
    }
}

void actual_owner_graph_and_reference_replacement() {
    static_assert(!std::is_copy_constructible_v<ModelSubcommandWrapper>);
    static_assert(!std::is_copy_constructible_v<ModelSubcommandList>);
    ModelSubcommandQueue initial;
    auto current_work_reference=initial.wrapper_owner();const auto old_wrapper=current_work_reference;
    const auto old_list=old_wrapper->list_owner();
    check(old_wrapper && old_list && old_list->empty() && initial.list_owner()==old_list && &initial.entries()==old_list.get(),
        "Factory preparation must allocate the actual wrapper and its distinct empty list.");
    rejects([&]{ModelSubcommandQueue absent(std::shared_ptr<ModelSubcommandWrapper>{});});
    rejects([&]{initial.entry_owner(0);});
    initial.append_sound(0,"first");initial.append_sound(0,"second");
    const auto first=initial.entry_owner(0),second=initial.entry_owner(1);
    check(first!=second && first.get()==&old_list->front() && second.get()==&old_list->back(),
        "The actual ordered list must retain separate entry allocations.");
    ModelSubcommandQueue old_alias(old_wrapper);std::vector<std::string> plays;std::vector<std::uint64_t> downs;
    bool second_available{};
    ModelSubcommandServices services;
    services.play_sound=[&](const auto& name) {
        if(name=="second" && !second_available)return ModelSoundPlayResult{};
        plays.push_back(*name);return ModelSoundPlayResult{true,name=="first"?101u:102u,4};
    };
    check(initial.update(2,context(),services).stop==ModelSubcommandStop::sound_play_required && old_alias.pending(),
        "A second facade did not observe the actual wrapper's pending pass.");
    check(std::get<ModelSoundSubcommandState>(first->value).handle==101 && second->started && second->elapsed==2,
        "Shared entry owners did not expose the committed start prefix.");
    current_work_reference=std::make_shared<ModelSubcommandWrapper>();ModelSubcommandQueue fresh(current_work_reference);
    check(current_work_reference!=old_wrapper && fresh.list_owner()!=old_list && fresh.entries().empty() &&
        plays==std::vector<std::string>{"first"} && downs.empty() && old_alias.pending(),
        "Replacing a work reference reset old commands, reused list storage or performed implicit cleanup.");
    fresh.append_sound(9,"new generation");
    check(fresh.entry_owner(0)!=first && fresh.entries()[0].id==first->id && old_list->size()==2,
        "Separate wrappers must own independent entries even when diagnostic IDs are equal.");
    second_available=true;check(old_alias.resume(services).complete() && !initial.pending(),
        "Retained old wrapper did not resume the same pass through another facade.");
    check(plays==std::vector<std::string>{"first","second"} && initial.entry_owner(0)==first && old_alias.entry_owner(1)==second &&
        first->elapsed==2 && second->elapsed==2 && fresh.entries()[0].elapsed==0,
        "Owner alias resume copied handles, replayed playback or advanced another generation.");
    services.sound_volume_down=[&](std::uint64_t handle) {if(handle==102)return false;downs.push_back(handle);return true;};
    check(initial.cleanup(services).stop==ModelSubcommandStop::sound_volume_down_required && old_alias.pending() && old_list->size()==2,
        "Pending cleanup did not retain its complete actual list.");
    ModelSubcommandQueue transferred(std::move(old_alias));
    check(transferred.wrapper_owner()==old_wrapper && old_alias.wrapper_owner()!=old_wrapper && transferred.entry_owner(1)==second,
        "Moving a facade copied entry payloads or moved the actual list into another owner.");
    services.sound_volume_down=[&](std::uint64_t handle){downs.push_back(handle);return true;};
    check(transferred.resume(services).complete() && initial.entries().empty() && old_list->empty() &&
        transferred.list_owner()==old_list && downs==std::vector<std::uint64_t>{101,102},
        "Cleanup replaced the list owner or replayed an already completed cleanup entry.");
    check(std::get<ModelSoundSubcommandState>(first->value).handle==101 && std::get<ModelSoundSubcommandState>(second->value).handle==102,
        "Removing list references mutated independently retained entry objects.");
    initial.resume(services);initial.cleanup(services);
    check(downs.size()==2 && fresh.entries().size()==1,"Empty shared cleanup replayed retained entry resources or touched fresh work.");
}

void owner_retention_clear_and_faults() {
    ModelSubcommandQueue queue;ModelSubcommandQueue alias(queue.wrapper_owner());
    const auto list=queue.list_owner();queue.append_hitbox({0,99,10,{},{}});
    const auto hitbox=queue.entry_owner(0);queue.append_sound(0,"pending");const auto sound=queue.entry_owner(1);
    auto effect_command=effect();effect_command.delay=4;queue.append_effect(effect_command);const auto delayed=queue.entry_owner(2);
    check(queue.update(1,context()).stop==ModelSubcommandStop::sound_play_required,"Clear fixture must pause at the actual sound entry.");
    alias.clear_hitboxes();
    check(queue.list_owner()==list && queue.entries().size()==2 && queue.entry_owner(0)==sound && queue.entry_owner(1)==delayed &&
        hitbox->elapsed==1 && !hitbox->started && sound->elapsed==1 && delayed->elapsed==0,
        "Selective clear relocated entry objects, changed the list owner or advanced the paused tail.");
    ModelSubcommandServices services;unsigned plays{};
    services.play_sound=[&](const auto&) {
        ++plays;alias.append_sound(0,"later");
        rejects([&]{alias.resume(services);});rejects([&]{alias.cleanup(services);});
        return ModelSoundPlayResult{true,0,4};
    };
    check(alias.resume(services).complete() && plays==1 && queue.entry_owner(0)==delayed && delayed->elapsed==1 &&
        queue.entries().back().elapsed==0 && sound->started,
        "Shared callback append/clear lost list order, entry identity or the original count snapshot.");
    queue.cleanup();check(list->empty() && delayed->elapsed==1,"Explicit cleanup did not release only the list's entry references.");

    std::weak_ptr<ModelSubcommandWrapper> weak_wrapper;std::weak_ptr<ModelSubcommandList> weak_list;
    std::shared_ptr<ModelSubcommandList> retained_list;std::shared_ptr<const ModelSubcommandEntry> retained_entry;
    unsigned cleanups{};
    {
        ModelSubcommandQueue owner;weak_wrapper=owner.wrapper_owner();retained_list=owner.list_owner();weak_list=retained_list;
        owner.append_sound(0,"retained");retained_entry=owner.entry_owner(0);
        services.play_sound=[](const auto&){return ModelSoundPlayResult{true,777,4};};
        services.sound_volume_down=[&](auto){++cleanups;return true;};owner.update(0,context(),services);
        const auto wrapper_copy=owner.wrapper_owner(),another_copy=wrapper_copy;
        check(wrapper_copy==another_copy && owner.entry_owner(0)==retained_entry,"Retaining wrapper handles copied live entry state.");
    }
    check(weak_wrapper.expired() && !weak_list.expired() && retained_list->entry_owner(0)==retained_entry &&
        std::get<ModelSoundSubcommandState>(retained_entry->value).handle==777 && cleanups==0,
        "Facade/wrapper destruction performed external cleanup or invalidated an independently retained list.");
    retained_list.reset();check(weak_list.expired() && retained_entry->started && cleanups==0,
        "Dropping the last list reference performed synthetic resource cleanup on its retained entry.");

    queue.append_sound(0,"uncertain");const auto failed_entry=queue.entry_owner(0);
    services.play_sound=[](const auto&)->ModelSoundPlayResult{throw std::runtime_error("Uncertain original manager outcome.");};
    rejects([&]{queue.update(1,context(),services);});
    ModelSubcommandQueue after_failure(queue.wrapper_owner());
    check(after_failure.faulted() && after_failure.entry_owner(0)==failed_entry,"Another facade bypassed the shared fault or replaced its entry.");
    rejects([&]{after_failure.resume(services);});rejects([&]{after_failure.cleanup(services);});
}

void canonical_wrapper_slot() {
    static_assert(std::is_same_v<decltype(std::declval<const ModelSubcommandQueue&>().wrapper_owner()),std::shared_ptr<const ModelSubcommandWrapper>>);
    static_assert(std::is_same_v<decltype(std::declval<const ModelSubcommandQueue&>().pending_wrapper_owner()),std::shared_ptr<const ModelSubcommandWrapper>>);
    static_assert(std::is_same_v<decltype(std::declval<const ModelSubcommandQueue&>().list_owner()),std::shared_ptr<const ModelSubcommandList>>);
    struct Work {std::shared_ptr<ModelSubcommandWrapper> subcommands=std::make_shared<ModelSubcommandWrapper>();};
    std::weak_ptr<Work> weak_work;
    {
        auto work=std::make_shared<Work>();weak_work=work;
        auto slot=std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>>(work,&work->subcommands);
        ModelSubcommandQueue queue(slot);auto old=queue.wrapper_owner();std::weak_ptr<ModelSubcommandWrapper> weak_old=old;
        const auto old_list=queue.list_owner();queue.append_sound(0,"old first");queue.append_sound(0,"old second");
        const auto first=queue.entry_owner(0),second=queue.entry_owner(1);
        auto fresh=std::make_shared<ModelSubcommandWrapper>();std::vector<std::string> plays;
        ModelSubcommandServices services;bool ready=false;
        services.play_sound=[&](const auto& name) {
            if(*name=="old second" && !ready)return ModelSoundPlayResult{};
            plays.push_back(*name);
            if(*name=="old first") {
                *slot=fresh;queue.append_sound(0,"fresh");
                check(queue.wrapper_owner()==fresh && queue.pending_wrapper_owner()==old && queue.entries().size()==1 &&
                    queue.entries().front().elapsed==0,"Callback replacement redirected the active pass or appended to a cached old wrapper.");
                rejects([&]{queue.resume(services);});rejects([&]{queue.update(1,context(),services);});
                rejects([&]{queue.cleanup(services);});rejects([&]{ModelSubcommandQueue moving(std::move(queue));});
                ModelSubcommandQueue same_wrapper(old);rejects([&]{same_wrapper.resume(services);});
            }
            return ModelSoundPlayResult{true,*name=="old first" ? 11u : 12u,4};
        };
        const auto paused=queue.update(2,context(),services);
        check(paused.stop==ModelSubcommandStop::sound_play_required &&
            first->started && second->elapsed==2 && second->started && plays==std::vector<std::string>{"old first"},
            "Slot replacement lost an original completed prefix or advanced a pending entry twice.");
        check(queue.pending() && queue.pending_wrapper_owner()==old && queue.wrapper_owner()==fresh &&
            old_list->size()==2 && fresh->list_owner()->size()==1,"Live and pending wrapper owners were conflated.");
        rejects([&]{queue.cleanup(services);});rejects([&]{queue.update(99,context(),services);});
        ModelSubcommandQueue separate(slot);
        check(separate.cleanup().complete() && fresh->list_owner()->empty() && old_list->size()==2,
            "Separate facade could not clean the new live wrapper without touching a captured old pass.");
        old.reset();check(!weak_old.expired(),"Pending update did not retain the replaced wrapper allocation.");
        ready=true;
        check(queue.resume(services).complete() && plays==std::vector<std::string>{"old first","old second"} &&
            first->elapsed==2 && second->elapsed==2 && second->started && !queue.pending(),
            "Resuming a captured update replayed its prefix or switched to the live replacement.");
        check(weak_old.expired() && old_list->size()==2 && std::get<ModelSoundSubcommandState>(first->value).handle==11,
            "Completed slot pass retained its old wrapper or performed synthetic external cleanup.");
        queue.append_hitbox({0,0,5,{},{}});check(queue.entries().size()==1,"New append ignored the current canonical wrapper.");
        separate.clear_hitboxes();check(queue.entries().empty(),"New clear did not resolve the canonical wrapper field.");
        ModelSubcommandQueue moved(std::move(queue));
        check(moved.wrapper_owner()==fresh && queue.wrapper_owner()!=fresh && queue.entries().empty(),
            "Moving a slot facade lost its field binding or left the moved-from facade bound to canonical work.");
        *slot=std::make_shared<ModelSubcommandWrapper>();
        check(moved.wrapper_owner()==*slot && separate.wrapper_owner()==*slot,"Moved slot facade cached the previous work reference.");
        work.reset();slot.reset();check(!weak_work.expired(),"Slot facade did not retain its aliasing containing work owner.");
    }
    check(weak_work.expired(),"Canonical work slot and subcommand facade formed an ownership cycle.");
}
void retained_slot_cleanup_null_and_faults() {
    auto slot=std::make_shared<std::shared_ptr<ModelSubcommandWrapper>>(std::make_shared<ModelSubcommandWrapper>());
    ModelSubcommandQueue queue(slot);queue.append_sound(0,"cleanup old");auto old=queue.wrapper_owner();const auto list=queue.list_owner();
    std::weak_ptr<ModelSubcommandWrapper> weak_old=old;
    ModelSubcommandServices services;services.play_sound=[](const auto&){return ModelSoundPlayResult{true,77,4};};
    queue.update(1,context(),services);
    check(queue.cleanup().stop==ModelSubcommandStop::sound_volume_down_required && queue.pending_wrapper_owner()==old,
        "Cleanup did not capture the actual old wrapper before pausing.");
    slot->reset();old.reset();
    check(!queue.wrapper_owner() && queue.pending_wrapper_owner() && !weak_old.expired(),
        "Nulling the actual work slot discarded its captured cleanup continuation.");
    rejects([&]{queue.append_sound(0,"missing");});rejects([&]{queue.entries();});rejects([&]{queue.list_owner();});
    rejects([&]{queue.cleanup();});
    unsigned cleaned{};services.sound_volume_down=[&](auto handle){++cleaned;return handle==77;};
    check(queue.resume(services).complete() && cleaned==1 && list->empty() && weak_old.expired() && !*slot && !queue.pending(),
        "Cleanup resume failed to retain old work, repeated its effect or fabricated a null live wrapper.");
    rejects([&]{queue.resume();});rejects([&]{queue.update(1,context());});
    check(!*slot,"Missing live operations repaired the canonical null field.");
    rejects([&]{ModelSubcommandQueue absent(std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>>{});});

    *slot=std::make_shared<ModelSubcommandWrapper>();queue.append_sound(0,"uncertain");const auto faulted=queue.wrapper_owner();
    services.play_sound=[](const auto&)->ModelSoundPlayResult{throw std::runtime_error("uncertain old service");};
    rejects([&]{queue.update(1,context(),services);});
    *slot=std::make_shared<ModelSubcommandWrapper>();
    check(queue.faulted() && queue.pending_wrapper_owner()==faulted,"Slot replacement bypassed the captured wrapper fault.");
    rejects([&]{queue.resume(services);});rejects([&]{queue.cleanup(services);});
    ModelSubcommandQueue fresh(slot);fresh.append_sound(0,"independent");
    services.play_sound=[](const auto&){return ModelSoundPlayResult{true,0,0};};
    check(fresh.update(1,context(),services).complete() && fresh.entries().empty() && queue.faulted(),
        "A separate new-wrapper operation cleared an old fault or inherited unrelated failed work.");
}

void cleanup_preflight_preserves_operation() {
    auto slot=std::make_shared<std::shared_ptr<ModelSubcommandWrapper>>(std::make_shared<ModelSubcommandWrapper>());
    ModelSubcommandQueue queue(slot);queue.append_sound(0,"pending");
    const auto original=queue.wrapper_owner();const auto list_entry=queue.entry_owner(0);
    check(queue.update(3,context()).stop==ModelSubcommandStop::sound_play_required,"Preflight fixture did not suspend sound start.");
    std::as_const(queue).require_cleanup_startable();
    check(queue.pending() && queue.pending_wrapper_owner()==original && list_entry->elapsed==3 && list_entry->started,
        "Read-only cleanup preflight canceled the update or changed its completed prefix.");
    *slot=std::make_shared<ModelSubcommandWrapper>();queue.append_sound(0,"new");
    rejects([&]{std::as_const(queue).require_cleanup_startable();});
    check(queue.entries().size()==1 && queue.pending_wrapper_owner()==original,"Rejected cleanup preflight captured the new wrapper.");
    slot->reset();rejects([&]{queue.require_cleanup_startable();});
    check(!*slot && queue.pending_wrapper_owner()==original,"Null cleanup preflight fabricated work or discarded a continuation.");
    *slot=original;
    ModelSubcommandServices services;
    services.play_sound=[&](const auto&) {
        rejects([&]{queue.require_cleanup_startable();});
        return ModelSoundPlayResult{true,77,4};
    };
    check(queue.resume(services).complete() && list_entry->elapsed==3,"Preflight disrupted resumed sound timing.");
    check(queue.cleanup().stop==ModelSubcommandStop::sound_volume_down_required,"Cleanup was not resumable after preflight.");
    queue.require_cleanup_startable();
    services.sound_volume_down=[](auto){return true;};check(queue.resume(services).complete(),"Preflight changed pending cleanup.");
    queue.append_hitbox({0,0,5,{},{}});
    services.check_hitbox=[](const auto&){return false;};
    check(queue.update(1,context(),services).stop==ModelSubcommandStop::hitbox_check_required,"Hitbox preflight fixture did not suspend.");
    rejects([&]{queue.require_cleanup_startable();});
    services.check_hitbox=[](const auto&){return true;};queue.resume(services);queue.cleanup();
    queue.append_sound(0,"uncertain");
    services.play_sound=[](const auto&)->ModelSoundPlayResult{throw std::runtime_error("uncertain");};
    rejects([&]{queue.update(1,context(),services);});rejects([&]{queue.require_cleanup_startable();});
    check(queue.faulted(),"Read-only cleanup preflight cleared an uncertain outcome.");
}

int numeric_oracle() {
    char kind{};
    while(std::cin>>kind) {
        try {
            if(kind=='T') {
                std::uint64_t current{},previous{};std::cin>>current>>previous;
                std::cout<<std::bit_cast<std::uint32_t>(original_system_frame_delta(current,previous))<<'\n';
            } else if(kind=='A') {
                std::uint32_t a{},b{};std::cin>>a>>b;
                ModelSubcommandQueue queue;queue.append_sound(0,"numeric");
                ModelSubcommandServices services;services.play_sound=[](const auto&){return ModelSoundPlayResult{true,1,4};};
                queue.update(std::bit_cast<float>(a),context(),services);
                queue.update(std::bit_cast<float>(b),context(),services);
                std::cout<<std::bit_cast<std::uint32_t>(queue.entries()[0].elapsed)<<'\n';
            } else throw std::runtime_error("Unknown numeric oracle operation.");
        } catch(const std::exception&) {std::cout<<"ERROR\n";}
    }
    return 0;
}

// Ignored original-instruction probe protocol. Each case owns mixed sound/effect
// commands, applies one cached-frame delta repeatedly, then clears the sequence.
// Kind 0=sound, 1=sequence-dead effect, 2=detached effect; services are completed.
int oracle() {
    std::size_t count{},updates{};std::uint32_t delta_word{};
    while(std::cin>>count>>updates>>delta_word) {
        ModelSubcommandQueue queue;std::vector<int> trace;
        for(std::size_t i=0;i<count;++i) {
            int kind{};std::uint32_t delay{};std::cin>>kind>>delay;
            if(kind==0)queue.append_sound(std::bit_cast<float>(delay),std::to_string(i));
            else {auto command=effect(kind==1);command.delay=std::bit_cast<float>(delay);command.model_name=std::to_string(i);queue.append_effect(command);}
        }
        ModelSubcommandServices services;
        services.play_sound=[&](const auto& name){const int id=std::stoi(*name);trace.push_back(100+id);return ModelSoundPlayResult{true,static_cast<std::uint64_t>(id+1),4};};
        services.find_effect_space=[](const auto& name){return ModelEffectSpaceResult{true,std::stoi(*name)+1};};
        services.generate_effect=[&](const auto& request){trace.push_back(200+std::stoi(*request.model_name));return true;};
        services.sound_volume_down=[&](std::uint64_t handle){trace.push_back(300+static_cast<int>(handle)-1);return true;};
        services.erase_effect=[&](const auto& name,std::int32_t){trace.push_back(400+std::stoi(*name));return true;};
        for(std::size_t tick=0;tick<updates;++tick)queue.update(std::bit_cast<float>(delta_word),context(),services);
        std::cout<<queue.entries().size();
        for(const auto& entry:queue.entries())std::cout<<' '<<entry.id<<' '<<std::bit_cast<std::uint32_t>(entry.elapsed)<<' '<<entry.started;
        queue.cleanup(services);std::cout<<' '<<trace.size();for(int event:trace)std::cout<<' '<<event;std::cout<<'\n';
    }
    return 0;
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--oracle")return oracle();
        if(argc==2 && std::string(argv[1])=="--numeric-oracle")return numeric_oracle();
        defaults_delay_and_results();pending_order_and_cleanup();effect_binding_and_interruption();callbacks_moves_and_bounds();
        live_effect_attachment_capture();
        original_timing_and_ni();actual_owner_graph_and_reference_replacement();owner_retention_clear_and_faults();
        canonical_wrapper_slot();retained_slot_cleanup_null_and_faults();cleanup_preflight_preserves_operation();
        std::cout<<"model subcommand checks: "<<checks<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
