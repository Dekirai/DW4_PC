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
PadMotionParameters::PadMotionParameters():list_(std::make_shared<ModelPadMotionList>()) {}
PadMotionParameters::PadMotionParameters(std::shared_ptr<ModelPadMotionList> list):list_(std::move(list)) {}
PadMotionParameters::PadMotionParameters(const PadMotionParameters& other):transferred_(other.transferred_) {
    if(!other.list_)return;
    std::shared_ptr<ModelPadMotionList::Storage> backing;
    if(other.list_->backing_) {
        pad_storage_size(other.list_->backing_->size());
        backing=std::make_shared<ModelPadMotionList::Storage>(*other.list_->backing_);
    }
    list_=std::make_shared<ModelPadMotionList>(other.list_->count_,std::move(backing));
}
PadMotionParameters::PadMotionParameters(PadMotionParameters&& other) noexcept
    : list_(std::move(other.list_)),transferred_(other.transferred_) { other.transferred_=true; }
PadMotionParameters& PadMotionParameters::operator=(const PadMotionParameters& other) {
    publish_snapshot(other);return *this;
}
PadMotionParameters& PadMotionParameters::operator=(PadMotionParameters&& other) {
    return operator=(static_cast<const PadMotionParameters&>(other));
}
PadMotionParameters PadMotionParameters::detached_snapshot() const { return *this; }
void PadMotionParameters::publish_snapshot(const PadMotionParameters& other) {
    if(this==&other)return;
    if(transferred_) {
        auto restored=other.detached_snapshot();
        list_=std::move(restored.list_);transferred_=restored.transferred_;return;
    }
    if(other.transferred_)throw MotionError("PDMOVE snapshot has transferred its actual list binding.");
    if(list_==other.list_)return;
    if(!list_ || !other.list_) {
        if(!list_ && !other.list_)return;
        throw MotionError("PDMOVE snapshot publication cannot replace an actual list reference.");
    }
    const auto& source=other.list_->backing_;
    if(source) {
        pad_storage_size(source->size());
        // Different list owners may intentionally share actual backing. A
        // value publication does not replace that allocation or its aliases.
        if(source!=list_->backing_) {
            prepare_storage(source->size());
            std::copy(source->begin(),source->end(),list_->backing_->begin());
        }
    } else if(other.list_->count_!=0 && list_->backing_) {
        throw MotionError("PDMOVE publication cannot turn missing source backing into valid storage.");
    }
    // In particular, assigning a fresh empty snapshot retains existing backing.
    list_->count_=other.list_->count_;
}
void PadMotionParameters::require_owner() const {
    if(!list_)throw MotionError("PDMOVE requires the actual work parameter-list owner.");
}
std::int32_t PadMotionParameters::size() const { require_owner();return list_->count_; }
void PadMotionParameters::clear() { require_owner();list_->clear(); }
void PadMotionParameters::prepare_storage(std::size_t size) {
    require_owner();pad_storage_size(size);
    if(!list_->backing_)list_->backing_=std::make_shared<ModelPadMotionList::Storage>(size);
    else {
        pad_storage_size(list_->backing_->size());
        if(list_->backing_->size()<size)list_->backing_->resize(size);
    }
}
void PadMotionParameters::set_values(const Values& values) {
    prepare_storage(values.size());
    std::copy(values.begin(),values.end(),list_->backing_->begin());list_->count_=4;
}
std::optional<PadMotionParameters::Values> PadMotionParameters::values() const {
    require_owner();
    if(list_->count_!=4)return std::nullopt;
    const auto stop=backing_pair(false),move=backing_pair(true);
    return Values{stop[0],stop[1],move[0],move[1]};
}
void PadMotionParameters::apply(const PadMotionCommand& command) {
    name(command.name);finite(command.value);finite(command.frames);require_owner();
    if(list_->count_==0) {
        // 8030AF48..AF90 appends four positive zeros to this same list.
        prepare_storage(4);
        std::fill_n(list_->backing_->begin(),4,0.0f);list_->count_=4;
    }
    const auto index=command.name=="Move" ? 2 : 0;
    const bool write_value=index<list_->count_,write_frames=index+1<list_->count_;
    if(write_value || write_frames) {
        const auto last=static_cast<std::size_t>(index+(write_frames ? 1 : 0));
        if(!list_->backing_ || last>=list_->backing_->size())
            throw MotionError("PDMOVE guarded store has no addressable backing float.");
        pad_storage_size(list_->backing_->size());
        if(write_value)(*list_->backing_)[static_cast<std::size_t>(index)]=command.value;
        if(write_frames)(*list_->backing_)[static_cast<std::size_t>(index+1)]=command.frames;
    }
}
std::array<float,2> PadMotionParameters::backing_pair(bool moving) const {
    require_owner();
    const std::size_t index=moving ? 2 : 0;
    if(!list_->backing_ || list_->backing_->size()<index+2)
        throw MotionError("onWalk transition requires addressable PDMOVE backing parameters.");
    pad_storage_size(list_->backing_->size());
    return {(*list_->backing_)[index],(*list_->backing_)[index+1]};
}
bool PadMotionParameters::operator==(const PadMotionParameters& other) const {
    if(transferred_!=other.transferred_)return false;
    if(!list_ || !other.list_)return list_==other.list_;
    if(list_->count_!=other.list_->count_ || bool(list_->backing_)!=bool(other.list_->backing_))return false;
    if(!list_->backing_)return true;
    const auto& a=*list_->backing_;const auto& b=*other.list_->backing_;
    if(a.size()!=b.size())return false;
    return std::equal(a.begin(),a.end(),b.begin(),[](float x,float y) {
        return std::bit_cast<std::uint32_t>(x)==std::bit_cast<std::uint32_t>(y);
    });
}

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
            if (!candidate.elapsed_timer) {
                candidate.elapsed_timer.emplace();
                start_motion_timer(*candidate.elapsed_timer,world_time);
            }
            if (candidate.pose.position_locked) {
                candidate.pose.position_locked=false;
                synchronize_model_heading(candidate.pose);
            }
            candidate.channels.apply(value);
        } else if constexpr (std::is_same_v<Command,ModelCtrlCommand>) {
            if (value.name=="onWalk") {candidate.mode=1;candidate.phase=0;}
            else if (value.name=="toWalk") {candidate.mode=2;candidate.phase=0;}
            else if (value.name=="toWait") {candidate.mode=3;candidate.phase=0;}
            else if (value.name=="isLive") {candidate.mode=4;candidate.phase=0;}
            else if (value.name=="onFall") candidate.check_fall=true;
            else if (value.name=="noFall") candidate.check_fall=false;
        } else if constexpr (std::is_same_v<Command,PadMotionCommand>) {
            candidate.pad_motion.apply(value);
        } else {
            finite(value.frames);
            // 8030C1F0 ensures KEIKATIMER even for the four flag-only names.
            if (!candidate.elapsed_timer) {
                candidate.elapsed_timer.emplace();
                start_motion_timer(*candidate.elapsed_timer,world_time);
            }
            if (value.name=="OFFWALL") candidate.wall_check=0;
            else if (value.name=="ONWALL") candidate.wall_check=1;
            else if (value.name=="OFFFLOOR") candidate.floor_check=0;
            else if (value.name=="ONFLOOR") candidate.floor_check=1;
            else candidate.invulnerability_frames=value.frames;
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
        if (candidate.mode==1) {
            // The original loads channel 2 before examining phase/input. It
            // requires a previously allocated channel list even on a no-op.
            if (!candidate.channels.allocated()) throw MotionError("onWalk control requires allocated motion channels.");
            std::optional<bool> moving;
            if (candidate.phase==0) moving=key_input!=0;
            else if (candidate.phase==1 && key_input==0) moving=false;
            else if (candidate.phase==2 && key_input==1) moving=true;
            if (moving) {
                const auto parameters=candidate.pad_motion.backing_pair(*moving);
                MotionCommand motion;
                motion.type="beModelCtrlMOVE";motion.channel=2;
                motion.setup={*moving ? MotionMode::accelerate_positive : MotionMode::stop_positive,
                    parameters[0],parameters[1],0};
                // 80309168 directly configures the existing scalar. Do not
                // run MOVE's timer creation, position unlock or heading sync.
                candidate.channels.apply(motion);
                candidate.phase=*moving ? 1 : 2;
            }
        } else if (candidate.mode==2 && key_input==1) decision.requested_sequence="SQ_WALK";
        else if (candidate.mode==3 && key_input==0) decision.requested_sequence="SQ_WAIT";
    }
    decision.dispatch_sequence=decision.requested_sequence!="SQ_NONE" && decision.requested_sequence!=current_sequence;
    state.publish_snapshot(candidate);
    return decision;
}

ModelMotionStep advance_model_motion(ModelControlState& state,std::uint64_t world_time) {
    if (!state.channels.allocated()) {state.pose.translated=false;return {};}
    if (!state.elapsed_timer) throw MotionError("Allocated model motion requires its KEIKATIMER.");
    return state.channels.advance_model(state.pose,*state.elapsed_timer,world_time);
}
} // namespace dw4
