#include "dw4/igb_thread_defaults.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbThreadDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
std::optional<IgbBootstrapPointer> physical_metadata(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbThreadDefaultsServices>& services){
    std::optional<IgbBootstrapPointer> result;
    if(services)for(const auto& pointer:services->physical_metadata){
        const auto owner=igb_bootstrap_metadata(pointer);
        if(owner!=metadata)continue;
        if(pointer.storage->faulted())fail("Thread defaults reached faulted metadata storage.");
        if(result && (result->storage!=pointer.storage || result->offset!=pointer.offset))fail("Thread metadata has conflicting physical identities.");
        result=pointer;
    }return result;
}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("Thread defaults store through a null descriptor.");if(!field->physical_storage)return {};
    IgbBootstrapPointer pointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
    if(!pointer.storage || pointer.storage->faulted() || igb_bootstrap_field(pointer)!=field)fail("Thread descriptor lost its canonical physical identity.");return pointer;
}
std::optional<std::uint32_t> metadata_address(const std::shared_ptr<IgbRuntimeMetaType>& metadata,const IgbBootstrapPointer& allocation,
    const std::shared_ptr<IgbThreadDefaultsServices>& services){
    if(!metadata){if(allocation.storage || allocation.offset)fail("Null dependency metadata has a nonnull physical allocation.");return 0;}
    std::optional<std::uint32_t> result;
    const auto accept=[&](std::uint32_t address){if(!address || (result && *result!=address))fail("Thread dependency metadata has conflicting arithmetic identities.");result=address;};
    if(allocation.storage || allocation.offset){if(igb_bootstrap_metadata(allocation)!=metadata)fail("Thread dependency allocation disagrees with its returned owner.");accept(igb_bootstrap_address(allocation));}
    if(const auto pointer=physical_metadata(metadata,services))accept(igb_bootstrap_address(*pointer));
    const auto pointers=services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;
    if(pointers)for(const auto& binding:pointers->values)if(binding.metadata==metadata){
        if(binding.field)fail("Thread dependency metadata address aliases a descriptor binding.");accept(binding.address);
    }return result;
}
std::shared_ptr<IgbDecodeRuntimeField> field_at(const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t index){
    if(!metadata || !metadata->fields)fail("Thread defaults read a null metadata field registry.");
    const auto backing=metadata->fields->backing;const auto slot=(index<<2u)>>2u;
    if(!backing || slot>=backing->size())fail("Thread defaults read an unavailable physical field slot.");return (*backing)[slot];
}
}
IgbThreadDefaultsServices::IgbThreadDefaultsServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x8002879cu,0x8002879cu});
    names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_threadList"),std::make_shared<const std::string>("_sema")});
    globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x805614ec},IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x805614f0}});
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8u,12u});
}
struct IgbThreadDefaultsInvocation::Impl {
    enum class P {volatile_owner,volatile_store,receiver,count,batch,field,lookup,type_store,construct_store,schema_setup,schema,
        redirect_owner,redirect_store,append,done};P phase=P::volatile_owner;
    std::shared_ptr<IgbThreadDefaultsServices> owner;std::shared_ptr<IgbRuntimeMetaType> metadata,store_owner,related;
    std::shared_ptr<IgbDecodeRuntimeField> field;IgbBootstrapPointer allocation;
    std::unique_ptr<IgbFieldBatchInvocation> batch;std::unique_ptr<IgbThreadDependencyLookupInvocation> lookup;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;std::unique_ptr<IgbBootstrapCallbackAppendInvocation> append;
    std::uint32_t first{},index{};bool running{},fault{};
};
IgbThreadDefaultsInvocation::IgbThreadDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbThreadDefaultsInvocation::~IgbThreadDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.store_owner)s.owner->retained.push_back(s.store_owner);
    if(s.related)s.owner->retained.push_back(s.related);if(s.field)s.owner->retained.push_back(s.field);if(s.allocation.storage)s.owner->retained.push_back(s.allocation.storage);
}}
bool IgbThreadDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbThreadDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbThreadDefaultsInvocation::resume(std::shared_ptr<IgbThreadDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Thread defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Thread defaults were invalidated by a callback.");};
    const auto global=[&](std::shared_ptr<IgbRuntimeMetaType>& destination)->bool{
        if(!services || !services->thread || !services->thread->metadata)return false;destination=*services->thread->metadata;return true;
    };
    try{for(;;){check();switch(s.phase){
    case P::volatile_owner:
        if(!global(s.store_owner))return pending("Thread defaults require canonical metadata805614E8 for the volatile store.");
        s.phase=P::volatile_store;break;
    case P::volatile_store:
        if(!s.store_owner)fail("Thread defaults store the volatile byte through null metadata.");
        if(const auto pointer=physical_metadata(s.store_owner,services))igb_bootstrap_write_byte(*pointer,0x18,1);
        s.store_owner->flag_18=std::uint8_t{1};s.store_owner.reset();s.phase=P::receiver;break;
    case P::receiver:
        if(!global(s.metadata))return pending("Thread defaults require reloaded metadata805614E8 for field construction.");
        s.phase=P::count;break;
    case P::count:
        s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services?services->factories:nullptr,2);s.phase=P::batch;break;
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::field;break;}
    case P::field:
        s.field=field_at(s.metadata,s.first+s.index);
        s.lookup=std::make_unique<IgbThreadDependencyLookupInvocation>(s.index==0?IgbThreadDependencyEntry::thread_list:IgbThreadDependencyEntry::semaphore);
        s.phase=P::lookup;break;
    case P::lookup:{const auto status=s.lookup->resume(services?services->dependencies:nullptr);check();if(!status.completed)return status;
        s.related=s.lookup->result();s.allocation=s.lookup->allocation();s.lookup.reset();check();s.phase=P::type_store;break;}
    case P::type_store:
        if(const auto pointer=physical_field(s.field)){const auto address=metadata_address(s.related,s.allocation,services);
            if(!address)return pending("Thread descriptor type store requires its captured dependency metadata arithmetic address.");
            igb_bootstrap_write_word(*pointer,0x38,*address);
        }
        s.field->object_type=std::weak_ptr<IgbRuntimeMetaType>(s.related);s.phase=P::construct_store;break;
    case P::construct_store:
        if(const auto pointer=physical_field(s.field))igb_bootstrap_write_byte(*pointer,0x34,1);
        s.field->flag_34=std::uint8_t{1};s.related.reset();s.allocation={};s.field.reset();++s.index;s.phase=s.index<2?P::field:P::schema_setup;break;
    case P::schema_setup:
        if(!services)return pending("Thread defaults require their original schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::redirect_owner;break;}
    case P::redirect_owner:
        if(!global(s.store_owner))return pending("Thread defaults require fresh metadata805614E8 for the redirect store.");
        s.phase=P::redirect_store;break;
    case P::redirect_store:
        if(!s.store_owner)fail("Thread defaults store the redirect through null reloaded metadata.");
        if(const auto pointer=physical_metadata(s.store_owner,services))igb_bootstrap_write_word(*pointer,0x3c,0x80023a10);
        s.store_owner->factory_redirect=0x80023a10;
        s.append=std::make_unique<IgbBootstrapCallbackAppendInvocation>(0x80023a50);s.phase=P::append;break;
    case P::append:{const auto status=s.append->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.append.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_thread_defaults_factory(std::shared_ptr<IgbThreadDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults || call.function!=0x80023940)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Thread defaults services expired before selection.");
        auto invocation=std::make_shared<IgbThreadDefaultsInvocation>();auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
