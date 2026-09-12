#include "dw4/model_player_work.hpp"

#include <bit>
#include <iostream>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F operation,const char* message) {
    ++checks;try {operation();}catch(const MotionError&) {return;}throw std::runtime_error(message);
}
bool positive_zero(float value) { return std::bit_cast<std::uint32_t>(value)==0; }
template<std::size_t N> bool zero(const std::array<float,N>& values) {
    for(const auto value:values)if(!positive_zero(value))return false;
    return true;
}
AnimationMatrix matrix() { return {1,0,0,0,0,1,0,0,0,0,1,0,19,23,29,1}; }

void original_factory_defaults() {
    static_assert(!std::is_copy_constructible_v<ModelPlayerWork> && !std::is_move_constructible_v<ModelPlayerWork>);
    const auto a=make_model_player_work(),b=make_model_player_work();
    // Recovered 800667F4 factory/default-field path and inherited metadata.
    // These are pre-GENERATE/pre-player-callback values.
    for(const auto& work:{a,b}) {
        const auto& w=*work;
        check(w.script.hp && w.script.max_hp && w.script.hp->data==0 && w.script.max_hp->data==0 &&
            !w.script.hp->name && !w.script.max_hp->name && w.script.record==-1,"Factory HP/MHP/record defaults changed.");
        check(!w.sequence.name && !w.sequence.command_data && w.sequence.command_index==0 && w.sequence.label_count==0,
            "Factory sequence defaults changed.");
        check(w.sequence.labels && !w.sequence.labels->sequences && !w.sequence.labels->names && !w.sequence.labels->offsets,
            "Factory allocated guest label lists.");
        check(!w.control.bullet_hit_script && !w.control.wall_hit_script && !w.control.floor_hit_script &&
            w.control.mode==0 && w.control.phase==0,"Factory hit script/control defaults changed.");
        check(w.sequence.wait.control_enabled==1 && w.frozen==0 && !w.sequence.wait.name &&
            w.sequence.wait.attack_state==0 && w.sequence.wait.attack_mode==0,"Factory wait/freeze defaults changed.");
        check(w.control.pad_motion && w.control.pad_motion->size()==0 && !w.control.pad_motion->backing_owner(),
            "Factory pad list has fabricated storage.");
        check(!w.ai && positive_zero(w.ai_height_offset) && !w.target_mode && !w.target_kind && !w.target_special && zero(w.target_position),
            "Factory AI/target defaults changed.");
        check(w.key_input==0 && !w.key_event_name && w.motion.translated==0 && positive_zero(w.control.invulnerability_frames),
            "Factory key/hit-check/invulnerability defaults changed.");
        check(zero(w.motion.turn.desired) && zero(w.motion.turn.rate) && zero(w.motion.turn.applied),
            "Factory invented player turn rates before the player callback.");
        check(w.saved_lod_high_range==-1 && w.motion.position_locked==0 && !w.position_lock_node && !w.position_lock_trace &&
            !w.position_lock_model_name && !w.position_lock_bone_name && zero(w.position_lock_offset),"Factory position-lock defaults changed.");
        check(w.control.movement && w.control.movement->size()==0 && !w.ride_data && !w.debug_hitbox_node && !w.hit_body,
            "Factory movement or scene-object defaults changed.");
        check(w.control.wall_check==1 && w.control.floor_check==1 && w.control.check_fall==1 && !w.hit_parent_name && w.hit_parent_ram==0,
            "Factory collision/attachment defaults changed.");
        check(w.subcommands && w.subcommands->list_owner() && w.subcommands->list_owner()->empty() && !w.base_box,
            "Factory subcommand/base-box defaults changed.");
        check(w.status.is_player==0 && !w.status.display_ram_numbers && !w.status.display_positions && !w.status.timers &&
            !w.status.first_flags && w.status.poison_damage==0 && w.status.poison_endurance==0 && !w.status.down_data && !w.status.up_data,
            "Factory status defaults changed.");
        check(!w.messenger && !w.turn_attack_timers && w.body_attack_state==0 && w.body_attack_pad==0 &&
            positive_zero(w.body_attack_timer) && w.panel==0 && w.script.original_is_main_lobby==0,"Factory player-tail defaults changed.");
    }
    std::set<const void*> allocations;
    for(const auto& w:{a,b})for(const void* p:std::initializer_list<const void*>{w->script.hp.get(),w->script.max_hp.get(),
        w->control.pad_motion.get(),w->control.movement.get(),w->subcommands.get(),w->subcommands->list_owner().get()})allocations.insert(p);
    check(allocations.size()==12,"Separate factory calls alias referenced guest allocations.");
}

void shared_fields_and_generation_lifetime() {
    auto first=make_model_player_work();
    std::weak_ptr<ModelPlayerWork> old=first;
    auto node=std::make_shared<AnimationMatrix>(matrix());
    auto timer=std::make_shared<ModelRamTimerSlot>();
    {
        auto control=bind_model_player_control(first,node,timer);
        auto script=bind_model_player_script(first,{3,7},"fixture","player",0,node);
        ModelStatusWork status(script,std::uint8_t{1});
        check(control.fields_owner().get()==&first->control && control.pose.fields_owner().get()==&first->motion &&
            script->fields_owner().get()==&first->script && status.fields_owner().get()==&first->status,
            "A facade detached its canonical field group.");
        control.mode()=2;control.pose.turn().rate={0,7,0};script->record()=17;status.poison_damage()=-3;
        check(first->control.mode==2 && first->motion.turn.rate[1]==7 && first->script.record==17 && first->status.poison_damage==-3,
            "Facade stores did not reach the shared work.");
        first->script.hp=std::make_shared<ModelScriptInt>();first->script.hp->data=41;
        first->status.timers=std::make_shared<std::vector<std::int32_t>>(8,3);
        first->control.pad_motion=std::make_shared<ModelPadMotionList>();
        first->control.movement=std::make_shared<MotionChannelList>();
        check(script->hp()==first->script.hp && status.timers()==first->status.timers &&
            control.pad_motion.list_owner()==first->control.pad_motion && control.channels.list_owner()==first->control.movement,
            "A facade cached a replaced reference field.");
        control.pad_motion.set_values({1,2,3,4});
        check(first->control.pad_motion->size()==4,"Pad stores went into a detached list.");
        auto snapshot=control.detached_snapshot();snapshot.mode()=8;snapshot.pose.matrix()[12]=101;
        check(first->control.mode==2 && (*node)[12]==19,"Editing a detached snapshot changed live fields.");
        control.publish_snapshot(snapshot);
        check(first->control.mode==8 && (*node)[12]==101 && control.timer_slot_owner()==timer,
            "Publication redirected canonical fields or RAM timer.");
        auto fresh=make_model_player_work();
        auto next=bind_model_player_control(fresh,node,timer);
        first.reset();
        check(!old.expired() && next.mode()==0 && control.mode()==8 && next.pose.turn().rate[1]==0 && control.pose.turn().rate[1]==7,
            "Fresh work redirected or released an older retained generation.");
        next.pose.matrix()[13]=77;
        check((*script->matrix())[13]==77 && next.timer_slot_owner()==control.timer_slot_owner(),
            "Replacing work detached its separately retained scene node or RAM timer.");
    }
    check(old.expired(),"Canonical work cached a retaining facade and created an ownership cycle.");
}
void prepared_profile_and_missing_owners() {
    ModelControlState values{ModelMotionPose(matrix())};values.mode()=7;values.pose.turn().rate={0,7,0};
    const auto pad=values.pad_motion.list_owner();const auto movement=values.channels.list_owner();
    auto work=prepare_model_player_work(values);
    check(work->control.mode==7 && work->motion.turn.rate[1]==7 && !work->script.original_is_main_lobby &&
        work->sequence.wait.control_enabled==0,"Prepared compatibility was confused with original factory defaults.");
    check(work->control.pad_motion==pad && work->control.movement==movement,"Prepared conversion detached actual list owners.");
    values.mode()=11;check(work->control.mode==7,"Prepared scalar conversion retained a second field group's alias.");
    rejects([&]{(void)bind_model_player_control({},values.pose.matrix_owner(),values.timer_slot_owner());},"Missing captured work was fabricated.");
    rejects([&]{(void)bind_model_player_control(work,{},values.timer_slot_owner());},"Missing actual node was fabricated.");
    rejects([&]{(void)bind_model_player_control(work,values.pose.matrix_owner(),std::shared_ptr<ModelRamTimerSlot>{});},"Missing RAM timer owner was fabricated.");
}
}
int main() {
    try { original_factory_defaults();shared_fields_and_generation_lifetime();prepared_profile_and_missing_owners();
        std::cout<<"model_player_work: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"model_player_work: "<<error.what()<<'\n';return 1;}
}
