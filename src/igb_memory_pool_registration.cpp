#include "dw4/igb_memory_pool_registration.hpp"

#include <array>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw IgbMemoryPoolRegistrationError(message); }
MessengerCallResult pending(const char* message) { return {false,0,message}; }
struct Running {
    bool& flag;
    explicit Running(bool& value):flag(value) { flag=true; }
    ~Running() { flag=false; }
};
using Lookup=IgbMemoryPoolLookup;
using Kind=IgbMemoryPoolKind;
void validate(Kind kind) {
    if(kind!=Kind::abstract_pool && kind!=Kind::malloc_pool)fail("Unknown memory-pool registration kind.");
}
std::optional<Kind> registration_function(std::uint32_t address,bool wrapper) {
    if(address==(wrapper?0x8002bf48u:0x8002bf70u))return Kind::abstract_pool;
    if(address==(wrapper?0x80039d6cu:0x80039d94u))return Kind::malloc_pool;
    return {};
}
std::shared_ptr<IgbRegistrationMetaSlot> metadata_slot(const std::shared_ptr<IgbMemoryPoolRegistrationServices>& services,Kind kind) {
    validate(kind);if(!services)return {};
    return kind==Kind::abstract_pool?services->metadata_slot:services->malloc_metadata_slot;
}
void validate(Lookup mode) {
    if(mode!=Lookup::allocate_if_null && mode!=Lookup::fully_registered && mode!=Lookup::plain)
        fail("Unknown memory-pool metadata lookup mode.");
}
std::optional<Lookup> lookup_function(std::uint32_t address) {
    if(address==0x8002bed0)return Lookup::allocate_if_null;
    if(address==0x8002bf0c || address==0x8002bfd8)return Lookup::fully_registered;
    if(address==0x80038f58)return Lookup::plain;
    return {};
}
struct FieldAccess {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> physical;
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t address{};
    explicit FieldAccess(std::shared_ptr<IgbDecodeRuntimeField> value):field(std::move(value)) {
        if(field && field->physical_storage) {
            physical=IgbBootstrapPointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
            if(physical->storage) { bytes=physical->storage->bytes; address=physical->storage->original_address; }
        }
    }
    void validate()const {
        if(!field || field->construction_fault_)fail("Memory-pool defaults require an unfaulted descriptor.");
        if(!physical) {
            if(field->physical_storage)fail("Memory-pool descriptor acquired a different physical identity.");
            return;
        }
        if(!physical->storage || physical->storage->faulted() || !bytes || bytes->released ||
            physical->storage->bytes!=bytes || physical->storage->original_address!=address ||
            !field->physical_storage || field->physical_storage->storage.lock()!=physical->storage ||
            field->physical_storage->offset!=physical->offset || igb_bootstrap_field(*physical)!=field)
            fail("Memory-pool defaults lost their captured descriptor identity.");
    }
    std::optional<std::uint32_t> table()const {
        validate();
        return physical?std::optional<std::uint32_t>{igb_bootstrap_read_word(*physical,0)}:field->vtable_word;
    }
};
std::optional<IgbBootstrapPointer> metadata_storage(
    const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbMemoryPoolRegistrationServices>& services) {
    std::optional<IgbBootstrapPointer> result;
    if(services)for(const auto& pointer:services->physical_metadata) {
        if(!pointer.storage)continue;
        bool matches=false;
        for(const auto& binding:pointer.storage->bindings)
            if(binding.offset==pointer.offset && binding.metadata==metadata)matches=true;
        if(!matches)continue;
        if(pointer.storage->faulted() || igb_bootstrap_metadata(pointer)!=metadata)
            fail("Memory-pool redirect requires unfaulted typed metadata storage.");
        if(result && (result->storage!=pointer.storage || result->offset!=pointer.offset))
            fail("Memory-pool metadata has conflicting physical allocations.");
        result=pointer;
    }
    return result;
}
IgbRuntimeFieldDefaultSource boolean_one() {
    auto bytes=std::make_shared<IgbScratchBuffer>();
    bytes->bytes={1};bytes->initialized={1};return {bytes,0};
}
} // namespace

IgbMemoryPoolRegistrationServices::IgbMemoryPoolRegistrationServices() {
    factories=std::make_shared<IgbFieldFactoryTable>(12,0x8002cea8u);
    factories->push_back(0x800385e0);factories->push_back(0x800385e0);
    names=std::make_shared<IgbFieldNameTable>();globals=std::make_shared<IgbFieldGlobalTable>();
    constexpr std::array labels={"_localAllocated","_localFreed","_localNetAllocated","_localNetAllocatedHigh",
        "_localAllocatedWithOverhead","_localFreedWithOverhead","_localNetAllocatedWithOverhead",
        "_localNetAllocatedWithOverheadHigh","_localAllocationCount","_localFreeCount",
        "_localNetAllocationCount","_localNetAllocationCountHigh","_updateStatistics","_updateGlobalStatistics"};
    for(std::size_t i=0;i<labels.size();++i) {
        names->push_back(std::make_shared<const std::string>(labels[i]));
        globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x805618b8u+static_cast<std::uint32_t>(4*i)});
    }
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{
        0x08,0x10,0x18,0x20,0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x69});
}
IgbTypeRegistrationArguments igb_memory_pool_registration_arguments(
    const std::shared_ptr<IgbMemoryPoolRegistrationServices>& services,Kind kind) {
    const auto slot=metadata_slot(services,kind);
    if(!slot)fail("Memory-pool registration requires its distinct canonical metadata storage.");
    IgbTypeRegistrationArguments args;
    args.metadata_slot=slot;
    if(kind==Kind::malloc_pool) {
        args.name=std::make_shared<const std::string>("igMallocMemoryPool");args.flag=0;args.native_size=0xa0;
        args.parent_registration=0x8002bf70;args.parent_getter=0x80038f58;args.field_callback=0x80039e08;
        args.layout_probe=0x80039cc0;args.defaults_callback=0x80039e28;
        args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(
            std::initializer_list<std::uint32_t>{0x80030d50,0x80026098,0});
        return args;
    }
    args.name=std::make_shared<const std::string>("igMemoryPool");
    args.flag=1;args.native_size=0x70;args.parent_registration=0x80066b08;args.parent_getter=0x800237d0;
    args.field_callback=0x8002bfd8;args.defaults_callback=0x8002bff8;
    return args;
}
struct IgbMemoryPoolRegistrationInvocation::Impl {
    Kind kind=Kind::abstract_pool;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    bool running{},fault{},done{};
};
IgbMemoryPoolRegistrationInvocation::IgbMemoryPoolRegistrationInvocation(Kind kind):impl_(std::make_unique<Impl>()) { validate(kind);impl_->kind=kind; }
IgbMemoryPoolRegistrationInvocation::~IgbMemoryPoolRegistrationInvocation()=default;
bool IgbMemoryPoolRegistrationInvocation::completed()const noexcept { return impl_->done; }
bool IgbMemoryPoolRegistrationInvocation::faulted()const noexcept { return impl_->fault; }
std::shared_ptr<IgbRegistrationCallbackList> IgbMemoryPoolRegistrationInvocation::result()const {
    if(!completed() || faulted())fail("Incomplete memory-pool registration has no callback result.");
    return impl_->result;
}
MessengerCallResult IgbMemoryPoolRegistrationInvocation::resume(std::shared_ptr<IgbMemoryPoolRegistrationServices> services) {
    auto& s=*impl_;
    if(s.running || s.fault) { s.fault=true;fail("Memory-pool registration requires an unfaulted nonrecursive invocation."); }
    if(s.done)return {true,0,{}};
    Running running(s.running);
    try {
        if(!s.registration) {
            if(!metadata_slot(services,s.kind))return pending("Memory-pool registration requires its distinct canonical metadata storage.");
            s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_memory_pool_registration_arguments(services,s.kind));
        }
        const auto status=s.registration->resume(services?services->types:nullptr);
        if(s.fault)fail("Memory-pool registration was invalidated by a callback.");
        if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();
        if(s.fault)fail("Memory-pool registration was invalidated by callback teardown.");
        s.done=true;return {true,0,{}};
    }catch(...) { s.fault=true;throw; }
}

struct IgbMemoryPoolMetadataLookupInvocation::Impl {
    enum class Phase { read,handle,pool,construct,publish,registration,reload,done };
    Phase phase=Phase::read;Lookup mode;
    std::shared_ptr<IgbMemoryPoolRegistrationServices> owner;
    std::shared_ptr<IgbRegistrationMetaSlot> slot;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRuntimeMetaType> result;
    std::shared_ptr<ModelSceneObject> pool;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbPoolByHandleInvocation> lookup;
    std::unique_ptr<IgbMetadataFactoryInvocation> factory;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    bool running{},fault{};
};
IgbMemoryPoolMetadataLookupInvocation::IgbMemoryPoolMetadataLookupInvocation(Lookup mode):impl_(std::make_unique<Impl>()) {
    validate(mode);impl_->mode=mode;
}
IgbMemoryPoolMetadataLookupInvocation::~IgbMemoryPoolMetadataLookupInvocation() {
    auto& s=*impl_;
    if((!completed() || faulted()) && s.owner) {
        if(s.result)s.owner->retained.push_back(s.result);
        if(s.pool)s.owner->retained.push_back(s.pool);
        if(s.allocation.storage)s.owner->retained.push_back(s.allocation.storage);
    }
}
bool IgbMemoryPoolMetadataLookupInvocation::completed()const noexcept { return impl_->phase==Impl::Phase::done; }
bool IgbMemoryPoolMetadataLookupInvocation::faulted()const noexcept { return impl_->fault; }
std::shared_ptr<IgbRuntimeMetaType> IgbMemoryPoolMetadataLookupInvocation::result()const {
    if(!completed() || faulted())fail("Incomplete memory-pool lookup has no result.");
    return impl_->result;
}
MessengerCallResult IgbMemoryPoolMetadataLookupInvocation::resume(std::shared_ptr<IgbMemoryPoolRegistrationServices> services) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault) { s.fault=true;fail("Memory-pool lookup requires an unfaulted nonrecursive invocation."); }
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&] { if(s.fault)fail("Memory-pool lookup was invalidated by a callback."); };
    try { for(;;) { check();switch(s.phase) {
    case P::read:
        if(!s.slot) {
            if(!services || !services->metadata_slot)return pending("Memory-pool lookup requires canonical805618B4 storage.");
            s.slot=services->metadata_slot;
        }
        if(!*s.slot)return pending("Memory-pool lookup requires represented canonical metadata.");
        if(s.mode==Lookup::plain)s.phase=P::reload;
        else if(s.mode==Lookup::allocate_if_null)s.phase=**s.slot?P::reload:P::handle;
        else if(**s.slot && ((**s.slot)->class_flags&4u)!=0)s.phase=P::reload;
        else { s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x8002bf70);s.phase=P::registration; }
        break;
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)
            return pending("Lazy memory-pool metadata requires live pool handle805621F4.");
        s.lookup=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool: {
        const auto status=s.lookup->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.lookup->result();s.lookup.reset();check();s.phase=P::construct;break;
    }
    case P::construct:
        if(!s.factory) {
            if(!services || !services->metadata)return pending("Lazy memory-pool metadata requires actual29E64 services.");
            s.metadata=services->metadata;s.factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);
        }
        { const auto status=s.factory->resume(s.metadata);check();if(!status.completed)return status; }
        s.result=s.factory->result();s.allocation=s.factory->allocation();
        if(s.owner && s.allocation.storage)s.owner->physical_metadata.push_back(s.allocation);
        s.factory.reset();check();s.phase=P::publish;break;
    case P::publish:
        if(*s.slot && **s.slot && **s.slot!=s.result && s.owner)s.owner->retained.push_back(**s.slot);
        *s.slot=s.result;s.phase=P::reload;break;
    case P::registration: {
        const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;
    }
    case P::reload:
        if(!*s.slot)return pending("Memory-pool lookup requires its freshly reloaded canonical metadata.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    } } }catch(...) { s.fault=true;throw; }
}

struct IgbMemoryPoolDefaultsInvocation::Impl {
    enum class Phase { receiver,count,batch,field,table,value,schema_setup,schema,redirect_owner,redirect_store,done };
    Phase phase=Phase::receiver;
    std::shared_ptr<IgbMemoryPoolRegistrationServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata,redirect_owner;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::shared_ptr<FieldAccess> access;
    IgbFieldOperationCall request;
    IgbFieldOperationsFactory factory;
    std::shared_ptr<IgbFieldOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked;
    std::uint32_t first{},index=12;
    bool selected{},running{},fault{};
};
IgbMemoryPoolDefaultsInvocation::IgbMemoryPoolDefaultsInvocation():impl_(std::make_unique<Impl>()) {}
IgbMemoryPoolDefaultsInvocation::~IgbMemoryPoolDefaultsInvocation() {
    auto& s=*impl_;
    if((!completed() || faulted()) && s.owner) {
        if(s.metadata)s.owner->retained.push_back(s.metadata);
        if(s.redirect_owner)s.owner->retained.push_back(s.redirect_owner);
        if(s.access)s.owner->retained.push_back(s.access);
        if(s.request.source && s.request.source->buffer)s.owner->retained.push_back(s.request.source->buffer);
        if(s.operation) {
            if(s.operation->field_result)s.owner->retained.push_back(s.operation->field_result);
            if(s.operation->pool_result)s.owner->retained.push_back(s.operation->pool_result);
            if(s.operation->buffer_result.buffer)s.owner->retained.push_back(s.operation->buffer_result.buffer);
        }
    }
}
bool IgbMemoryPoolDefaultsInvocation::completed()const noexcept { return impl_->phase==Impl::Phase::done; }
bool IgbMemoryPoolDefaultsInvocation::faulted()const noexcept { return impl_->fault; }
MessengerCallResult IgbMemoryPoolDefaultsInvocation::resume(std::shared_ptr<IgbMemoryPoolRegistrationServices> services,
    const std::function<void()>& enclosing_check) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault) { s.fault=true;fail("Memory-pool defaults require an unfaulted nonrecursive invocation."); }
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&] {
        if(s.fault)fail("Memory-pool defaults were invalidated by a callback.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Memory-pool defaults were invalidated by their enclosing callback.");
    };
    try { for(;;) { check();switch(s.phase) {
    case P::receiver:
        if(!services || !services->metadata_slot || !*services->metadata_slot)
            return pending("Memory-pool defaults require canonical805618B4 metadata.");
        s.metadata=**services->metadata_slot;s.phase=P::count;break;
    case P::count:
        s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services?services->factories:nullptr,14);s.phase=P::batch;break;
    case P::batch: {
        const auto status=s.batch->resume(services?services->schema:nullptr,check);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::field;break;
    }
    case P::field:
        s.access=std::make_shared<FieldAccess>(igb_runtime_field_at(s.metadata,static_cast<std::int32_t>(((s.first+s.index)<<2u)>>2u)));
        s.request={};s.request.kind=IgbFieldOperationKind::field_virtual;s.request.field=s.access->field;
        s.request.source=boolean_one();s.request.virtual_slot=0x8c;s.phase=P::table;break;
    case P::table: {
        const auto table=s.access->table();
        if(!table)return pending("Boolean default requires its live descriptor vtable.");
        if(!*table)fail("Boolean default dereferences a null descriptor vtable.");
        s.request.vtable_word=*table;
        if(services && services->schema && services->schema->fields)s.factory=services->schema->fields->factory;
        s.phase=P::value;break;
    }
    case P::value:
        if(!s.factory && services && services->schema && services->schema->fields)s.factory=services->schema->fields->factory;
        if(!s.factory)return pending("Boolean default requires its reached virtual+8C factory.");
        if(!s.operation) { s.operation=s.factory(s.request);check(); }
        if(!s.operation)return pending("Boolean default reached an unbound virtual+8C.");
        if(!s.selected && (s.operation->resume_checked || s.operation->resume)) {
            s.checked=s.operation->resume_checked;if(!s.checked)s.callback=s.operation->resume;s.selected=true;
        }
        if(!s.selected)return pending("Boolean default requires its retained virtual continuation.");
        { check();const auto status=s.checked?s.checked(check):s.callback();check();if(!status.completed)return status; }
        s.callback={};s.checked={};s.operation.reset();s.factory={};s.request={};s.access.reset();s.selected=false;check();
        ++s.index;s.phase=s.index==13?P::field:P::schema_setup;break;
    case P::schema_setup:
        if(!services)return pending("Memory-pool defaults require their original schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);
        s.phase=P::schema;break;
    case P::schema: {
        const auto status=s.schema->resume(services?services->schema:nullptr,check);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::redirect_owner;break;
    }
    case P::redirect_owner:
        if(!services || !services->metadata_slot || !*services->metadata_slot)
            return pending("Memory-pool defaults require freshly reloaded canonical metadata for the redirect.");
        s.redirect_owner=**services->metadata_slot;s.phase=P::redirect_store;break;
    case P::redirect_store:
        if(!s.redirect_owner)fail("Memory-pool defaults store a redirect through null reloaded metadata.");
        if(const auto pointer=metadata_storage(s.redirect_owner,services))igb_bootstrap_write_word(*pointer,0x3c,0x8002c0a0);
        s.redirect_owner->factory_redirect=0x8002c0a0;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    } } }catch(...) { s.fault=true;throw; }
}

IgbRegistrationCallbacksFactory make_native_igb_memory_pool_registration_function_factory(
    std::shared_ptr<IgbMemoryPoolRegistrationServices> services,IgbRegistrationCallbacksFactory remaining) {
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)
        ->std::shared_ptr<IgbRegistrationCallbacksOperation> {
        const auto direct=registration_function(call.function,false),wrapper=registration_function(call.function,true);
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (!direct && !wrapper))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Memory-pool registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(direct) {
            auto invocation=std::make_shared<IgbMemoryPoolRegistrationInvocation>(*direct);
            operation->resume=[selected,invocation,out=operation.get()] {
                const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;
            };
        }else {
            auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(*wrapper==Kind::abstract_pool?0x8002bf70u:0x80039d94u);
            operation->resume=[selected,invocation] { return invocation->resume(selected->callbacks); };
        }
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_memory_pool_registration_type_factory(
    std::shared_ptr<IgbMemoryPoolRegistrationServices> services,IgbTypeRegistrationFactory remaining) {
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)
        ->std::shared_ptr<IgbTypeRegistrationOperation> {
        const auto registration=call.kind==IgbTypeRegistrationServiceKind::register_parent?registration_function(call.function,false):std::optional<Kind>{};
        const bool defaults=call.kind==IgbTypeRegistrationServiceKind::defaults && call.function==0x8002bff8;
        const auto lookup=call.kind==IgbTypeRegistrationServiceKind::get_parent?lookup_function(call.function):std::optional<Lookup>{};
        if(!registration && !defaults && !lookup)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Memory-pool registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration) {
            auto invocation=std::make_shared<IgbMemoryPoolRegistrationInvocation>(*registration);
            operation->resume=[selected,invocation,out=operation.get()] {
                const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;
            };
        }else if(lookup) {
            auto invocation=std::make_shared<IgbMemoryPoolMetadataLookupInvocation>(*lookup);
            operation->resume=[selected,invocation,out=operation.get()] {
                const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;
            };
        }else {
            auto invocation=std::make_shared<IgbMemoryPoolDefaultsInvocation>();
            operation->resume=[selected,invocation] { return invocation->resume(selected); };
        }
        return operation;
    };
}
} // namespace dw4
