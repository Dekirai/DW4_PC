#include "dw4/igb_reflected_clone.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbReflectedCloneError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
using K=IgbReflectedCloneServiceKind;
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
std::uint32_t object_table(const IgbObjectInstance& object){
    if(!address(object))fail("Reflected copy dereferences a null object.");
    igb_object_validate_instance(object);
    const auto table=igb_buffer_word(object.storage->bytes,object.offset);
    if(!table)fail("Reflected copy dereferences a null object vtable.");
    if(object.field)object.field->vtable_word=table;return table;
}
std::optional<std::uint32_t> field_table(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(field->physical_storage){
        const auto& binding=*field->physical_storage;const auto storage=binding.storage.lock();
        if(!storage)fail("Field copy dispatch reached expired physical storage.");
        if(igb_bootstrap_field({storage,binding.offset})!=field)fail("Field copy dispatch lost its physical identity.");
        field->vtable_word=igb_buffer_word(storage->bytes,binding.offset);
    }
    if(field->vtable_word && !*field->vtable_word)fail("Field copy dispatch dereferences a null field vtable.");
    return field->vtable_word;
}
struct Call {
    IgbReflectedCloneCall request;IgbReflectedCloneFactory factory;
    std::shared_ptr<IgbReflectedCloneOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbReflectedCloneServices>& services,Check check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Reflected copy requires its selected native operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Reflected copy reached an unbound original operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Reflected copy requires its retained operation continuation.");
        const auto status=callback();check();return status;
    }
};
void preserve(const std::shared_ptr<IgbReflectedCloneServices>& services,const IgbObjectInstance& object){
    if(services && object.storage)services->unfinished.push_back(object);
}
}

struct IgbFieldCopyDispatchInvocation::Impl {
    enum class P {policy,table,call,done};
    std::shared_ptr<IgbDecodeRuntimeField> field;IgbObjectInstance destination,source;
    std::uint32_t policy{},slot{};Call call;P phase=P::policy;bool running{},fault{};
};
IgbFieldCopyDispatchInvocation::IgbFieldCopyDispatchInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,
    IgbObjectInstance destination,IgbObjectInstance source,std::uint32_t policy):impl_(std::make_unique<Impl>()){
    if(policy<0x27 || policy>0x29)fail("Unsupported original field copy policy offset.");
    impl_->field=std::move(field);impl_->destination=std::move(destination);impl_->source=std::move(source);impl_->policy=policy;
}
IgbFieldCopyDispatchInvocation::~IgbFieldCopyDispatchInvocation()=default;
bool IgbFieldCopyDispatchInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldCopyDispatchInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldCopyDispatchInvocation::resume(std::shared_ptr<IgbReflectedCloneServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Field copy dispatch requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);
    const auto check=[&]{if(s.fault)fail("Field copy dispatch was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::policy:{
        if(!s.field)fail("Field copy policy dereferences a null descriptor.");
        const auto value=s.policy==0x27?s.field->flag_27:s.policy==0x28?s.field->flag_28:s.field->flag_29;
        if(!value)return pending("Field copy requires its selected signed policy byte.");
        const auto mode=std::bit_cast<std::int8_t>(*value);
        if(mode<0 || mode>4){s.phase=P::done;break;}
        s.slot=0x90u+4u*static_cast<std::uint32_t>(mode);s.phase=P::table;break;
    }
    case P::table:{
        const auto table=field_table(s.field);if(!table)return pending("Field copy requires its selected field vtable.");
        s.call.request.kind=K::field_virtual;s.call.request.field=s.field;
        s.call.request.destination=s.destination;s.call.request.source=s.source;
        s.call.request.vtable_word=*table;s.call.request.virtual_slot=s.slot;
        if(services)s.call.factory=services->factory;s.phase=P::call;break;
    }
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;
        s.call.clear();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbObjectCopyInvocation::Impl {
    enum class P {mode,metadata,after_metadata,destination_hook,source_hook,range,base,count,visit,member,tail,call,done};
    IgbObjectInstance destination,source;std::uint32_t mode{},index{},byte_offset{},visits{};
    std::int32_t count{};std::shared_ptr<IgbRuntimeMetaType> metadata,base;
    std::shared_ptr<IgbDecodeFieldRegistry> registry;
    std::shared_ptr<IgbReflectedCloneServices> owner;
    std::unique_ptr<IgbFieldCopyDispatchInvocation> member;Call call;
    P phase=P::mode,after=P::done;bool running{},fault{};
};
IgbObjectCopyInvocation::IgbObjectCopyInvocation(IgbObjectInstance destination,IgbObjectInstance source,std::uint32_t mode)
    :impl_(std::make_unique<Impl>()){
    impl_->destination=std::move(destination);impl_->source=std::move(source);impl_->mode=mode;
}
IgbObjectCopyInvocation::~IgbObjectCopyInvocation(){auto& s=*impl_;if(!completed() || faulted())preserve(s.owner,s.destination);}
bool IgbObjectCopyInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbObjectCopyInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbObjectCopyInvocation::resume(std::shared_ptr<IgbReflectedCloneServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Object copy requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Object copy was invalidated by a callback.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto object_virtual=[&](const IgbObjectInstance& object,std::uint32_t slot,P after){
        clear();const auto table=object_table(object);
        s.call.request.kind=K::object_virtual;s.call.request.object=object;
        s.call.request.destination=s.destination;s.call.request.source=s.source;
        s.call.request.virtual_slot=slot;s.call.request.vtable_word=table;
        if(services)s.call.factory=services->factory;s.after=after;s.phase=P::call;
    };
    try{for(;;){check();switch(s.phase){
    case P::mode:s.phase=s.mode<=2?P::metadata:P::done;break;
    case P::metadata:object_virtual(s.destination,0x58,P::after_metadata);break;
    case P::after_metadata:
        s.metadata=std::move(s.call.operation->metadata_result);clear();
        s.phase=s.mode==1?P::destination_hook:P::range;break;
    case P::destination_hook:object_virtual(s.destination,0x48,P::source_hook);break;
    case P::source_hook:object_virtual(s.source,0x48,P::range);break;
    case P::range:
        clear();if(!s.metadata)fail("Object copy dereferences null captured metadata at +28.");
        s.registry=s.metadata->fields;s.phase=P::base;break;
    case P::base:
        if(!services || !services->object_metadata)return pending("Object copy requires base igObject metadata805622A4.");
        s.base=*services->object_metadata;s.phase=P::count;break;
    case P::count:
        if(!s.registry)fail("Object copy dereferences a null field registry count.");
        s.count=s.registry->count;
        if(!s.base)fail("Object copy dereferences null base metadata at +28.");
        if(!s.base->fields)fail("Object copy dereferences null base field registry at +0C.");
        s.index=std::bit_cast<std::uint32_t>(s.base->fields->count);
        s.byte_offset=s.index<<2u;s.phase=P::visit;break;
    case P::visit:{
        if(std::bit_cast<std::int32_t>(s.index)>=s.count){s.phase=s.mode==2?P::done:P::tail;break;}
        if(++s.visits>262144u)fail("Object copy exceeds the represented field visit bound.");
        // slwi/addi wrap independently from the signed loop index.
        const auto physical_index=s.byte_offset>>2u;const auto backing=s.registry->backing;
        if(!backing || physical_index>=backing->size())fail("Object copy reaches an unavailable physical field slot.");
        const auto field=backing->at(physical_index);
        s.member=std::make_unique<IgbFieldCopyDispatchInvocation>(field,s.destination,s.source,0x27u+s.mode);
        s.phase=P::member;break;
    }
    case P::member:{const auto status=s.member->resume(services);check();if(!status.completed)return status;
        s.member.reset();check();++s.index;s.byte_offset+=4u;s.phase=P::visit;break;}
    case P::tail:object_virtual(s.destination,s.mode==1?0x14:0x18,P::done);break;
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbReflectedCloneInvocation::Impl {
    enum class P {metadata,after_metadata,pool,after_pool,allocate,after_allocate,initialize,copy,call,done};
    IgbObjectInstance source,destination;std::uint32_t mode{};
    std::shared_ptr<IgbRuntimeMetaType> metadata;std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbReflectedCloneServices> owner;
    std::unique_ptr<IgbReflectedFactoryInvocation> reflected;
    std::unique_ptr<IgbObjectCopyInvocation> copy;Call call;
    P phase=P::metadata,after=P::done;bool running{},fault{};
};
IgbReflectedCloneInvocation::IgbReflectedCloneInvocation(IgbObjectInstance source,std::uint32_t mode):impl_(std::make_unique<Impl>()){
    impl_->source=std::move(source);impl_->mode=mode;
}
IgbReflectedCloneInvocation::~IgbReflectedCloneInvocation(){auto& s=*impl_;if(!completed() || faulted())preserve(s.owner,s.destination);}
bool IgbReflectedCloneInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbReflectedCloneInvocation::faulted()const noexcept{return impl_->fault;}
IgbObjectInstance IgbReflectedCloneInvocation::result_instance()const{
    if(!completed() || faulted())fail("Incomplete reflected clone has no result.");return impl_->destination;
}
MessengerCallResult IgbReflectedCloneInvocation::resume(std::shared_ptr<IgbReflectedCloneServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Reflected clone requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,address(s.destination),{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Reflected clone was invalidated by a callback.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after){
        s.call.request.kind=kind;s.call.request.object=s.source;s.call.request.source=s.source;
        s.call.request.metadata=s.metadata;s.call.request.pool=s.pool;
        if(services)s.call.factory=services->factory;s.after=after;s.phase=P::call;
    };
    try{for(;;){check();switch(s.phase){
    case P::metadata:{clear();const auto table=object_table(s.source);
        select(K::object_virtual,P::after_metadata);s.call.request.vtable_word=table;s.call.request.virtual_slot=0x58;break;}
    case P::after_metadata:s.metadata=std::move(s.call.operation->metadata_result);clear();s.phase=P::pool;break;
    case P::pool:{clear();igb_object_validate_instance(s.source);
        const auto handle=igb_bootstrap_read_byte({s.source.storage,s.source.offset},4);
        select(K::tagged_pool,P::after_pool);s.call.request.handle=handle;break;}
    case P::after_pool:s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::allocate;break;
    case P::allocate:
        if(services && services->reflected){s.reflected=std::make_unique<IgbReflectedFactoryInvocation>(s.metadata,s.pool);s.phase=P::initialize;}
        else{clear();select(K::reflected_factory,P::after_allocate);}break;
    case P::after_allocate:s.destination=std::move(s.call.operation->object_result);clear();s.phase=P::copy;break;
    case P::initialize:{const auto status=s.reflected->resume(services?services->reflected:nullptr);check();if(!status.completed)return status;
        s.destination=s.reflected->result_instance();s.reflected.reset();check();s.phase=P::copy;break;}
    case P::copy:
        if(!s.copy)s.copy=std::make_unique<IgbObjectCopyInvocation>(s.destination,s.source,s.mode);
        {const auto status=s.copy->resume(services);check();if(!status.completed)return status;}
        s.copy.reset();check();s.phase=P::done;break;
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:clear();return {true,address(s.destination),{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbReflectedCloneFactory make_native_igb_reflected_clone_virtual_factory(
    std::shared_ptr<IgbObjectInitServices> initializer,IgbReflectedCloneFactory remaining){
    return [weak=std::weak_ptr(initializer),remaining=std::move(remaining)](const IgbReflectedCloneCall& call)
        ->std::shared_ptr<IgbReflectedCloneOperation>{
        const auto delegate=[&]{return remaining?remaining(call):nullptr;};
        if(call.kind!=K::object_virtual)return delegate();
        // The memory and unsigned-byte tables also point these three slots
        // directly at667E8/667AC/667B8; they do not invoke member destruction.
        if((call.vtable_word==0x80473f24 || call.vtable_word==0x80473e30 || call.vtable_word==0x8047693c ||
            call.vtable_word==0x80476088 || call.vtable_word==0x80470a44) &&
            (call.virtual_slot==0x48 || call.virtual_slot==0x14 || call.virtual_slot==0x18)){
            auto operation=std::make_shared<IgbReflectedCloneOperation>();operation->resume=[]{return MessengerCallResult{true,0,{}};};return operation;
        }
        if(call.virtual_slot!=0x58)return delegate();
        const auto selected=weak.lock();if(!selected || !selected->factory)return delegate();
        IgbObjectInitCall request;request.kind=IgbObjectInitServiceKind::object_virtual;request.object=call.object;
        request.vtable_word=call.vtable_word;request.virtual_slot=call.virtual_slot;
        const auto nested=selected->factory(request);if(!nested)return delegate();
        auto operation=std::make_shared<IgbReflectedCloneOperation>();
        operation->resume=[nested,selected,out=operation.get(),callback=std::function<MessengerCallResult()>{}]()mutable{
            if(!callback)callback=nested->resume;if(!callback)return pending("Reflected clone requires its retained metadata continuation.");
            const auto status=callback();if(status.completed)out->metadata_result=nested->metadata_result;return status;
        };return operation;
    };
}
IgbFieldCloneFactory make_native_igb_reflected_field_clone_factory(
    std::shared_ptr<IgbReflectedCloneServices> services,IgbFieldCloneFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbFieldCloneCall& call)->std::shared_ptr<IgbFieldCloneOperation>{
        if(call.kind!=IgbFieldCloneServiceKind::reflected_clone || call.function!=0x800680b4)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Reflected field clone services expired before selection.");
        struct State {std::shared_ptr<IgbReflectedCloneInvocation> invocation;bool fault{};};auto state=std::make_shared<State>();
        auto operation=std::make_shared<IgbFieldCloneOperation>();
        operation->resume=[state,selected,source=call.source,mode=call.argument,out=operation.get()]{
            if(state->fault)fail("Reflected field clone adapter was invalidated.");
            try{
            if(!state->invocation){
                IgbObjectInstance object;
                if(source){
                    if(!source->physical_storage)return pending("Reflected clone requires the source field's physical allocation binding.");
                    const auto& binding=*source->physical_storage;object.storage=binding.storage.lock();object.offset=binding.offset;object.field=source;
                    if(!object.storage)fail("Reflected clone reached an expired source allocation.");
                }
                state->invocation=std::make_shared<IgbReflectedCloneInvocation>(object,mode);
            }
            const auto status=state->invocation->resume(selected);
            if(status.completed){
                const auto result=state->invocation->result_instance();
                if(address(result)){
                    igb_object_validate_instance(result);
                    if(!result.field || result.file || result.metadata || result.fields || result.types)
                        fail("Reflected field clone returned a non-field instance.");
                }
                out->allocation_result={result.storage,result.offset};
            }
            return status;
            }catch(...){state->fault=true;throw;}
        };return operation;
    };
}
IgbReflectedCloneFactory make_native_igb_reflected_member_copy_factory(
    std::shared_ptr<IgbFieldMemberCopyServices> services,IgbReflectedCloneFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbReflectedCloneCall& call)
        ->std::shared_ptr<IgbReflectedCloneOperation>{
        const auto delegate=[&]{return remaining?remaining(call):nullptr;};
        if(call.kind!=K::field_virtual || call.virtual_slot<0x90 || call.virtual_slot>0xa0 || (call.virtual_slot&3u))return delegate();
        const auto table=call.vtable_word;
        const bool scalar=table==0x80475b60 || table==0x80476848 || table==0x80477054 ||
            table==0x80473f24 || table==0x80473e30 || table==0x80476630 || table==0x804758e4 || table==0x80470a44;
        if(!scalar && table!=0x8047693c && table!=0x804762fc && table!=0x80476088)return delegate();
        const auto slot=(call.virtual_slot-0x90u)/4u;
        constexpr std::uint32_t raw[]={0x800642e4,0x8006434c,0x80064318,0x80064380,0x800643a0};
        constexpr std::uint32_t string[]={0x80071b58,0x8006434c,0x80071bc4,0x80064380,0x800643a0};
        constexpr std::uint32_t object[]={0x8006a844,0x8006a81c,0x8006a964,0x8006aa54,0x800643a0};
        constexpr std::uint32_t memory[]={0x80062824,0x8006434c,0x80062998,0x80064380,0x800643a0};
        const auto function=scalar?raw[slot]:table==0x8047693c?string[slot]:table==0x804762fc?object[slot]:memory[slot];
        auto operation=std::make_shared<IgbReflectedCloneOperation>();
        if(function==0x800643a0){operation->resume=[]{return MessengerCallResult{true,0,{}};};return operation;}
        const auto selected=weak.lock();if(!selected)fail("Reflected member copy services expired before selection.");
        auto invocation=std::make_shared<IgbFieldMemberCopyInvocation>(call.field,call.destination,call.source,function);
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
