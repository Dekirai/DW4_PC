#include "dw4/hitland_wall.hpp"
#include "dw4/hitland_floor.hpp"
#include "dw4/hitland_wall_math.hpp"

#include <bit>
#include <initializer_list>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw HitLandQueryError(reason);}
std::shared_ptr<HitLandCandidate> candidate(const HitLandQuery& query,std::int32_t index) {
    const auto result=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(query.candidates,index));
    if(!result)fail("Original CHECKWALL reads an unavailable candidate owner.");
    return result;
}
void bounded_count(std::int32_t count) {
    if(count>65536)fail("Original CHECKWALL candidate count exceeds its native allocation bound.");
}
struct Minimum {std::int32_t index{};float distance{};};
Minimum longitudinal_minimum(const HitLandQuery& query) {
    const auto count=query.candidate_count;bounded_count(count);
    if(count<=0)fail("Longitudinal CHECKWALL selection requires a reached candidate.");
    Minimum selected;
    for(std::int32_t index=0;index<count;++index) {
        const float distance=candidate(query,index)->payload.distance;
        if(index==0 || distance<selected.distance) {selected.index=index;selected.distance=distance;}
    }
    return selected;
}
std::int32_t lateral_minimum(const HitLandQuery& query) {
    const auto count=query.candidate_count;bounded_count(count);
    if(count<=0)fail("Lateral CHECKWALL selection requires a reached candidate.");
    std::int32_t selected{};
    for(std::int32_t index=1;index<count;++index) {
        const auto next=candidate(query,index);
        const auto previous=candidate(query,selected);
        if(next->payload.distance<previous->payload.distance)selected=index;
    }
    return selected;
}
void metadata(NativeMessenger& messenger,const HitLandQuery& query,std::int32_t index) {
    const auto selected=candidate(query,index);
    const auto name=selected->name;
    messenger.buffer().add_int(selected->child_index);
    messenger.buffer().add_string(name);
}
}

struct HitLandWallInvocation::Impl {
    enum class Phase {gate,capture,prepare,preflight,longitudinal,lateral,traverse,
        after_preflight,after_longitudinal,after_lateral,publish,done};
    Phase phase=Phase::gate,after_traverse=Phase::done;
    std::shared_ptr<HitLandTraversalManager> manager;
    std::shared_ptr<HitLandQuery> query;
    std::unique_ptr<HitLandTraversalInvocation> traversal;
    NativeMessenger* messenger{};
    std::array<float,3> position{},direction{},side{},minus{},plus{};
    float width{},down{},radius{},doubled_radius{};
    std::uint32_t flags{};
    bool skip_preflight{},hit{},executing{},fault{};

    void reset_query() {
        if(!manager || !manager->query)fail("CHECKWALL resets a null current query.");
        manager->query->reset();query=manager->query;
    }
    void bounds(const std::array<float,3>& seed,
        std::initializer_list<std::array<float,3>> comparisons) {
        auto maximum=seed;
        for(const auto axis:{0u,2u})for(const auto& value:comparisons)
            original_hitland_wall_extremum(maximum[axis],value[axis],0);
        query->maximum={maximum[0],position[1],maximum[2]};
        auto minimum=seed;
        for(const auto axis:{0u,2u})for(const auto& value:comparisons)
            original_hitland_wall_extremum(minimum[axis],value[axis],1);
        query->minimum={minimum[0],position[1],minimum[2]};
    }
    void parallel_segments(float displacement) {
        const auto positive_start=original_hitland_wall_offset(minus,displacement,side);
        const auto positive_end=original_hitland_wall_offset(plus,displacement,side);
        query->append_segment(positive_start,positive_end);
        const auto negative_start=original_hitland_wall_offset(minus,-displacement,side);
        const auto negative_end=original_hitland_wall_offset(plus,-displacement,side);
        query->append_segment(negative_start,negative_end);
        bounds(positive_start,{negative_start,positive_end,negative_end});
    }
    void begin_traversal(Phase continuation) {
        traversal=std::make_unique<HitLandTraversalInvocation>(manager,query);
        after_traverse=continuation;phase=Phase::traverse;
    }
};
HitLandWallInvocation::HitLandWallInvocation(std::shared_ptr<HitLandTraversalManager> manager)
    :impl_(std::make_unique<Impl>()) {impl_->manager=std::move(manager);}
HitLandWallInvocation::~HitLandWallInvocation()=default;
bool HitLandWallInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool HitLandWallInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult HitLandWallInvocation::resume(NativeMessenger& messenger,
    std::shared_ptr<HitLandIntersectionServices> services) {
    auto& s=*impl_;using Phase=Impl::Phase;
    if(s.fault || s.executing || (s.messenger && s.messenger!=&messenger)) {
        s.fault=true;fail("CHECKWALL requires its original nonrecursive messenger invocation.");
    }
    if(completed())return {true,1,{}};
    s.messenger=&messenger;
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.executing);
    try {
        for(;;)switch(s.phase) {
        case Phase::gate:
            if(!messenger.buffer().booleans().empty() || messenger.buffer().vectors().empty() ||
                messenger.buffer().floats().empty() || messenger.buffer().integers().empty()) {
                s.phase=Phase::done;break;
            }
            s.phase=Phase::capture;break;
        case Phase::capture:
            s.position=messenger.buffer().vectors()[0];
            if(messenger.buffer().vectors().size()<2)fail("CHECKWALL reads an unavailable direction vector.");
            s.direction=messenger.buffer().vectors()[1];
            s.width=messenger.buffer().floats()[0];
            // Original float1 is read even though its value is not consumed.
            if(messenger.buffer().floats().size()<2)fail("CHECKWALL reads an unavailable upper range.");
            if(messenger.buffer().floats().size()<3)fail("CHECKWALL reads an unavailable lower range.");
            s.down=messenger.buffer().floats()[2];
            s.flags=std::bit_cast<std::uint32_t>(messenger.buffer().integers()[0])&~1u;
            if(messenger.buffer().integers().size()<2)fail("CHECKWALL reads an unavailable previous-hit indicator.");
            s.skip_preflight=messenger.buffer().integers()[1]!=0;
            messenger.buffer().clear_integers();s.phase=Phase::prepare;break;
        case Phase::prepare: {
            const auto base=original_hitland_wall_height(s.position,s.down);
            s.radius=original_hitland_wall_radius(s.width);
            if(!s.manager)fail("CHECKWALL reads a null manager rotation.");
            s.side=original_hitland_wall_side(s.direction,s.manager->wall_rotation);
            s.plus=original_hitland_wall_offset(base,s.radius,s.direction);
            s.minus=original_hitland_wall_offset(base,-s.radius,s.direction);
            s.phase=s.skip_preflight ? Phase::longitudinal:Phase::preflight;break;
        }
        case Phase::preflight:
            s.reset_query();s.query->flags=s.flags;
            s.query->append_segment(s.minus,s.plus);
            s.parallel_segments(s.radius);s.begin_traversal(Phase::after_preflight);break;
        case Phase::longitudinal:
            s.reset_query();s.doubled_radius=original_hitland_wall_twice_radius(s.radius);
            s.query->flags=s.flags;s.query->append_segment(s.minus,s.plus);
            s.parallel_segments(original_hitland_wall_half_radius(s.radius));
            s.begin_traversal(Phase::after_longitudinal);break;
        case Phase::lateral: {
            s.reset_query();auto base=original_hitland_wall_height(s.position,s.down);
            base[0]=original_hitland_wall_narrow(base[0]);
            const auto forward=original_hitland_wall_forward_offset(s.radius);
            s.query->flags=s.flags;base[2]=original_hitland_wall_narrow(base[2]);
            const auto center=original_hitland_wall_offset(base,forward,s.direction);
            const auto positive=original_hitland_wall_fused_offset(center,s.radius,s.side);
            s.query->append_segment(center,positive);
            const auto negative=original_hitland_wall_fused_offset(center,-s.radius,s.side);
            s.query->append_segment(center,negative);
            s.bounds(positive,{negative});s.begin_traversal(Phase::after_lateral);break;
        }
        case Phase::traverse: {
            const auto result=s.traversal->resume(services);
            if(s.fault)fail("A caught recursive call invalidated CHECKWALL traversal.");
            if(!result.completed)return result;
            s.traversal.reset();s.phase=s.after_traverse;break;
        }
        case Phase::after_preflight:
            s.phase=s.query->candidate_count>0 ? Phase::longitudinal:Phase::publish;break;
        case Phase::after_longitudinal:
            if(s.query->candidate_count>0) {
                const auto selected=longitudinal_minimum(*s.query);
                s.position=original_hitland_wall_longitudinal(s.position,s.doubled_radius,selected.distance,s.direction);
                metadata(messenger,*s.query,selected.index);s.hit=true;
            }
            s.phase=Phase::lateral;break;
        case Phase::after_lateral:
            if(s.query->candidate_count>0) {
                const auto index=lateral_minimum(*s.query);
                const auto selected=candidate(*s.query,index);
                s.position=original_hitland_wall_lateral(s.position,s.radius,selected->payload.distance,selected->payload.normal);
                metadata(messenger,*s.query,index);s.hit=true;
            }
            s.phase=Phase::publish;break;
        case Phase::publish:
            messenger.buffer().clear_booleans();messenger.buffer().clear_vectors();
            messenger.buffer().add_vector(s.position);messenger.buffer().add_bool(s.hit ? 1:0);
            s.phase=Phase::done;break;
        case Phase::done:return {true,1,{}};
        }
    }catch(...){s.fault=true;throw;}
}

MessengerManager make_hitland_query_receiver(std::shared_ptr<HitLandTraversalManager> manager,
    std::shared_ptr<HitLandIntersectionServices> services) {
    return {[manager=std::move(manager),services=std::move(services),floor=std::shared_ptr<HitLandFloorInvocation>{},
        wall=std::shared_ptr<HitLandWallInvocation>{},disabled=false]
        (NativeMessenger& messenger,const MessengerDelivery& delivery) mutable -> MessengerCallResult {
        if(floor)return floor->resume(messenger,services);
        if(wall)return wall->resume(messenger,services);
        if(disabled)return {true,1,{}};
        const auto& command=delivery.message.command;
        if(command==std::optional<std::string>{"CHECKLAND"}) {
            floor=std::make_shared<HitLandFloorInvocation>(manager);return floor->resume(messenger,services);
        }
        if(command==std::optional<std::string>{"CHECKWALL"} || command==std::optional<std::string>{"CHECKWALLFULL"}) {
            if(!manager)fail("HITLANDMODEL reads a null wall-enabled manager.");
            if(!manager->wall_enabled) {
                messenger.buffer().clear_booleans();messenger.buffer().add_bool(0);disabled=true;return {true,1,{}};
            }
            if(command==std::optional<std::string>{"CHECKWALL"}) {
                wall=std::make_shared<HitLandWallInvocation>(manager);return wall->resume(messenger,services);
            }
        }
        return {false,0,"HITLANDMODEL requires the original implementation of this command."};
    },{}};
}
} // namespace dw4
