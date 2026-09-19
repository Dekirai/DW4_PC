#include "dw4/hitland_floor.hpp"

#include <cmath>
#include <limits>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw HitLandQueryError(reason);}
float finite(float value) {
    if(!std::isfinite(value))fail("Original CHECKLAND exceeds its finite arithmetic profile.");
    return value;
}
float rounded(float value,double exact) {
    finite(value);
    constexpr float minimum=std::numeric_limits<float>::min();
    return std::abs(value)<minimum || (std::abs(value)==minimum && std::abs(exact)<minimum)
        ? std::copysign(0.f,value):value;
}
float sum(float a,float b) {finite(a);finite(b);return rounded(a+b,static_cast<double>(a)+b);}
float difference(float a,float b) {finite(a);finite(b);return rounded(a-b,static_cast<double>(a)-b);}
std::shared_ptr<HitLandCandidate> candidate(const HitLandQuery& query,std::int32_t index) {
    const auto result=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(query.candidates,index));
    if(!result)fail("Original CHECKLAND reads an unavailable candidate owner.");
    return result;
}
bool inside(float height,float y,float up,float down) {
    // Preserve the short circuit: the second arithmetic operation is reached
    // only when the first strict comparison passes.
    return sum(up,height)>y && difference(height,down)<y;
}
}

HitLandFloorSelection original_hitland_floor_selection(const HitLandQuery& query,
    float y,float up,float down) {
    HitLandFloorSelection selected;
    const auto count=query.candidate_count;
    if(count<=0)return selected;
    if(count>65536)fail("Original CHECKLAND candidate count exceeds its native allocation bound.");
    finite(y);
    float best_delta{};
    for(std::int32_t index=0;index<count;++index) {
        const float height=finite(candidate(query,index)->payload.position[1]);
        if(!inside(height,y,up,down))continue;
        const float delta=difference(height,y);
        if(!selected.found || delta>best_delta) {
            selected.found=true;selected.height=height;selected.index=index;best_delta=delta;
        }
    }
    if(!selected.found)for(std::int32_t index=0;index<count;++index) {
        const float height=finite(candidate(query,index)->payload.position[1]);
        if(height>=y)continue;
        const float delta=difference(y,height);
        if(!selected.found || delta<best_delta) {
            selected.found=true;selected.height=height;selected.index=index;best_delta=delta;
        }
    }
    if(!selected.found)for(std::int32_t index=0;index<count;++index) {
        const float height=finite(candidate(query,index)->payload.position[1]);
        if(index==0 || height<=selected.height) {
            selected.found=true;selected.height=height;selected.index=index;
        }
    }
    selected.in_range=inside(selected.height,y,up,down);
    selected.adjusted=difference(y,selected.height)<=0.f;
    return selected;
}

struct HitLandFloorInvocation::Impl {
    enum class Phase {gate,capture,prepare,traverse,reduce,publish,done};
    Phase phase=Phase::gate;
    std::shared_ptr<HitLandTraversalManager> manager;
    std::shared_ptr<HitLandQuery> query;
    std::unique_ptr<HitLandTraversalInvocation> traversal;
    NativeMessenger* messenger{};
    std::array<float,3> position{};
    float width{},up{},down{};
    HitLandFloorSelection selected;
    bool executing{},fault{};
};
HitLandFloorInvocation::HitLandFloorInvocation(std::shared_ptr<HitLandTraversalManager> manager)
    :impl_(std::make_unique<Impl>()) {impl_->manager=std::move(manager);}
HitLandFloorInvocation::~HitLandFloorInvocation()=default;
bool HitLandFloorInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool HitLandFloorInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult HitLandFloorInvocation::resume(NativeMessenger& messenger,
    std::shared_ptr<HitLandIntersectionServices> services) {
    auto& s=*impl_;using Phase=Impl::Phase;
    if(s.fault || s.executing || (s.messenger && s.messenger!=&messenger)) {
        s.fault=true;fail("CHECKLAND requires its original nonrecursive messenger invocation.");
    }
    if(completed())return {true,1,{}};
    s.messenger=&messenger;
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.executing);
    try {
        for(;;)switch(s.phase) {
        case Phase::gate:
            if(!messenger.buffer().booleans().empty() || messenger.buffer().vectors().empty() ||
                messenger.buffer().floats().empty()) {s.phase=Phase::done;break;}
            s.phase=Phase::capture;break;
        case Phase::capture:
            s.position=messenger.buffer().vectors()[0];
            // Only nonzero float count is checked by the original guard. The
            // three later physical reads are still required for a short list.
            s.width=messenger.buffer().floats()[0];
            if(messenger.buffer().floats().size()<2)fail("CHECKLAND reads an unavailable upper range.");
            s.up=messenger.buffer().floats()[1];
            if(messenger.buffer().floats().size()<3)fail("CHECKLAND reads an unavailable lower range.");
            s.down=messenger.buffer().floats()[2];s.phase=Phase::prepare;break;
        case Phase::prepare: {
            if(!s.manager || !s.manager->query)fail("CHECKLAND resets a null current query.");
            s.manager->query->reset();
            s.query=s.manager->query;
            const float right=sum(s.width,s.position[0]),far=sum(s.width,s.position[2]);
            const float left=difference(s.position[0],s.width),near=difference(s.position[2],s.width);
            constexpr float top=1000.f,bottom=-100.f;
            s.query->append_segment({right,top,far},{right,bottom,far});
            s.query->append_segment({left,top,far},{left,bottom,far});
            s.query->append_segment({right,top,near},{right,bottom,near});
            s.query->append_segment({left,top,near},{left,bottom,near});
            s.query->maximum={right,top,far};s.query->minimum={left,bottom,near};s.query->flags=1;
            s.traversal=std::make_unique<HitLandTraversalInvocation>(s.manager,s.query);
            s.phase=Phase::traverse;break;
        }
        case Phase::traverse: {
            const auto result=s.traversal->resume(services);
            if(s.fault)fail("A caught recursive call invalidated CHECKLAND traversal.");
            if(!result.completed)return result;
            s.traversal.reset();s.phase=Phase::reduce;break;
        }
        case Phase::reduce:
            s.selected=original_hitland_floor_selection(*s.query,s.position[1],s.up,s.down);
            if(s.selected.found) {
                messenger.buffer().clear_integers();
                const auto result=candidate(*s.query,s.selected.index);
                const auto name=result->name;
                messenger.buffer().add_int(result->child_index);
                messenger.buffer().add_string(name);
            }
            s.phase=Phase::publish;break;
        case Phase::publish:
            messenger.buffer().clear_booleans();
            messenger.buffer().clear_vectors();
            messenger.buffer().add_bool(s.selected.in_range ? 1:0);
            messenger.buffer().add_bool(s.selected.adjusted ? 1:0);
            messenger.buffer().add_bool(s.selected.found ? 1:0);
            messenger.buffer().add_vector({s.position[0],s.selected.height,s.position[2]});
            s.phase=Phase::done;break;
        case Phase::done:return {true,1,{}};
        }
    }catch(...){s.fault=true;throw;}
}

MessengerManager make_hitland_floor_receiver(std::shared_ptr<HitLandTraversalManager> manager,
    std::shared_ptr<HitLandIntersectionServices> services) {
    return {[manager=std::move(manager),services=std::move(services),call=std::shared_ptr<HitLandFloorInvocation>{}]
        (NativeMessenger& messenger,const MessengerDelivery& delivery) mutable -> MessengerCallResult {
        if(!call) {
            if(delivery.message.command!=std::optional<std::string>{"CHECKLAND"})
                return {false,0,"HITLANDMODEL requires the original implementation of this command."};
            call=std::make_shared<HitLandFloorInvocation>(manager);
        }
        return call->resume(messenger,services);
    },{}};
}
} // namespace dw4
