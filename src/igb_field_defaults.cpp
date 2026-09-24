#include "dw4/igb_field_defaults.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbFieldDefaultsError(message);}
bool scalar_table(std::uint32_t table) {
    return table==0x80476e6cu || table==0x80475b60u || table==0x80473e30u ||
        table==0x80477054u || table==0x80476848u || table==0x80473f24u || table==0x804758e4u || table==0x80470a44u;
}
struct ScalarCopy {
    IgbObjectInstance object;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::uint32_t destination{};
    std::uint16_t size{};
    bool captured{},done{},fault{};

    MessengerCallResult resume() {
        if(fault)fail("Scalar field defaults require an unfaulted continuation.");
        if(done)return {true,0,{}};
        try {
            // Both original wrappers read +08 and +14 before +20. Keep those
            // values when the physical default-pointer binding is unavailable.
            if(!captured) {destination=object.offset+field->offset;size=field->size;captured=true;}
            if(!field->default_bytes)return {false,0,"Scalar field default requires represented descriptor+20 storage."};
            const auto source=*field->default_bytes;
            if(size==0) {done=true;return {true,0,{}};}
            const auto input=igb_buffer_bytes(source.buffer,source.offset,size);
            if(!object.storage || !object.storage->bytes)fail("Scalar field default has no represented destination allocation.");
            const auto output=object.storage->bytes;
            if(output->released || destination>output->bytes.size() || size>output->bytes.size()-destination ||
                destination>output->initialized.size() || size>output->initialized.size()-destination)
                fail("Scalar field default reaches unavailable physical destination bytes.");
            // Actual800031E8 compares original source/destination addresses and
            // copies backwards when source precedes destination. Distinct
            // native allocations cannot alias; within one allocation preserve
            // that direction, including overlapping and identical ranges.
            const bool backwards=source.buffer==output && source.offset<destination;
            for(std::uint32_t step=0;step<size;++step) {
                const auto index=backwards?size-1u-step:step;
                output->bytes[destination+index]=input[index];
                output->initialized[destination+index]=1;
            }
            // The initializer owns the physical header during construction.
            // Keep its typed view coherent if an explicit descriptor aliases it.
            const auto header=static_cast<std::uint64_t>(object.offset)+4;
            if(object.file && destination<header+4 && static_cast<std::uint64_t>(destination)+size>header &&
                header+4<=output->bytes.size() && header+4<=output->initialized.size()) {
                bool initialized=true;
                for(unsigned byte=0;byte<4;++byte)initialized=initialized && output->initialized[static_cast<std::size_t>(header)+byte]!=0;
                if(initialized)object.file->reference_word=igb_buffer_word(output,static_cast<std::size_t>(header));
            }
            igb_synchronize_field_storage(object,destination,size);
            done=true;return {true,0,{}};
        }catch(...) {fault=true;throw;}
    }
};
}
void igb_synchronize_field_storage(const IgbObjectInstance& object,std::uint32_t offset,std::uint32_t count) {
    if(!object.storage || !object.storage->bytes || count==0)return;
    const auto bytes=object.storage->bytes;
    const auto ready=[&](std::uint32_t relative,unsigned width) {
        const auto begin=std::uint64_t(object.offset)+relative,end=begin+width;
        if(begin>=std::uint64_t(offset)+count || end<=offset || bytes->released ||
            end>bytes->bytes.size() || end>bytes->initialized.size())return false;
        for(auto i=begin;i<end;++i)if(!bytes->initialized[static_cast<std::size_t>(i)])return false;
        return true;
    };
    const auto word=[&](std::uint32_t relative){return igb_buffer_word(bytes,std::size_t(object.offset)+relative);};
    const auto half=[&](std::uint32_t relative){const auto i=std::size_t(object.offset)+relative;
        return static_cast<std::uint16_t>((std::uint16_t(bytes->bytes[i])<<8)|bytes->bytes[i+1]);};
    const auto byte=[&](std::uint32_t relative){return bytes->bytes[std::size_t(object.offset)+relative];};
    if(object.file && ready(4,4))object.file->reference_word=word(4);
    if(object.metadata){
        auto& m=*object.metadata;
        const auto retain=[&](const auto& owner){if(owner)m.overwritten_constructor_owners_.push_back(owner);};
        if(ready(4,4))m.reference_word=word(4);
        if(ready(8,4))m.word_08=word(8);if(ready(12,4))m.word_0c=word(12);
        if(ready(0x10,4))m.major=word(0x10);if(ready(0x14,4))m.minor=word(0x14);
        if(ready(0x18,1))m.flag_18=byte(0x18);if(ready(0x19,1))m.flag_19=byte(0x19);if(ready(0x1a,1))m.flag_1a=byte(0x1a);
        if(ready(0x20,4))m.allocation_prefix=word(0x20);if(ready(0x24,4))m.class_flags=word(0x24);
        if(ready(0x2c,4))m.instance_count=word(0x2c);
        if(ready(0x3c,4))m.factory_redirect=word(0x3c);if(ready(0x40,4))m.word_40=word(0x40);if(ready(0x44,4))m.word_44=word(0x44);
        if(ready(0x48,4))m.native_size=word(0x48);if(ready(0x4c,4))m.post_layout_callback=word(0x4c);
        if(ready(0x50,4))m.word_50=word(0x50);if(ready(0x54,4))m.field_callback=word(0x54);
        if(ready(0x58,2))m.native_alignment=half(0x58);if(ready(0x5c,4))m.type_word=word(0x5c);
        if(ready(0x28,4)){
            const auto address=word(0x28);if(!m.fields_address || *m.fields_address!=address || !address){retain(m.fields);m.fields.reset();}m.fields_address=address;
        }
        if(ready(0x30,4)){
            const auto address=word(0x30);if(!m.instances_address || *m.instances_address!=address || !address){
                if(m.instances)retain(*m.instances);m.instances.reset();if(!address)m.instances=std::shared_ptr<IgbObjectInstanceList>{};}m.instances_address=address;
        }
        if(ready(0x34,4)){
            const auto address=word(0x34);m.word_34=address;if(!m.children_address || *m.children_address!=address || !address){
                if(m.children)retain(*m.children);m.children.reset();if(!address)m.children=std::shared_ptr<IgbRuntimeTypeList>{};}m.children_address=address;
        }
        if(ready(0x38,4)){
            const auto address=word(0x38);if(!m.parent_address || *m.parent_address!=address || !address){
                if(m.parent)retain(*m.parent);m.parent.reset();if(!address)m.parent=std::shared_ptr<IgbRuntimeMetaType>{};}m.parent_address=address;
        }
        if(ready(0x1c,4)){
            const auto address=word(0x1c);if(!m.name_string_address || *m.name_string_address!=address || !address){
                if(m.name)m.overwritten_constructor_strings_.push_back(m.name);if(m.name_string)retain(*m.name_string);
                m.name.reset();m.name_string.reset();if(!address)m.name_string=std::shared_ptr<IgbInternedString>{};}m.name_string_address=address;
        }
        if(ready(0x60,4)){
            const auto address=word(0x60);if(!m.default_pool_string_address || *m.default_pool_string_address!=address || !address){
                if(m.default_pool_name && *m.default_pool_name)m.overwritten_constructor_strings_.push_back(*m.default_pool_name);
                if(m.default_pool_string)retain(*m.default_pool_string);m.default_pool_name.reset();m.default_pool_string.reset();
                if(!address){m.default_pool_name=SystemStageString{};m.default_pool_string=std::shared_ptr<IgbInternedString>{};}}
            m.default_pool_string_address=address;
        }
    }
    if(object.fields){
        auto& list=*object.fields;if(ready(4,4))list.reference_word=word(4);
        if(ready(8,4)){const auto address=word(8);if(!address || address!=list.backing_address){
            if(list.backing)list.retained_backings_.push_back(list.backing);if(list.backing_bytes)list.retained_backings_.push_back(list.backing_bytes);
            list.backing.reset();list.backing_bytes.reset();list.backing_offset=0;list.backing_address=address;}}
        if(ready(12,4))list.count=std::bit_cast<std::int32_t>(word(12));
    }
    if(object.types){
        auto& list=*object.types;if(ready(4,4))list.reference_word=word(4);
        if(ready(8,4)){const auto address=word(8);if(!address || address!=list.backing_address){
            if(list.backing)list.retained_backings_.push_back(list.backing);if(list.backing_bytes)list.retained_backings_.push_back(list.backing_bytes);
            list.backing.reset();list.backing_bytes.reset();list.backing_offset=0;list.backing_address=address;}}
        if(ready(12,4))list.count=std::bit_cast<std::int32_t>(word(12));
    }
    if(!object.field)return;
    auto& field=*object.field;
    if(ready(0,4))field.vtable_word=word(0);
    if(ready(4,4))field.reference_word=word(4);
    if(ready(8,4))field.offset=word(8);
    if(ready(0x10,2))field.type_ordinal=half(0x10);
    if(ready(0x12,2))field.ordinal=half(0x12);
    if(ready(0x14,2))field.size=half(0x14);
    if(ready(0x24,1))field.default_mode=byte(0x24);
    if(ready(0x25,1))field.flag_25=byte(0x25);
    if(ready(0x26,1))field.flag_26=byte(0x26);
    if(ready(0x27,1))field.flag_27=byte(0x27);
    if(ready(0x28,1))field.flag_28=byte(0x28);
    if(ready(0x29,1))field.flag_29=byte(0x29);
    if(ready(0x2a,1))field.flag_2a=byte(0x2a);
    if(ready(0x2b,1))field.flag_2b=byte(0x2b);
    if(ready(0x2c,1))field.flag_2c=byte(0x2c);
    if(ready(0x30,4))field.word_30=word(0x30);
    if(ready(0x0c,4)) {
        const auto address=word(0x0c);
        if(!field.name_string_address || *field.name_string_address!=address || address==0) {
            if(field.name)field.overwritten_constructor_strings_.push_back(field.name);
            if(field.name_string && *field.name_string)field.overwritten_constructor_owners_.push_back(*field.name_string);
            field.name.reset();field.name_string.reset();
            if(address==0)field.name_string=std::shared_ptr<IgbInternedString>{};
        }
        field.name_string_address=address;
    }
    if(ready(0x18,4)) {
        field.declaring_type.reset();
        if(word(0x18)==0)field.declaring_type=std::weak_ptr<IgbRuntimeMetaType>{};
    }
    if(ready(0x1c,4)) {
        const auto address=word(0x1c);
        if(!field.global_slot_address || *field.global_slot_address!=address || address==0) {
            field.global_slot.reset();if(address==0)field.global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
        }
        field.global_slot_address=address;
    }
    if(ready(0x20,4)) {
        const auto address=word(0x20);
        if(!field.default_bytes_address || *field.default_bytes_address!=address || address==0) {
            if(field.default_bytes && field.default_bytes->buffer)field.overwritten_constructor_owners_.push_back(field.default_bytes->buffer);
            field.default_bytes.reset();if(address==0)field.default_bytes=IgbRuntimeFieldDefaultSource{};
        }
        field.default_bytes_address=address;
    }
    const auto table=field.vtable_word.value_or(0);
    if(table==0x804758e4 && ready(0x34,4))field.method_function=word(0x34);
    const bool reference=table==0x8047693c || table==0x804762fc || table==0x80476630 || table==0x80476088;
    if(reference && ready(0x34,1))field.flag_34=byte(0x34);
    if(reference && ready(0x35,1))field.flag_35=byte(0x35);
    if(table==0x804762fc && ready(0x3c,1))field.flag_3c=byte(0x3c);
    if(table==0x804762fc && ready(0x38,4)) {
        field.object_type.reset();if(word(0x38)==0)field.object_type=std::weak_ptr<IgbRuntimeMetaType>{};
    }
    if(table==0x804762fc && ready(0x40,4))field.word_40=word(0x40);
    if(table==0x8047693c && ready(0x38,1))field.flag_38=byte(0x38);
    if(table==0x80476088) {
        if(ready(0x38,4))field.word_38=word(0x38);
        if(ready(0x3c,4)) {
            field.element_field.reset();if(word(0x3c)==0)field.element_field=std::weak_ptr<IgbDecodeRuntimeField>{};
        }
        if(ready(0x40,1))field.flag_40=byte(0x40);
        if(ready(0x41,1))field.flag_41=byte(0x41);
        if(ready(0x42,1))field.flag_42=byte(0x42);
        if(ready(0x44,4))field.word_44=word(0x44);
        if(ready(0x48,4))field.word_48=word(0x48);
        if(ready(0x4c,4))field.word_4c=word(0x4c);
    }
}
IgbObjectInitFactory make_native_igb_scalar_field_factory(IgbObjectInitFactory remaining) {
    return [remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation> {
        if(call.kind!=IgbObjectInitServiceKind::field_virtual || !call.field || !call.field->vtable_word ||
            (!scalar_table(*call.field->vtable_word) && *call.field->vtable_word!=0x80476630u))
            return remaining?remaining(call):nullptr;
        const bool pointer=*call.field->vtable_word==0x80476630u;
        if(pointer && (call.virtual_slot==0x84 || call.virtual_slot==0xf0))return remaining?remaining(call):nullptr;
        if(call.virtual_slot==0x84 || call.virtual_slot==0x7c || call.virtual_slot==0x74 || call.virtual_slot==0xf0) {
            auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[] {return MessengerCallResult{true,0,{}};};return operation;
        }
        if(call.virtual_slot!=0x70 && call.virtual_slot!=0x78)return remaining?remaining(call):nullptr;
        auto state=std::make_shared<ScalarCopy>();state->object=call.object;state->field=call.field;
        auto operation=std::make_shared<IgbObjectInitOperation>();operation->resume=[state] {return state->resume();};return operation;
    };
}
} // namespace dw4
