#include "dw4/model_collision.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw MessengerError(message); }
bool player_name(std::string_view name) { return !name.empty() && name.front()=='p'; }
const std::string& source_name(const MessengerBuffer& buffer) {
    if(buffer.strings().empty() || !buffer.strings()[0]) fail("CHKBULLET requires a non-null source name when read.");
    return *buffer.strings()[0];
}
float difference(float a,float b) {
    const float value=a-b;
    return std::abs(value)<std::numeric_limits<float>::min() ? std::copysign(0.0f,value) : value;
}
void face_source(ModelMotionPose& pose,const std::array<float,3>& source,const std::array<float,3>& target) {
    // 8030E81C..E850 uses the positions captured BEFORE target Lua. Scalar
    // translation copies preserve tiny values; the two fsubs round under NI=1.
    const auto x=difference(source[0],target[0]),z=difference(source[2],target[2]);
    const double angle=std::atan2(static_cast<double>(x),static_cast<double>(z));
    float heading=static_cast<float>(angle);
    if(std::abs(angle)<std::numeric_limits<float>::min()) heading=std::copysign(0.0f,heading);
    if(!std::isfinite(heading)) fail("Original collision heading has no finite native result.");
    auto candidate=pose;candidate.turn().desired[1]=heading;
    force_model_turn(candidate);pose=candidate;
}
}
struct ModelCollisionRuntime::Impl {
    enum class Phase { gate, body, arguments, push, record, script, heading, pop };
    struct Pending {
        std::uint64_t dispatch;
        std::size_t row;
        NativeMessenger* messenger;
        ModelControlState* model;
        ControllerObjectId recipient;
        Phase phase=Phase::gate;
        MessengerBox incoming;
        std::optional<ModelTargetHitRequest> request;
        std::array<float,3> source_position{},target_position{};
        std::optional<MessengerDispatch> record;
        std::size_t pushed_depth{};
        const MessengerBuffer* child{};
        Pending(const MessengerDelivery& delivery,NativeMessenger& sender,ModelControlState& target)
            :dispatch(delivery.dispatch_id),row(delivery.row_index),messenger(&sender),model(&target),recipient(delivery.recipient) {}
    };
    ModelHitBodyCache body;
    std::optional<Pending> pending;
    ModelCollisionResult result;
    bool running{},faulted{};
    explicit Impl(ModelHitBodyCache value):body(std::move(value)) {
        if(body.definition().source_occurrence()>std::numeric_limits<std::uint32_t>::max() ||
           body.instance_index()>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
            fail("Collision target identity exceeds the original messenger profile.");
    }
    ModelCollisionResult finish() {
        result.stop=ModelCollisionStop::complete;result.diagnostic.clear();pending.reset();return result;
    }
    ModelCollisionResult stop(ModelCollisionStop reason,const char* diagnostic) {
        result.stop=reason;result.diagnostic=diagnostic;return result;
    }
    void check_child(NativeMessenger& messenger) const {
        if(messenger.buffer_depth()!=pending->pushed_depth || &messenger.buffer()!=pending->child)
            fail("Complete the target hit's nested buffer work before restoring its parent.");
    }
};
ModelCollisionRuntime::ModelCollisionRuntime(ModelHitBodyCache body):impl_(std::make_unique<Impl>(std::move(body))) {}
ModelCollisionRuntime::~ModelCollisionRuntime()=default;
const ModelHitBodyCache& ModelCollisionRuntime::body() const noexcept { return impl_->body; }
bool ModelCollisionRuntime::pending() const noexcept { return impl_->pending.has_value(); }
bool ModelCollisionRuntime::faulted() const noexcept { return impl_->faulted; }
bool ModelCollisionRuntime::owns_delivery(const MessengerDelivery& delivery) const noexcept {
    return impl_->pending && impl_->pending->dispatch==delivery.dispatch_id && impl_->pending->row==delivery.row_index &&
        impl_->pending->recipient==delivery.recipient;
}
ModelCollisionResult ModelCollisionRuntime::result() const { return impl_->result; }
ModelCollisionResult ModelCollisionRuntime::handle(NativeMessenger& messenger,const MessengerDelivery& delivery,
    ModelControlState& model,ModelCollisionWork work,const MessengerServices& recipients,const ModelCollisionServices& services) {
    auto& owner=*impl_;
    if(owner.faulted) fail("The collision target has an uncertain outcome; automatic replay is forbidden.");
    if(owner.running) fail("Recursive collision delivery to this target is unsupported.");
    if(owner.pending && (!owns_delivery(delivery) || owner.pending->messenger!=&messenger || owner.pending->model!=&model))
        fail("Resume the same target collision delivery with its original live owners.");
    if(!owner.pending) {
        if(delivery.message.command!="CHKBULLET") fail("The prepared collision target handles CHKBULLET only.");
        if(delivery.recipient.is_native() || delivery.recipient.file_index!=owner.body.definition().source_occurrence() ||
           (delivery.recipient.reference_index!=owner.body.ram_reference() &&
            delivery.recipient.reference_index!=owner.body.ram_owner_reference()))
            fail("Collision delivery does not belong to this source-qualified target RAM.");
        owner.pending.emplace(delivery,messenger,model);
        owner.result={};
    }
    struct Guard { bool& flag; explicit Guard(bool& value):flag(value) {flag=true;} ~Guard(){flag=false;} } guard(owner.running);
    try {
        auto& state=*owner.pending;
        for(;;) switch(state.phase) {
        case Impl::Phase::gate:
            if(!std::isfinite(model.invulnerability_frames())) fail("Prepared collision invulnerability must be finite.");
            if(model.invulnerability_frames()!=0 &&
               (!player_name(owner.body.definition().model_name()) || !player_name(source_name(messenger.buffer())) || !work.hp_reference_present))
                return owner.finish();
            if(work.control_enabled==0) return owner.finish();
            if(!work.current_sequence) fail("Enabled collision target requires its actual current sequence name.");
            if(*work.current_sequence=="SQ_ERAS") return owner.finish();
            state.phase=Impl::Phase::body;break;
        case Impl::Phase::body: {
            if(!state.incoming) {
                const auto& objects=messenger.buffer().objects();
                if(objects.empty()) fail("CHKBULLET requires its incoming AABox object.");
                const auto* box=std::get_if<MessengerBox>(&objects[0]);
                if(!box || !*box) fail("CHKBULLET object zero is not an owned AABox.");
                state.incoming=*box;
            }
            if(!services.system_time) return owner.stop(ModelCollisionStop::system_time_required,"Target body calculation requires the original system timestamp.");
            const auto time=services.system_time();
            if(!time) return owner.stop(ModelCollisionStop::system_time_required,"Original system timestamp is unavailable.");
            const auto& boxes=owner.body.update(model.pose.matrix(),*time);
            for(std::size_t i=0;i<boxes.size();++i) if(original_boxes_overlap(*state.incoming,boxes[i])) {owner.result.body_ordinal=i;break;}
            if(!owner.result.body_ordinal) return owner.finish();
            state.phase=Impl::Phase::arguments;break;
        }
        case Impl::Phase::arguments: {
            const auto& buffer=messenger.buffer();
            if(buffer.integers().size()<2 || buffer.matrices().empty()) fail("Overlapping CHKBULLET requires kind, source RAM and matrix.");
            const auto& definition=owner.body.definition();
            ModelTargetHitRequest request;
            request.target=state.recipient;
            request.target_source=definition.source();request.target_name=definition.model_name();
            request.target_ram=static_cast<std::int32_t>(owner.body.instance_index());
            request.kind=buffer.integers()[0];request.body_ordinal=static_cast<std::int32_t>(*owner.result.body_ordinal);
            request.source_name=source_name(buffer);request.source_ram=buffer.integers()[1];
            for(std::size_t i=0;i<3;++i) {state.source_position[i]=buffer.matrices()[0][12+i];state.target_position[i]=model.pose.matrix()[12+i];}
            state.request=std::move(request);state.phase=Impl::Phase::push;break;
        }
        case Impl::Phase::push:
            messenger.push_buffer();state.pushed_depth=messenger.buffer_depth();state.child=&messenger.buffer();
            state.phase=player_name(state.request->source_name) ? Impl::Phase::script : Impl::Phase::record;break;
        case Impl::Phase::record: {
            if(!state.record) state.record.emplace(messenger.begin_send_to("MODELCTRL",state.request->source_name,state.request->source_ram,"GETRECNO"));
            const auto result=messenger.resume(*state.record,recipients);
            if(result.stop==MessengerStop::faulted) throw MessengerError(result.diagnostic);
            if(result.stop!=MessengerStop::completed) return owner.stop(ModelCollisionStop::record_required,"Complete the source GETRECNO delivery before target hit effects.");
            state.record.reset();owner.check_child(messenger);
            const auto& records=messenger.buffer().integers();
            state.request->source_record=records.size()==1 ? records[0] : -9;
            state.phase=state.request->source_record==-9 ? Impl::Phase::pop : Impl::Phase::script;break;
        }
        case Impl::Phase::script: {
            if(!services.hit_script) return owner.stop(ModelCollisionStop::hit_script_required,"The target hit script requires its real Lua/HP/global-context owner.");
            const auto direction=services.hit_script(*state.request);
            if(!direction) return owner.stop(ModelCollisionStop::hit_script_required,"The target hit script is incomplete.");
            owner.check_child(messenger);
            state.phase=*direction!=0 ? Impl::Phase::heading : Impl::Phase::pop;break;
        }
        case Impl::Phase::heading:
            face_source(model.pose,state.source_position,state.target_position);state.phase=Impl::Phase::pop;break;
        case Impl::Phase::pop:
            owner.check_child(messenger);messenger.pop_buffer();
            if(messenger.buffer().booleans().empty()) messenger.buffer().add_bool(1);
            return owner.finish();
        }
    } catch(...) {owner.faulted=true;throw;}
}
} // namespace dw4
