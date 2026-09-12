#include "dw4/model_motion_actual.hpp"

#include "dw4/actor_scene.hpp"
#include "dw4/model_player_work.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/model_timer.hpp"

#include <bit>
#include <cmath>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw MotionError(message);}
std::shared_ptr<MotionState> entry(const std::shared_ptr<MotionChannelList>& list,std::size_t index) {
    if(!list)fail("Actual movement dereferences a null movement list.");
    if(index>=list->size())fail("Actual movement has no represented physical channel slot.");
    const auto value=list->entry_owner(index);
    if(!value)fail("Actual movement dereferences a null reached channel.");
    return value;
}
std::shared_ptr<ModelSceneOne> one(const std::shared_ptr<ModelSceneRam>& ram) {
    if(!ram)fail("Actual movement dereferences a null RAM.");
    const auto value=find_model_one(*ram);
    if(!value)fail("Actual movement dereferences a missing first named ONE.");
    return value;
}
ActorSceneNode& node(const std::shared_ptr<ModelSceneOne>& one) {
    if(!one || !one->node)fail("Actual movement dereferences a null reached ONE node.");
    return *one->node;
}
AnimationMatrix copy_matrix(const AnimationMatrix& source) {
    AnimationMatrix result;
    for(std::size_t i=0;i<result.size();++i) {
        if(!std::isfinite(source[i]))fail("Actual movement reached a non-finite matrix-copy value.");
        const auto word=std::bit_cast<std::uint32_t>(source[i]);
        // A610 / 80128CF4 GQR0 paired stores flush copied subnormal values.
        result[i]=(word&0x7f800000u)==0 ? std::bit_cast<float>(word&0x80000000u) : source[i];
    }
    return result;
}
}
struct ModelMotionInvocation::Impl {
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelPlayerWork> work;
    std::shared_ptr<ModelRamTimerAccess> timer;
    std::shared_ptr<MotionChannelList> captured_list;
    std::shared_ptr<ModelSceneOne> translation_one;
    std::shared_ptr<ModelPlacementNode> setter_node;
    std::function<MessengerCallResult(const AnimationMatrix&)> setter;
    AnimationMatrix candidate{};
    ModelMotionStep result;
    enum class Phase {start,integrate,publish,box,completed} phase=Phase::start;
    bool executing{},fault{};
    float delta{};
    ModelRamTimerAccess& timer_access() {
        if(!timer)timer=std::make_shared<ModelRamTimerAccess>(ram);
        if(timer->ram_owner()!=ram)fail("Actual movement requires a timer endpoint for its captured RAM.");
        return *timer;
    }
    void healthy() const {if(fault)fail("A caught recursive failure invalidated this actual movement invocation.");}
};
ModelMotionInvocation::ModelMotionInvocation(std::shared_ptr<ModelSceneRam> ram,std::shared_ptr<ModelSceneInfo> parent,
    std::shared_ptr<ModelPlayerWork> work,std::shared_ptr<ModelRamTimerAccess> timer):impl_(std::make_unique<Impl>()) {
    impl_->ram=std::move(ram);impl_->parent=std::move(parent);impl_->work=std::move(work);impl_->timer=std::move(timer);
}
ModelMotionInvocation::~ModelMotionInvocation()=default;
bool ModelMotionInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelMotionInvocation::faulted() const noexcept {return impl_->fault;}
const ModelMotionStep& ModelMotionInvocation::result() const noexcept {return impl_->result;}
MessengerCallResult ModelMotionInvocation::resume() {
    auto& state=*impl_;
    if(state.fault || state.executing) {state.fault=true;fail("Actual movement requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Guard {bool& running;explicit Guard(bool& value):running(value){running=true;}~Guard(){running=false;}} guard(state.executing);
    try {
        for(;;)switch(state.phase) {
        case Impl::Phase::start: {
            if(!state.work)fail("Actual movement dereferences its captured null work.");
            state.captured_list=state.work->control.movement;
            state.work->motion.translated=0;
            if(!state.captured_list)fail("Actual movement count dereferences its captured null list.");
            if(state.captured_list->size()==0) {state.phase=Impl::Phase::completed;break;}
            state.delta=state.timer_access().frames("KEIKATIMER");state.healthy();
            // A49C uses the captured list, while A4B4 reloads work+C8.
            if(entry(state.captured_list,3)->active) {
                state.result.channels.rotation=advance_motion_actual(*entry(state.work->control.movement,3),state.delta);
                const float radians=0x1.1df46ap-6f*state.result.channels.rotation;
                state.work->motion.turn.desired[1]=radians+state.work->motion.turn.desired[1];
                auto bits=std::bit_cast<std::uint32_t>(radians);
                if(bits>0x80000000u && bits<=0xff800000u)bits^=0x80000000u;
                state.work->motion.turn.rate[1]=1.25f*std::bit_cast<float>(bits);
            }
            // C85C computes angles before its independent RAM->ONE lookup.
            const auto angles=prepare_model_turn_angles(state.work->motion,&state.timer_access());state.healthy();
            if(angles) {
                auto matrix=node(one(state.ram)).matrix_owner();
                auto candidate=*matrix;original_model_turn_basis(candidate,*angles);
                *matrix=candidate;state.work->motion.turn.applied=*angles;state.result.turned=true;
            }
            // A510 reloads the captured list's backing after turning. No
            // shape/alias or unrelated scalar-payload validation precedes it.
            state.result.channels.translation_updated=entry(state.captured_list,0)->active ||
                entry(state.captured_list,1)->active || entry(state.captured_list,2)->active;
            if(!state.result.channels.translation_updated) {state.phase=Impl::Phase::completed;break;}
            state.translation_one=one(state.ram);
            state.candidate=copy_matrix(*node(state.translation_one).matrix_owner());
            state.phase=Impl::Phase::integrate;break;
        }
        case Impl::Phase::integrate:
            for(std::size_t i=0;i<3;++i)
                state.result.channels.translation[i]=advance_motion_actual(*entry(state.work->control.movement,i),state.delta);
            translate_model_local(state.candidate,state.result.channels.translation);
            // A668 reads the retained translation ONE's current node again.
            state.setter_node=node(state.translation_one).placement_node();
            state.phase=Impl::Phase::publish;break;
        case Impl::Phase::publish: {
            if(!state.setter) {
                if(!state.setter_node || !state.setter_node->set_matrix)
                    return {false,0,"Actual movement requires its captured node's concrete virtual+94 setter."};
                state.setter=state.setter_node->set_matrix;
            }
            const auto result=state.setter(state.candidate);state.healthy();
            if(!result.completed)return result;
            state.work->motion.translated=1;
            state.setter={};state.setter_node.reset();state.phase=Impl::Phase::box;break;
        }
        case Impl::Phase::box: {
            const auto source=state.work->base_box;
            if(source) {
                auto& target=node(state.translation_one);auto destination=target.box_owner();
                if(!destination) {destination=std::make_shared<CollisionBox>();target.set_box_owner(destination);}
                *destination=*source;
                *destination=original_transform_box(*target.matrix_owner(),*destination);
            }
            state.phase=Impl::Phase::completed;break;
        }
        case Impl::Phase::completed:return {true,0,{}};
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
