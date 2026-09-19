#include "dw4/igb_field_defaults.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbFieldDefaultsError(message);}
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
            done=true;return {true,0,{}};
        }catch(...) {fault=true;throw;}
    }
};
}
IgbObjectInitFactory make_native_igb_scalar_field_factory(IgbObjectInitFactory remaining) {
    return [remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation> {
        if(call.kind!=IgbObjectInitServiceKind::field_virtual || !call.field || !call.field->vtable_word ||
            (*call.field->vtable_word!=0x80475b60u && *call.field->vtable_word!=0x80473e30u))
            return remaining?remaining(call):nullptr;
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
