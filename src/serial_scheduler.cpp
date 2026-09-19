#include "dw4/serial_scheduler.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace dw4 {
namespace {
constexpr std::uint32_t maximum_slots=262144;
[[noreturn]] void fail(const char* reason) {throw SerialSchedulerError(reason);}
struct UnknownSlot final : ModelSceneObject {
    UnknownSlot():ModelSceneObject("uninitialized scheduler pointer") {}
};
struct ReleasedSlot final : ModelSceneObject {
    explicit ReleasedSlot(const std::shared_ptr<ModelSceneObject>& owner)
        :ModelSceneObject("released scheduler pointer"),target(owner) {}
    std::weak_ptr<ModelSceneObject> target;
};
std::shared_ptr<ModelSceneObject>& slot(const std::shared_ptr<ModelSceneList>& list,std::uint32_t index) {
    if(!list)fail("Original serial scheduler dereferences a null physical list.");
    if(index>=maximum_slots || !list->backing || index>=list->backing->size())
        fail("Original serial scheduler reaches an unavailable physical pointer slot.");
    return (*list->backing)[index];
}
void release(const std::shared_ptr<ModelSceneList>& list,std::uint32_t index) {
    auto& value=slot(list,index);
    if(dynamic_cast<UnknownSlot*>(value.get()))fail("Original scheduler release reads an uninitialized pointer.");
    if(dynamic_cast<ReleasedSlot*>(value.get()))
        fail("Original scheduler releases a raw pointer whose represented reference was already released.");
    if(!value)return;
    if(value.use_count()==1)
        fail("Original scheduler reaches a manager final-release destructor that has not been represented.");
    // The original first pass drops the reference while retaining the pointer
    // word. Keep that readable identity without retaining an additional owner.
    auto released=std::make_shared<ReleasedSlot>(value);
    value=std::move(released);
}
void zero(const std::shared_ptr<ModelSceneList>& list,std::uint32_t index) {
    auto& value=slot(list,index);
    if(value && !dynamic_cast<ReleasedSlot*>(value.get()) && !dynamic_cast<UnknownSlot*>(value.get()))
        fail("Original scheduler raw zero would overwrite an unreleased spare reference.");
    value.reset();
}
void grow(const std::shared_ptr<ModelSceneList>& list,std::int32_t requested) {
    auto capacity=std::max<std::int32_t>(4,list->capacity);
    while(capacity<requested) {
        if(static_cast<std::uint32_t>(capacity)>=maximum_slots)
            fail("Original scheduler growth exceeds the bounded native allocation profile.");
        capacity=capacity<1024 ? capacity*2:capacity+1024;
    }
    if(static_cast<std::uint32_t>(capacity)>maximum_slots)
        fail("Original scheduler allocation exceeds the bounded physical profile.");
    const auto previous=list->backing;
    auto replacement=std::make_shared<ModelSceneList::Storage>(static_cast<std::size_t>(capacity),std::make_shared<UnknownSlot>());
    list->backing=replacement;
    if(previous) {
        const auto copied=std::min(list->capacity,capacity);
        if(copied<0 || static_cast<std::size_t>(copied)>previous->size())
            fail("Original scheduler capacity copy exceeds the retained old allocation.");
        std::copy_n(previous->begin(),static_cast<std::size_t>(copied),replacement->begin());
    }
    list->capacity=capacity;list->count=requested;
}
void resize(const std::shared_ptr<ModelSceneList>& list,std::int32_t requested) {
    if(!list)fail("Original scheduler resize dereferences a null cache list.");
    const auto previous=list->count;
    if(requested<=previous) {
        for(std::int64_t i=requested;i<previous;++i)release(list,static_cast<std::uint32_t>(i));
        for(std::int64_t i=requested;i<previous;++i)zero(list,static_cast<std::uint32_t>(i));
        list->count=requested;
    } else {
        if(requested>=0) {
            if(requested<=list->capacity)list->count=requested;
            else grow(list,requested);
        }
        for(std::int64_t i=previous;i<requested;++i)zero(list,static_cast<std::uint32_t>(i));
    }
}
std::shared_ptr<ModelSceneList> source_list(const std::shared_ptr<SerialSchedulerRegistry>& registry) {
    if(!registry)fail("Original serial scheduler dereferences its null registered-manager wrapper.");
    if(!registry->entries)fail("Original serial scheduler dereferences the registered wrapper's null list.");
    return registry->entries;
}
}
SerialSchedulerManager::SerialSchedulerManager(std::function<MessengerCallResult()> value)
    :ModelSceneObject("native serial scheduler manager"),update(std::move(value)) {}
SerialSchedulerState::SerialSchedulerState():registered(std::make_shared<SerialSchedulerRegistry>()),
    cache(std::make_shared<ModelSceneList>("igInfoManagerList")) {
    registered->entries=std::make_shared<ModelSceneList>("igObjectList");
}
std::shared_ptr<ModelSceneObject> serial_scheduler_physical_object(
    const std::shared_ptr<ModelSceneList>& list,std::uint32_t index) {
    const auto& value=slot(list,index);
    if(dynamic_cast<UnknownSlot*>(value.get()))fail("Original scheduler reads an uninitialized pointer slot.");
    if(const auto released=dynamic_cast<ReleasedSlot*>(value.get())) {
        auto owner=released->target.lock();
        if(!owner)fail("Original scheduler reads a dangling released pointer.");
        return owner;
    }
    return value;
}
struct SerialSchedulerInvocation::Impl {
    enum class Phase {capture,copy,update,clear,done};
    std::shared_ptr<SerialSchedulerState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::shared_ptr<SerialSchedulerManager> manager;
    std::function<MessengerCallResult()> callback;
    std::uint32_t count{},index{};
    Phase phase=Phase::capture;
    bool owns{},executing{},fault{};
};
SerialSchedulerInvocation::SerialSchedulerInvocation(std::shared_ptr<SerialSchedulerState> state)
    :impl_(std::make_unique<Impl>()) {impl_->state=std::move(state);}
SerialSchedulerInvocation::~SerialSchedulerInvocation() {
    const auto& s=*impl_;
    if(s.owns && s.phase!=Impl::Phase::done && s.state)s.state->fault_=true;
}
bool SerialSchedulerInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool SerialSchedulerInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult SerialSchedulerInvocation::resume() {
    auto& s=*impl_;
    if(s.fault || s.executing) {
        s.fault=true;if(s.owns && s.state)s.state->fault_=true;
        fail("Serial scheduler requires an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,0,{}};
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(s.executing);
    auto check=[&] {if(s.fault || s.state->fault_)fail("A caught recursive operation faulted the serial scheduler.");};
    try {
        if(!s.state)fail("Original serial scheduler dereferences a null scheduler owner.");
        if(s.state->fault_)fail("A faulted serial scheduler cannot restart its visible batch prefix.");
        if(!s.owns) {
            const auto active=s.state->active_batch_.lock();
            if(active && active!=s.token)fail("Another pending batch owns this serial scheduler.");
            s.state->active_batch_=s.token;s.owns=true;
        }
        while(!completed())switch(s.phase) {
        case Impl::Phase::capture: {
            const auto registry=s.state->registered;
            const auto cache=s.state->cache;
            s.count=std::bit_cast<std::uint32_t>(source_list(registry)->count);
            resize(cache,std::bit_cast<std::int32_t>(s.count));
            s.phase=Impl::Phase::copy;break;
        }
        case Impl::Phase::copy: {
            if(s.index==s.count) {s.index=0;s.phase=Impl::Phase::update;break;}
            const auto registry=s.state->registered; // Original wrapper load precedes cache capture.
            const auto cache=s.state->cache;
            const auto source=source_list(registry);
            auto incoming=serial_scheduler_physical_object(source,s.index);
            try {
                release(cache,s.index);
                const auto current_count=cache->count;
                if(current_count!=0 && std::bit_cast<std::int32_t>(s.index)>=0 &&
                    std::bit_cast<std::int32_t>(s.index)<current_count)
                    slot(cache,s.index)=std::move(incoming);
                else if(incoming)s.state->unpublished_references_.push_back(std::move(incoming));
            }catch(...) {
                // 8028B804..80C acquired this reference before a later cache
                // read/release/store boundary. Faulting does not undo it.
                if(incoming)s.state->unpublished_references_.push_back(std::move(incoming));
                throw;
            }
            ++s.index;break;
        }
        case Impl::Phase::update: {
            if(s.index==s.count) {s.phase=Impl::Phase::clear;break;}
            if(!s.manager) {
                const auto cache=s.state->cache;
                s.manager=std::dynamic_pointer_cast<SerialSchedulerManager>(serial_scheduler_physical_object(cache,s.index));
                if(!s.manager)fail("Original scheduler reaches a null or unsupported concrete manager virtual+74.");
            }
            if(!s.callback)s.callback=s.manager->update;
            if(!s.callback)return {false,0,"Original serial scheduler requires the reached manager virtual+74 implementation."};
            const auto result=s.callback();check();
            if(!result.completed)return result;
            s.callback={};s.manager.reset();check();
            ++s.index;break;
        }
        case Impl::Phase::clear: {
            const auto cache=s.state->cache;
            resize(cache,0);s.phase=Impl::Phase::done;break;
        }
        case Impl::Phase::done:break;
        }
        s.state->active_batch_.reset();s.token.reset();s.owns=false;
        return {true,0,{}};
    }catch(...) {s.fault=true;if(s.owns)s.state->fault_=true;throw;}
}
std::function<MessengerCallResult()> make_serial_scheduler_update(std::shared_ptr<SerialSchedulerState> state) {
    struct Retained {std::shared_ptr<SerialSchedulerState> state;std::unique_ptr<SerialSchedulerInvocation> active;};
    auto retained=std::make_shared<Retained>();retained->state=std::move(state);
    return [retained] {
        if(!retained->active)retained->active=std::make_unique<SerialSchedulerInvocation>(retained->state);
        const auto result=retained->active->resume();
        if(result.completed)retained->active.reset();
        return result;
    };
}
} // namespace dw4
