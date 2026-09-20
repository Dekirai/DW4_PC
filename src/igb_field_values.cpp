#include "dw4/igb_field_values.hpp"
#include "dw4/igb_stream.hpp"

#include <array>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldValuesError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
struct Profile {std::uint32_t vtable,size;};
constexpr std::array<Profile,8> profiles={{{0x80476e6c,4},{0x80475b60,4},{0x80473e30,1},
    {0x8047693c,4},{0x804762fc,4},{0x80476630,4},{0x80477054,2},{0x80476088,4}}};
std::optional<std::size_t> profile(std::uint32_t table){for(std::size_t i=0;i<profiles.size();++i)if(profiles[i].vtable==table)return i;return {};}
IgbRuntimeFieldSlot* prototype(IgbFieldValuesServices& s,std::size_t index){
    switch(index){case 0:return s.fields?&s.fields->unsigned_int_prototype:nullptr;case 1:return &s.signed_int_prototype;
    case 2:return &s.boolean_prototype;case 3:return &s.string_prototype;case 4:return &s.object_prototype;
    case 5:return &s.pointer_prototype;case 6:return &s.unsigned_short_prototype;default:return &s.memory_reference_prototype;}
}
bool range(const IgbRuntimeFieldDefaultSource& view,std::uint32_t count,bool read){
    if(!view.buffer || view.buffer->released)return false;
    const auto end=std::uint64_t(view.offset)+count;
    if(end>view.buffer->bytes.size() || end>view.buffer->initialized.size())return false;
    if(read)for(std::uint32_t i=0;i<count;++i)if(!view.buffer->initialized[view.offset+i])return false;
    return true;
}
std::uint8_t read_byte(const IgbRuntimeFieldDefaultSource& view,std::uint32_t relative){
    return igb_buffer_bytes(view.buffer,static_cast<std::uint32_t>(view.offset+relative),1).front();
}
void write_byte(const IgbRuntimeFieldDefaultSource& view,std::uint32_t relative,std::uint8_t value){
    const auto offset=static_cast<std::uint32_t>(view.offset+relative);
    if(!view.buffer || view.buffer->released || offset>=view.buffer->bytes.size() || offset>=view.buffer->initialized.size())
        fail("Field default copy writes unavailable physical storage.");
    view.buffer->bytes[offset]=value;view.buffer->initialized[offset]=1;
}
void write_word(const IgbRuntimeFieldDefaultSource& view,std::uint32_t value){
    if(!range(view,4,false))fail("String default writes unavailable physical pointer storage.");
    for(unsigned i=0;i<4;++i)write_byte(view,i,static_cast<std::uint8_t>(value>>((3-i)*8)));
}
std::shared_ptr<IgbInternedString> string_owner(const IgbFieldValuesServices& s,std::uint32_t address){
    std::shared_ptr<IgbInternedString> result;
    for(const auto& binding:s.strings)if(binding.address==address){
        if(result && result!=binding.owner)fail("String default address has conflicting header owners.");
        if(!binding.owner)fail("Nonnull string default address has a null header binding.");result=binding.owner;
    }return result;
}
std::optional<std::uint32_t> string_address(const IgbFieldValuesServices& s,const std::shared_ptr<IgbInternedString>& owner){
    if(!owner)return 0;std::optional<std::uint32_t> result;
    for(const auto& binding:s.strings)if(binding.owner==owner){
        if(binding.address==0 || (result && *result!=binding.address))fail("String default header has conflicting arithmetic addresses.");result=binding.address;
    }return result;
}
std::optional<std::uint32_t> buffer_address(const IgbFieldValuesServices& s,const IgbRuntimeFieldDefaultSource& view){
    std::optional<std::uint32_t> result;
    for(const auto& binding:s.buffers)if(binding.buffer && binding.buffer==view.buffer){const auto address=binding.base_address+view.offset;
        if(result && *result!=address)fail("Field copy buffer has conflicting arithmetic identities.");result=address;}
    return result;
}
struct StringCall {
    IgbDependenciesCall request;IgbDependenciesFactory factory;std::shared_ptr<IgbDependenciesOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const IgbDependenciesFactory& fallback,Check check){
        if(!factory)factory=fallback;if(!factory)return pending("String default requires its reached dependency factory.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("String default reached an unbound dependency operation.");
        if(!callback)callback=operation->resume;if(!callback)return pending("String default requires its retained continuation.");
        const auto status=callback();check();return status;
    }
};
}
void igb_bind_field_string(const std::shared_ptr<IgbFieldValuesServices>& services,std::uint32_t address,const std::shared_ptr<IgbInternedString>& owner){
    if(!services || address==0 || !owner)fail("Field string binding requires services, a nonzero address and its header.");
    const auto existing=string_owner(*services,address);const auto previous=string_address(*services,owner);
    if((existing && existing!=owner) || (previous && *previous!=address))fail("Field string binding conflicts with canonical pointer identity.");
    if(!existing)services->strings.push_back({address,owner});
}
struct IgbFieldValueInvocation::Impl {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbRuntimeFieldDefaultSource> source,destination;
    std::optional<std::uint32_t> source_address,destination_address;
    std::shared_ptr<IgbFieldValuesServices> owner;
    std::unique_ptr<IgbFieldBufferInvocation> buffer;
    std::uint32_t count{};unsigned phase{};bool running{},fault{};
};
IgbFieldValueInvocation::IgbFieldValueInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,
    std::optional<IgbRuntimeFieldDefaultSource> source,std::optional<std::uint32_t> address):impl_(std::make_unique<Impl>()){
    impl_->field=std::move(field);impl_->source=std::move(source);impl_->source_address=address;
    impl_->buffer=std::make_unique<IgbFieldBufferInvocation>(impl_->field);
}
IgbFieldValueInvocation::~IgbFieldValueInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.field)s.owner->unfinished.push_back(s.field);if(s.source && s.source->buffer)s.owner->unfinished.push_back(s.source->buffer);
    if(s.destination && s.destination->buffer)s.owner->unfinished.push_back(s.destination->buffer);
}}
bool IgbFieldValueInvocation::completed()const noexcept{return impl_->phase==3;}
bool IgbFieldValueInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldValueInvocation::resume(std::shared_ptr<IgbFieldValuesServices> services){
    auto& s=*impl_;if(s.running || s.fault){s.fault=true;fail("Field default copy requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field default copy continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case 0:{const auto status=s.buffer->resume(services?services->fields:nullptr);check();if(!status.completed)return status;
        s.buffer.reset();check();
        if(s.source_address && ((*s.source_address==0)==bool(s.source)))fail("Field copy source pointer and arithmetic identity disagree.");
        s.phase=s.source?1:3;break;}
    case 1:
        if(!s.field)fail("Field default copy reloads a null field.");
        if(!s.field->default_bytes)return pending("Field default copy requires the live destination pointer+20.");
        s.destination=*s.field->default_bytes;s.destination_address=s.field->default_bytes_address;s.count=s.field->size;s.phase=2;break;
    case 2:{
        if(s.count==0){s.phase=3;break;}
        if(services){if(!s.source_address)s.source_address=buffer_address(*services,*s.source);
            if(!s.destination_address)s.destination_address=buffer_address(*services,*s.destination);}
        if(s.source->buffer && s.source->buffer==s.destination->buffer){
            if(s.source_address && !s.destination_address)s.destination_address=*s.source_address-s.source->offset+s.destination->offset;
            if(s.destination_address && !s.source_address)s.source_address=*s.destination_address-s.destination->offset+s.source->offset;
            if(s.source_address && s.destination_address && *s.source_address-s.source->offset!=*s.destination_address-s.destination->offset)
                fail("Field copy aliases have inconsistent arithmetic allocation identities.");
        }
        if(s.source_address && *s.source_address==0)fail("Nonnull field copy source resolves to a null arithmetic pointer.");
        bool backwards{};
        if(s.source_address && s.destination_address)backwards=*s.source_address<*s.destination_address;
        else if(s.source->buffer && s.source->buffer==s.destination->buffer)backwards=s.source->offset<s.destination->offset;
        else if(!range(*s.source,s.count,true) || !range(*s.destination,s.count,false))
            return pending("Directional partial field copy requires represented source and destination addresses.");
        //031E8 compares unsigned pointers and reads/writes one byte at a time.
        // Two fully represented disjoint allocations have the same result in
        // either direction; no pointer arithmetic identity is manufactured.
        for(std::uint32_t i=0;i<s.count;++i){const auto at=backwards?s.count-1-i:i;const auto byte=read_byte(*s.source,at);write_byte(*s.destination,at,byte);}
        s.phase=3;break;
    }
    default:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbFieldStringDefaultInvocation::Impl {
    enum class P {global,capture,pool,after_allocate,after_initialize,publish_pool,intern,after_intern,old,resolve_old,release,publish,call,done};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    IgbDependencyName name;
    IgbRuntimeFieldDefaultSource buffer;
    std::shared_ptr<IgbFieldValuesServices> owner;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbInternedString> incoming,old;
    StringCall call;P phase=P::global,after=P::done;std::uint32_t old_address{};bool pool_exists{},running{},fault{};
};
IgbFieldStringDefaultInvocation::IgbFieldStringDefaultInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,IgbDependencyName name):impl_(std::make_unique<Impl>()){
    impl_->field=std::move(field);impl_->name=std::move(name);
}
IgbFieldStringDefaultInvocation::~IgbFieldStringDefaultInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.field)s.owner->unfinished.push_back(s.field);if(s.buffer.buffer)s.owner->unfinished.push_back(s.buffer.buffer);
    if(s.incoming)s.owner->unfinished.push_back(s.incoming);if(s.old)s.owner->unfinished.push_back(s.old);if(s.pool)s.owner->unfinished.push_back(s.pool);
}}
bool IgbFieldStringDefaultInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldStringDefaultInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldStringDefaultInvocation::resume(std::shared_ptr<IgbFieldValuesServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbDependenciesServiceKind;
    if(s.running || s.fault){s.fault=true;fail("String default requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("String default continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto dependencies=[&]{return services && services->fields?services->fields->dependencies:std::shared_ptr<IgbDependenciesServices>{};};
    const auto globals=[&]{const auto dep=dependencies();return dep?dep->globals:std::shared_ptr<SystemStageServices>{};};
    const auto select=[&](K kind,P after)->IgbDependenciesCall&{clear();s.call.request.kind=kind;
        if(const auto dep=dependencies())s.call.factory=dep->factory;s.after=after;s.phase=P::call;return s.call.request;};
    try{for(;;){check();switch(s.phase){
    case P::global:{const auto global=globals();if(!global)return pending("String default requires canonical string-pool80562140.");
        s.pool_exists=bool(global->string_pool);s.phase=P::capture;break;}
    case P::capture:
        if(!s.field)fail("String default captures a null field's default pointer.");
        if(!s.field->default_bytes)return pending("String default requires its captured default pointer+20.");
        s.buffer=*s.field->default_bytes;s.phase=P::pool;break;
    case P::pool:
        if(s.pool_exists)s.phase=P::intern;else select(K::create_string_pool,P::after_allocate).bytes=16;break;
    case P::after_allocate:s.pool=s.call.operation->pool_result;clear();
        if(s.pool)select(K::initialize_string_pool,P::after_initialize).pool=s.pool;else s.phase=P::publish_pool;break;
    case P::after_initialize:s.pool=s.call.operation->pool_result;clear();s.phase=P::publish_pool;break;
    case P::publish_pool:{const auto global=globals();if(!global)return pending("String default requires its canonical pool publication storage.");
        if(global->string_pool && global->string_pool!=s.pool && s.owner)s.owner->unfinished.push_back(global->string_pool);
        global->string_pool=s.pool;s.pool.reset();s.phase=P::intern;break;}
    case P::intern:{const auto global=globals();if(!global)return pending("String default requires the reloaded string-pool global.");
        const auto pool=global->string_pool;auto& call=select(K::intern_name,P::after_intern);call.pool=pool;call.name=s.name;break;}
    case P::after_intern:s.incoming=s.call.operation->string_result;clear();s.phase=P::old;break;
    case P::old:s.old_address=igb_buffer_word(s.buffer.buffer,s.buffer.offset);s.phase=s.old_address?P::resolve_old:P::publish;break;
    case P::resolve_old:
        if(!services)return pending("String default requires its canonical header-address registry.");
        s.old=string_owner(*services,s.old_address);if(!s.old)return pending("String default requires the old nonnull pointer's canonical header.");
        if(s.old->released)fail("String default decrements a released old string header.");
        --s.old->references;s.phase=s.old->references==0?P::release:P::publish;break;
    case P::release:{const auto old=s.old;auto& call=select(K::release_string,P::publish);call.string=old;call.pool=old->container;break;}
    case P::publish:{
        clear();std::optional<std::uint32_t> address;
        if(!s.incoming)address=0;else if(services)address=string_address(*services,s.incoming);
        if(!address)return pending("String default requires the returned string's arithmetic address before publication.");
        write_word(s.buffer,*address);s.phase=P::done;break;
    }
    case P::call:{const auto dep=dependencies();const auto status=s.call.poll(dep?dep->factory:IgbDependenciesFactory{},check);
        if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbFieldOperationsFactory make_native_igb_field_values_factory(std::shared_ptr<IgbFieldValuesServices> services,IgbFieldOperationsFactory remaining){
    std::weak_ptr<IgbFieldValuesServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbFieldOperationCall& call){
        const auto known=profile(call.vtable_word);
        if(call.kind!=IgbFieldOperationKind::field_virtual || !known ||
            (call.virtual_slot!=0x5c && call.virtual_slot!=0x64 && call.virtual_slot!=0x8c) ||
            (call.virtual_slot==0x8c && !call.source && call.argument!=0))return remaining?remaining(call):std::shared_ptr<IgbFieldOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Field value services expired before selection.");auto operation=std::make_shared<IgbFieldOperation>();
        if(call.virtual_slot==0x64)operation->resume=[index=*known]{return MessengerCallResult{true,profiles[index].size,{}};};
        else if(call.virtual_slot==0x5c)operation->resume=[selected,index=*known,out=operation.get()]{
            const auto slot=prototype(*selected,index);if(!slot || !*slot)return pending("Field prototype getter requires its canonical global cell.");
            out->field_result=**slot;return MessengerCallResult{true,0,{}};};
        else{auto invocation=std::make_shared<IgbFieldValueInvocation>(call.field,call.source,call.source_address);
            operation->resume=[invocation,selected]{return invocation->resume(selected);};}
        return operation;
    };
}
IgbMetadataLayoutFactory make_native_igb_field_alignment_factory(IgbMetadataLayoutFactory remaining){
    return [remaining=std::move(remaining)](const IgbMetadataLayoutCall& call){const auto known=profile(call.vtable_word);
        if(call.kind!=IgbMetadataLayoutKind::field_alignment || call.virtual_slot!=0x6c || !known)
            return remaining?remaining(call):std::shared_ptr<IgbMetadataLayoutOperation>{};
        auto operation=std::make_shared<IgbMetadataLayoutOperation>();operation->resume=[index=*known]{return MessengerCallResult{true,profiles[index].size,{}};};return operation;};
}
IgbMetadataDefaultsFactory make_native_igb_metadata_string_default_factory(std::shared_ptr<IgbFieldValuesServices> services,IgbMetadataDefaultsFactory remaining){
    std::weak_ptr<IgbFieldValuesServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbMetadataDefaultsCall& call){
        if(call.function!=0x80071694 || call.argument!=0)return remaining?remaining(call):std::shared_ptr<IgbMetadataDefaultsOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata string-default services expired before selection.");
        auto invocation=std::make_shared<IgbFieldStringDefaultInvocation>(call.field,IgbDependencyName{});
        auto operation=std::make_shared<IgbMetadataDefaultsOperation>();operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;};
}
} // namespace dw4
