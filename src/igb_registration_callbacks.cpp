#include "dw4/igb_registration_callbacks.hpp"

#include <array>
#include <bit>
#include <iomanip>
#include <sstream>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbRegistrationCallbacksError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
using K=IgbRegistrationCallbacksServiceKind;
const char* boundary(K kind){switch(kind){
case K::invoke_function:return "Registration requires its captured original function body.";
case K::allocate_backing:return "Registration callback list requires allocation80056138.";
case K::reallocate_backing:return "Registration callback list requires reallocation8005624C.";
case K::free_backing:return "Registration callback list requires free80056378.";
case K::free_list:return "Registration callback list requires delete800564E8.";
}return "Registration requires its reached operation.";}
std::string reached_boundary(const IgbRegistrationCallbacksCall& call){
    if(call.kind!=K::invoke_function)return boundary(call.kind);
    std::ostringstream out;out<<"Registration requires original function 0x"<<std::hex<<std::uppercase<<call.function<<".";return out.str();
}
struct Call {
    IgbRegistrationCallbacksCall request;
    IgbRegistrationCallbacksFactory factory;
    std::shared_ptr<IgbRegistrationCallbacksOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    void select(IgbRegistrationCallbacksCall value,const std::shared_ptr<IgbRegistrationCallbacksServices>& services){
        clear();request=std::move(value);factory=services?services->factory:IgbRegistrationCallbacksFactory{};
    }
    template<class Check> MessengerCallResult poll(const std::shared_ptr<IgbRegistrationCallbacksServices>& services,Check&& check){
        if(!factory && services)factory=services->factory;
        if(!factory)return {false,0,reached_boundary(request)};
        if(!operation){operation=factory(request);check();}
        if(!operation)return {false,0,reached_boundary(request)};
        if(!callback)callback=operation->resume;
        if(!callback)return {false,0,reached_boundary(request)};
        const auto result=callback();check();return result;
    }
};
void readable(const std::shared_ptr<IgbRegistrationCallbackList>& list){
    if(!list)fail("Registration callback list is null.");
    if(list->released)fail("Registration callback list has been released.");
}
std::uint32_t function_at(const std::shared_ptr<IgbRegistrationCallbackList>& list,std::uint32_t index){
    readable(list);const auto backing=list->backing;
    const auto physical_index=(index*4u)/4u;
    if(!backing || physical_index>=backing->size())fail("Registration callback lies outside physical list backing.");
    return (*backing)[physical_index];
}
std::uint32_t registration_body(std::uint32_t wrapper){switch(wrapper){
case 0x8002f178:return 0x8002f1a0;
case 0x80023880:return 0x800238a8;
case 0x800240cc:return 0x800240f4;
case 0x80023c40:return 0x80023c68;
case 0x80031250:return 0x80031278;
case 0x800374e8:return 0x80037510;
default:return 0;}}
}
void igb_initialize_registration_callbacks(const std::shared_ptr<IgbRegistrationCallbackList>& list){
    readable(list);
    if(list->backing)list->overwritten_backings_.push_back(list->backing);
    list->backing.reset();list->capacity=0;list->count=0;
}

struct IgbRegistrationCallbackAppendInvocation::Impl {
    enum class P {entry,allocate_global,allocate_pool,store_result,store,call,done};
    std::shared_ptr<IgbRegistrationCallbackList> list;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::shared_ptr<IgbRegistrationCallbacksServices> owner;
    std::shared_ptr<IgbRegistrationCallbackList::Storage> incoming;
    Call call;P phase=P::entry,after=P::done;
    std::uint32_t function{},bytes{};bool running{},fault{};
};
IgbRegistrationCallbackAppendInvocation::IgbRegistrationCallbackAppendInvocation(
    std::shared_ptr<IgbRegistrationCallbackList> list,std::uint32_t function):impl_(std::make_unique<Impl>()){
    impl_->list=std::move(list);impl_->function=function;
}
IgbRegistrationCallbackAppendInvocation::~IgbRegistrationCallbackAppendInvocation(){
    auto& s=*impl_;if(!completed() && s.owner){
        if(s.list)s.owner->unfinished_lists_.push_back(s.list);
        if(s.call.request.backing)s.owner->unfinished_backings_.push_back(s.call.request.backing);
        if(s.incoming)s.owner->unfinished_backings_.push_back(s.incoming);
    }
}
bool IgbRegistrationCallbackAppendInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbRegistrationCallbackAppendInvocation::faulted() const noexcept{return impl_->fault;}
MessengerCallResult IgbRegistrationCallbackAppendInvocation::resume(std::shared_ptr<IgbRegistrationCallbacksServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Callback append requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Callback append continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbRegistrationCallbacksCall&{
        clear();IgbRegistrationCallbacksCall call;call.kind=kind;call.callbacks=s.list;
        s.call.select(std::move(call),services);check();s.after=after;s.phase=P::call;return s.call.request;
    };
    try{for(;;){check();switch(s.phase){
    case P::entry:{
        readable(s.list);const auto count=s.list->count,capacity=s.list->capacity;
        if(count!=capacity){s.phase=P::store;break;}
        const auto bits=static_cast<std::uint32_t>(capacity);
        const auto grown=bits==0?4u:bits*static_cast<std::uint32_t>(std::bit_cast<std::int32_t>(bits*3u)/2);
        s.list->capacity=std::bit_cast<std::int32_t>(grown);
        const auto backing=s.list->backing;
        if(!backing)s.phase=P::allocate_global;
        else {auto& call=select(K::reallocate_backing,P::store_result);call.backing=backing;
            call.bytes=static_cast<std::uint32_t>(s.list->capacity)*4u;}
        break;
    }
    case P::allocate_global:
        if(!services || !services->runtime || !services->runtime->singleton)
            return pending("Callback allocation requires represented ArkCore80562100.");
        s.core=*services->runtime->singleton;
        readable(s.list);s.bytes=static_cast<std::uint32_t>(s.list->capacity)*4u;
        s.phase=P::allocate_pool;break;
    case P::allocate_pool:
        if(!s.core)fail("Callback allocation dereferences null ArkCore.");
        if(!s.core->registration_callback_pool)return pending("Callback allocation requires represented ArkCore+34 pool.");
        {const auto pool=*s.core->registration_callback_pool;auto& call=select(K::allocate_backing,P::store_result);
            call.pool=pool;call.bytes=s.bytes;}break;
    case P::store_result:
        s.incoming=std::move(s.call.operation->backing_result);clear();readable(s.list);
        if(s.list->backing && s.list->backing!=s.incoming)s.list->overwritten_backings_.push_back(s.list->backing);
        s.list->backing=s.incoming;s.incoming.reset();s.phase=P::store;break;
    case P::store:{
        readable(s.list);const auto index=static_cast<std::uint32_t>(s.list->count);
        const auto backing=s.list->backing;
        s.list->count=std::bit_cast<std::int32_t>(index+1u);
        const auto physical_index=(index*4u)/4u;
        if(!backing || physical_index>=backing->size())fail("Callback append store exceeds physical backing after count publication.");
        (*backing)[physical_index]=s.function;s.phase=P::done;return {true,0,{}};
    }
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbRegistrationCallbacksInvocation::Impl {
    enum class P {entry,capture,next,after_function,cleanup,after_free_backing,after_free_list,call,done};
    std::uint32_t function{},index{};std::int32_t count{};
    std::shared_ptr<IgbRegistrationCallbackList> list;
    std::shared_ptr<IgbRegistrationCallbacksServices> owner;
    Call call;P phase=P::entry,after=P::done;bool running{},fault{};
};
IgbRegistrationCallbacksInvocation::IgbRegistrationCallbacksInvocation(std::uint32_t function):impl_(std::make_unique<Impl>()){impl_->function=function;}
IgbRegistrationCallbacksInvocation::~IgbRegistrationCallbacksInvocation(){
    auto& s=*impl_;if(!completed() && s.owner){
        if(s.list)s.owner->unfinished_lists_.push_back(s.list);
        if(s.call.request.backing)s.owner->unfinished_backings_.push_back(s.call.request.backing);
    }
}
bool IgbRegistrationCallbacksInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbRegistrationCallbacksInvocation::faulted() const noexcept{return impl_->fault;}
MessengerCallResult IgbRegistrationCallbacksInvocation::resume(std::shared_ptr<IgbRegistrationCallbacksServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Registration callbacks require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Registration callback continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbRegistrationCallbacksCall&{
        clear();IgbRegistrationCallbacksCall call;call.kind=kind;call.callbacks=s.list;
        s.call.select(std::move(call),services);check();s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto function=[&](std::uint32_t target,P after){
        if(target==0)fail("Registration reaches a null function call.");
        select(K::invoke_function,after).function=target;
    };
    try{for(;;){check();switch(s.phase){
    case P::entry:function(s.function,P::capture);break;
    case P::capture:
        s.list=std::move(s.call.operation->callbacks_result);clear();
        if(!s.list){s.phase=P::done;return {true,0,{}};}
        readable(s.list);s.count=s.list->count;s.phase=P::next;break;
    case P::next:
        if(std::bit_cast<std::int32_t>(s.index)<s.count)function(function_at(s.list,s.index),P::after_function);
        else s.phase=P::cleanup;break;
    case P::after_function:clear();++s.index;s.phase=P::next;break;
    case P::cleanup:{
        readable(s.list);const auto backing=s.list->backing;
        if(backing)select(K::free_backing,P::after_free_backing).backing=backing;
        else s.phase=P::after_free_backing;break;
    }
    case P::after_free_backing:clear();select(K::free_list,P::after_free_list);break;
    case P::after_free_list:
        clear();s.list->released=true;s.phase=P::done;return {true,0,{}};
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbRegistrationPrerequisitesInvocation::Impl {
    enum class P {initialize,test,next,call,done};
    std::shared_ptr<IgbRegistrationPrerequisitesState> state;
    std::size_t index{};Call call;P phase=P::initialize;bool running{},fault{};
};
IgbRegistrationPrerequisitesInvocation::IgbRegistrationPrerequisitesInvocation():impl_(std::make_unique<Impl>()){}
IgbRegistrationPrerequisitesInvocation::~IgbRegistrationPrerequisitesInvocation()=default;
bool IgbRegistrationPrerequisitesInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbRegistrationPrerequisitesInvocation::faulted() const noexcept{return impl_->fault;}
MessengerCallResult IgbRegistrationPrerequisitesInvocation::resume(std::shared_ptr<IgbRegistrationCallbacksServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Registration prerequisites require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Prerequisite continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    static constexpr std::array<std::uint32_t,5> wrappers{0x80023880,0x800240cc,0x80023c40,0x80031250,0x800374e8};
    try{for(;;){check();switch(s.phase){
    case P::initialize:
        if(!s.state){if(!services || !services->prerequisites)return pending("Registration requires prerequisite byte storage80561488/489.");s.state=services->prerequisites;}
        if(!s.state->initialized)return pending("Registration requires represented prerequisite initialized byte80561489.");
        if(*s.state->initialized==0){s.state->started=std::uint8_t{0};s.state->initialized=std::uint8_t{1};}
        s.phase=P::test;break;
    case P::test:
        if(!s.state->started)return pending("Registration requires represented prerequisite started byte80561488.");
        if(*s.state->started!=0){s.phase=P::done;return {true,0,{}};}
        s.state->started=std::uint8_t{1};s.phase=P::next;break;
    case P::next:{
        clear();if(s.index==wrappers.size()){s.phase=P::done;return {true,0,{}};}
        IgbRegistrationCallbacksCall call;call.kind=K::invoke_function;call.function=wrappers[s.index];
        s.call.select(std::move(call),services);check();s.phase=P::call;break;
    }
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;++s.index;s.phase=P::next;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbTypeRegistrationArguments igb_file_type_registration_arguments(const std::shared_ptr<IgbFileFactoryServices>& file){
    if(!file)fail("File registration requires the canonical metadata-cell owner.");
    IgbTypeRegistrationArguments args;args.metadata_slot=std::shared_ptr<IgbRegistrationMetaSlot>(file,&file->file_metadata);
    args.name=std::make_shared<const std::string>("igIGBFile");args.flag=0;args.native_size=0x134;
    args.parent_registration=0x80032950;args.parent_getter=0x8002f23c;args.field_callback=0x8002f21c;
    args.layout_probe=0x8002ec9c;args.defaults_callback=0x8002f244;args.trailing_callback=0x8002f724;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{
        0x80030078,0x80034130,0x800344a0,0x8002a6b0,0x80036684,0x8002bf48,0x80029054,0});
    return args;
}
struct IgbFileTypeRegistrationInvocation::Impl {
    enum class P {prerequisites,arguments,registration,done};
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    P phase=P::prerequisites;bool running{},fault{};
};
IgbFileTypeRegistrationInvocation::IgbFileTypeRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbFileTypeRegistrationInvocation::~IgbFileTypeRegistrationInvocation()=default;
bool IgbFileTypeRegistrationInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFileTypeRegistrationInvocation::faulted() const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbFileTypeRegistrationInvocation::result() const{
    if(!completed() || faulted())fail("Incomplete file registration has no returned callback list.");return impl_->result;
}
MessengerCallResult IgbFileTypeRegistrationInvocation::resume(std::shared_ptr<IgbRegistrationCallbacksServices> callbacks,
    std::shared_ptr<IgbFileFactoryServices> file,std::shared_ptr<IgbTypeRegistrationServices> types){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("File registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);const auto check=[&]{if(s.fault)fail("File registration continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto r=s.prerequisites.resume(callbacks);check();if(!r.completed)return r;s.phase=P::arguments;break;}
    case P::arguments:
        if(!file)return pending("File registration requires canonical80561A04 storage.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_file_type_registration_arguments(file));
        s.phase=P::registration;break;
    case P::registration:{
        const auto r=s.registration->resume(types);check();if(!r.completed)return r;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;return {true,0,{}};
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbRegistrationCallbacksFactory make_native_igb_registration_function_factory(
    std::shared_ptr<IgbFileFactoryServices> file,std::shared_ptr<IgbTypeRegistrationServices> types,
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks,IgbRegistrationCallbacksFactory remaining){
    std::weak_ptr<IgbFileFactoryServices> weak_file=file;std::weak_ptr<IgbTypeRegistrationServices> weak_types=types;
    std::weak_ptr<IgbRegistrationCallbacksServices> weak_callbacks=callbacks;
    return [weak_file,weak_types,weak_callbacks,remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        if(call.kind!=K::invoke_function || (call.function!=0x8002f1a0 && registration_body(call.function)==0))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto cb=weak_callbacks.lock();if(!cb)fail("Native registration callback services expired before selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x8002f1a0){
            const auto f=weak_file.lock();const auto t=weak_types.lock();
            if(!f || !t)fail("Native file registration services expired before selection.");
            auto invocation=std::make_shared<IgbFileTypeRegistrationInvocation>();
            operation->resume=[invocation,cb,f,t,out=operation.get()]{const auto r=invocation->resume(cb,f,t);
                if(r.completed)out->callbacks_result=invocation->result();return r;};
        }else{
            auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(registration_body(call.function));
            operation->resume=[invocation,cb]{return invocation->resume(cb);};
        }
        return operation;
    };
}
IgbFileFactoryFactory make_native_igb_file_registration_factory(std::shared_ptr<IgbRegistrationCallbacksServices> callbacks,
    IgbFileFactoryFactory remaining){
    std::weak_ptr<IgbRegistrationCallbacksServices> weak=callbacks;
    return [weak,remaining=std::move(remaining)](const IgbFileFactoryCall& call){
        if(call.kind!=IgbFileFactoryServiceKind::register_file_type)
            return remaining?remaining(call):std::shared_ptr<IgbFileFactoryOperation>{};
        const auto services=weak.lock();if(!services)fail("Native file registration callback services expired before selection.");
        auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x8002f1a0);
        auto operation=std::make_shared<IgbFileFactoryOperation>();operation->resume=[invocation,services]{return invocation->resume(services);};return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_registration_list_factory(std::shared_ptr<IgbRegistrationCallbacksServices> callbacks,
    IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbRegistrationCallbacksServices> weak=callbacks;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::append_callback)
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto services=weak.lock();if(!services)fail("Native registration list services expired before selection.");
        auto invocation=std::make_shared<IgbRegistrationCallbackAppendInvocation>(call.callbacks,call.function);
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[invocation,services]{return invocation->resume(services);};return operation;
    };
}
} // namespace dw4
