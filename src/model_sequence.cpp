#include "dw4/model_sequence.hpp"

#include <algorithm>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t max_sequences=4096,max_commands=65536,max_text=8u*1024u*1024u;
void checked_name(std::string_view value) {
    if(value.size()>4096 || value.find('\0')!=std::string_view::npos)
        throw MotionError("Sequence names must contain at most 4096 bytes without NUL.");
}
void charge(std::size_t value,std::size_t& total,std::size_t limit) {
    if(value>limit-total)throw MotionError("Model sequence allocation budget exceeded.");
    total+=value;
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
            if(type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX" || type=="beModelCtrlCTRL" || type=="beModelCtrlPDMOVE") {
                command.name=text(doc,object,2,bytes);command.opcode=ModelSequenceOpcode::model;
                command.model_command=decode_model_command(doc,*entry);charge(command.name.size(),bytes,max_text);
                if(type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX")charge(type.size(),bytes,max_text);
            } else if(type=="beModelCtrlLABEL" || type=="beModelCtrlLABELCTRL" || type=="beModelCtrlACTION" || type=="beModelCtrlJUMP") {
                command.name=text(doc,object,2,bytes);
                command.opcode=type=="beModelCtrlLABEL" ? ModelSequenceOpcode::label :
                    type=="beModelCtrlLABELCTRL" ? ModelSequenceOpcode::label_control :
                    type=="beModelCtrlACTION" ? ModelSequenceOpcode::action : ModelSequenceOpcode::jump;
                if(command.opcode==ModelSequenceOpcode::label_control) {
                    auto scope=text(doc,object,3,bytes);
                    if(doc.field(object,3)->size!=4)command.label_sequence=std::move(scope);
                }
            }
            decoded.commands.push_back(std::move(command));
        }
        result.sequences_.push_back(std::move(decoded));
    }
    return result;
}

ModelSequenceRuntime::ModelSequenceRuntime(ModelSequenceProgram program,ModelControlState model,
    std::string info_name,std::int32_t ram_no)
    :program_(std::make_shared<const ModelSequenceProgram>(std::move(program))),model_(std::move(model)),
    info_name_(std::move(info_name)),ram_no_(ram_no) {checked_name(info_name_);}

std::string_view ModelSequenceRuntime::current_sequence() const noexcept {
    return current_ ? std::string_view(program_->sequences()[*current_].name) : std::string_view{};
}
void ModelSequenceRuntime::set_label(std::optional<std::string> sequence,std::string label) {
    if(running_)throw MotionError("Reentrant sequence mutation is unsupported.");
    if(sequence)checked_name(*sequence);
    checked_name(label);
    auto candidate=labels_;
    const auto found=std::find_if(candidate.begin(),candidate.end(),[&](const auto& value){return value.sequence==sequence;});
    if(found!=candidate.end()) {*found={std::move(sequence),std::move(label),-1};}
    else {
        if(candidate.size()==max_sequences)throw MotionError("Sequence label count exceeds its budget.");
        candidate.push_back({std::move(sequence),std::move(label),-1});
    }
    std::size_t budget=0;
    for(const auto& item:candidate) {charge(item.label.size(),budget,max_text);if(item.sequence)charge(item.sequence->size(),budget,max_text);}
    labels_=std::move(candidate);
}
void ModelSequenceRuntime::select(std::string_view sequence) {
    if(running_)throw MotionError("Reentrant sequence mutation is unsupported.");
    checked_name(sequence);if(!ordinary(sequence))throw MotionError("This command requires the outer model controller.");
    const auto& data=program_->sequences();
    const auto found=std::find_if(data.begin(),data.end(),[&](const auto& item){return item.name==sequence;});
    if(found==data.end())throw MotionError("Requested model sequence is absent from its owned program.");
    auto labels=labels_;std::int32_t start=0;
    const auto selected=std::find_if(labels.begin(),labels.end(),[&](const auto& item){return item.sequence && *item.sequence==sequence;});
    if(selected!=labels.end()) {
        start=selected->cached_offset;
        if(start==-1) {
            const auto label=std::find_if(found->commands.begin(),found->commands.end(),[&](const auto& command){
                return command.opcode==ModelSequenceOpcode::label && command.name==selected->label;
            });
            if(label!=found->commands.end())start=static_cast<std::int32_t>(label-found->commands.begin());
            selected->cached_offset=start;
        }
    }
    if(start<0 || std::size_t(start)>found->commands.size())throw MotionError("Selected sequence label has no valid command offset.");
    auto candidate=model_;candidate.mode=0;candidate.phase=0;candidate.pad_motion.reset();
    candidate.check_fall=true;candidate.channels.on_sequence_change();
    // Other full-work fields and original subcommand/virtual callbacks are
    // outside this owned preparation API, never acknowledged as executed here.
    model_=std::move(candidate);labels_=std::move(labels);
    current_=std::size_t(found-data.begin());cursor_=static_cast<std::uint32_t>(start);label_count_=0;
}

ModelSequenceResult ModelSequenceRuntime::run(std::uint64_t world_time,const ModelActionDispatch& action,const ModelSequenceCleanup& cleanup) {
    if(running_)throw MotionError("Reentrant sequence execution is unsupported.");
    if(!current_)throw MotionError("Model sequence work has not been selected/prepared.");
    ModelSequenceResult result;
    for(std::uint32_t visited=0;visited<65536;++visited) {
        const auto& sequence=program_->sequences()[*current_];
        if(cursor_==sequence.commands.size()) {result.stop=ModelSequenceStop::end;return result;}
        const auto& command=sequence.commands.at(cursor_);
        const auto stopped=[&](ModelSequenceStop why) {
            result.stop=why;result.command_reference=command.reference_index;result.type=command.type;result.name=command.name;return result;
        };
        switch(command.opcode) {
        case ModelSequenceOpcode::model: apply_model_command(model_,*command.model_command,world_time);break;
        case ModelSequenceOpcode::label:
            if(label_count_!=0)return stopped(ModelSequenceStop::label_boundary);
            label_count_=1;break;
        case ModelSequenceOpcode::label_control:set_label(command.label_sequence,command.name);break;
        case ModelSequenceOpcode::action:
            if(!action)return stopped(ModelSequenceStop::action_required);
            running_=true;
            try {action({program_->source(),command.reference_index,info_name_,ram_no_,command.name});}
            catch(...) {running_=false;throw;}
            running_=false;break;
        case ModelSequenceOpcode::jump: {
            if(!ordinary(command.name) || std::none_of(program_->sequences().begin(),program_->sequences().end(),
                [&](const auto& item){return item.name==command.name;}))return stopped(ModelSequenceStop::controller_required);
            auto candidate=*this; candidate.select(command.name);
            if(cleanup) {
                running_=true;
                bool complete;
                try { complete=cleanup(command.name); } catch(...) { running_=false; throw; }
                running_=false;
                if(!complete) return stopped(ModelSequenceStop::sequence_cleanup_required);
            }
            *this=std::move(candidate);++result.commands_executed;continue;
        }
        case ModelSequenceOpcode::unsupported:return stopped(ModelSequenceStop::unsupported_command);
        }
        ++cursor_;++result.commands_executed;
    }
    throw MotionError("Model sequence execution budget exhausted; completed command effects are retained.");
}
} // namespace dw4
