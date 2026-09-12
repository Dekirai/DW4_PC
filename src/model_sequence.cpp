#include "dw4/model_sequence.hpp"
#include "dw4/model_scene.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace dw4 {
struct ModelSequenceExecutorLease {std::size_t actual_frames{},actual_started_frames{};};
std::shared_ptr<const ModelSequenceData> ModelSequenceDataOwner::prepared_owner() const {
    if(const auto* owner=std::get_if<std::shared_ptr<const ModelSequenceData>>(&value_))return *owner;
    throw MotionError("Actual sequence data cannot be read as a prepared command vector.");
}
std::shared_ptr<ModelSceneSequence> ModelSequenceDataOwner::actual_owner() const {
    if(const auto* owner=std::get_if<std::shared_ptr<ModelSceneSequence>>(&value_))return *owner;
    if(!std::get<std::shared_ptr<const ModelSequenceData>>(value_))return {};
    throw MotionError("Prepared sequence data cannot substitute for a mutable graph owner.");
}
ModelSequenceDataOwner::operator bool() const noexcept {
    return std::visit([](const auto& owner){return bool(owner);},value_);
}
ModelSequenceActualFrame::~ModelSequenceActualFrame() {
    if(lease_ && !completed_) {--lease_->actual_frames;if(began_)--lease_->actual_started_frames;}
    if(began_ && !completed_)if(const auto fields=fields_.lock())fields->executor_unavailable_=true;
}
namespace {
constexpr std::size_t max_sequences=4096,max_commands=65536,max_text=8u*1024u*1024u;
void checked_name(std::string_view value) {
    if(value.size()>4096 || value.find('\0')!=std::string_view::npos)
        throw MotionError("Sequence names must contain at most 4096 bytes without NUL.");
}
void checked_entry_fields(const ModelControlState& state) {
    for(const auto* script:{&state.bullet_hit_script(),&state.wall_hit_script(),&state.floor_hit_script()})
        if(*script)checked_name(**script);
    if(!std::isfinite(state.invulnerability_frames()))throw MotionError("Prepared invulnerability frames must be finite.");
}
void finish_entry_fields(ModelControlState& candidate) {
    candidate.bullet_hit_script().reset();candidate.wall_hit_script().reset();candidate.floor_hit_script().reset();
    candidate.wall_check()=1;candidate.floor_check()=1;candidate.check_fall()=true;
    if(candidate.invulnerability_frames()<0)candidate.invulnerability_frames()=0;
    candidate.channels.on_sequence_change();
}
void charge(std::size_t value,std::size_t& total,std::size_t limit) {
    if(value>limit-total)throw MotionError("Model sequence allocation budget exceeded.");
    total+=value;
}
void advance_command_index(ModelSequenceFields& fields) noexcept {
    // Original signed work field is incremented with a 32-bit integer add.
    // A callback can replace its value; avoid host signed-overflow UB before
    // the next checked command access rejects an unaddressable result.
    fields.command_index=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(fields.command_index)+1u);
}
const IgbObject& exact(const IgbDocument& doc,std::uint32_t ref,std::string_view type) {
    const auto& object=doc.object(ref);
    if(doc.metadata.object_types.at(object.type_index).name!=type)throw MotionError("Unsupported sequence object type.");
    return object;
}
const IgbFieldValue& field(const IgbDocument& doc,const IgbObject& obj,std::uint16_t slot,std::string_view type) {
    const auto* f=doc.field(obj,slot);
    if(!f || doc.metadata.field_types.at(f->type_index).name!=type)
        throw MotionError("Invalid sequence field at object "+std::to_string(obj.reference_index)+
            " slot "+std::to_string(slot)+"; expected "+std::string(type)+".");
    return *f;
}
std::string text(const IgbDocument& doc,const IgbObject& obj,std::uint16_t slot,std::size_t& budget) {
    const auto& f=field(doc,obj,slot,"igStringMetaField");
    if(f.size>4104)throw MotionError("Serialized sequence string exceeds its byte budget.");
    charge(f.size,budget,max_text);
    const auto value=doc.field_string(obj,slot);checked_name(value);charge(value.size(),budget,max_text);
    return std::string(value);
}
float number(const IgbDocument& doc,const IgbObject& obj,std::uint16_t slot) {
    if(field(doc,obj,slot,"igFloatMetaField").size!=4)throw MotionError("Invalid sequence float width.");
    const auto value=doc.field_f32(obj,slot);
    if(!std::isfinite(value))throw MotionError("Sequence command parameters must be finite.");
    return value;
}
std::array<float,3> vector(const IgbDocument& doc,const IgbObject& obj,std::uint16_t slot) {
    const auto& f=field(doc,obj,slot,"igVec3fMetaField");
    if(f.size!=12)throw MotionError("Invalid sequence vector width.");
    const auto bytes=doc.field_bytes(f);std::array<float,3> result;
    for(std::size_t i=0;i<3;++i) {
        std::uint32_t word=0;for(std::size_t j=0;j<4;++j)word|=std::uint32_t(bytes[4*i+j])<<(8*j);
        result[i]=std::bit_cast<float>(word);
        if(!std::isfinite(result[i]))throw MotionError("Sequence command vectors must be finite.");
    }
    return result;
}
std::uint32_t reference(const IgbDocument& doc,const IgbObject& obj,std::uint16_t slot) {
    if(field(doc,obj,slot,"igObjectRefMetaField").size!=4)throw MotionError("Invalid sequence reference width.");
    const auto ref=doc.field_reference(obj,slot);
    if(!ref)throw MotionError("A null sequence list is not an empty command list.");
    return *ref;
}
std::vector<std::optional<std::uint32_t>> rows(const IgbDocument& doc,std::uint32_t ref,std::size_t limit) {
    const auto& obj=exact(doc,ref,"igNamedObjectList");
    for(std::uint16_t slot=2;slot<=3;++slot) {
        if(field(doc,obj,slot,"igIntMetaField").size!=4 || doc.field_u32(obj,slot)>limit)
            throw MotionError("Sequence list count/capacity exceeds its budget.");
    }
    if(field(doc,obj,4,"igMemoryRefMetaField").size!=4)throw MotionError("Invalid sequence list memory width.");
    return doc.object_list(ref);
}
bool ordinary(std::string_view name) {
    // 802CCD64 registers the 33 exact names at 804D1084. 8030D910 handles
    // those before named sequences, plus its two special erase sequence names.
    // Other sequence names do not require an SQ_ prefix.
    constexpr std::array<std::string_view,35> controller_commands{
        "GENERATE","GENERATE_NDSP","REMOVE","GETINFONAME","GETINFONAME_RUN",
        "SETMATRIX","CHKBULLET","SETLABEL","SETEFFECT","SETSOUND","GETMATRIX",
        "GETMATRIX_ALL","GETMATRIX_ACTIVE","GETTRCDATA","GETHP","SETHP","GETRECNO",
        "ACTION1","ONCTRL","OFCTRL","ONFREEZE","OFFREEZE","SETLODHIGH","RESETLODHIGH",
        "MOVETRANS","SETCONVEYOR","SETQUICKSAND","SETSLIPPEDFLOOR","ISRUN","ISSEQNAME",
        "ONHITBULLETBOX","OFHITBULLETBOX","GETSEQNAMELIST","SQ_ERAS","SQ_ERAS2"};
    return std::find(controller_commands.begin(),controller_commands.end(),name)==controller_commands.end();
}
} // namespace

ModelSequenceProgram decode_model_sequences(const IgbDocument& doc,std::uint32_t ref,std::string source) {
    checked_name(source);if(source.empty())throw MotionError("Sequence source identity must be explicit.");
    if(doc.metadata.header.byte_order!=IgbByteOrder::little)throw MotionError("Big-endian model sequences are unverified.");
    ModelSequenceProgram result;result.source_=std::move(source);result.reference_=ref;
    std::size_t bytes=result.source_.size(),commands=0;
    const auto& seq=exact(doc,ref,"beModelCtrlInfoDataSeq");
    if(text(doc,seq,2,bytes)!="SEQ")throw MotionError("Expected the explicitly named SEQ record.");
    if(field(doc,seq,3,"igIntMetaField").size!=4)throw MotionError("Invalid sequence data field width.");
    const auto sequences=rows(doc,reference(doc,seq,4),max_sequences);
    for(auto row:sequences) {
        if(!row)throw MotionError("Null model sequence entry.");
        const auto& data=exact(doc,*row,"beModelCtrlData");
        ModelSequenceData decoded{*row,text(doc,data,2,bytes),{}};
        const auto list=rows(doc,reference(doc,data,3),max_commands);charge(list.size(),commands,max_commands);
        decoded.commands.reserve(list.size());
        for(auto entry:list) {
            if(!entry)throw MotionError("Null sequence command.");
            const auto& object=doc.object(*entry);
            ModelSequenceCommand command;command.reference_index=*entry;
            command.type=doc.metadata.object_types.at(object.type_index).name;
            if(command.type.size()>256)throw MotionError("Sequence command type exceeds its byte budget.");
            charge(command.type.size(),bytes,max_text);
            const auto& type=command.type;
            if(type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX" || type=="beModelCtrlCTRL" || type=="beModelCtrlPDMOVE" || type=="beModelCtrlMUTEKI") {
                command.name=text(doc,object,2,bytes);command.opcode=ModelSequenceOpcode::model;
                command.model_command=decode_model_command(doc,*entry);charge(command.name.size(),bytes,max_text);
                if(type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX")charge(type.size(),bytes,max_text);
            } else if(type=="beModelCtrlLABEL" || type=="beModelCtrlLABELCTRL" || type=="beModelCtrlACTION" || type=="beModelCtrlJUMP" || type=="beModelCtrlWCHECK") {
                command.name=text(doc,object,2,bytes);
                command.opcode=type=="beModelCtrlLABEL" ? ModelSequenceOpcode::label :
                    type=="beModelCtrlLABELCTRL" ? ModelSequenceOpcode::label_control :
                    type=="beModelCtrlACTION" ? ModelSequenceOpcode::action :
                    type=="beModelCtrlJUMP" ? ModelSequenceOpcode::jump : ModelSequenceOpcode::wait_check;
                if(command.opcode==ModelSequenceOpcode::label_control) {
                    auto scope=text(doc,object,3,bytes);
                    if(doc.field(object,3)->size!=4)command.label_sequence=std::move(scope);
                }
            } else if(type=="beModelCtrlHITBOX") {
                command.name=text(doc,object,2,bytes);command.opcode=ModelSequenceOpcode::hitbox;
                if(field(doc,object,3,"igIntMetaField").size!=4)throw MotionError("Invalid HITBOX kind width.");
                // The checked prepared profile validates every serialized field,
                // including parameters ignored by AllClear and unknown names.
                ModelHitboxSubcommand parameters{doc.field_i32(object,3),number(doc,object,4),number(doc,object,5),
                    vector(doc,object,6),vector(doc,object,7)};
                command.hitbox_command=ModelHitboxCommand{*entry,command.name,parameters};charge(command.name.size(),bytes,max_text);
            } else if(type=="beModelCtrlLUACALL") {
                command.name=text(doc,object,2,bytes);command.opcode=ModelSequenceOpcode::lua_call;
                auto mode=text(doc,object,3,bytes);
                if(doc.field(object,3)->size!=4)command.lua_mode=std::move(mode);
            }
            decoded.commands.push_back(std::move(command));
        }
        result.sequences_.push_back(std::move(decoded));
    }
    return result;
}

ModelSequenceLabels::ModelSequenceLabels(std::shared_ptr<ModelSequenceLabelFields> fields)
    :fields_(std::move(fields)) {
    if(!fields_)throw MotionError("Sequence label fields require their actual owner.");
}
ModelSequenceLabels::ModelSequenceLabels(const ModelSequenceLabels& other)
    :ModelSequenceLabels(std::make_shared<ModelSequenceLabelFields>()) {
    *this=other;
}
ModelSequenceLabels& ModelSequenceLabels::operator=(const ModelSequenceLabels& other) {
    if(this==&other)return *this;
    if(!other.fields_)throw MotionError("Sequence label fields were moved out of this view.");
    const auto& source=*other.fields_;
    auto detached=std::make_shared<ModelSequenceLabelFields>();
    if(source.sequences)detached->sequences=std::make_shared<ModelSequenceLabelFields::StringList>(*source.sequences);
    if(source.names)detached->names=source.names==source.sequences ? detached->sequences :
        std::make_shared<ModelSequenceLabelFields::StringList>(*source.names);
    if(source.offsets)detached->offsets=std::make_shared<ModelSequenceLabelFields::OffsetList>(*source.offsets);
    if(!fields_) {fields_=std::move(detached);return *this;}
    // Copying prepared state is not a guest list access. Preserve partial shapes
    // and aliases; the first actual label consumer performs checked validation.
    const bool source_alias=detached->sequences && detached->sequences==detached->names;
    const bool destination_alias=fields_->sequences && fields_->sequences==fields_->names;
    const auto publish_list=[](auto& destination,auto& values) {
        if(!destination || !values)destination=std::move(values);
        else destination->swap(*values);
    };
    publish_list(fields_->sequences,detached->sequences);
    if(source_alias)fields_->names=fields_->sequences;
    else if(destination_alias)fields_->names=std::move(detached->names);
    else publish_list(fields_->names,detached->names);
    publish_list(fields_->offsets,detached->offsets);return *this;
}
ModelSequenceLabels& ModelSequenceLabels::operator=(ModelSequenceLabels&& other) {
    return *this=static_cast<const ModelSequenceLabels&>(other);
}
std::vector<ModelSequenceLabel> ModelSequenceLabels::snapshot() const {
    if(!fields_)throw MotionError("Sequence label fields were moved out of this view.");
    const auto& fields=*fields_;
    if(!fields.sequences && !fields.names && !fields.offsets)return {};
    if(!fields.sequences || !fields.names || !fields.offsets || fields.sequences==fields.names)
        throw MotionError("Sequence label fields require three distinct, complete lists.");
    const auto count=fields.sequences->size();
    if(count>max_sequences || fields.names->size()!=count || fields.offsets->size()!=count)
        throw MotionError("Sequence label lists have incompatible counts or exceed their budget.");
    std::vector<ModelSequenceLabel> rows;rows.reserve(count);
    std::size_t budget=0;
    for(std::size_t i=0;i<count;++i) {
        for(const auto* value:{&(*fields.sequences)[i],&(*fields.names)[i]}) {
            if(*value) {checked_name(**value);charge((*value)->size(),budget,max_text);}
        }
        rows.push_back({(*fields.sequences)[i],(*fields.names)[i],(*fields.offsets)[i]});
    }
    return rows;
}
void ModelSequenceLabels::publish(const std::vector<ModelSequenceLabel>& rows,bool allocated) {
    // Stage all allocations before publication. Swapping list contents preserves
    // the actual list allocations retained by other views of this same work.
    if(!allocated) {fields_->sequences.reset();fields_->names.reset();fields_->offsets.reset();return;}
    auto sequences=std::make_shared<ModelSequenceLabelFields::StringList>();
    auto names=std::make_shared<ModelSequenceLabelFields::StringList>();
    auto offsets=std::make_shared<ModelSequenceLabelFields::OffsetList>();
    sequences->reserve(rows.size());names->reserve(rows.size());offsets->reserve(rows.size());
    std::size_t budget=0;
    for(const auto& row:rows) {
        for(const auto* value:{&row.sequence,&row.label}) {
            if(*value) {checked_name(**value);charge((*value)->size(),budget,max_text);}
        }
        sequences->push_back(row.sequence);names->push_back(row.label);offsets->push_back(row.cached_offset);
    }
    // Reject a partial prepared owner instead of silently repairing it.
    static_cast<void>(snapshot());
    if(fields_->sequences) {
        fields_->sequences->swap(*sequences);fields_->names->swap(*names);fields_->offsets->swap(*offsets);
    } else {
        fields_->sequences=std::move(sequences);fields_->names=std::move(names);fields_->offsets=std::move(offsets);
    }
}
void ModelSequenceLabels::set(std::optional<std::string> sequence,std::optional<std::string> label) {
    if(sequence)checked_name(*sequence);
    if(label)checked_name(*label);
    auto rows=snapshot();
    const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& value){return value.sequence==sequence;});
    if(found!=rows.end()) {*found={std::move(sequence),std::move(label),-1};}
    else {
        if(rows.size()==max_sequences)throw MotionError("Sequence label count exceeds its budget.");
        rows.push_back({std::move(sequence),std::move(label),-1});
    }
    publish(rows,true);
}
void ModelSequenceLabels::cache(std::size_t index,std::int32_t offset) {
    // Selection already validated the three lists and its detached plan. No
    // allocation or reference replacement occurs in this original cache write.
    if(!fields_ || !fields_->offsets || index>=fields_->offsets->size())
        throw MotionError("Sequence label cache has no retained entry.");
    (*fields_->offsets)[index]=offset;
}

ModelSequenceRuntime::ModelSequenceRuntime(ModelSequenceProgram program,ModelControlState model,
    std::string info_name,std::int32_t ram_no)
    :ModelSequenceRuntime(std::make_shared<const ModelSequenceProgram>(std::move(program)),
        std::move(model),std::move(info_name),ram_no,std::make_shared<ModelSequenceFields>()) {}
ModelSequenceRuntime::ModelSequenceRuntime(ModelSequenceProgram program,ModelControlState model,
    std::string info_name,std::int32_t ram_no,std::shared_ptr<ModelSequenceLabelFields> labels)
    :ModelSequenceRuntime(std::move(program),std::move(model),std::move(info_name),ram_no) {
    if(!labels)throw MotionError("Sequence label fields require their actual owner.");
    fields_->labels=std::move(labels);
}
ModelSequenceRuntime::ModelSequenceRuntime(std::shared_ptr<const ModelSequenceProgram> program,ModelControlState model,
    std::string info_name,std::int32_t ram_no,std::shared_ptr<ModelSequenceFields> fields)
    :program_(std::move(program)),model_(std::move(model)),fields_(std::move(fields)),
    info_name_(std::move(info_name)),ram_no_(ram_no) {
    if(!program_)throw MotionError("Sequence work requires its actual immutable program owner.");
    require_fields();
    checked_name(info_name_);checked_entry_fields(model_);
    acquire_executor();
}
ModelSequenceRuntime::ModelSequenceRuntime(ModelControlState model,std::shared_ptr<ModelSequenceFields> fields)
    :model_(std::move(model)),fields_(std::move(fields)),actual_profile_(true) {
    if(!model_.timer_access_owner())throw MotionError("Actual sequence execution requires its real RAM timer endpoint.");
    require_fields();acquire_executor();
}
ModelSequenceRuntime::ModelSequenceRuntime(const ModelSequenceRuntime& other)
    :program_(other.program_),model_(other.model_),fields_(std::make_shared<ModelSequenceFields>()),
    info_name_(other.info_name_),ram_no_(other.ram_no_) {
    other.require_fields();
    fields_->name=other.fields_->name;fields_->command_data=other.fields_->command_data;
    fields_->command_index=other.fields_->command_index;fields_->label_count=other.fields_->label_count;
    fields_->wait=other.fields_->wait;
    ModelSequenceLabels source(other.fields_->labels);
    fields_->labels=ModelSequenceLabels(source).fields();
    pending_jump_=other.pending_jump_;running_=other.running_;selection_pending_=other.selection_pending_;
    lua_faulted_=other.lua_faulted_;lua_pending_=other.lua_pending_;
    action_pending_=other.action_pending_;hitbox_pending_=other.hitbox_pending_;acquire_executor();
    if(other.fields_->executor_unavailable_ || running_ || selection_pending_ || pending_jump_ || lua_faulted_ || external_pending())
        fields_->executor_unavailable_=true; // An inspection copy never duplicates an external continuation.
}
std::shared_ptr<const ModelSequenceProgram> ModelSequenceRuntime::transfer_program(ModelSequenceRuntime& other) {
    other.require_fields();
    if(other.running_)throw MotionError("Cannot move an executing sequence facade.");
    return std::move(other.program_);
}
ModelSequenceRuntime::ModelSequenceRuntime(ModelSequenceRuntime&& other)
    :program_(transfer_program(other)),model_(std::move(other.model_)),fields_(std::move(other.fields_)),
    executor_lease_(std::move(other.executor_lease_)),info_name_(std::move(other.info_name_)),ram_no_(other.ram_no_),
    pending_jump_(std::move(other.pending_jump_)),running_(other.running_),selection_pending_(other.selection_pending_),
    lua_faulted_(other.lua_faulted_),lua_pending_(other.lua_pending_),action_pending_(other.action_pending_),hitbox_pending_(other.hitbox_pending_),
    actual_profile_(other.actual_profile_),actual_jump_window_(other.actual_jump_window_),actual_leaf_(std::move(other.actual_leaf_)) {}
ModelSequenceRuntime::~ModelSequenceRuntime() {
    if(fields_ && executor_lease_ && (running_ || selection_pending_ || pending_jump_ || lua_faulted_ || external_pending() || executor_lease_->actual_started_frames))
        fields_->executor_unavailable_=true;
}
void ModelSequenceRuntime::acquire_executor() {
    require_fields();
    if(fields_->executor_unavailable_ || !fields_->executor_.expired())
        throw MotionError("Sequence fields already have an executor or an abandoned pending/faulted operation.");
    executor_lease_=std::make_shared<ModelSequenceExecutorLease>();fields_->executor_=executor_lease_;
}
ModelSequenceRuntime& ModelSequenceRuntime::operator=(const ModelSequenceRuntime& other) {
    if(this==&other)return *this;
    if(fields_) {
        require_no_lua_fault();
        if(running_ || selection_pending() || external_pending())
            throw MotionError("Cannot replace a sequence snapshot during execution or a pending external operation.");
    }
    // Stage detached values before changing a published generation. Immutable
    // command-data references retain their original program/source allocation.
    ModelSequenceRuntime candidate(other);
    const bool unavailable=candidate.fields_->executor_unavailable_;
    model_=std::move(candidate.model_);
    if(!fields_) {fields_=std::move(candidate.fields_);executor_lease_=std::move(candidate.executor_lease_);}
    else {
        ModelSequenceLabels destination(fields_->labels),source(candidate.fields_->labels);
        destination=source;
        fields_->name=std::move(candidate.fields_->name);
        fields_->command_data=std::move(candidate.fields_->command_data);
        fields_->command_index=candidate.fields_->command_index;fields_->label_count=candidate.fields_->label_count;
        fields_->wait=std::move(candidate.fields_->wait);
    }
    fields_->executor_unavailable_=unavailable;
    program_=std::move(candidate.program_);info_name_=std::move(candidate.info_name_);ram_no_=candidate.ram_no_;
    pending_jump_=std::move(candidate.pending_jump_);running_=candidate.running_;selection_pending_=candidate.selection_pending_;
    lua_faulted_=candidate.lua_faulted_;lua_pending_=candidate.lua_pending_;
    action_pending_=candidate.action_pending_;hitbox_pending_=candidate.hitbox_pending_;return *this;
}
ModelSequenceRuntime& ModelSequenceRuntime::operator=(ModelSequenceRuntime&& other) {
    return *this=static_cast<const ModelSequenceRuntime&>(other);
}
void ModelSequenceRuntime::require_fields() const {
    if(!fields_)throw MotionError("Sequence fields were transferred out of this facade.");
    if(!fields_->labels)throw MotionError("Sequence fields require their actual label-field group.");
}
const ModelSequenceData& ModelSequenceRuntime::current_data() const {
    require_fields();
    const auto data=fields_->command_data.prepared_owner();
    if(!data)throw MotionError("Model sequence work has not been selected/prepared.");
    if(!program_ || data.owner_before(program_) || program_.owner_before(data) ||
       std::none_of(program_->sequences().begin(),program_->sequences().end(),
           [&](const auto& value){return &value==data.get();}))
        throw MotionError("Selected command data does not belong to this exact source program owner.");
    if(fields_->command_index<0 || static_cast<std::size_t>(fields_->command_index)>data->commands.size())
        throw MotionError("Selected command index has no addressable command in its actual data owner.");
    return *data;
}
std::shared_ptr<ModelSequenceFields> ModelSequenceRuntime::fields() {
    require_no_lua_fault();
    if(running_ || selection_pending() || external_pending())throw MotionError("Sequence fields are unavailable during execution or a pending external operation.");
    return fields_;
}
const ModelWaitState& ModelSequenceRuntime::wait_state() const {require_fields();return fields_->wait;}
std::int32_t ModelSequenceRuntime::command_index() const {require_fields();return fields_->command_index;}
std::int32_t ModelSequenceRuntime::label_count() const {require_fields();return fields_->label_count;}
std::vector<ModelSequenceLabel> ModelSequenceRuntime::labels() const {
    require_fields();return ModelSequenceLabels(fields_->labels).snapshot();
}
std::shared_ptr<const ModelSequenceLabelFields> ModelSequenceRuntime::label_fields() const {
    require_fields();return fields_->labels;
}

std::string_view ModelSequenceRuntime::current_sequence() const noexcept {
    return fields_ && fields_->name ? std::string_view(*fields_->name) : std::string_view{};
}
void ModelSequenceRuntime::require_no_lua_fault() const {
    require_fields();
    if(fields_->executor_unavailable_)throw MotionError("A detached or abandoned sequence continuation cannot execute or mutate.");
    if(lua_faulted_)throw MotionError("The Lua callback has an uncertain outcome; this sequence owner cannot resume or mutate.");
}
ModelControlState& ModelSequenceRuntime::model() {
    require_no_lua_fault();
    if(running_ || selection_pending() || external_pending())throw MotionError("Model mutation is unavailable during sequence execution or a pending external operation.");
    return model_;
}
ModelWaitState& ModelSequenceRuntime::wait_state() {
    require_no_lua_fault();
    if(running_ || selection_pending() || external_pending())throw MotionError("Wait mutation is unavailable during sequence execution or a pending external operation.");
    return fields_->wait;
}
void ModelSequenceRuntime::set_label(std::optional<std::string> sequence,std::optional<std::string> label) {
    require_no_lua_fault();
    if(running_ || selection_pending() || external_pending())throw MotionError("Sequence mutation is unavailable during execution or a pending external operation.");
    ModelSequenceLabels(fields_->labels).set(std::move(sequence),std::move(label));
}
std::shared_ptr<ModelSequenceLabelFields> ModelSequenceRuntime::label_fields() {
    require_no_lua_fault();
    if(running_ || selection_pending() || external_pending())throw MotionError("Label fields are unavailable during execution or a pending external operation.");
    return fields_->labels;
}
void ModelSequenceRuntime::select(std::string_view sequence) {
    require_no_lua_fault();
    if(running_ || selection_pending() || external_pending())throw MotionError("Sequence mutation is unavailable during execution or a pending external operation.");
    auto plan=prepare_selection(sequence);
    auto candidate=model_.snapshot_work();candidate.mode()=0;candidate.phase()=0;candidate.pad_motion.clear();
    finish_entry_fields(candidate);
    publish_selection(std::move(plan),std::move(candidate),false);
}
void ModelSequenceRuntime::begin_select(std::string_view sequence) {
    require_no_lua_fault();
    if(running_ || selection_pending() || external_pending())throw MotionError("Sequence mutation is unavailable during execution or a pending external operation.");
    auto plan=prepare_selection(sequence);
    auto candidate=model_.snapshot_work();candidate.mode()=0;candidate.phase()=0;candidate.pad_motion.clear();
    publish_selection(std::move(plan),std::move(candidate),true);
}
ModelSequenceRuntime::SelectionPlan ModelSequenceRuntime::prepare_selection(std::string_view sequence) const {
    if(actual_profile_)throw MotionError("Actual sequence entry requires its selected mutable graph owner and invocation frame.");
    checked_name(sequence);if(!ordinary(sequence))throw MotionError("This command requires the outer model controller.");
    checked_entry_fields(model_);
    const auto& data=program_->sequences();
    const auto found=std::find_if(data.begin(),data.end(),[&](const auto& item){return item.name==sequence;});
    if(found==data.end())throw MotionError("Requested model sequence is absent from its owned program.");
    const auto labels=ModelSequenceLabels(fields_->labels).snapshot();std::int32_t start=0;
    SelectionPlan plan;
    const auto selected=std::find_if(labels.begin(),labels.end(),[&](const auto& item){return item.sequence && *item.sequence==sequence;});
    if(selected!=labels.end()) {
        start=selected->cached_offset;
        if(start==-1) {
            // 8030D1F8 passes this stored pointer directly to strcmp. Preserve
            // null on SETLABEL, but reject the later unsafe string lookup.
            if(!selected->label)throw MotionError("Selected sequence label requires a non-null string for the original lookup.");
            const auto label=std::find_if(found->commands.begin(),found->commands.end(),[&](const auto& command){
                return command.opcode==ModelSequenceOpcode::label && command.name==*selected->label;
            });
            if(label!=found->commands.end())start=static_cast<std::int32_t>(label-found->commands.begin());
            plan.cache_index=std::size_t(selected-labels.begin());plan.cache_offset=start;
        }
    }
    if(start<0 || std::size_t(start)>found->commands.size())throw MotionError("Selected sequence label has no valid command offset.");
    // Check post-cleanup motion constraints without stopping the live channels
    // yet. finish_select uses their then-current state, not this validation copy.
    auto stopped=model_.channels;stopped.on_sequence_change();
    plan.name=sequence;plan.data=std::shared_ptr<const ModelSequenceData>(program_,&*found);plan.cursor=start;
    return plan;
}
void ModelSequenceRuntime::publish_selection(SelectionPlan&& plan,ModelControlState&& candidate,bool pending) {
    model_.publish_work(candidate);
    if(plan.cache_index)ModelSequenceLabels(fields_->labels).cache(*plan.cache_index,plan.cache_offset);
    fields_->name=std::move(plan.name);fields_->command_data=std::move(plan.data);
    fields_->command_index=plan.cursor;fields_->label_count=0;
    fields_->wait.name.reset();
    selection_pending_=pending;
}
void ModelSequenceRuntime::complete_selection() {
    checked_entry_fields(model_);
    auto candidate=model_.snapshot_work();
    finish_entry_fields(candidate);
    model_.publish_work(candidate);selection_pending_=false;
}
void ModelSequenceRuntime::finish_select() {
    require_no_lua_fault();
    if(running_ || external_pending())throw MotionError("Sequence mutation is unavailable during execution or a pending external operation.");
    if(!selection_pending_)throw MotionError("No sequence selection is pending.");
    if(pending_jump_)throw MotionError("Resume the pending local jump through its cleanup hook.");
    complete_selection();
}

std::shared_ptr<ModelSequenceActualFrame> ModelSequenceRuntime::make_actual_frame() {
    require_no_lua_fault();
    if(!actual_profile_)throw MotionError("Actual invocation frames require the actual sequence profile.");
    const auto parent=actual_leaf_.lock();
    if(parent && !parent->completed_ && !(running_ && actual_jump_window_)) {
        if(running_)parent->faulted_=true;
        throw MotionError("Resume the retained actual sequence invocation before another ordinary entry.");
    }
    if(executor_lease_->actual_frames>=64)throw MotionError("Actual sequence invocation depth exceeds 64 frames.");
    auto result=std::shared_ptr<ModelSequenceActualFrame>(new ModelSequenceActualFrame());
    result->lease_=executor_lease_;result->fields_=fields_;
    if(parent && !parent->completed_)result->parent_=parent;
    ++executor_lease_->actual_frames;actual_leaf_=result;return result;
}
std::shared_ptr<ModelSequenceActualFrame> ModelSequenceRuntime::make_actual_resume_frame() {
    auto frame=make_actual_frame();frame->began_=true;++executor_lease_->actual_started_frames;return frame;
}
void ModelSequenceRuntime::require_actual_frame(const std::shared_ptr<ModelSequenceActualFrame>& frame,bool nested) const {
    require_no_lua_fault();
    if(!actual_profile_ || !frame || frame->lease_!=executor_lease_)
        throw MotionError("Actual sequence frame does not belong to this exact executor.");
    if(frame->faulted_ || frame->completed_)throw MotionError("A completed or faulted actual sequence frame cannot resume.");
    if(running_ && !actual_jump_window_) {
        if(const auto leaf=actual_leaf_.lock())leaf->faulted_=true;
        throw MotionError("Only original direct JUMP dispatch permits nested sequence entry.");
    }
    auto leaf=actual_leaf_.lock();
    if(nested && frame->command_ && frame->command_->opcode==ModelSequenceOpcode::jump)
        while(leaf && leaf!=frame)leaf=leaf->parent_.lock();
    if(leaf!=frame)throw MotionError("Resume the captured child sequence invocation first.");
}
namespace {
std::shared_ptr<ModelSceneObject> actual_command_slot(const std::shared_ptr<ModelSceneSequence>& data,std::int32_t index) {
    if(!data || !data->commands)throw MotionError("Actual sequence command data/list is null at its original read.");
    const auto backing=data->commands->backing;
    if(index<0 || !backing || static_cast<std::size_t>(index)>=backing->size() || index>=65536)
        throw MotionError("Actual sequence cursor has no addressable physical command slot.");
    const auto command=(*backing)[static_cast<std::size_t>(index)];
    if(!command)throw MotionError("Actual sequence reached a null physical command slot.");
    return command;
}
}
std::int32_t ModelSequenceRuntime::actual_label_offset() const {
    const auto labels=fields_->labels;
    if(!labels->sequences)return 0;
    if(labels->sequences->size()>4096)throw MotionError("Actual sequence label lookup exceeds its entry budget.");
    const auto found=std::find(labels->sequences->begin(),labels->sequences->end(),fields_->name);
    if(found==labels->sequences->end())return 0;
    const auto index=static_cast<std::size_t>(found-labels->sequences->begin());
    if(!labels->offsets || index>=labels->offsets->size())throw MotionError("Actual sequence label offset has no physical slot.");
    const auto cached=(*labels->offsets)[index];if(cached!=-1)return cached;
    if(!labels->names || index>=labels->names->size())throw MotionError("Actual sequence label name has no physical slot.");
    const auto wanted=(*labels->names)[index];
    const auto data=fields_->command_data.actual_owner();
    if(!data || !data->commands)throw MotionError("Actual sequence label scan reached null command data/list.");
    const auto count=data->commands->count;
    if(count>65536)throw MotionError("Actual sequence label scan exceeds its command budget.");
    for(std::int32_t i=0;i<count;++i) {
        const auto command=actual_command_slot(data,i);
        if(command->type()!="beModelCtrlLABEL")continue;
        if(!wanted)throw MotionError("Actual LABEL comparison reached a null stored label name.");
        if(command->name.value_or("")!=*wanted)continue;
        // The original result is independent of its guarded cache write.
        if(labels->offsets && index<labels->offsets->size())(*labels->offsets)[index]=i;
        return i;
    }
    return -1;
}
void ModelSequenceRuntime::begin_actual_select(const std::shared_ptr<ModelSequenceActualFrame>& frame,
    std::optional<std::string> name,std::shared_ptr<ModelSceneSequence> selected) {
    require_actual_frame(frame);
    if(frame->began_)throw MotionError("Actual selection cannot repeat its published entry prefix.");
    if(name) {
        checked_name(*name);if(!ordinary(*name))throw MotionError("This name requires its original special controller branch.");
    }
    frame->began_=true;++executor_lease_->actual_started_frames;
    try {
        fields_->name=std::move(name);fields_->command_data=std::move(selected);
        fields_->command_index=actual_label_offset();
        fields_->label_count=0;model_.mode()=0;model_.phase()=0;
        model_.pad_motion.clear();fields_->wait.name.reset();
        frame->selection_pending_=true;
    } catch(...) {frame->faulted_=true;throw;}
}
void ModelSequenceRuntime::finish_actual_select(const std::shared_ptr<ModelSequenceActualFrame>& frame) {
    require_actual_frame(frame);
    if(!frame->selection_pending_)throw MotionError("No actual selection cleanup is pending.");
    try {
        model_.bullet_hit_script().reset();model_.wall_hit_script().reset();model_.floor_hit_script().reset();
        model_.wall_check()=1;model_.floor_check()=1;model_.check_fall()=1;
        if(model_.invulnerability_frames()<0)model_.invulnerability_frames()=0;
        model_.channels.on_sequence_change_actual();frame->selection_pending_=false;
    } catch(...) {frame->faulted_=true;throw;}
}
ModelSequenceResult ModelSequenceRuntime::run_actual(const std::shared_ptr<ModelSequenceActualFrame>& frame,
    const ModelSequenceActualServices& services) {
    require_actual_frame(frame,true);
    if(!frame->began_ || frame->selection_pending_)throw MotionError("Complete actual entry cleanup before its command loop.");
    if(!frame->loop_started_) {
        frame->cursor_=fields_->command_index;frame->data_=fields_->command_data.actual_owner();frame->loop_started_=true;
    }
    ModelSequenceResult result;
    const auto finish=[&](ModelSequenceStop stop,bool publish=true) {
        if(publish)fields_->command_index=frame->cursor_;
        result.stop=stop;frame->completed_=true;--executor_lease_->actual_frames;
        --executor_lease_->actual_started_frames;frame->lease_.reset();
        actual_leaf_=frame->parent_;return result;
    };
    const bool previous_running=running_,previous_jump=actual_jump_window_;
    running_=true;actual_jump_window_=false;
    struct Restore {bool& running;bool& jump;bool old_running,old_jump;
        ~Restore(){running=old_running;jump=old_jump;}} restore{running_,actual_jump_window_,previous_running,previous_jump};
    try {
        for(std::uint32_t visited=0;visited<65536;++visited) {
            if(!frame->command_) {
                if(!frame->data_ || !frame->data_->commands)throw MotionError("Actual command loop reached null command data/list.");
                // 8030C82C uses !=, not <. A slot beyond count remains a physical
                // read if the local cursor differs; backing checks guard host memory.
                if(frame->cursor_==frame->data_->commands->count)return finish(ModelSequenceStop::end);
                const auto owner=actual_command_slot(frame->data_,frame->cursor_);
                const auto command=std::dynamic_pointer_cast<ModelSceneCommand>(owner);
                if(!command)throw MotionError("Actual command dispatch reached an unsupported object layout.");
                frame->raw_command_=command;
                const auto& type=command->type();
                if(type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX" || type=="beModelCtrlCTRL" ||
                    type=="beModelCtrlPDMOVE" || type=="beModelCtrlMUTEKI") {
                    // These helpers consume raw name/parameter fields after
                    // their own timer/heading prefixes. Type dispatch alone
                    // must not decode or reject those later reads in advance.
                    ModelSequenceCommand metadata;metadata.reference_index=command->reference().value_or(0xffffffffu);
                    metadata.type=type;metadata.opcode=ModelSequenceOpcode::model;frame->command_=std::move(metadata);
                } else frame->command_=model_scene_command(*command);
            }
            const auto& command=*frame->command_;
            const auto stopped=[&](ModelSequenceStop stop) {
                result.stop=stop;result.command_reference=command.reference_index;result.type=command.type;result.name=command.name;
                return result;
            };
            const auto identity=[&]() -> const ModelSequenceIdentity& {
                if(!frame->identity_) {
                    if(!services.identity)throw MotionError("Actual command dispatch requires its original source/parent/RAM reader.");
                    frame->identity_=services.identity();
                    if(frame->faulted_)throw MotionError("An invalid recursive identity read faulted the actual invocation.");
                }
                return *frame->identity_;
            };
            switch(command.opcode) {
            case ModelSequenceOpcode::model:
                apply_model_command(model_,*frame->raw_command_);break;
            case ModelSequenceOpcode::label:
                if(fields_->label_count!=0) {stopped(ModelSequenceStop::label_boundary);return finish(ModelSequenceStop::label_boundary);}
                fields_->label_count=1;break;
            case ModelSequenceOpcode::label_control:
                ModelSequenceLabels(fields_->labels).set(command.label_sequence,command.name);break;
            case ModelSequenceOpcode::action: {
                if(!services.action)return stopped(ModelSequenceStop::action_required);
                const auto& source=identity();
                const bool complete=services.action({source.source,command.reference_index,source.info_name,source.ram_no,command.name});
                if(frame->faulted_)throw MotionError("An invalid recursive ACTION faulted the actual invocation.");
                if(!complete)return stopped(ModelSequenceStop::action_required);
                break;
            }
            case ModelSequenceOpcode::jump: {
                if(!services.jump)return stopped(ModelSequenceStop::controller_required);
                actual_jump_window_=true;const bool complete=services.jump(command.name);actual_jump_window_=false;
                if(frame->faulted_)throw MotionError("An invalid recursive callback faulted the actual command invocation.");
                if(!complete)return stopped(ModelSequenceStop::controller_required);
                const auto leaf=actual_leaf_.lock();
                if(leaf && leaf!=frame && !leaf->completed_)throw MotionError("Direct JUMP returned before its child invocation completed.");
                ++result.commands_executed;stopped(ModelSequenceStop::jump_completed);
                return finish(ModelSequenceStop::jump_completed,false);
            }
            case ModelSequenceOpcode::wait_check:
                fields_->wait.name=command.name;
                frame->cursor_=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(frame->cursor_)+1u);
                ++result.commands_executed;stopped(ModelSequenceStop::waiting);return finish(ModelSequenceStop::waiting);
            case ModelSequenceOpcode::hitbox: {
                if(command.name!="Set" && command.name!="AllClear")break;
                if(!services.hitbox)return stopped(ModelSequenceStop::hitbox_required);
                const bool complete=services.hitbox(*command.hitbox_command);
                if(frame->faulted_)throw MotionError("An invalid recursive HITBOX faulted the actual invocation.");
                if(!complete)return stopped(ModelSequenceStop::hitbox_required);
                break;
            }
            case ModelSequenceOpcode::lua_call: {
                std::optional<std::string>* target=nullptr;
                if(command.lua_mode=="BHit")target=&model_.bullet_hit_script();
                else if(command.lua_mode=="WHit")target=&model_.wall_hit_script();
                else if(command.lua_mode=="FHit")target=&model_.floor_hit_script();
                if(target) {*target=command.name;break;}
                if(!services.lua)return stopped(ModelSequenceStop::lua_required);
                const auto& source=identity();
                const bool complete=services.lua({source.source,command.reference_index,source.info_name,source.ram_no,command.name,command.lua_mode});
                if(frame->faulted_)throw MotionError("An invalid recursive Lua callback faulted the actual invocation.");
                if(!complete)return stopped(ModelSequenceStop::lua_required);
                break;
            }
            case ModelSequenceOpcode::unsupported:return stopped(ModelSequenceStop::unsupported_command);
            }
            if(frame->faulted_)throw MotionError("An invalid recursive callback faulted the actual command invocation.");
            frame->cursor_=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(frame->cursor_)+1u);
            frame->command_.reset();frame->raw_command_.reset();frame->identity_.reset();++result.commands_executed;
        }
        throw MotionError("Actual command loop exhausted its native operation budget; completed effects remain published.");
    } catch(...) {frame->faulted_=true;throw;}
}

ModelSequenceResult ModelSequenceRuntime::run(std::uint64_t world_time,const ModelActionDispatch& action,const ModelSequenceCleanup& cleanup,
    const ModelActionTryDispatch& try_action,const ModelHitboxDispatch& hitbox,const ModelLuaDispatch& lua,
    const std::function<void()>& cleanup_preflight) {
    require_no_lua_fault();
    if(actual_profile_)throw MotionError("Actual command execution requires its retained invocation frame.");
    if(running_)throw MotionError("Reentrant sequence execution is unsupported.");
    if(!selected())throw MotionError("Model sequence work has not been selected/prepared.");
    if(selection_pending_ && !pending_jump_)throw MotionError("Finish the pending sequence selection before executing commands.");
    ModelSequenceResult result;
    if(fields_->wait.name) {
        result.stop=ModelSequenceStop::waiting;result.name=*fields_->wait.name;
        return result;
    }
    const auto resume_jump=[&]() {
        auto& pending=*pending_jump_;
        if(!pending.cleanup_complete && cleanup) {
            running_=true;
            bool complete;
            try {complete=cleanup(pending.name);} catch(...) {running_=false;throw;}
            running_=false;
            pending.cleanup_complete=complete;
        }
        if(!pending.cleanup_complete) {
            result.stop=ModelSequenceStop::sequence_cleanup_required;
            result.command_reference=pending.reference;result.type=pending.type;result.name=pending.name;
            return false;
        }
        // Keep completed cleanup recorded if a later checked motion boundary
        // fails, so a retry cannot repeat an external cleanup side effect.
        complete_selection();pending_jump_.reset();++result.commands_executed;
        return true;
    };
    if(pending_jump_ && !resume_jump())return result;
    for(std::uint32_t visited=0;visited<65536;++visited) {
        const auto& sequence=current_data();
        if(static_cast<std::size_t>(fields_->command_index)==sequence.commands.size()) {result.stop=ModelSequenceStop::end;return result;}
        const auto& command=sequence.commands.at(static_cast<std::size_t>(fields_->command_index));
        const auto stopped=[&](ModelSequenceStop why) {
            result.stop=why;result.command_reference=command.reference_index;result.type=command.type;result.name=command.name;return result;
        };
        switch(command.opcode) {
        case ModelSequenceOpcode::model: apply_model_command(model_,*command.model_command,world_time);break;
        case ModelSequenceOpcode::label:
            if(fields_->label_count!=0)return stopped(ModelSequenceStop::label_boundary);
            fields_->label_count=1;break;
        case ModelSequenceOpcode::label_control:set_label(command.label_sequence,command.name);break;
        case ModelSequenceOpcode::action: {
            if(!action && !try_action)return stopped(ModelSequenceStop::action_required);
            running_=true;action_pending_=true;
            bool complete=true;
            try {
                const ModelActionRequest request{program_->source(),command.reference_index,info_name_,ram_no_,command.name};
                if(try_action)complete=try_action(request);else action(request);
            }
            catch(...) {running_=false;throw;}
            running_=false;
            if(!complete)return stopped(ModelSequenceStop::action_required);
            action_pending_=false;
            break;
        }
        case ModelSequenceOpcode::jump: {
            if(!ordinary(command.name) || std::none_of(program_->sequences().begin(),program_->sequences().end(),
                [&](const auto& item){return item.name==command.name;}))return stopped(ModelSequenceStop::controller_required);
            // Own the originating command metadata BEFORE publishing another
            // sequence. Suspended results must never reference replaced state.
            PendingJump pending{command.reference_index,command.type,command.name,!cleanup};
            if(cleanup_preflight) {
                running_=true;
                try {cleanup_preflight();}catch(...) {running_=false;throw;}
                running_=false;
            }
            begin_select(pending.name);pending_jump_.emplace(std::move(pending));
            if(!resume_jump())return result;
            continue;
        }
        case ModelSequenceOpcode::wait_check: {
            // 8030BCFC acquires the replacement before releasing the old name.
            // Serialized null was decoded as empty: the assigned wait remains
            // non-null. 8030C5D4 advances the cursor before yielding, without
            // invoking the outer common/player wait resolver.
            std::string name=command.name;fields_->wait.name=std::move(name);
            advance_command_index(*fields_);++result.commands_executed;
            return stopped(ModelSequenceStop::waiting);
        }
        case ModelSequenceOpcode::hitbox: {
            if(command.name!="Set" && command.name!="AllClear")break;
            if(!hitbox)return stopped(ModelSequenceStop::hitbox_required);
            running_=true;hitbox_pending_=true;bool complete;
            try {complete=hitbox(*command.hitbox_command);}catch(...) {running_=false;throw;}
            running_=false;if(!complete)return stopped(ModelSequenceStop::hitbox_required);
            hitbox_pending_=false;
            break;
        }
        case ModelSequenceOpcode::lua_call: {
            // 8030B8A4 only these exact modes configure a later callback.
            // Prefix null was normalized to owned empty during decoding.
            std::optional<std::string>* target=nullptr;
            if(command.lua_mode=="BHit")target=&model_.bullet_hit_script();
            else if(command.lua_mode=="WHit")target=&model_.wall_hit_script();
            else if(command.lua_mode=="FHit")target=&model_.floor_hit_script();
            if(target) {*target=std::string(command.name);break;}
            if(!lua)return stopped(ModelSequenceStop::lua_required);
            // Request allocation has no external effect. Only exceptions from
            // an actually invoked Lua owner make this cursor unsafe to replay.
            const ModelLuaRequest request{program_->source(),command.reference_index,info_name_,ram_no_,command.name,command.lua_mode};
            running_=true;bool complete;
            try {complete=lua(request);}
            catch(...) {running_=false;lua_faulted_=true;throw;}
            running_=false;lua_pending_=!complete;
            if(!complete)return stopped(ModelSequenceStop::lua_required);
            break;
        }
        case ModelSequenceOpcode::unsupported:return stopped(ModelSequenceStop::unsupported_command);
        }
        advance_command_index(*fields_);++result.commands_executed;
    }
    throw MotionError("Model sequence execution budget exhausted; completed command effects are retained.");
}
} // namespace dw4
