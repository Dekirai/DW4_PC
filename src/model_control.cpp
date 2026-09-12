#include "dw4/model_control.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <type_traits>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t pad_storage_limit=4096;
void pad_storage_size(std::size_t size) {
    if(size>pad_storage_limit)throw MotionError("PDMOVE backing exceeds its native float budget.");
}
void name(std::string_view value) {
    if (value.size()>4096 || value.find('\0')!=std::string_view::npos)
        throw MotionError("Model command/sequence names must contain at most 4096 bytes without NUL.");
}
void finite(float value) {
    if (!std::isfinite(value)) throw MotionError("Model command parameters must be finite.");
}
void field(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot,std::string_view type) {
    const auto* stored=doc.field(object,slot);
    if (!stored || doc.metadata.field_types.at(stored->type_index).name!=type)
        throw MotionError("Model command has a missing or unsupported field.");
}
float number(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot) {
    field(doc,object,slot,"igFloatMetaField");
    const auto value=doc.field_f32(object,slot); finite(value); return value;
}
} // namespace

ModelPadMotionList::ModelPadMotionList(Storage values) {
    pad_storage_size(values.size());
    count_=static_cast<std::int32_t>(values.size());
    if(!values.empty())backing_=std::make_shared<Storage>(std::move(values));
}
PadMotionParameters::PadMotionParameters():PadMotionParameters(std::make_shared<ModelPadMotionList>()) {}
PadMotionParameters::PadMotionParameters(std::shared_ptr<ModelPadMotionList> list)
    :slot_(std::make_shared<std::shared_ptr<ModelPadMotionList>>(std::move(list))) {}
PadMotionParameters::PadMotionParameters(std::shared_ptr<std::shared_ptr<ModelPadMotionList>> slot):slot_(std::move(slot)) {
    if(!slot_)throw MotionError("PDMOVE requires an actual reference field slot.");
}
PadMotionParameters::PadMotionParameters(const PadMotionParameters& other) {
    if(!other.slot_)return;
    slot_=std::make_shared<std::shared_ptr<ModelPadMotionList>>();
    const auto source=other.list_owner();
    if(!source)return;
    std::shared_ptr<ModelPadMotionList::Storage> backing;
    if(source->backing_) {
        pad_storage_size(source->backing_->size());
        backing=std::make_shared<ModelPadMotionList::Storage>(*source->backing_);
    }
    *slot_=std::make_shared<ModelPadMotionList>(source->count_,std::move(backing));
}
PadMotionParameters::PadMotionParameters(PadMotionParameters&& other) noexcept
    :slot_(std::move(other.slot_)) {}
PadMotionParameters& PadMotionParameters::operator=(const PadMotionParameters& other) {
    publish_snapshot(other);return *this;
}
PadMotionParameters& PadMotionParameters::operator=(PadMotionParameters&& other) {
    return operator=(static_cast<const PadMotionParameters&>(other));
}
PadMotionParameters PadMotionParameters::detached_snapshot() const { return *this; }
void PadMotionParameters::publish_snapshot(const PadMotionParameters& other) {
    if(this==&other)return;
    if(!slot_) {
        auto restored=other.detached_snapshot();
        slot_=std::move(restored.slot_);return;
    }
    if(!other.slot_)throw MotionError("PDMOVE snapshot has transferred its actual list binding.");
    const auto list=list_owner();const auto source_list=other.list_owner();
    if(list==source_list)return;
    if(!list || !source_list) {
        throw MotionError("PDMOVE snapshot publication cannot replace an actual list reference.");
    }
    const auto& source=source_list->backing_;
    if(source) {
        pad_storage_size(source->size());
        // Different list owners may intentionally share actual backing. A
        // value publication does not replace that allocation or its aliases.
        if(source!=list->backing_) {
            prepare_storage(source->size());
            std::copy(source->begin(),source->end(),list->backing_->begin());
        }
    } else if(source_list->count_!=0 && list->backing_) {
        throw MotionError("PDMOVE publication cannot turn missing source backing into valid storage.");
    }
    // In particular, assigning a fresh empty snapshot retains existing backing.
    list->count_=source_list->count_;
}
void PadMotionParameters::require_owner() const {
    if(!list_owner())throw MotionError("PDMOVE requires the actual work parameter-list owner.");
}
std::int32_t PadMotionParameters::size() const { require_owner();return list_owner()->count_; }
void PadMotionParameters::clear() { require_owner();list_owner()->clear(); }
void PadMotionParameters::prepare_storage(std::size_t size) {
    require_owner();pad_storage_size(size);
    const auto list=list_owner();
    if(!list->backing_)list->backing_=std::make_shared<ModelPadMotionList::Storage>(size);
    else {
        pad_storage_size(list->backing_->size());
        if(list->backing_->size()<size)list->backing_->resize(size);
    }
}
void PadMotionParameters::set_values(const Values& values) {
    prepare_storage(values.size());
    const auto list=list_owner();
    std::copy(values.begin(),values.end(),list->backing_->begin());list->count_=4;
}
std::optional<PadMotionParameters::Values> PadMotionParameters::values() const {
    require_owner();
    if(list_owner()->count_!=4)return std::nullopt;
    const auto stop=backing_pair(false),move=backing_pair(true);
    return Values{stop[0],stop[1],move[0],move[1]};
}
void PadMotionParameters::apply(const PadMotionCommand& command) {
    name(command.name);finite(command.value);finite(command.frames);require_owner();
    const auto list=list_owner();
    if(list->count_==0) {
        // 8030AF48..AF90 appends four positive zeros to this same list.
        prepare_storage(4);
        std::fill_n(list->backing_->begin(),4,0.0f);list->count_=4;
    }
    const auto index=command.name=="Move" ? 2 : 0;
    const bool write_value=index<list->count_,write_frames=index+1<list->count_;
    if(write_value || write_frames) {
        const auto last=static_cast<std::size_t>(index+(write_frames ? 1 : 0));
        if(!list->backing_ || last>=list->backing_->size())
            throw MotionError("PDMOVE guarded store has no addressable backing float.");
        pad_storage_size(list->backing_->size());
        if(write_value)(*list->backing_)[static_cast<std::size_t>(index)]=command.value;
        if(write_frames)(*list->backing_)[static_cast<std::size_t>(index+1)]=command.frames;
    }
}
std::array<float,2> PadMotionParameters::backing_pair(bool moving) const {
    require_owner();
    const auto list=list_owner();
    const std::size_t index=moving ? 2 : 0;
    if(!list->backing_ || list->backing_->size()<index+2)
        throw MotionError("onWalk transition requires addressable PDMOVE backing parameters.");
    pad_storage_size(list->backing_->size());
    return {(*list->backing_)[index],(*list->backing_)[index+1]};
}
bool PadMotionParameters::operator==(const PadMotionParameters& other) const {
    if(bool(slot_)!=bool(other.slot_))return false;
    const auto list=list_owner();const auto other_list=other.list_owner();
    if(!list || !other_list)return list==other_list;
    if(list->count_!=other_list->count_ || bool(list->backing_)!=bool(other_list->backing_))return false;
    if(!list->backing_)return true;
    const auto& a=*list->backing_;const auto& b=*other_list->backing_;
    if(a.size()!=b.size())return false;
    return std::equal(a.begin(),a.end(),b.begin(),[](float x,float y) {
        return std::bit_cast<std::uint32_t>(x)==std::bit_cast<std::uint32_t>(y);
    });
}

namespace {
std::shared_ptr<ModelControlFields> detached_control_fields(const ModelControlState& source) {
    auto fields=std::make_shared<ModelControlFields>(*source.fields_owner());
    MotionChannels movement(source.channels);PadMotionParameters pad(source.pad_motion);
    fields->movement=movement.list_owner();fields->pad_motion=pad.list_owner();
    return fields;
}
}
ModelControlState::ModelControlState(ModelMotionPose prepared_pose)
    :ModelControlState(std::move(prepared_pose),std::make_shared<ModelControlFields>(),std::make_shared<ModelRamTimerSlot>()) {}
ModelControlState::ModelControlState(ModelMotionPose prepared_pose,std::shared_ptr<ModelPadMotionList> pad)
    :ModelControlState(std::move(prepared_pose),std::move(pad),std::make_shared<MotionChannelList>()) {}
ModelControlState::ModelControlState(ModelMotionPose prepared_pose,std::shared_ptr<ModelPadMotionList> pad,
    std::shared_ptr<MotionChannelList> movement)
    :ModelControlState(std::move(prepared_pose),std::make_shared<ModelControlFields>(std::move(pad),std::move(movement)),
        std::make_shared<ModelRamTimerSlot>()) {}
ModelControlState::ModelControlState(ModelMotionPose prepared_pose,std::shared_ptr<ModelControlFields> fields,
    std::shared_ptr<ModelRamTimerSlot> timer)
    :fields_(std::move(fields)),timer_(std::move(timer)),pose(std::move(prepared_pose)),
      channels(std::shared_ptr<std::shared_ptr<MotionChannelList>>(fields_owner(),&fields_owner()->movement)),
      pad_motion(std::shared_ptr<std::shared_ptr<ModelPadMotionList>>(fields_owner(),&fields_owner()->pad_motion)) {
    (void)timer_slot_owner();
}
ModelControlState::ModelControlState(const ModelControlState& other)
    :ModelControlState(ModelMotionPose(other.pose),detached_control_fields(other),
        std::make_shared<ModelRamTimerSlot>(*other.timer_slot_owner())) {}
ModelControlState& ModelControlState::operator=(const ModelControlState& other) {
    if(this==&other)return *this;
    if(!fields_ || !timer_) {
        // Only a moved-from host view is reconstructed. Prepare all allocations
        // first; live views always publish into their existing field owners.
        auto restored=other.detached_snapshot();
        static_assert(std::is_nothrow_move_constructible_v<ModelControlState>);
        std::destroy_at(this);std::construct_at(this,std::move(restored));return *this;
    }
    auto fields=*other.fields_owner();const auto timer=*other.timer_slot_owner();
    auto next_pose=other.pose;
    // Check cross-binding publication before touching any live field. These
    // detached probes preserve the destination's unused backing/list shape.
    auto movement=channels;movement=other.channels;
    auto pad=pad_motion;pad=other.pad_motion;
    channels=movement;pad_motion=pad;pose=next_pose;timer_->value=timer.value;
    fields_->mode=fields.mode;fields_->phase=fields.phase;
    fields_->bullet_hit_script=std::move(fields.bullet_hit_script);
    fields_->wall_hit_script=std::move(fields.wall_hit_script);
    fields_->floor_hit_script=std::move(fields.floor_hit_script);
    fields_->check_fall=fields.check_fall;fields_->wall_check=fields.wall_check;
    fields_->floor_check=fields.floor_check;fields_->invulnerability_frames=fields.invulnerability_frames;
    return *this;
}
ModelControlState& ModelControlState::operator=(ModelControlState&& other) {
    return operator=(static_cast<const ModelControlState&>(other));
}
std::shared_ptr<ModelControlFields> ModelControlState::fields_owner() {
    if(!fields_)throw MotionError("This model control view has transferred its work fields.");
    return fields_;
}
std::shared_ptr<const ModelControlFields> ModelControlState::fields_owner() const {
    if(!fields_)throw MotionError("This model control view has transferred its work fields.");
    return fields_;
}
std::shared_ptr<ModelRamTimerSlot> ModelControlState::timer_slot_owner() {
    if(!timer_)throw MotionError("Model control requires its actual RAM timer slot.");
    return timer_;
}
std::shared_ptr<const ModelRamTimerSlot> ModelControlState::timer_slot_owner() const {
    if(!timer_)throw MotionError("Model control requires its actual RAM timer slot.");
    return timer_;
}
std::optional<MotionTimer>& ModelControlState::elapsed_timer() { return timer_slot_owner()->value; }
const std::optional<MotionTimer>& ModelControlState::elapsed_timer() const { return timer_slot_owner()->value; }
std::int32_t& ModelControlState::mode() { return fields_owner()->mode; }
const std::int32_t& ModelControlState::mode() const { return fields_owner()->mode; }
std::int32_t& ModelControlState::phase() { return fields_owner()->phase; }
const std::int32_t& ModelControlState::phase() const { return fields_owner()->phase; }
std::optional<std::string>& ModelControlState::bullet_hit_script() { return fields_owner()->bullet_hit_script; }
const std::optional<std::string>& ModelControlState::bullet_hit_script() const { return fields_owner()->bullet_hit_script; }
std::optional<std::string>& ModelControlState::wall_hit_script() { return fields_owner()->wall_hit_script; }
const std::optional<std::string>& ModelControlState::wall_hit_script() const { return fields_owner()->wall_hit_script; }
std::optional<std::string>& ModelControlState::floor_hit_script() { return fields_owner()->floor_hit_script; }
const std::optional<std::string>& ModelControlState::floor_hit_script() const { return fields_owner()->floor_hit_script; }
std::uint8_t& ModelControlState::check_fall() { return fields_owner()->check_fall; }
const std::uint8_t& ModelControlState::check_fall() const { return fields_owner()->check_fall; }
std::uint8_t& ModelControlState::wall_check() { return fields_owner()->wall_check; }
const std::uint8_t& ModelControlState::wall_check() const { return fields_owner()->wall_check; }
std::uint8_t& ModelControlState::floor_check() { return fields_owner()->floor_check; }
const std::uint8_t& ModelControlState::floor_check() const { return fields_owner()->floor_check; }
float& ModelControlState::invulnerability_frames() { return fields_owner()->invulnerability_frames; }
const float& ModelControlState::invulnerability_frames() const { return fields_owner()->invulnerability_frames; }

ModelCommand decode_model_command(const IgbDocument& doc,std::uint32_t reference) {
    const auto& object=doc.object(reference);
    const auto& type=doc.metadata.object_types.at(object.type_index).name;
    if (type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX") return decode_motion_command(doc,reference);
    if (type!="beModelCtrlCTRL" && type!="beModelCtrlPDMOVE" && type!="beModelCtrlMUTEKI")
        throw MotionError("Unsupported model command type.");
    if (doc.metadata.header.byte_order!=IgbByteOrder::little)
        throw MotionError("Big-endian model command objects are unverified.");
    field(doc,object,2,"igStringMetaField");
    if (doc.field(object,2)->size>4104) throw MotionError("Serialized model command name exceeds its byte budget.");
    const auto text=doc.field_string(object,2); name(text);
    if (type=="beModelCtrlCTRL") return ModelCtrlCommand{reference,std::string(text)};
    if (type=="beModelCtrlMUTEKI") return ModelMutekiCommand{reference,std::string(text),number(doc,object,3)};
    return PadMotionCommand{reference,std::string(text),number(doc,object,3),number(doc,object,4)};
}

void apply_model_command(ModelControlState& state,const ModelCommand& command,std::uint64_t world_time) {
    auto candidate=state.detached_snapshot();
    std::visit([&](const auto& value) {
        using Command=std::decay_t<decltype(value)>;
        name(value.name);
        if constexpr (std::is_same_v<Command,MotionCommand>) {
            if (!candidate.elapsed_timer()) {
                candidate.elapsed_timer().emplace();
                start_motion_timer(*candidate.elapsed_timer(),world_time);
            }
            if (candidate.pose.position_locked()) {
                candidate.pose.position_locked()=false;
                synchronize_model_heading(candidate.pose);
            }
            candidate.channels.apply(value);
        } else if constexpr (std::is_same_v<Command,ModelCtrlCommand>) {
            if (value.name=="onWalk") {candidate.mode()=1;candidate.phase()=0;}
            else if (value.name=="toWalk") {candidate.mode()=2;candidate.phase()=0;}
            else if (value.name=="toWait") {candidate.mode()=3;candidate.phase()=0;}
            else if (value.name=="isLive") {candidate.mode()=4;candidate.phase()=0;}
            else if (value.name=="onFall") candidate.check_fall()=true;
            else if (value.name=="noFall") candidate.check_fall()=false;
        } else if constexpr (std::is_same_v<Command,PadMotionCommand>) {
            candidate.pad_motion.apply(value);
        } else {
            finite(value.frames);
            // 8030C1F0 ensures KEIKATIMER even for the four flag-only names.
            if (!candidate.elapsed_timer()) {
                candidate.elapsed_timer().emplace();
                start_motion_timer(*candidate.elapsed_timer(),world_time);
            }
            if (value.name=="OFFWALL") candidate.wall_check()=0;
            else if (value.name=="ONWALL") candidate.wall_check()=1;
            else if (value.name=="OFFFLOOR") candidate.floor_check()=0;
            else if (value.name=="ONFLOOR") candidate.floor_check()=1;
            else candidate.invulnerability_frames()=value.frames;
        }
    },command);
    state.publish_snapshot(candidate);
}

ModelControlDecision update_model_control(ModelControlState& state,std::uint8_t key_input,
    std::string_view current_sequence,std::string_view requested_sequence) {
    name(current_sequence);name(requested_sequence);
    auto candidate=state.detached_snapshot();
    ModelControlDecision decision{std::string(requested_sequence),false};
    if (requested_sequence=="SQ_NONE") {
        if (candidate.mode()==1) {
            // The original loads channel 2 before examining phase/input. It
            // requires a previously allocated channel list even on a no-op.
            if (!candidate.channels.allocated()) throw MotionError("onWalk control requires allocated motion channels.");
            std::optional<bool> moving;
            if (candidate.phase()==0) moving=key_input!=0;
            else if (candidate.phase()==1 && key_input==0) moving=false;
            else if (candidate.phase()==2 && key_input==1) moving=true;
            if (moving) {
                const auto parameters=candidate.pad_motion.backing_pair(*moving);
                MotionCommand motion;
                motion.type="beModelCtrlMOVE";motion.channel=2;
                motion.setup={*moving ? MotionMode::accelerate_positive : MotionMode::stop_positive,
                    parameters[0],parameters[1],0};
                // 80309168 directly configures the existing scalar. Do not
                // run MOVE's timer creation, position unlock or heading sync.
                candidate.channels.apply(motion);
                candidate.phase()=*moving ? 1 : 2;
            }
        } else if (candidate.mode()==2 && key_input==1) decision.requested_sequence="SQ_WALK";
        else if (candidate.mode()==3 && key_input==0) decision.requested_sequence="SQ_WAIT";
    }
    decision.dispatch_sequence=decision.requested_sequence!="SQ_NONE" && decision.requested_sequence!=current_sequence;
    state.publish_snapshot(candidate);
    return decision;
}

ModelMotionStep advance_model_motion(ModelControlState& state,std::uint64_t world_time) {
    if (!state.channels.allocated()) {state.pose.translated()=false;return {};}
    if (!state.elapsed_timer()) throw MotionError("Allocated model motion requires its KEIKATIMER.");
    return state.channels.advance_model(state.pose,*state.elapsed_timer(),world_time);
}
} // namespace dw4
