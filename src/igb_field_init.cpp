#include "dw4/igb_field_init.hpp"
#include "dw4/igb_field_string_init.hpp"
#include "dw4/igb_object_field_construct.hpp"
#include "dw4/igb_memory_field_construct.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldInitError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
bool reference_table(std::uint32_t table) {
    return table==0x8047693c || table==0x804762fc || table==0x80476630 || table==0x80476088;
}
struct VirtualCall {
    IgbFieldOperationCall request;
    IgbFieldOperationsFactory factory;
    std::shared_ptr<IgbFieldOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    MessengerCallResult resume(const std::shared_ptr<IgbFieldInitServices>& services) {
        if(!factory && services && services->values && services->values->fields)factory=services->values->fields->factory;
        if(!factory)return pending("Field initialization requires its reached descriptor virtual.");
        if(!operation)operation=factory(request);
        if(!operation)return pending("Field initialization reached an unbound descriptor virtual.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Field initialization requires its retained descriptor continuation.");
        return callback();
    }
};
struct ReferenceClear {
    IgbObjectInstance object;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    VirtualCall call;
    std::uint32_t destination{};
    std::uint16_t size{};
    unsigned phase{};
    bool running{},fault{};
    MessengerCallResult resume(const std::shared_ptr<IgbFieldInitServices>& services) {
        if(running || fault){fault=true;fail("Reference initialization requires an unfaulted nonrecursive operation.");}
        if(phase==3)return {true,0,{}};
        struct Guard{bool& v;Guard(bool& value):v(value){v=true;}~Guard(){v=false;}} guard(running);
        try {
            if(phase==0) {
                if(!field || !field->vtable_word)return pending("Reference initialization requires its live descriptor vtable.");
                if(*field->vtable_word==0)fail("Reference initialization dereferences a null descriptor vtable at +64.");
                call.request.kind=IgbFieldOperationKind::field_virtual;call.request.field=field;
                call.request.vtable_word=*field->vtable_word;call.request.virtual_slot=0x64;
                destination=object.offset+field->offset;phase=1;
            }
            if(phase==1) {
                const auto status=call.resume(services);
                if(fault)fail("Reference initialization was invalidated by a callback.");
                if(!status.completed)return status;
                size=static_cast<std::uint16_t>(status.value);call.clear();
                if(fault)fail("Reference initialization was invalidated by callback teardown.");
                phase=2;
            }
            if(size!=0) {
                const auto bytes=object.storage?object.storage->bytes:nullptr;
                if(!bytes || bytes->released || destination>bytes->bytes.size() || size>bytes->bytes.size()-destination ||
                    destination>bytes->initialized.size() || size>bytes->initialized.size()-destination)
                    fail("Reference initialization reaches unavailable physical zero-fill storage.");
                for(std::uint32_t i=0;i<size;++i){bytes->bytes[destination+i]=0;bytes->initialized[destination+i]=1;}
                igb_synchronize_field_storage(object,destination,size);
            }
            phase=3;return {true,0,{}};
        }catch(...){fault=true;throw;}
    }
};
struct ObjectFieldTail {
    IgbObjectInstance object;
    VirtualCall call;
    bool captured{},done{},running{},fault{};
    MessengerCallResult resume(const std::shared_ptr<IgbFieldInitServices>& services) {
        if(running || fault){fault=true;fail("Object-field tail requires an unfaulted nonrecursive operation.");}
        if(done)return {true,0,{}};
        struct Guard{bool& v;Guard(bool& value):v(value){v=true;}~Guard(){v=false;}} guard(running);
        try {
            // Actual6A438 calls return-only667D0, then reloads the object's
            // physical table and selects +8C with a null source pointer.
            if(!captured) {
                if(!object.field || object.file || !object.storage)fail("Object-field tail requires its typed descriptor allocation.");
                call.request.kind=IgbFieldOperationKind::field_virtual;call.request.field=object.field;
                call.request.vtable_word=igb_buffer_word(object.storage->bytes,object.offset);
                object.field->vtable_word=call.request.vtable_word;
                call.request.virtual_slot=0x8c;captured=true;
            }
            const auto status=call.resume(services);
            if(fault)fail("Object-field tail was invalidated by a callback.");
            if(!status.completed)return status;
            call.clear();if(fault)fail("Object-field tail was invalidated by callback teardown.");
            done=true;return {true,0,{}};
        }catch(...){fault=true;throw;}
    }
};
std::shared_ptr<IgbObjectInitOperation> leaf() {
    auto operation=std::make_shared<IgbObjectInitOperation>();
    operation->resume=[] {return MessengerCallResult{true,0,{}};};return operation;
}
}
IgbObjectInitFactory make_native_igb_field_init_factory(std::shared_ptr<IgbFieldInitServices> services,IgbObjectInitFactory remaining) {
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation> {
        const auto delegate=[&]{return remaining?remaining(call):std::shared_ptr<IgbObjectInitOperation>{};};
        if(call.kind==IgbObjectInitServiceKind::object_virtual && (call.vtable_word==0x804762fc ||
            call.vtable_word==0x80475b60 || call.vtable_word==0x80476088 || call.vtable_word==0x80473e30)) {
            const auto kind=call.vtable_word==0x80475b60?IgbFieldTypeKind::signed_int:
                call.vtable_word==0x80476088?IgbFieldTypeKind::memory_reference:
                call.vtable_word==0x80473e30?IgbFieldTypeKind::boolean:IgbFieldTypeKind::object;
            if(call.virtual_slot==0x0c || call.virtual_slot==0x34 || call.virtual_slot==0x24 ||
                (kind!=IgbFieldTypeKind::object && call.virtual_slot==0x2c))return leaf();
            if(call.virtual_slot!=0x58 && call.virtual_slot!=0x2c)return delegate();
            const auto selected=weak.lock();if(!selected)fail("Field initializer services expired before selection.");
            auto operation=std::make_shared<IgbObjectInitOperation>();
            if(call.virtual_slot==0x58)operation->resume=[selected,kind,out=operation.get()] {
                const auto cell=igb_field_type_metadata_slot(selected->registration,kind);
                if(!cell || !*cell)return pending("Field metadata getter requires its represented canonical class cell.");
                out->metadata_result=**cell;return MessengerCallResult{true,0,{}};
            };
            else {auto state=std::make_shared<ObjectFieldTail>();state->object=call.object;
                operation->resume=[state,selected]{return state->resume(selected);};}
            return operation;
        }
        if(call.kind!=IgbObjectInitServiceKind::field_virtual || !call.field || !call.field->vtable_word ||
            !reference_table(*call.field->vtable_word))return delegate();
        const auto table=*call.field->vtable_word;
        if(call.virtual_slot==0x74 || (call.virtual_slot==0x7c && (table==0x8047693c || table==0x80476630)))return leaf();
        if(call.virtual_slot==0xf0) {
            auto operation=std::make_shared<IgbObjectInitOperation>();operation->resume=[field=call.field] {
                if(!field->flag_34)return pending("Reference default gate requires its live construct flag+34.");
                return MessengerCallResult{true,*field->flag_34,{}};
            };return operation;
        }
        if(call.virtual_slot==0x7c && table==0x804762fc){
            auto invocation=std::make_shared<IgbObjectFieldConstructInvocation>(call.field,call.object);
            auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[invocation,selected=weak.lock()]{return invocation->resume(selected?selected->object_construct:nullptr);};return operation;
        }
        if(call.virtual_slot==0x7c && table==0x80476088) {
            auto invocation=std::make_shared<IgbMemoryFieldConstructInvocation>(call.field,call.object);
            auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[invocation,selected=weak.lock()]{return invocation->resume(selected?selected->memory_construct:nullptr);};return operation;
        }
        if(call.virtual_slot==0x70 && table==0x804762fc) {
            auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[field=call.field,object=call.object,target=std::optional<std::uint32_t>{},fault=false]()mutable {
                if(fault)fail("Object-reference defaults require an unfaulted continuation.");
                try {
                    if(!target) {
                        if(!object.storage)fail("Object-reference defaults have no represented target allocation.");
                        target=igb_buffer_word(object.storage->bytes,object.offset+field->offset);
                    }
                    if(*target==0)return MessengerCallResult{true,0,{}};
                    return pending("Object-reference default requires actual80067098 on its captured nonnull member.");
                }catch(...){fault=true;throw;}
            };return operation;
        }
        if(call.virtual_slot!=0x84 && !(call.virtual_slot==0x70 && table==0x8047693c))return delegate();
        const auto selected=weak.lock();if(!selected)fail("Reference initializer services expired before selection.");
        auto operation=std::make_shared<IgbObjectInitOperation>();
        if(call.virtual_slot==0x84) {
            auto state=std::make_shared<ReferenceClear>();state->object=call.object;state->field=call.field;
            operation->resume=[state,selected]{return state->resume(selected);};
        }else {
            auto state=std::make_shared<IgbFieldStringInitInvocation>(call.field,call.object);
            operation->resume=[state,selected]{return state->resume(selected->values);};
        }
        return operation;
    };
}
} // namespace dw4
