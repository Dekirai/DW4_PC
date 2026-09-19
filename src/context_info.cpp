#include "dw4/context_info.hpp"

#include <algorithm>
#include <bit>

namespace dw4 {
struct ContextInfoReferenceState {
    std::vector<std::shared_ptr<ModelSceneObject>> retained;
    std::vector<std::shared_ptr<ModelSceneList::Storage>> allocations;
};
namespace {
constexpr std::uint32_t maximum_slots=262144;
[[noreturn]] void fail(const char* reason) {throw ContextInfoError(reason);}
class UninitializedInfoSlot final : public ModelSceneObject {
public:
    UninitializedInfoSlot():ModelSceneObject("uninitialized native info pointer") {}
};
std::shared_ptr<ModelSceneObject>& physical(const std::shared_ptr<ModelSceneList::Storage>& backing,
    std::uint32_t word_index) {
    // Original slwi wraps the byte offset to 32 bits before the pointer access.
    const auto index=(word_index<<2)>>2;
    if(index>=maximum_slots || !backing || index>=backing->size())
        fail("Context info append reaches an unavailable physical pointer slot.");
    return (*backing)[index];
}
}
struct ContextInfoInvocation::Impl {
    enum class Phase {visit,enter,call,append,done};
    std::shared_ptr<ModelSceneContext> context;
    std::shared_ptr<ModelSceneObject> info,acquired;
    std::shared_ptr<SerialSchedulerManager> manager;
    decltype(SerialSchedulerManager::info_factory) factory;
    ModelManagerRamCall callback;
    std::shared_ptr<ModelSceneList::Storage> previous,result;
    std::shared_ptr<ContextInfoReferenceState> references;
    std::uint32_t index{};
    Phase phase=Phase::visit;
    bool running{},fault{};
};
ContextInfoInvocation::ContextInfoInvocation(std::shared_ptr<ModelSceneContext> context,
    std::shared_ptr<ModelSceneObject> info):impl_(std::make_unique<Impl>()) {
    impl_->context=std::move(context);impl_->info=std::move(info);
}
ContextInfoInvocation::~ContextInfoInvocation()=default;
bool ContextInfoInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool ContextInfoInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<ModelSceneList::Storage> ContextInfoInvocation::appended_backing() const noexcept {return impl_->result;}
MessengerCallResult ContextInfoInvocation::resume() {
    auto& s=*impl_;
    if(s.fault || s.running) {s.fault=true;fail("Context info registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.running);
    const auto check=[&] {if(s.fault)fail("A caught recursive call faulted this context info registration.");};
    try {
        if(!s.context)fail("Info registration dereferences a null application context.");
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::visit: {
            const auto registry=s.context->managers;
            if(!registry || !registry->entries)fail("Info registration dereferences a null manager registry or list.");
            const auto list=registry->entries;
            if(s.index>=std::bit_cast<std::uint32_t>(list->count)) {s.phase=Impl::Phase::append;break;}
            s.manager=std::dynamic_pointer_cast<SerialSchedulerManager>(serial_scheduler_physical_object(list,s.index));
            if(!s.manager)fail("Info registration reaches a null or unsupported manager virtual+6C.");
            s.factory=s.manager->info_factory;s.phase=Impl::Phase::enter;break;
        }
        case Impl::Phase::enter:
            if(!s.factory)s.factory=s.manager->info_factory;
            if(!s.factory)return {false,0,"Info registration requires the reached manager virtual+6C implementation."};
            s.callback=s.factory(s.info);check();
            if(!s.callback)fail("A bound manager info factory returned no operation.");
            s.factory={};check();s.phase=Impl::Phase::call;break;
        case Impl::Phase::call: {
            const auto result=s.callback();check();
            if(!result.completed)return result;
            s.callback={};s.manager.reset();check();
            if((static_cast<std::uint32_t>(result.value)&0xff)!=0)s.phase=Impl::Phase::append;
            else {++s.index;s.phase=Impl::Phase::visit;}
            break;
        }
        case Impl::Phase::append: {
            const auto list=s.context->infos;
            // 80069128 acquires the incoming reference before any list access.
            if(!s.context->info_reference_state)
                s.context->info_reference_state=std::make_shared<ContextInfoReferenceState>();
            s.references=s.context->info_reference_state;s.acquired=s.info;
            if(!list)fail("Info registration appends through a null application info list.");
            const auto count=list->count;
            const auto next=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(count)+1u);
            if(count<list->capacity)list->count=next;
            else {
                // Recovered80041970/80041660 growth, using native storage in
                // place of console allocation pools. Spare pointer words have
                // unknown values until initialized, rather than invented nulls.
                auto capacity=std::max<std::int32_t>(4,list->capacity);
                while(capacity<next) {
                    if(static_cast<std::uint32_t>(capacity)>=maximum_slots)
                        fail("Info list growth exceeds its bounded native allocation profile.");
                    capacity=capacity<1024 ? capacity*2:capacity+1024;
                }
                if(static_cast<std::uint32_t>(capacity)>maximum_slots)
                    fail("Info list allocation exceeds its bounded native storage profile.");
                s.previous=list->backing;
                list->backing=std::make_shared<ModelSceneList::Storage>(static_cast<std::size_t>(capacity),
                    std::make_shared<UninitializedInfoSlot>());
                if(s.previous) {
                    const auto copied=std::min(list->capacity,capacity);
                    if(copied<0 || static_cast<std::size_t>(copied)>s.previous->size())
                        fail("Info list growth reaches an unavailable old-capacity copy range.");
                    std::copy_n(s.previous->begin(),static_cast<std::size_t>(copied),list->backing->begin());
                    // Raw allocation release does not release pointer words
                    // outside the copied capacity. Keep any represented owners.
                    for(std::size_t i=static_cast<std::size_t>(copied);i<s.previous->size();++i)
                        if((*s.previous)[i])s.references->retained.push_back((*s.previous)[i]);
                }
                s.previous.reset();list->capacity=capacity;list->count=next;
            }
            s.result=list->backing;
            auto& target=physical(s.result,std::bit_cast<std::uint32_t>(count));
            if(target && !dynamic_cast<UninitializedInfoSlot*>(target.get()))
                s.references->retained.push_back(target); // Original raw overwrite has no release.
            target=std::move(s.acquired);
            s.phase=Impl::Phase::done;return {true,0,{}};
        }
        case Impl::Phase::done:return {true,0,{}};
        }}
    }catch(...) {
        s.fault=true;
        if(s.references) {
            if(s.acquired)s.references->retained.push_back(std::move(s.acquired));
            if(s.previous)s.references->allocations.push_back(std::move(s.previous));
        }
        throw;
    }
}
ModelManagerRamCall make_context_info_registration(std::shared_ptr<ModelSceneContext> context,
    std::shared_ptr<ModelSceneObject> info) {
    const auto call=std::make_shared<ContextInfoInvocation>(std::move(context),std::move(info));
    return [call] {return call->resume();};
}
} // namespace dw4
