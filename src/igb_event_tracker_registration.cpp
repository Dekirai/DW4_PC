#include "dw4/igb_event_tracker_registration.hpp"
#include "dw4/igb_object_field_construct.hpp"

#include <array>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw IgbEventTrackerRegistrationError(message); }
MessengerCallResult pending(const char* message) { return {false,0,message}; }
struct Running {
    bool& flag;
    explicit Running(bool& value):flag(value) { flag=true; }
    ~Running() { flag=false; }
};
using Services=IgbEventTrackerRegistrationServices;
using Lookup=IgbEventTrackerLookup;
std::shared_ptr<IgbTypeRegistrationServices> types(const std::shared_ptr<Services>& services) {
    return services && services->memory_pool?services->memory_pool->types:nullptr;
}
std::shared_ptr<IgbRegistrationCallbacksServices> callbacks(const std::shared_ptr<Services>& services) {
    return services && services->memory_pool?services->memory_pool->callbacks:nullptr;
}
std::shared_ptr<IgbObjectFieldConstructServices> cleanup(const std::shared_ptr<Services>& services) {
    return services && services->memory_pool?services->memory_pool->object_cleanup:nullptr;
}
std::optional<Lookup> lookup_function(std::uint32_t function) {
    if(function==0x80030c8c || function==0x80030df0)return Lookup::fully_registered;
    if(function==0x80093d1c)return Lookup::plain;
    return {};
}
bool same_object(const IgbObjectInstance& left,const IgbObjectInstance& right) {
    return left.storage==right.storage && left.offset==right.offset && left.file==right.file &&
        left.field==right.field && left.metadata==right.metadata && left.fields==right.fields &&
        left.types==right.types && left.runtime_object==right.runtime_object;
}
std::optional<IgbObjectInstance> resolve_object(const std::shared_ptr<Services>& services,std::uint32_t address) {
    std::optional<IgbObjectInstance> result;
    const auto objects=cleanup(services);
    if(objects)for(const auto& object:objects->objects) {
        if(igb_bootstrap_address({object.storage,object.offset})!=address)continue;
        if(!object.storage || object.storage->faulted())fail("EventTracker cleanup requires an unfaulted object allocation.");
        igb_object_validate_instance(object);
        if(result && !same_object(*result,object))fail("EventTracker cleanup address has conflicting typed identities.");
        result=object;
    }
    return result;
}
struct ObjectAccess {
    IgbObjectInstance object;
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t address{};
    explicit ObjectAccess(IgbObjectInstance value):object(std::move(value)) {
        if(object.storage) { bytes=object.storage->bytes;address=object.storage->original_address; }
    }
    void validate()const {
        if(!object.storage || object.storage->faulted() || object.storage->bytes!=bytes ||
            object.storage->original_address!=address || !bytes || bytes->released)
            fail("EventTracker cleanup lost its captured object allocation identity.");
        igb_object_validate_instance(object);
    }
};
constexpr std::array<std::uint32_t,12> initialized_members{
    0x08,0x30,0x34,0x38,0x3c,0x40,0x44,0x48,0x4c,0x50,0x54,0x5c};
constexpr std::array<std::uint32_t,12> released_members{
    0x5c,0x54,0x50,0x4c,0x48,0x44,0x40,0x3c,0x38,0x34,0x30,0x08};
} // namespace

std::shared_ptr<IgbRegistrationMetaSlot> igb_event_tracker_metadata_slot(const std::shared_ptr<Services>& services) {
    return services && services->memory_pool?services->memory_pool->event_tracker_metadata:nullptr;
}
IgbTypeRegistrationArguments igb_event_tracker_registration_arguments(const std::shared_ptr<Services>& services) {
    IgbTypeRegistrationArguments args;
    args.metadata_slot=igb_event_tracker_metadata_slot(services);
    if(!args.metadata_slot)fail("EventTracker registration requires the shared canonical80561BA4 cell.");
    args.name=std::make_shared<const std::string>("igEventTracker");
    args.flag=0;args.native_size=0x2530;
    args.parent_registration=0x80066b08;args.parent_getter=0x800237d0;
    args.field_callback=0x80030df0;args.layout_probe=0x80030cc8;args.defaults_callback=0x80030e10;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(
        std::initializer_list<std::uint32_t>{0x800249f4,0x80038478,0x8002d584,0x800368d4,
            0x800364c4,0x80038244,0x80026098,0});
    return args;
}
struct IgbEventTrackerRegistrationInvocation::Impl {
    enum class Phase { prerequisites,arguments,registration,done };
    Phase phase=Phase::prerequisites;
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    bool running{},fault{};
};
IgbEventTrackerRegistrationInvocation::IgbEventTrackerRegistrationInvocation():impl_(std::make_unique<Impl>()) {}
IgbEventTrackerRegistrationInvocation::~IgbEventTrackerRegistrationInvocation()=default;
bool IgbEventTrackerRegistrationInvocation::completed()const noexcept { return impl_->phase==Impl::Phase::done; }
bool IgbEventTrackerRegistrationInvocation::faulted()const noexcept { return impl_->fault; }
std::shared_ptr<IgbRegistrationCallbackList> IgbEventTrackerRegistrationInvocation::result()const {
    if(!completed() || faulted())fail("Incomplete EventTracker registration has no callback result.");
    return impl_->result;
}
MessengerCallResult IgbEventTrackerRegistrationInvocation::resume(std::shared_ptr<Services> services) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault) { s.fault=true;fail("EventTracker registration requires an unfaulted nonrecursive invocation."); }
    if(completed())return {true,0,{}};
    Running running(s.running);
    const auto check=[&] { if(s.fault)fail("EventTracker registration was invalidated by a callback."); };
    try { for(;;) { check();switch(s.phase) {
    case P::prerequisites: {
        const auto status=s.prerequisites.resume(callbacks(services));check();if(!status.completed)return status;
        s.phase=P::arguments;break;
    }
    case P::arguments:
        if(!igb_event_tracker_metadata_slot(services))return pending("EventTracker registration requires the shared80561BA4 cell after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_event_tracker_registration_arguments(services));
        s.phase=P::registration;break;
    case P::registration: {
        const auto status=s.registration->resume(types(services));check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;
    }
    case P::done:return {true,0,{}};
    } } }catch(...) { s.fault=true;throw; }
}

struct IgbEventTrackerLookupInvocation::Impl {
    enum class Phase { read,registration,reload,done };
    Phase phase=Phase::read;
    Lookup mode=Lookup::fully_registered;
    std::shared_ptr<IgbRegistrationMetaSlot> slot;
    std::shared_ptr<IgbRuntimeMetaType> result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    bool running{},fault{};
};
IgbEventTrackerLookupInvocation::IgbEventTrackerLookupInvocation(Lookup mode):impl_(std::make_unique<Impl>()) {
    if(mode!=Lookup::fully_registered && mode!=Lookup::plain)fail("Unknown EventTracker metadata lookup mode.");
    impl_->mode=mode;
}
IgbEventTrackerLookupInvocation::~IgbEventTrackerLookupInvocation()=default;
bool IgbEventTrackerLookupInvocation::completed()const noexcept { return impl_->phase==Impl::Phase::done; }
bool IgbEventTrackerLookupInvocation::faulted()const noexcept { return impl_->fault; }
std::shared_ptr<IgbRuntimeMetaType> IgbEventTrackerLookupInvocation::result()const {
    if(!completed() || faulted())fail("Incomplete EventTracker getter has no metadata result.");
    return impl_->result;
}
MessengerCallResult IgbEventTrackerLookupInvocation::resume(std::shared_ptr<Services> services) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault) { s.fault=true;fail("EventTracker getter requires an unfaulted nonrecursive invocation."); }
    if(completed())return {true,0,{}};
    Running running(s.running);
    const auto check=[&] { if(s.fault)fail("EventTracker getter was invalidated by a callback."); };
    try { for(;;) { check();switch(s.phase) {
    case P::read:
        if(!s.slot) {
            s.slot=igb_event_tracker_metadata_slot(services);
            if(!s.slot)return pending("EventTracker getter requires shared canonical80561BA4 storage.");
        }
        if(!*s.slot)return pending("EventTracker getter requires a represented canonical metadata value.");
        if(s.mode==Lookup::plain || (**s.slot && ((**s.slot)->class_flags&4u)!=0))s.phase=P::reload;
        else {
            //30D50 is exactly66188(30D78); do not recursively require a host wrapper.
            s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x80030d78);
            s.phase=P::registration;
        }
        break;
    case P::registration: {
        const auto status=s.registration->resume(callbacks(services));check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;
    }
    case P::reload:
        if(!*s.slot)return pending("EventTracker getter requires its freshly reloaded canonical metadata value.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    } } }catch(...) { s.fault=true;throw; }
}

struct IgbEventTrackerProbeInvocation::Impl {
    enum class Phase { reset,core,derived_vtable,null_members,offset,word,
        cleanup_address,cleanup_vtable,member,resolve,decrement,test,destroy,next,done };
    Phase phase=Phase::reset;
    IgbBootstrapPointer local;
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t allocation_address{},local_address{},offset{},value{},member_address{},object_address{};
    std::size_t initialized_index{},member_index{};
    std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<Services> owner;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::shared_ptr<ObjectAccess> object;
    IgbObjectFieldConstructCall request;
    IgbObjectFieldConstructFactory factory;
    std::shared_ptr<IgbObjectFieldConstructOperation> operation;
    std::function<MessengerCallResult()> callback;
    bool running{},fault{};
};
IgbEventTrackerProbeInvocation::IgbEventTrackerProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()) {
    auto& s=*impl_;s.local=std::move(local);s.local_address=igb_bootstrap_address(s.local);
    if(s.local.storage) { s.bytes=s.local.storage->bytes;s.allocation_address=s.local.storage->original_address; }
}
IgbEventTrackerProbeInvocation::~IgbEventTrackerProbeInvocation() {
    auto& s=*impl_;
    if((!completed() || faulted()) && s.owner) {
        if(s.local.storage)s.owner->retained.push_back(s.local.storage);
        if(s.bytes)s.owner->retained.push_back(s.bytes);
        if(s.object)s.owner->retained.push_back(s.object);
        // Retain result owners, not a callback closure that may capture services.
        if(s.operation && s.operation->pool_result)s.owner->retained.push_back(s.operation->pool_result);
    }
}
bool IgbEventTrackerProbeInvocation::completed()const noexcept { return impl_->phase==Impl::Phase::done; }
bool IgbEventTrackerProbeInvocation::faulted()const noexcept { return impl_->fault; }
std::uint32_t IgbEventTrackerProbeInvocation::value()const {
    if(!completed() || faulted())fail("Incomplete EventTracker probe has no captured layout word.");
    return impl_->value;
}
MessengerCallResult IgbEventTrackerProbeInvocation::resume(std::shared_ptr<Services> services,
    const std::function<void()>& enclosing_check) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault) { s.fault=true;fail("EventTracker probe requires an unfaulted nonrecursive invocation."); }
    if(completed())return {true,s.value,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&] {
        if(s.fault)fail("EventTracker probe was invalidated by a callback.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("EventTracker probe was invalidated by its enclosing callback.");
    };
    const auto local_check=[&] {
        if(!s.local.storage || s.local.storage->faulted() || s.local.storage->bytes!=s.bytes ||
            s.local.storage->original_address!=s.allocation_address || !s.bytes || s.bytes->released)
            fail("EventTracker probe lost its captured local allocation identity.");
    };
    const auto write=[&](std::uint32_t offset,std::uint32_t value) {
        local_check();igb_bootstrap_write_word(s.local,offset,value);
    };
    const auto fallback=[&] {
        const auto selected=cleanup(services);
        return selected?selected->factory:IgbObjectFieldConstructFactory{};
    };
    try { for(;;) { check();switch(s.phase) {
    case P::reset:
        local_check();
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        { const auto status=s.reset->resume();check();if(!status.completed)return status; }
        s.reset.reset();check();s.phase=P::core;break;
    case P::core: {
        //30CEC captures ArkCore before30CF0 stores the derived vtable.
        const auto selected=types(services);
        if(!selected || !selected->runtime || !selected->runtime->singleton)
            return pending("EventTracker probe requires represented ArkCore80562100 before derived construction.");
        s.core=*selected->runtime->singleton;s.phase=P::derived_vtable;break;
    }
    case P::derived_vtable:write(0,0x80472a58);s.phase=P::null_members;break;
    case P::null_members:
        while(s.initialized_index<initialized_members.size()) {
            write(initialized_members[s.initialized_index],0);++s.initialized_index;
        }
        s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("EventTracker probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("EventTracker probe requires its captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>0x252cu || (s.offset&3u)!=0)fail("EventTracker probe exceeds its aligned0x2530-byte local-object profile.");
        local_check();s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::cleanup_address;break;
    case P::cleanup_address:
        s.member_address=s.local_address+released_members[0];s.phase=P::cleanup_vtable;break;
    case P::cleanup_vtable:write(0,0x80472a58);s.phase=P::member;break;
    case P::member:
        if(!s.member_address) { s.phase=P::next;break; }
        local_check();s.object_address=igb_bootstrap_read_word(s.local,released_members[s.member_index]);
        s.phase=s.object_address?P::resolve:P::next;break;
    case P::resolve: {
        const auto selected=resolve_object(services,s.object_address);
        if(!selected)return pending("EventTracker probe requires a physical binding for its captured cleanup member.");
        s.object=std::make_shared<ObjectAccess>(*selected);s.phase=P::decrement;break;
    }
    case P::decrement:
        s.object->validate();igb_object_set_reference_word(s.object->object,igb_object_reference_word(s.object->object)-1u);
        s.phase=P::test;break;
    case P::test:
        s.object->validate();
        if((igb_object_reference_word(s.object->object)&0x7fffffu)!=0) { s.phase=P::next;break; }
        s.request={};s.request.kind=IgbObjectFieldConstructServiceKind::destroy_object;s.request.object=s.object->object;
        s.factory=fallback();s.phase=P::destroy;break;
    case P::destroy:
        if(!s.factory)s.factory=fallback();
        if(!s.factory)return pending("EventTracker probe requires reached object destruction80066E1C.");
        if(!s.operation) { s.operation=s.factory(s.request);check(); }
        if(!s.operation)return pending("EventTracker probe reached unbound object destruction80066E1C.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("EventTracker probe requires its retained object-destruction continuation.");
        { const auto status=s.callback();check();if(!status.completed)return status; }
        s.callback={};s.operation.reset();s.factory={};s.request={};check();s.phase=P::next;break;
    case P::next:
        s.object.reset();check();++s.member_index;
        if(s.member_index==released_members.size()) { s.phase=P::done;break; }
        s.member_address=s.local_address+released_members[s.member_index];s.phase=P::member;break;
    case P::done:return {true,s.value,{}};
    } } }catch(...) { s.fault=true;throw; }
}

IgbRegistrationCallbacksFactory make_native_igb_event_tracker_registration_function_factory(
    std::shared_ptr<Services> services,IgbRegistrationCallbacksFactory remaining) {
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)
        ->std::shared_ptr<IgbRegistrationCallbacksOperation> {
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function ||
            (call.function!=0x80030d50 && call.function!=0x80030d78))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("EventTracker registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x80030d78) {
            auto invocation=std::make_shared<IgbEventTrackerRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()] {
                const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;
            };
        }else {
            auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x80030d78);
            operation->resume=[selected,invocation] { return invocation->resume(callbacks(selected)); };
        }
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_event_tracker_registration_type_factory(
    std::shared_ptr<Services> services,IgbTypeRegistrationFactory remaining) {
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)
        ->std::shared_ptr<IgbTypeRegistrationOperation> {
        const bool registration=call.kind==IgbTypeRegistrationServiceKind::register_parent && call.function==0x80030d78;
        const auto lookup=call.kind==IgbTypeRegistrationServiceKind::get_parent?lookup_function(call.function):std::optional<Lookup>{};
        const bool probe=call.kind==IgbTypeRegistrationServiceKind::layout_probe && call.function==0x80030cc8;
        if(!registration && !lookup && !probe)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("EventTracker registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration) {
            auto invocation=std::make_shared<IgbEventTrackerRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()] {
                const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;
            };
        }else if(lookup) {
            auto invocation=std::make_shared<IgbEventTrackerLookupInvocation>(*lookup);
            operation->resume=[selected,invocation,out=operation.get()] {
                const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;
            };
        }else {
            struct State {std::shared_ptr<IgbEventTrackerProbeInvocation> invocation;bool running{},fault{};};
            auto state=std::make_shared<State>();
            operation->resume=[selected,state] {
                if(state->running || state->fault) { state->fault=true;fail("EventTracker probe adapter requires an unfaulted nonrecursive operation."); }
                Running running(state->running);
                const auto check=[&] { if(state->fault)fail("EventTracker probe adapter was invalidated by a callback."); };
                try {
                    if(!state->invocation) {
                        if(!selected->local_storage)return pending("EventTracker probe requires its original0x2530-byte local storage.");
                        auto local=selected->local_storage();check();
                        if(!igb_bootstrap_address(local))fail("EventTracker probe local has a null arithmetic address.");
                        state->invocation=std::make_shared<IgbEventTrackerProbeInvocation>(std::move(local));
                    }
                    const auto status=state->invocation->resume(selected,check);check();return status;
                }catch(...) { state->fault=true;throw; }
            };
        }
        return operation;
    };
}
} // namespace dw4
