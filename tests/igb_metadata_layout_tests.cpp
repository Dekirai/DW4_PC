#include "dw4/igb_metadata_layout.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbMetadataLayoutKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){++checks;try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
auto field(std::uint32_t offset=4,std::uint16_t size=4){auto f=std::make_shared<IgbDecodeRuntimeField>();
    f->vtable_word=0x80476e6c;f->offset=offset;f->size=size;f->ordinal=0xbeef;f->declaring_type=std::weak_ptr<IgbRuntimeMetaType>{};return f;}
auto metadata(const std::vector<std::shared_ptr<IgbDecodeRuntimeField>>& fields={}){
    auto m=std::make_shared<IgbRuntimeMetaType>();m->parent=std::shared_ptr<IgbRuntimeMetaType>{};m->native_size=8;m->allocation_prefix=91;
    m->fields=std::make_shared<IgbDecodeFieldRegistry>();m->fields->count=static_cast<std::int32_t>(fields.size());
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(fields);return m;
}
auto callbacks(std::vector<std::uint32_t> functions){auto list=std::make_shared<IgbRegistrationCallbackList>();
    list->count=static_cast<std::int32_t>(functions.size());list->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(std::move(functions));return list;}
struct Fixture {
    std::shared_ptr<IgbMetadataLayoutServices> services=std::make_shared<IgbMetadataLayoutServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    Fixture(){services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;core->registration_callbacks=callbacks({});}
};
struct Effect {std::function<void()> action;~Effect(){action();}};
void original_layout_and_no_size_publication(){
    Fixture f;auto first=field(),special=field(0x1234,3),inherited=field(12,4),last=field(0x7ffffffe,1);
    special->flag_25=1;special->vtable_word=11;last->vtable_word=22;
    auto parent=metadata({field(),field(),inherited});inherited->ordinal=44;inherited->declaring_type=parent;
    auto child=metadata({first,special,inherited,last});child->parent=parent;child->class_flags=8;
    f.core->registration_callbacks=callbacks({0x80012340});unsigned aligns{},dispatches{};
    f.services->factory=[&](const IgbMetadataLayoutCall& c){auto op=std::make_shared<IgbMetadataLayoutOperation>();op->resume=[&,c]{
        if(c.kind==K::field_alignment){++aligns;check(c.virtual_slot==0x6c && c.metadata==child,"Layout selected a different descriptor virtual/caller.");return MessengerCallResult{true,c.vtable_word==11?8u:0x10010u,{}};}
        ++dispatches;check(c.function==0x80012340 && c.core==f.core && c.metadata==child,"Registered callback lost captured core or metadata argument.");
        check(child->class_flags==12 && child->allocation_prefix==16 && child->native_alignment==16 && child->native_size==8,"Layout stores were missing or native_size was fabricated before callbacks.");
        return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataLayoutInvocation call(child);check(call.resume(f.services).completed && aligns==2 && dispatches==1,"Native layout or registered callbacks did not complete.");
    check(special->offset==0xfffffff8 && special->ordinal==0xbeef && igb_field_declaring_type_is_null(*special->declaring_type),"Special field rounding overwrote an ordinary-field ordinal/owner.");
    check(first->ordinal==0 && first->declaring_type->lock()==child && last->ordinal==2 && last->declaring_type->lock()==child,"Local ordinal did not subtract special fields or publish nonowning declaring metadata.");
    check(inherited->ordinal==44 && inherited->declaring_type->lock()==parent,"Inherited field identity did not preserve the parent's ordinal/owner.");
    check(call.resume().completed && aligns==2 && dispatches==1,"Completed layout replayed callbacks.");
    child.reset();parent.reset();check(first->declaring_type->lock()!=nullptr,"Live invocation lost its captured metadata owner.");
}
void captured_count_live_fields_and_parent(){
    Fixture f;auto old=field(3,5),replacement=field(99,7),unused=field();old->vtable_word=7;
    auto m=metadata({old,unused});auto parent=metadata({field(),replacement});m->parent=parent;
    auto storage=std::make_shared<IgbDecodeFieldRegistry::Storage>(IgbDecodeFieldRegistry::Storage{old,replacement});
    unsigned calls{};
    f.services->factory=[&](const IgbMetadataLayoutCall& c){check(c.field==old,"Next iteration did not reload field-list backing or used a substituted captured field.");++calls;
        auto op=std::make_shared<IgbMetadataLayoutOperation>();auto effect=std::make_shared<Effect>();effect->action=[&]{
            old->flag_25=1;old->size=5;m->fields->backing=storage;m->fields->count=0;
        };op->resume=[effect]{return MessengerCallResult{true,8,{}};};return op;};
    IgbMetadataLayoutInvocation call(m);check(call.resume(f.services).completed && calls==1,"Initial captured field count was replaced by its mutated live count.");
    check(old->offset==0xfffffff8 && replacement->ordinal==0xbeef && igb_field_declaring_type_is_null(*replacement->declaring_type),"Callback teardown mutations or fresh parent identity were not observed.");
    check(unused->ordinal==0xbeef && m->allocation_prefix==8,"Layout visited a stale field backing or lost negative-prefix rounding.");

    Fixture pending;auto captured=field();captured->vtable_word.reset();auto m2=metadata({captured});IgbMetadataLayoutInvocation held(m2);
    check(!held.resume(pending.services).completed,"Unknown vtable was fabricated a known alignment.");
    m2->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(1,field());captured->vtable_word=0x80476e6c;
    check(held.resume(pending.services).completed && captured->ordinal==0 && m2->fields->backing->at(0)->ordinal==0xbeef,"Pending field alignment reselected its receiver.");

    auto unknown_parent=metadata();unknown_parent->parent.reset();IgbMetadataLayoutInvocation initial(unknown_parent);
    check(!initial.resume(pending.services).completed,"Unused initial parent load skipped its represented-state boundary.");
    unknown_parent->fields->count=1;unknown_parent->fields->backing.reset();unknown_parent->parent=std::shared_ptr<IgbRuntimeMetaType>{};
    check(initial.resume(pending.services).completed,"Resuming parent state recaptured the original zero field count.");
}
void arithmetic_and_nonowning_declaring_type(){
    Fixture f;auto normal=field(),special=field(123,7);special->vtable_word=7;special->flag_25=255;
    auto m=metadata({special,normal});m->native_size=0xffffffff;m->allocation_prefix=0x87654321;
    f.services->factory=[](const IgbMetadataLayoutCall&){auto op=std::make_shared<IgbMetadataLayoutOperation>();op->resume=[]{return MessengerCallResult{true,0x10000,{}};};return op;};
    IgbMetadataLayoutInvocation call(m);check(call.resume(f.services).completed && special->offset==0 && m->native_alignment==4 && m->allocation_prefix==0x87654321,"Zero narrowed alignment or native_size sentinel branch was normalized incorrectly.");
    check(normal->ordinal==0 && m->native_size==0xffffffff,"Layout recomputed native size from observed field extent.");
    auto negative=metadata();negative->fields->count=-1;negative->fields->backing.reset();IgbMetadataLayoutInvocation skipped(negative);
    check(skipped.resume(f.services).completed && negative->native_alignment==4 && negative->allocation_prefix==0,"Negative signed field count read nonexistent fields.");
    auto repeated=field();auto large=metadata();large->fields->count=65538;large->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(65538,repeated);
    IgbMetadataLayoutInvocation wrap(large);check(wrap.resume(f.services).completed && repeated->ordinal==1,"Descriptor ordinal did not truncate the wrapped index difference to16 bits.");
    std::weak_ptr<IgbRuntimeMetaType> weak;
    auto retained=field();{auto owner=metadata({retained});weak=owner;IgbMetadataLayoutInvocation linked(owner);check(linked.resume(f.services).completed,"Nonowning owner fixture failed.");}
    check(weak.expired() && retained->declaring_type->expired() && !igb_field_declaring_type_is_null(*retained->declaring_type),"Metadata/field owner binding formed a cycle or lost expired nonnull identity.");
}
void ready_flag_and_callbacks(){
    Fixture f;auto m=metadata();f.services->runtime->singleton.reset();IgbMetadataLayoutInvocation call(m);
    check(!call.resume(f.services).completed && (m->class_flags&4)!=0 && m->allocation_prefix==0,"Unknown ArkCore blocked stores that precede its original read.");
    m->class_flags=2;f.services->runtime->singleton=f.core;
    check(call.resume(f.services).completed && m->class_flags==2,"Resuming ArkCore boundary replayed the ready-flag store.");
    Fixture null;null.services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};auto owner=metadata();IgbMetadataLayoutInvocation bad(owner);
    rejects([&]{bad.resume(null.services);},"Null captured core completed metadata callback dispatch.");check(owner->class_flags==4,"Null core failure rolled back the ready flag.");

    Fixture live;auto captured_core=live.core;auto initial=callbacks({0x80011110,0x80022220});auto second=callbacks({0,0x80033330});second->count=0;
    captured_core->registration_callbacks=initial;auto unrelated=std::make_shared<IgbRuntimeTypeRegistry>();unrelated->registration_callbacks=callbacks({0});
    unsigned selections{},polls{};bool ready{};std::vector<std::uint32_t> functions;
    live.services->factory=[&](const IgbMetadataLayoutCall& c){++selections;functions.push_back(c.function);
        auto op=std::make_shared<IgbMetadataLayoutOperation>();op->resume=[&,c]{++polls;
            check(c.metadata==m && c.core==captured_core,"Callback replaced captured core/metadata from later globals.");
            if(c.function==0x80011110){if(!ready)return MessengerCallResult{false,0,"prepared callback"};
                captured_core->registration_callbacks=second;live.services->runtime->singleton=unrelated;}
            return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataCallbacksInvocation dispatch(captured_core,m);check(!dispatch.resume(live.services).completed && selections==1,"Selected callback did not suspend.");
    (*initial->backing)[0]=0;initial->count=0;ready=true;
    check(dispatch.resume(live.services).completed && selections==2 && polls==3 && functions==std::vector<std::uint32_t>{0x80011110,0x80033330},"Callback dispatch lost captured count/function or fresh list backing.");
    check(!initial->released && !second->released,"Registered notification dispatch invented callback-list cleanup.");
    auto negative_list=callbacks({});negative_list->count=-1;negative_list->backing.reset();captured_core->registration_callbacks=negative_list;
    IgbMetadataCallbacksInvocation negative(captured_core,{});check(negative.resume().completed,"Negative callback count dereferenced backing or metadata argument.");
}
void boundaries_adapters_and_teardown(){
    Fixture f;auto m=metadata();f.core->registration_callbacks=callbacks({0x80012340});IgbMetadataLayoutInvocation missing(m);
    const auto status=missing.resume(f.services);check(!status.completed && status.diagnostic.find("80012340")!=std::string::npos && m->class_flags==4,"Missing callback boundary omitted its function identity or ready-flag prefix.");
    f.core->registration_callbacks=callbacks({});IgbMetadataLayoutInvocation null_meta({});rejects([&]{null_meta.resume(f.services);},"Null metadata bypassed original slot-count dereference.");
    auto null_field=metadata({{}});IgbMetadataLayoutInvocation field_access(null_field);rejects([&]{field_access.resume(f.services);},"Null field bypassed original vtable access.");
    auto bad_vtable=field();bad_vtable->vtable_word=0;auto malformed=metadata({bad_vtable});IgbMetadataLayoutInvocation vtable(malformed);rejects([&]{vtable.resume(f.services);},"Null vtable became an unknown alignment service.");
    auto no_functions=callbacks({0});f.core->registration_callbacks=no_functions;IgbMetadataCallbacksInvocation null_function(f.core,m);rejects([&]{null_function.resume();},"Null callback function was delegated.");
    f.core->registration_callbacks=std::shared_ptr<IgbRegistrationCallbackList>{};IgbMetadataCallbacksInvocation null_list(f.core,m);rejects([&]{null_list.resume();},"Null callback list skipped its count read.");
    f.core->registration_callbacks.reset();IgbMetadataCallbacksInvocation unbound(f.core,m);check(!unbound.resume().completed,"Unknown callback pointer was fabricated empty.");
    f.core->registration_callbacks=callbacks({});check(unbound.resume().completed,"Late empty callback list could not resume.");
    auto adapter=make_native_igb_metadata_layout_factory(f.services);IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::compute_layout;request.metadata=m;
    auto op=adapter(request);check(op && op->resume().completed,"Registration adapter did not execute native layout.");request.kind=IgbTypeRegistrationServiceKind::post_layout;check(!adapter(request),"Layout adapter fabricated unrelated post-layout completion.");
    auto recursive_field=field();recursive_field->vtable_word=9;auto recursive_meta=metadata({recursive_field});IgbMetadataLayoutInvocation recursive(recursive_meta);
    f.services->factory=[&](const IgbMetadataLayoutCall&){auto op=std::make_shared<IgbMetadataLayoutOperation>();op->resume=[&]{try{recursive.resume(f.services);}catch(const std::runtime_error&){}return MessengerCallResult{true,4,{}};};return op;};
    rejects([&]{recursive.resume(f.services);},"Swallowed recursive alignment failure completed the outer layout.");

    Fixture teardown;auto original=callbacks({0x80011110,0x80022220});auto replacement=callbacks({0,0x80033330});replacement->count=0;teardown.core->registration_callbacks=original;
    std::vector<std::uint32_t> visited;
    teardown.services->factory=[&](const IgbMetadataLayoutCall& c){visited.push_back(c.function);auto op=std::make_shared<IgbMetadataLayoutOperation>();
        auto effect=std::make_shared<Effect>();effect->action=[&,c]{if(c.function==0x80011110)teardown.core->registration_callbacks=replacement;};
        op->resume=[effect]{return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataCallbacksInvocation clear_first(teardown.core,m);check(clear_first.resume(teardown.services).completed && visited==std::vector<std::uint32_t>{0x80011110,0x80033330},"Completed callback teardown followed the next original backing load.");
}
}
int main(){try{original_layout_and_no_size_publication();captured_count_live_fields_and_parent();arithmetic_and_nonowning_declaring_type();ready_flag_and_callbacks();boundaries_adapters_and_teardown();
    std::cout<<"Metadata layout checks: "<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"Metadata layout failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
