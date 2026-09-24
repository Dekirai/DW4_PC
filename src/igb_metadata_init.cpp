#include "dw4/igb_metadata_init.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetadataInitError(message);}
}
IgbObjectInitFactory make_native_igb_metadata_init_factory(std::shared_ptr<IgbMetadataInitServices> services,IgbObjectInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation>{
        if(call.kind!=IgbObjectInitServiceKind::object_virtual || (call.vtable_word!=0x804761d8 && call.vtable_word!=0x80475cb0 && call.vtable_word!=0x80475c54) ||
            (call.virtual_slot!=0x58 && call.virtual_slot!=0x0c && call.virtual_slot!=0x24 && call.virtual_slot!=0x2c && call.virtual_slot!=0x34))
            return remaining?remaining(call):nullptr;
        const bool registry=call.vtable_word==0x80475cb0,children=call.vtable_word==0x80475c54;
        if(children?!call.object.types:registry?(!call.object.fields && !call.object.types):!call.object.metadata)
            fail("Metadata/list initialization virtual requires its exact typed instance.");
        igb_object_validate_instance(call.object);
        auto op=std::make_shared<IgbObjectInitOperation>();
        if(call.virtual_slot==0x58){
            const auto selected=weak.lock();if(!selected)fail("Metadata initialization services expired before getter selection.");
            op->resume=[selected,registry,children,types=bool(call.object.types),out=op.get()]{
                if(children || (registry && types)){
                    const auto slot=selected->type_lists?(children?selected->type_lists->children_metadata:selected->type_lists->registry_metadata):nullptr;
                    if(!slot || !*slot)return MessengerCallResult{false,0,children?
                        "Children-list getter requires represented805619D4.":"Type-registry getter requires represented805619CC."};
                    out->metadata_result=**slot;return MessengerCallResult{true,0,{}};
                }
                if(registry){
                    if(!selected->field_lists || !selected->field_lists->metadata)return MessengerCallResult{false,0,"Field-list getter requires represented805619CC."};
                    out->metadata_result=*selected->field_lists->metadata;return MessengerCallResult{true,0,{}};
                }
                if(!selected->metadata || !selected->metadata->metadata)return MessengerCallResult{false,0,"Metadata getter requires represented8056174C."};
                out->metadata_result=*selected->metadata->metadata;return MessengerCallResult{true,0,{}};
            };
        }else op->resume=[] {return MessengerCallResult{true,0,{}};};
        return op;
    };
}
IgbMetadataFactory make_native_igb_reflected_metadata_factory(std::shared_ptr<IgbMetadataInitServices> services,IgbMetadataFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbMetadataFactoryCall& call)->std::shared_ptr<IgbMetadataFactoryOperation>{
        if(call.kind!=IgbMetadataFactoryServiceKind::reflected_factory)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Reflected metadata services expired before selection.");
        auto invocation=std::make_shared<IgbReflectedFactoryInvocation>(call.metadata,call.pool);
        auto operation=std::make_shared<IgbMetadataFactoryOperation>();
        operation->resume=[invocation,selected,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            const auto status=invocation->resume(selected->reflected);if(!status.completed)return status;
            const auto instance=invocation->result_instance();
            if(instance.storage){
                if(!instance.metadata)fail("Reflected metadata factory returned another native object kind.");
                igb_object_validate_instance(instance);selected->completed_allocations.push_back(instance);
            }
            out->allocation_result={instance.storage,instance.offset};done=true;return status;
        };return operation;
    };
}
IgbTypeListFactory make_native_igb_reflected_type_list_factory(std::shared_ptr<IgbMetadataInitServices> services,IgbTypeListFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeListCall& call)->std::shared_ptr<IgbTypeListOperation>{
        if(call.kind!=IgbTypeListServiceKind::reflected_factory)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Reflected type-list services expired before selection.");
        auto invocation=std::make_shared<IgbReflectedFactoryInvocation>(call.metadata,call.pool);
        auto operation=std::make_shared<IgbTypeListOperation>();
        operation->resume=[invocation,selected,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            const auto status=invocation->resume(selected->reflected);if(!status.completed)return status;
            const auto instance=invocation->result_instance();
            if(instance.storage){
                if(!instance.types)fail("Reflected type-list factory returned another native object kind.");
                igb_object_validate_instance(instance);selected->completed_allocations.push_back(instance);
            }
            out->allocation_result={instance.storage,instance.offset};done=true;return status;
        };return operation;
    };
}
} // namespace dw4
