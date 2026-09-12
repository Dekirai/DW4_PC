#include "dw4/model_movement_collision.hpp"

#include "dw4/action_event_runtime.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* text) {throw ModelSceneError(text);}
}
bool model_floor_motion(const std::shared_ptr<MotionChannelList>& list,ModelFloorMotion mode) {
    if(!list)fail("Original floor motion helper dereferences a null movement list.");
    if(list->size()==0)return false;
    if(list->size()<2)fail("Original floor motion helper has no represented physical vertical slot.");
    const auto vertical=list->entry_owner(1);
    if(!vertical)fail("Original floor motion helper dereferences a null vertical entry.");
    switch(mode) {
    case ModelFloorMotion::rising:return vertical->active && vertical->mode==MotionMode::impulse_positive;
    case ModelFloorMotion::falling:return vertical->active && vertical->mode==MotionMode::accelerate_negative;
    case ModelFloorMotion::land:vertical->active=false;vertical->speed=0.0f;return true;
    }
    fail("Unsupported original floor motion helper mode.");
}
struct ModelMovementCollisionInvocation::Impl {
    enum class Phase { wall_gate,wall_send,wall_reply,wall_script,floor_gate,floor_send,floor_reply,
        fall_gate,adjust,land,fall_sequence,floor_script_gate,floor_script,ride_gate,ride,completed };
    struct Query {
        std::string command,name;
        std::int32_t number{};
        std::optional<MessengerDispatch> dispatch;
    };
    ModelMovementCollisionScope scope;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::shared_ptr<ModelMovementCollisionServices> services;
    std::optional<Query> query;
    std::optional<ModelMovementHitRequest> script;
    std::optional<ModelMovementRideTarget> ride_target;
    std::unique_ptr<ModelBaseInvocation> fall;
    std::uint8_t floor_now{},floor_crossed{};
    bool update_ride{},return_after_adjust{},executing{},fault{};
    Phase phase=Phase::wall_gate;
    void check() const {if(fault)fail("A caught recursive failure invalidated original movement collision.");}
    std::string parent_name() const {
        if(!scope.parent)fail("Original movement collision dereferences its captured null parent name.");
        return scope.parent->name.value_or("");
    }
    void select(const char* command) {
        // Command choice precedes parent.name, then live RAM.number reads.
        Query selected;selected.command=command;selected.name=parent_name();
        if(!scope.ram)fail("Original movement collision dereferences a null RAM number.");
        selected.number=scope.ram->number;
        query=std::move(selected);
    }
    MessengerCallResult send() {
        auto& current=*query;
        const auto missing=[&] {return MessengerCallResult{false,0,"Original movement collision awaits HITLANDDELIVER/"+current.command+"."};};
        if(!current.dispatch) {
            if(!messenger->has_category("HITLANDDELIVER"))return missing();
            current.dispatch.emplace(messenger->begin_send_to("HITLANDDELIVER",current.name,current.number,current.command));
            check(); // Packet-owner release during selection can invoke host callbacks.
        }
        const auto result=events->resume(*current.dispatch);check();
        if(result.stop==MessengerStop::faulted)throw MessengerError(result.diagnostic);
        if(result.stop!=MessengerStop::completed) {
            auto pending=missing();if(!result.diagnostic.empty())pending.diagnostic+=' '+result.diagnostic;
            return pending;
        }
        query.reset();return {true,0,{}};
    }
    std::uint8_t boolean(std::size_t index) const {
        const auto& values=messenger->buffer().booleans();
        if(index>=values.size())fail("Original movement collision reads an unavailable physical response boolean.");
        return values[index];
    }
    MessengerCallResult run_script() {
        if(!services || !services->hit_script)
            return {false,0,"Original movement collision requires its actual "+script->event+" script/context invocation."};
        const auto result=services->hit_script(scope,*script);check();
        if(result.completed)script.reset();return result;
    }
    MessengerCallResult complete() {phase=Phase::completed;return {true,0,{}};}
};
ModelMovementCollisionInvocation::ModelMovementCollisionInvocation(std::shared_ptr<ModelSceneRam> ram,
    std::shared_ptr<ModelSceneInfo> parent,std::shared_ptr<ModelPlayerWork> work):impl_(std::make_unique<Impl>()) {
    impl_->scope.ram=std::move(ram);impl_->scope.parent=std::move(parent);impl_->scope.work=std::move(work);
}
ModelMovementCollisionInvocation::~ModelMovementCollisionInvocation()=default;
bool ModelMovementCollisionInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelMovementCollisionInvocation::faulted() const noexcept {return impl_->fault;}
const ModelMovementCollisionScope& ModelMovementCollisionInvocation::captured() const noexcept {return impl_->scope;}
MessengerCallResult ModelMovementCollisionInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    std::shared_ptr<ModelMovementCollisionServices> services) {
    events.require_model_execution_access();auto& state=*impl_;auto& scope=state.scope;using P=Impl::Phase;
    if(state.fault || state.executing || (state.messenger && (state.messenger!=&messenger || state.events!=&events ||
        scope.manager.lock()!=events.model_manager()))) {
        state.fault=true;fail("Original movement collision requires the same nonrecursive, unfaulted execution owners.");
    }
    if(completed())return {true,0,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(state.executing);
    try {
        if(!state.messenger) {
            if(&events.messenger()!=&messenger)fail("Original movement collision requires its controller's own messenger.");
            state.messenger=&messenger;state.events=&events;scope.manager=events.model_manager();
        }
        if(state.services && services && state.services!=services)fail("A retained movement collision requires its original service owner.");
        if(!state.services)state.services=std::move(services);
        if(!scope.work)fail("Original movement collision dereferences its captured null work.");
        for(;;)switch(state.phase) {
        case P::wall_gate:
            if(scope.work->control.wall_check!=0) {
                state.select(scope.work->ride_data ? "HITWALLFULL":"HITWALL");state.phase=P::wall_send;
            } else state.phase=P::floor_gate;
            break;
        case P::wall_send: {
            const auto result=state.send();if(!result.completed)return result;state.phase=P::wall_reply;break;
        }
        case P::wall_reply:
            if(scope.work->control.wall_hit_script && state.boolean(1)!=0) {
                auto name=state.parent_name(); // Wall prefix is reloaded AFTER this parent read.
                state.script=ModelMovementHitRequest{scope.work->control.wall_hit_script,"WHit",std::move(name)};
                state.phase=P::wall_script;
            } else state.phase=P::floor_gate;
            break;
        case P::wall_script: {
            const auto result=state.run_script();if(!result.completed)return result;state.phase=P::floor_gate;break;
        }
        case P::floor_gate:
            if(scope.work->control.floor_check==0 || model_floor_motion(scope.work->control.movement,ModelFloorMotion::rising))return state.complete();
            state.select("HITFLOORCHECK");state.phase=P::floor_send;break;
        case P::floor_send: {
            const auto result=state.send();if(!result.completed)return result;state.phase=P::floor_reply;break;
        }
        case P::floor_reply: {
            if(messenger.buffer().booleans().empty())return state.complete();
            const auto& names=messenger.buffer().strings();
            if(!names.empty()) {
                const auto& name=names.front();if(!name)fail("Original floor response compares a null string.");
                if(!name->empty()) {
                    const auto& integers=messenger.buffer().integers();
                    if(integers.empty())fail("Original floor response reads an unavailable physical integer.");
                    state.ride_target=ModelMovementRideTarget{*name,integers.front()};
                }
            }
            state.floor_now=state.boolean(0);state.floor_crossed=state.boolean(1);state.phase=P::fall_gate;break;
        }
        case P::fall_gate:
            if(scope.work->control.check_fall==0) {state.phase=P::floor_script_gate;break;}
            if(model_floor_motion(scope.work->control.movement,ModelFloorMotion::falling)) {
                if(state.floor_crossed==0) {state.phase=P::floor_script_gate;break;}
                state.return_after_adjust=false;state.select("HITFLOORADJUST");state.phase=P::adjust;
            } else if(state.floor_now!=0) {
                state.return_after_adjust=true;state.select("HITFLOORADJUST");state.phase=P::adjust;
            } else {
                if(!scope.work->sequence.name)fail("Original fall gate compares a null sequence name.");
                if(*scope.work->sequence.name=="SQ_SLEP")return state.complete();
                state.fall=std::make_unique<ModelBaseInvocation>(scope.ram,"SQ_FALL");state.phase=P::fall_sequence;
            }
            break;
        case P::adjust: {
            const auto result=state.send();if(!result.completed)return result;state.phase=P::land;break;
        }
        case P::land:
            model_floor_motion(scope.work->control.movement,ModelFloorMotion::land);
            if(state.return_after_adjust)return state.complete();
            state.update_ride=true;state.phase=P::floor_script_gate;break;
        case P::fall_sequence: {
            const auto result=state.fall->resume(messenger,events,events.player_creation_services());state.check();
            if(!result.completed)return result;return state.complete();
        }
        case P::floor_script_gate: {
            const auto prefix=scope.work->control.floor_hit_script; // Floor prefix precedes the parent-name read.
            if(prefix && state.floor_crossed!=0) {
                state.script=ModelMovementHitRequest{prefix,"FHit",state.parent_name()};state.phase=P::floor_script;
            } else state.phase=P::ride_gate;
            break;
        }
        case P::floor_script: {
            const auto result=state.run_script();if(!result.completed)return result;state.phase=P::ride_gate;break;
        }
        case P::ride_gate:
            if(!state.update_ride || (!scope.work->ride_data && !state.ride_target))return state.complete();
            state.phase=P::ride;break;
        case P::ride:
            if(!state.services || !state.services->ride_update)
                return {false,0,"Original movement collision requires actual ride type, identity and initialization effects."};
            {const auto result=state.services->ride_update(scope,state.ride_target);state.check();
                if(!result.completed)return result;return state.complete();}
        case P::completed:return {true,0,{}};
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
