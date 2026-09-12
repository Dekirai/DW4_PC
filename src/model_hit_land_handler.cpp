#include "dw4/model_hit_land_handler.hpp"

#include "dw4/action_event_runtime.hpp"
#include "dw4/hitland_math.hpp"
#include "dw4/model_scene.hpp"

#include <array>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw ModelSceneError(reason);}
std::shared_ptr<AnimationMatrix> matrix(ModelHitLandRam& ram) {
    const auto node=ram.node;
    if(!node)fail("HITLANDDELIVER reached its actual RAM's null current node.");
    return node->matrix_owner();
}
std::array<float,3> translation(const AnimationMatrix& value) {
    return {value[12],value[13],value[14]};
}
void store_translation(AnimationMatrix& target,const std::array<float,3>& value) {
    // 80128D90 uses scalar lfs/stfs. Preserve tiny/negative-zero values and
    // consume no unrelated matrix field or virtual node setter.
    target[12]=value[0];target[13]=value[1];target[14]=value[2];
}
}
struct ModelHitLandInvocation::Impl {
    enum class Phase {command,capture,select,send,wall_result,floor_result,adjust,completed};
    enum class Command {wall,wall_full,floor_check,floor_adjust,floor};
    std::shared_ptr<ModelHitLandRam> ram;
    std::weak_ptr<const ModelSceneManager> manager;
    std::optional<std::string> requested;
    Command command{};
    Phase phase=Phase::command;
    std::array<float,3> position{};
    std::optional<MessengerDispatch> dispatch;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::uint32_t returned{};
    bool queried{},executing{},fault{};
    Impl(std::shared_ptr<ModelHitLandRam> owner,std::optional<std::string> name,
        std::shared_ptr<const ModelSceneManager> invoking_manager)
        :ram(std::move(owner)),manager(std::move(invoking_manager)),requested(std::move(name)) {
        if(!ram)fail("HITLANDDELIVER requires its actual recipient RAM owner.");
    }
    void check() const {if(fault)fail("A caught recursive call invalidated HITLANDDELIVER.");}
};
ModelHitLandInvocation::ModelHitLandInvocation(std::shared_ptr<ModelHitLandRam> ram,std::optional<std::string> command,
    std::shared_ptr<const ModelSceneManager> manager)
    :impl_(std::make_unique<Impl>(std::move(ram),std::move(command),std::move(manager))) {}
ModelHitLandInvocation::~ModelHitLandInvocation()=default;
bool ModelHitLandInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelHitLandInvocation::faulted() const noexcept {return impl_->fault;}
const std::shared_ptr<ModelHitLandRam>& ModelHitLandInvocation::ram_owner() const noexcept {return impl_->ram;}

MessengerCallResult ModelHitLandInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events) {
    auto& s=*impl_;using P=Impl::Phase;using C=Impl::Command;
    if(s.fault || s.executing ||
        (s.messenger && (s.messenger!=&messenger || s.events!=&events))) {
        s.fault=true;fail("HITLANDDELIVER requires its same nonrecursive messenger execution owners.");
    }
    try {
        events.require_model_execution_access();
        if(!s.messenger && &events.messenger()!=&messenger)
            fail("HITLANDDELIVER requires its invoking manager's event messenger.");
    }catch(...){s.fault=true;throw;}
    if(completed())return {true,s.returned,{}};
    // The dispatcher supplies original r3, independently of later RAM.manager
    // replacement. Keep it alive during this resume, without a retained cycle.
    const auto manager=s.manager.lock();
    if(!manager){s.fault=true;fail("HITLANDDELIVER lost its captured invoking manager.");}
    s.messenger=&messenger;s.events=&events;
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.executing);
    try {
        for(;;)switch(s.phase) {
        case P::command:
            if(!s.requested)fail("HITLANDDELIVER command lookup dereferences a null command string.");
            if(s.requested->size()>65535 || s.requested->find('\0')!=std::string::npos)
                fail("HITLANDDELIVER command exceeds the represented native string profile.");
            if(*s.requested=="HITWALL")s.command=C::wall;
            else if(*s.requested=="HITWALLFULL")s.command=C::wall_full;
            else if(*s.requested=="HITFLOORCHECK")s.command=C::floor_check;
            else if(*s.requested=="HITFLOORADJUST")s.command=C::floor_adjust;
            else if(*s.requested=="HITFLOOR")s.command=C::floor;
            else {s.phase=P::completed;break;}
            s.returned=1;s.phase=s.command==C::floor_adjust ? P::adjust:P::capture;break;
        case P::capture:
            s.position=translation(*matrix(*s.ram));
            // DAA0..DABC: ordered X then Z equality; Y is unused by this gate.
            if(s.command==C::wall && s.ram->previous_position[0]==s.position[0] &&
                s.ram->previous_position[2]==s.position[2])s.phase=P::wall_result;
            else s.phase=P::select;
            break;
        case P::select: {
            if(!messenger.has_category("HITLANDMODEL"))
                return {false,0,"HITLANDDELIVER requires the actual HITLANDMODEL query manager."};
            messenger.select("HITLANDMODEL",std::nullopt,-1);
            s.check();
            auto& packet=messenger.buffer();
            packet.add_vector(s.position);
            const bool wall=s.command==C::wall || s.command==C::wall_full;
            if(s.command==C::wall) {
                // DAF8/DB08 reload previous coordinates after selection and
                // the first vector append; no eager arithmetic before that prefix.
                packet.add_vector(original_hitland_wall_direction(s.position,s.ram->previous_position));
            }else if(s.command==C::wall_full)packet.add_vector(s.position);
            packet.add_float(s.ram->half_width);
            packet.add_float(s.ram->up_range);
            packet.add_float(s.ram->down_range);
            packet.add_int(s.ram->check_flags);
            if(wall)packet.add_int(s.ram->position[0]==0.0f ? 0:1);
            s.dispatch.emplace(messenger.begin_send(s.command==C::wall ? "CHECKWALL":
                s.command==C::wall_full ? "CHECKWALLFULL":"CHECKLAND"));
            s.queried=true;s.phase=P::send;break;
        }
        case P::send: {
            const auto result=events.resume(*s.dispatch);s.check();
            if(result.stop==MessengerStop::faulted)fail("HITLANDDELIVER nested query delivery faulted.");
            if(result.stop!=MessengerStop::completed)return {false,0,result.diagnostic};
            s.dispatch.reset();s.phase=(s.command==C::wall || s.command==C::wall_full) ? P::wall_result:P::floor_result;
            break;
        }
        case P::wall_result: {
            std::uint8_t hit{};
            if(s.queried) {
                const auto& booleans=messenger.buffer().booleans();
                if(!booleans.empty())hit=booleans[0];
                s.ram->position={hit==1 ? 1.0f:0.0f,0.0f,0.0f};
            }
            std::uint8_t moved{};
            if(!messenger.buffer().vectors().empty()) {
                if(hit==1) {
                    const auto target=matrix(*s.ram);
                    const auto result=messenger.buffer().vectors()[0];
                    store_translation(*target,result);moved=1;
                }
                // This is a separate fresh result read after the node store.
                s.ram->previous_position=messenger.buffer().vectors()[0];
            }
            const std::array<std::uint8_t,3> values{moved,moved,0};
            messenger.buffer().replace_booleans(values);s.phase=P::completed;break;
        }
        case P::floor_result: {
            std::array<float,3> land{0.0f,-1.0f,0.0f};
            std::uint8_t hit{},adjusted{},valid{};
            if(messenger.buffer().booleans().size()>=3 && !messenger.buffer().vectors().empty()) {
                land=messenger.buffer().vectors()[0];
                hit=messenger.buffer().booleans()[0];adjusted=messenger.buffer().booleans()[1];
                valid=messenger.buffer().booleans()[2];
            }
            // E080..E108: bge after fcmpo is the complement of ordered <.
            // Query payloads are finite; a later current node Y remains live.
            if(land[1]<1.0f) {
                const auto current=translation(*matrix(*s.ram));
                hit=1;
                if(current[1]<0.0f) {
                    land[1]=0.0f;s.ram->land_position=land;s.ram->previous_position=land;
                    s.ram->land_position_ok=1;adjusted=1;
                }else {s.ram->land_position_ok=0;adjusted=0;}
            }else {
                s.ram->land_position=land;s.ram->previous_position=land;s.ram->land_position_ok=valid;
            }
            const std::array<std::uint8_t,2> values{hit,adjusted};
            messenger.buffer().replace_booleans(values);
            s.phase=s.command==C::floor ? P::adjust:P::completed;break;
        }
        case P::adjust:
            if(s.ram->land_position_ok!=0) {
                const auto target=matrix(*s.ram);
                store_translation(*target,s.ram->land_position);
            }
            s.phase=P::completed;break;
        case P::completed:return {true,s.returned,{}};
        }
    }catch(...){s.fault=true;throw;}
}
} // namespace dw4
