#include "dw4/model_control.hpp"

#include <cmath>
#include <type_traits>
#include <utility>

namespace dw4 {
namespace {
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
    auto candidate=state;
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
            finite(value.value);finite(value.frames);
            if (!candidate.pad_motion) candidate.pad_motion=std::array<float,4>{};
            const auto index=value.name=="Move" ? 2u : 0u;
            (*candidate.pad_motion)[index]=value.value;
            (*candidate.pad_motion)[index+1]=value.frames;
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
    state=std::move(candidate);
}

ModelControlDecision update_model_control(ModelControlState& state,std::uint8_t key_input,
    std::string_view current_sequence,std::string_view requested_sequence) {
    name(current_sequence);name(requested_sequence);
    auto candidate=state;
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
                if (!candidate.pad_motion) throw MotionError("onWalk transition requires PDMOVE parameters.");
                const auto index=*moving ? 2u : 0u;
                MotionCommand motion;
                motion.type="beModelCtrlMOVE";motion.channel=2;
                motion.setup={*moving ? MotionMode::accelerate_positive : MotionMode::stop_positive,
                    (*candidate.pad_motion)[index],(*candidate.pad_motion)[index+1],0};
                // 80309168 directly configures the existing scalar. Do not
                // run MOVE's timer creation, position unlock or heading sync.
                candidate.channels.apply(motion);
                candidate.phase=*moving ? 1 : 2;
            }
        } else if (candidate.mode==2 && key_input==1) decision.requested_sequence="SQ_WALK";
        else if (candidate.mode==3 && key_input==0) decision.requested_sequence="SQ_WAIT";
    }
    decision.dispatch_sequence=decision.requested_sequence!="SQ_NONE" && decision.requested_sequence!=current_sequence;
    state=std::move(candidate);
    return decision;
}

ModelMotionStep advance_model_motion(ModelControlState& state,std::uint64_t world_time) {
    if (!state.channels.allocated()) {state.pose.translated=false;return {};}
    if (!state.elapsed_timer) throw MotionError("Allocated model motion requires its KEIKATIMER.");
    return state.channels.advance_model(state.pose,*state.elapsed_timer,world_time);
}
} // namespace dw4
