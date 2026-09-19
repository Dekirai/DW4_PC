#include "dw4/igb_registration_callbacks.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbTypeRegistrationServiceKind;using C=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
SystemStageString text(std::string value){return std::make_shared<const std::string>(std::move(value));}
struct Fixture {
    std::shared_ptr<IgbRuntimeMetaType> meta=std::make_shared<IgbRuntimeMetaType>(),parent=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbTypeRegistrationServices> services=std::make_shared<IgbTypeRegistrationServices>();
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRegistrationCallbackList> inherited;
    IgbTypeRegistrationArguments args;
    std::vector<IgbTypeRegistrationCall> trace;
    std::vector<IgbRegistrationCallbacksCall> backing_trace;
    std::function<void(const IgbTypeRegistrationCall&,IgbTypeRegistrationOperation&,MessengerCallResult&)> after;
    std::function<void(const IgbTypeRegistrationCall&,std::shared_ptr<IgbTypeRegistrationOperation>&)> select;
    std::function<void(const IgbRegistrationCallbacksCall&,std::shared_ptr<IgbRegistrationCallbacksOperation>&)> select_backing;
    Fixture(){
        meta->reference_word=2;meta->post_layout_callback=0;core->registration_filter_enabled=0;core->registration_callback_pool=pool;
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;services->metadata_pool_handle=7;
        callbacks->runtime=services->runtime;
        args.metadata_slot=std::make_shared<IgbRegistrationMetaSlot>(meta);args.name=text("PreparedType");args.native_size=0x134;
        args.parent_registration=10;args.parent_getter=11;args.field_callback=12;args.layout_probe=13;args.defaults_callback=14;args.trailing_callback=15;
        args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{16,17,0});
        callbacks->factory=[this](const auto& call){auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
            operation->resume=[this,call,out=operation.get()]{backing_trace.push_back(call);
                if(call.kind!=C::allocate_backing && call.kind!=C::reallocate_backing)throw std::runtime_error("Unexpected callback-list service in registrar fixture.");
                if(call.bytes>1024*1024)throw std::runtime_error("Prepared backing allocation exceeds its bound.");
                auto storage=call.backing?std::make_shared<IgbRegistrationCallbackList::Storage>(*call.backing):std::make_shared<IgbRegistrationCallbackList::Storage>();
                storage->resize(call.bytes/4);out->backing_result=storage;return MessengerCallResult{true,0,{}};};
            if(select_backing)select_backing(call,operation);return operation;};
        // Only the callback list append/constructor is concrete here. Parent,
        // field, name, metadata allocation and registry bodies are prepared.
        services->factory=make_native_igb_registration_list_factory(callbacks,[this](const auto& call){
            auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[this,call,out=operation.get()]{
                trace.push_back(call);MessengerCallResult result{true,0,{}};
                switch(call.kind){
                case K::pool_for_handle:out->object_result=pool;break;
                case K::create_metadata:out->metadata_result=meta;break;
                case K::register_parent:out->callbacks_result=inherited;break;
                case K::get_parent:out->metadata_result=parent;break;
                case K::set_name:call.metadata->name=call.name;break;
                case K::layout_probe:result.value=0x804724c4;break;
                case K::compute_layout:call.metadata->class_flags|=4;break;
                case K::create_callbacks:out->callbacks_result=std::make_shared<IgbRegistrationCallbackList>();break;
                case K::append_callback:throw std::runtime_error("Native append adapter did not handle registration callback.");
                default:break;
                }
                if(after)after(call,*out,result);return result;};
            if(select)select(call,operation);return operation;});
    }
    IgbTypeRegistrationInvocation call(){return IgbTypeRegistrationInvocation(args);}
    std::size_t count(K kind)const{return static_cast<std::size_t>(std::count_if(trace.begin(),trace.end(),[&](const auto& call){return call.kind==kind;}));}
};
void flags_and_order(){
    for(std::uint32_t flags=0;flags<16;++flags){Fixture f;f.meta->class_flags=flags;auto call=f.call();
        check(call.resume(f.services).completed,"Registrar flag corpus did not complete.");
        check(f.trace.front().kind==K::pool_for_handle,"Registrar skipped initial pool lookup.");
        const bool busy=(flags&8)!=0,finalized=(flags&4)!=0,first=(flags&2)==0;
        const auto expected_flags=busy?flags:finalized?flags&~8u:first?(flags|7u)&~8u:(flags|4u)&~8u;
        check(f.meta->class_flags==expected_flags,"Busy/finalized flags were not preserved.");
        if(busy || finalized){check(f.trace.size()==1 && !call.result(),"Busy/finalized metadata executed registration work.");continue;}
        check(f.count(K::register_parent)==(first && (flags&1)==0?1u:0u),"Parent-linked flag did not gate registration.");
        check(f.count(K::defaults)==(first?1u:0u) && f.count(K::compute_layout)==1,"First-registration and layout paths differ.");
        const auto list=call.result();check(list && list->count==(first?3:2) && list->backing->at(0)==16 && list->backing->at(1)==17,"Callback array or first-only tail was lost.");
        if(first){check(f.meta->name==f.args.name && f.meta->native_size==0x134 && f.meta->field_callback==12 && f.meta->type_word==0x804724c4,"Registration header arguments were not published.");
            check(list->backing->at(2)==15 && f.meta->reference_word==1,"Trailing callback or metadata release order changed.");}
    }
    Fixture f;f.inherited=std::make_shared<IgbRegistrationCallbackList>();f.inherited->capacity=8;f.inherited->count=1;f.inherited->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(8);f.inherited->backing->at(0)=99;
    auto call=f.call();check(call.resume(f.services).completed && call.result()==f.inherited && f.inherited->count==4,"Parent callback list was replaced or reset.");
    check(f.count(K::pool_for_handle)==1 && f.count(K::create_callbacks)==0,"Existing parent callback list triggered allocation.");
    const std::vector<K> expected{K::pool_for_handle,K::register_parent,K::get_parent,K::attach_parent,K::set_name,K::layout_probe,K::defaults,K::publish_type,K::compute_layout};
    std::vector<K> actual;for(const auto& item:f.trace)actual.push_back(item.kind);check(actual==expected,"Parent/name/default/registry/layout ordering changed.");
}
void creation_and_live_cache(){
    Fixture f;*f.args.metadata_slot=std::shared_ptr<IgbRuntimeMetaType>{};f.meta->class_flags=4;
    f.after=[&](const auto& c,auto&,auto&){if(c.kind==K::create_metadata)check(c.pool==f.pool && c.allocation_label && *c.allocation_label=="igMetaObject: PreparedType","Metadata allocation lost its pool or reached stack label.");};
    auto created=f.call();check(created.resume(f.services).completed && **f.args.metadata_slot==f.meta && f.count(K::create_metadata)==1,"Null cache did not publish the allocator result.");
    Fixture live;auto replacement=std::make_shared<IgbRuntimeMetaType>();replacement->class_flags=8;
    live.after=[&](const auto& c,auto&,auto&){if(c.kind==K::pool_for_handle)*live.args.metadata_slot=replacement;};
    auto selected=live.call();check(selected.resume(live.services).completed && live.trace.size()==1 && live.meta->class_flags==0,"Cache was read before pool callback completed.");
    Fixture unknown;unknown.args.metadata_slot->reset();auto late=unknown.call();check(!late.resume(unknown.services).completed && unknown.trace.size()==1,"Unknown cache was treated as null.");
    *unknown.args.metadata_slot=unknown.meta;check(late.resume(unknown.services).completed && unknown.count(K::pool_for_handle)==2,"Late cache replayed the first pool lookup.");
    Fixture failed;*failed.args.metadata_slot=std::shared_ptr<IgbRuntimeMetaType>{};failed.after=[](const auto& c,auto& out,auto&){if(c.kind==K::create_metadata)out.metadata_result.reset();};
    auto null=failed.call();rejects([&]{null.resume(failed.services);},"Null metadata allocator did not reach flags dereference.");
    check(failed.args.metadata_slot->has_value() && !**failed.args.metadata_slot,"Null allocator result was not published before failure.");
}
void flags_width_and_release(){
    for(std::uint32_t flag:{0u,1u,2u,255u,256u,257u}){Fixture f;f.args.flag=flag;auto call=f.call();
        check(call.resume(f.services).completed && f.meta->flag_1a==static_cast<std::uint8_t>(flag),"Abstract flag store did not truncate to byte.");
        check(f.count(K::layout_probe)==((flag&255u)==0?1u:0u),"Layout probe did not test low input byte.");}
    for(std::uint32_t references:{0u,1u,2u,0x00800001u,0xff800001u,0xffffffffu}){Fixture f;f.meta->reference_word=references;auto call=f.call();
        check(call.resume(f.services).completed && f.meta->reference_word==references-1u,"Metadata release did not wrap full32.");
        check(f.count(K::destroy_metadata)==(((references-1u)&0x7fffffu)==0?1u:0u),"Final metadata destruction did not use low23.");}
    Fixture disabled;disabled.core->registration_filter_pool.reset();disabled.core->registration_filter_names.reset();auto call=disabled.call();
    check(call.resume(disabled.services).completed,"Disabled filter read its unrepresented pool/name state.");
}
void filters_and_live_callbacks(){
    Fixture f;f.core->registration_filter_enabled=-1;f.core->registration_filter_pool=f.pool;auto names=std::make_shared<ModelSceneData>();f.core->registration_filter_names=names;
    auto fresh=std::make_shared<IgbRuntimeTypeRegistry>();auto replacement_pool=std::make_shared<ModelSceneData>();fresh->registration_filter_pool=replacement_pool;
    f.after=[&](const auto& c,auto&,auto& result){if(c.kind==K::find_filter_name){check(c.object==names && c.name==f.args.name,"Filter lookup lost captured names/name.");f.services->runtime->singleton=fresh;result.value=0xfffffffe;}
        if(c.kind==K::configure_filter)check(c.pool==replacement_pool,"Filter configuration did not reload ArkCore pool after lookup.");
        if(c.kind==K::compute_layout)f.meta->post_layout_callback=123;
        if(c.kind==K::post_layout)check(c.function==123 && (c.metadata->class_flags&4)!=0,"Live post-layout callback was selected before layout publication.");};
    f.inherited=std::make_shared<IgbRegistrationCallbackList>();f.inherited->capacity=8;f.inherited->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(8);
    auto call=f.call();check(call.resume(f.services).completed && f.count(K::configure_filter)==1 && f.count(K::post_layout)==1,"Matched filter or layout callback was skipped.");
    Fixture absent;absent.core->registration_filter_enabled=1;absent.core->registration_filter_pool=absent.pool;absent.core->registration_filter_names=names;
    absent.after=[](const auto& c,auto&,auto& result){if(c.kind==K::find_filter_name)result.value=0xffffffff;};
    auto miss=absent.call();check(miss.resume(absent.services).completed && absent.count(K::configure_filter)==0,"Missing-name -1 selected filter configuration.");
}
void pending_ownership_and_live_array(){
    Fixture f;bool ready{};unsigned selected{},resumes{};
    f.select_backing=[&](const auto& c,auto& out){if(c.kind==C::allocate_backing){++selected;out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(c.bytes/4);
        out->resume=[&]{++resumes;return MessengerCallResult{ready,0,"Prepared backing allocation."};};}};
    auto call=f.call();check(!call.resume(f.services).completed && (f.meta->class_flags&8)!=0,"Pending append did not retain busy registration state.");
    f.args.extra_callbacks->at(0)=99;f.args.extra_callbacks->at(1)=88;ready=true;
    check(call.resume(f.services).completed && selected==1 && resumes==2,"Callback append restarted its selected allocation.");
    check(call.result()->backing->at(0)==16 && call.result()->backing->at(1)==88,"Append did not capture current function and reload next array entry.");
    Fixture parent;bool parent_ready{};unsigned parent_selected{},parent_resumes{};
    parent.select=[&](const auto& c,auto& out){if(c.kind==K::register_parent){++parent_selected;out->resume=[&]{++parent_resumes;return MessengerCallResult{parent_ready,0,"Prepared parent."};};}};
    auto pending=parent.call();check(!pending.resume(parent.services).completed,"Parent callback did not suspend.");
    auto nested=parent.call();check(nested.resume(parent.services).completed && !nested.result(),"Nested registration did not use original busy fast path.");
    parent_ready=true;check(pending.resume(parent.services).completed && parent_selected==1 && parent_resumes==2,"Pending parent was reselected or restarted.");
}
void constructor_and_errors(){
    Fixture initialized;initialized.after=[](const auto& c,auto& out,auto&){if(c.kind==K::create_callbacks){out.callbacks_result->count=99;out.callbacks_result->capacity=99;out.callbacks_result->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(99,66);}};
    auto real_constructor=initialized.call();check(real_constructor.resume(initialized.services).completed && real_constructor.result()->count==3 && real_constructor.result()->backing->at(0)==16,"Callback allocator output skipped actual zeroing constructor.");
    Fixture null;null.args.extra_callbacks.reset();null.args.trailing_callback=0;null.after=[](const auto& c,auto& out,auto&){if(c.kind==K::create_callbacks)out.callbacks_result.reset();};
    auto no_append=null.call();check(no_append.resume(null.services).completed && !no_append.result() && null.meta->class_flags==7,"Null list allocation without appends should finish registration.");
    Fixture bad;bad.args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{1});auto unterminated=bad.call();
    rejects([&]{unterminated.resume(bad.services);},"Unterminated callback array silently completed.");check(unterminated.faulted() && (bad.meta->class_flags&8)!=0,"Callback failure rolled back busy state.");
    Fixture recursive;auto recursion=recursive.call();recursive.after=[&](const auto&,auto&,auto&){rejects([&]{recursion.resume(recursive.services);},"Same invocation recursed.");};
    rejects([&]{recursion.resume(recursive.services);},"Caught recursive error did not invalidate outer registrar.");
    Fixture abandoned;auto list=std::make_shared<IgbRegistrationCallbackList>();std::weak_ptr<IgbRegistrationCallbackList> weak=list;abandoned.inherited=list;list.reset();
    abandoned.select=[](const auto& c,auto& out){if(c.kind==K::get_parent)out->resume=[] {return MessengerCallResult{false,0,"Prepared missing parent."};};};
    {auto pending=abandoned.call();check(!pending.resume(abandoned.services).completed,"Prepared parent getter should remain pending.");abandoned.inherited.reset();}
    check(!weak.expired() && (abandoned.meta->class_flags&8)!=0,"Abandoned registration lost deferred callbacks or reset original flags.");
    Fixture missing;missing.services->metadata_pool_handle.reset();auto before=missing.call();check(!before.resume(missing.services).completed && missing.trace.empty(),"Missing pool global reached the metadata cache.");
    missing.services->metadata_pool_handle=0;check(before.resume(missing.services).completed,"Late represented zero pool handle could not resume.");
}
}
int main(){try{flags_and_order();creation_and_live_cache();flags_width_and_release();filters_and_live_callbacks();pending_ownership_and_live_array();constructor_and_errors();
    std::cout<<"IGB type registration: "<<checks<<" checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"IGB type registration after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
