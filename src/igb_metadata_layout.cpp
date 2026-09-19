#include "dw4/igb_metadata_layout.hpp"

#include <bit>
#include <iomanip>
#include <sstream>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetadataLayoutError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
using K=IgbMetadataLayoutKind;
struct Call {
    IgbMetadataLayoutCall request;IgbMetadataLayoutFactory factory;
    std::shared_ptr<IgbMetadataLayoutOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    MessengerCallResult boundary()const{
        if(request.kind!=K::registered_callback)return pending("Metadata layout requires its captured field alignment virtual+6C.");
        std::ostringstream message;message<<"Metadata layout requires registered callback 0x"<<std::hex<<std::uppercase<<request.function<<".";
        return {false,0,message.str()};
    }
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbMetadataLayoutServices>& services,Check check){
        if(!factory && services)factory=services->factory;
        if(!factory)return boundary();
        if(!operation){operation=factory(request);check();}
        if(!operation)return boundary();
        if(!callback)callback=operation->resume;
        if(!callback)return boundary();
        const auto status=callback();check();return status;
    }
};
std::shared_ptr<IgbRegistrationCallbackList> callback_list(const std::shared_ptr<IgbRuntimeTypeRegistry>& core){
    if(!core)fail("Metadata callbacks dereference a null captured ArkCore.");
    if(!core->registration_callbacks)return {};
    const auto list=*core->registration_callbacks;
    if(!list)fail("Metadata callbacks dereference a null registration list.");
    if(list->released)fail("Metadata callbacks dereference a released registration list.");
    return list;
}
}

struct IgbMetadataCallbacksInvocation::Impl {
    enum class P{count,next,call,after,done};
    std::shared_ptr<IgbRuntimeTypeRegistry> core;std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::int32_t count{};std::uint32_t index{},offset{};Call call;P phase=P::count;
    bool running{},fault{};
};
IgbMetadataCallbacksInvocation::IgbMetadataCallbacksInvocation(std::shared_ptr<IgbRuntimeTypeRegistry> core,std::shared_ptr<IgbRuntimeMetaType> metadata)
    :impl_(std::make_unique<Impl>()){impl_->core=std::move(core);impl_->metadata=std::move(metadata);}
IgbMetadataCallbacksInvocation::~IgbMetadataCallbacksInvocation()=default;
bool IgbMetadataCallbacksInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataCallbacksInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMetadataCallbacksInvocation::resume(std::shared_ptr<IgbMetadataLayoutServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata callbacks require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Metadata callback continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    try{for(;;){check();switch(s.phase){
    case P::count:{const auto list=callback_list(s.core);if(!list)return pending("Metadata callbacks require captured ArkCore+2C storage.");
        s.count=list->count;s.phase=P::next;break;}
    case P::next:{
        if(std::bit_cast<std::int32_t>(s.index)>=s.count){s.phase=P::done;break;}
        const auto list=callback_list(s.core);if(!list)return pending("Metadata callbacks require freshly reloaded ArkCore+2C storage.");
        const auto backing=list->backing;const auto physical=s.offset/4u;
        if(!backing || physical>=backing->size())fail("Metadata callback index reaches unavailable physical backing.");
        const auto function=(*backing)[physical];if(!function)fail("Metadata callbacks reach a null function.");
        clear();s.call.request.kind=K::registered_callback;s.call.request.core=s.core;s.call.request.metadata=s.metadata;s.call.request.function=function;
        if(services)s.call.factory=services->factory;s.phase=P::call;break;
    }
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=P::after;break;}
    case P::after:clear();++s.index;s.offset+=4u;s.phase=P::next;break;
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbMetadataLayoutInvocation::Impl {
    enum class P{count,initial_parent,next,alignment,after_alignment,parent_count,parent_field,ordinal,normal_alignment,extent,publish_alignment,prefix,flag,core,callbacks,call,done};
    std::shared_ptr<IgbRuntimeMetaType> metadata;std::shared_ptr<IgbDecodeRuntimeField> field;
    std::int32_t count{};std::uint32_t index{},negative_cursor{},special_count{},maximum_extent{};
    std::uint16_t maximum_alignment{4},normal_maximum_alignment{4},alignment{};
    std::unique_ptr<IgbMetadataCallbacksInvocation> callbacks;Call call;P phase=P::count;
    bool running{},fault{};
};
IgbMetadataLayoutInvocation::IgbMetadataLayoutInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata):impl_(std::make_unique<Impl>()){impl_->metadata=std::move(metadata);}
IgbMetadataLayoutInvocation::~IgbMetadataLayoutInvocation()=default;
bool IgbMetadataLayoutInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataLayoutInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMetadataLayoutInvocation::resume(std::shared_ptr<IgbMetadataLayoutServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata layout requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Metadata layout continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    try{for(;;){check();switch(s.phase){
    case P::count:s.count=igb_runtime_slot_count(s.metadata);s.phase=P::initial_parent;break;
    case P::initial_parent:
        // The original loads+38 even though its following andc with zero
        // yields a zero cursor for both null and nonnull parent identities.
        if(!s.metadata->parent)return pending("Metadata layout requires its initial parent pointer+38.");
        s.negative_cursor=0;s.phase=P::next;break;
    case P::next:
        if(std::bit_cast<std::int32_t>(s.index)>=s.count){s.phase=P::publish_alignment;break;}
        s.field=igb_runtime_field_at(s.metadata,std::bit_cast<std::int32_t>(s.index));
        if(!s.field)fail("Metadata layout dereferences a null field.");s.phase=P::alignment;break;
    case P::alignment:
        if(!s.field->vtable_word)return pending("Metadata layout requires its captured field vtable.");
        if(!*s.field->vtable_word)fail("Metadata layout dereferences a null field vtable.");
        if(*s.field->vtable_word==0x80476e6cu){s.alignment=4;s.phase=P::after_alignment;break;}
        clear();s.call.request.kind=K::field_alignment;s.call.request.metadata=s.metadata;s.call.request.field=s.field;
        s.call.request.vtable_word=*s.field->vtable_word;s.call.request.virtual_slot=0x6c;
        if(services)s.call.factory=services->factory;s.phase=P::call;break;
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;
        s.alignment=static_cast<std::uint16_t>(status.value);clear();s.phase=P::after_alignment;break;}
    case P::after_alignment:
        if(s.alignment>s.maximum_alignment)s.maximum_alignment=s.alignment;
        if(s.field->flag_25){
            ++s.special_count;s.negative_cursor=(s.negative_cursor-static_cast<std::uint32_t>(s.field->size))&~(static_cast<std::uint32_t>(s.alignment)-1u);
            s.field->offset=s.negative_cursor;s.phase=P::extent;
        }else s.phase=P::parent_count;break;
    case P::parent_count:{
        if(!s.metadata->parent)return pending("Metadata layout requires its freshly reloaded parent pointer.");
        const auto parent=*s.metadata->parent;
        if(!parent || igb_runtime_slot_count(parent)<=std::bit_cast<std::int32_t>(s.index))s.phase=P::ordinal;
        else s.phase=P::parent_field;break;
    }
    case P::parent_field:{
        if(!s.metadata->parent)return pending("Metadata layout requires parent storage for its inherited field comparison.");
        const auto parent_field=igb_runtime_field_at(*s.metadata->parent,std::bit_cast<std::int32_t>(s.index));
        s.phase=parent_field==s.field?P::normal_alignment:P::ordinal;break;
    }
    case P::ordinal:
        s.field->ordinal=static_cast<std::uint16_t>(s.index-s.special_count);s.field->declaring_type=s.metadata;s.phase=P::normal_alignment;break;
    case P::normal_alignment:
        if(s.alignment>s.normal_maximum_alignment)s.normal_maximum_alignment=s.alignment;s.phase=P::extent;break;
    case P::extent:{
        const auto end=s.field->offset+static_cast<std::uint32_t>(s.field->size);
        if(std::bit_cast<std::int32_t>(end)>std::bit_cast<std::int32_t>(s.maximum_extent))s.maximum_extent=end;
        ++s.index;s.phase=P::next;break;
    }
    case P::publish_alignment:s.metadata->native_alignment=s.maximum_alignment;s.phase=P::prefix;break;
    case P::prefix:
        if(s.metadata->native_size!=0xffffffffu)s.metadata->allocation_prefix=0u-(s.negative_cursor&~(static_cast<std::uint32_t>(s.normal_maximum_alignment)-1u));
        s.phase=P::flag;break;
    case P::flag:s.metadata->class_flags|=4u;s.phase=P::core;break;
    case P::core:
        if(!services || !services->runtime || !services->runtime->singleton)return pending("Metadata layout requires ArkCore80562100 after ready-flag publication.");
        s.callbacks=std::make_unique<IgbMetadataCallbacksInvocation>(*services->runtime->singleton,s.metadata);s.phase=P::callbacks;break;
    case P::callbacks:{const auto status=s.callbacks->resume(services);check();if(!status.completed)return status;
        s.callbacks.reset();check();s.phase=P::done;break;}
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_metadata_layout_factory(std::shared_ptr<IgbMetadataLayoutServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbMetadataLayoutServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::compute_layout)return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata layout services expired before selection.");
        auto invocation=std::make_shared<IgbMetadataLayoutInvocation>(call.metadata);auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
