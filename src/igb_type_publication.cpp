#include "dw4/igb_type_publication.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbTypePublicationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
}
std::int32_t igb_runtime_type_index(const std::shared_ptr<IgbRuntimeTypeList>& list,
    const std::shared_ptr<IgbRuntimeMetaType>& metadata){
    if(!list)fail("Runtime type identity search reads the count of a null list.");
    const auto count=list->count;
    const auto backing=list->backing;
    for(std::int32_t index=0;index<count;++index){
        if(index>=262144 || !backing || static_cast<std::size_t>(index)>=backing->size())
            fail("Runtime type identity search reaches an unavailable physical pointer-owner mapping.");
        if((*backing)[static_cast<std::size_t>(index)]==metadata)return index;
    }
    return -1;
}

struct IgbTypePublicationInvocation::Impl {
    enum class P {first_search,first_store,append_registry,retain,identity,append,second_search,second_store,done};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbRuntimeTypeList> append_list;
    std::shared_ptr<IgbTypePublicationServices> owner;
    std::unique_ptr<IgbPointerListInvocation> append;
    P phase=P::first_search;std::int32_t index{};
    bool retained{},transferred{},running{},fault{};
};
IgbTypePublicationInvocation::IgbTypePublicationInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata)
    :impl_(std::make_unique<Impl>()){impl_->metadata=std::move(metadata);}
IgbTypePublicationInvocation::~IgbTypePublicationInvocation(){
    auto& s=*impl_;
    if(s.retained && !s.transferred && s.owner){
        if(s.metadata)s.owner->unfinished.push_back(s.metadata);
        if(s.append_list)s.owner->unfinished.push_back(s.append_list);
    }
}
bool IgbTypePublicationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbTypePublicationInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbTypePublicationInvocation::resume(std::shared_ptr<IgbTypePublicationServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Type publication requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Type publication continuation was invalidated.");};
    const auto registry=[&]()->std::shared_ptr<IgbRuntimeTypeRegistry>{
        if(!*services->runtime->singleton)fail("Type publication dereferences a null ArkCore singleton.");
        return *services->runtime->singleton;
    };
    const auto represented=[&]{return services && services->runtime && services->runtime->singleton.has_value();};
    try{for(;;){check();switch(s.phase){
    case P::first_search:
    case P::second_search:
        if(!represented())return pending("Type publication requires original singleton global80562100.");
        s.index=igb_runtime_type_index(registry()->types,s.metadata);
        s.phase=s.phase==P::first_search?P::first_store:P::second_store;break;
    case P::first_store:
    case P::second_store:{
        if(!s.metadata)fail("Type publication writes the index of null metadata.");
        s.metadata->word_0c=std::bit_cast<std::uint32_t>(s.index);
        s.phase=s.phase==P::first_store && s.index==-1?P::append_registry:P::done;break;
    }
    case P::append_registry:
        if(!represented())return pending("Type append requires original singleton global80562100.");
        s.append_list=registry()->types;s.phase=P::retain;break;
    case P::retain:
        if(s.metadata)++s.metadata->reference_word;
        s.retained=true;s.phase=P::identity;break;
    case P::identity:{
        // 53E6C has retained its input before raw53BC0 reads the list. Keep the
        // represented null-list fault ahead of the host arithmetic binding.
        if(!s.append_list)fail("Type append dereferences a null registry list.");
        IgbPointerListValue value;bool found=!s.metadata;
        if(services && services->pointer_lists)for(const auto& candidate:services->pointer_lists->values){
            if(!s.metadata || candidate.metadata!=s.metadata)continue;
            if(found && (value.address!=candidate.address || value.metadata!=candidate.metadata || value.field!=candidate.field))
                fail("Type publication has conflicting original arithmetic metadata identities.");
            value=candidate;found=true;
        }
        if(!found)return pending("Type publication append requires the metadata's original arithmetic identity.");
        s.append=std::make_unique<IgbPointerListInvocation>(s.append_list,value);s.phase=P::append;break;
    }
    case P::append:{
        const auto result=s.append->resume(services?services->pointer_lists:nullptr);check();
        if(!result.completed)return result;
        s.transferred=true;s.append.reset();check();s.phase=P::second_search;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbTypeRegistrationFactory make_native_igb_type_publication_factory(
    std::shared_ptr<IgbTypePublicationServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbTypePublicationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::publish_type)
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Type publication services expired before selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        auto invocation=std::make_shared<IgbTypePublicationInvocation>(call.metadata);
        operation->resume=[selected,invocation]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
