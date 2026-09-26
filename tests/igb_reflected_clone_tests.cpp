#include "dw4/igb_reflected_clone.hpp"
#include "dw4/igb_field_init.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using K=IgbReflectedCloneServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
void put(const IgbObjectInstance& object,std::uint32_t relative,std::uint32_t value){
    const auto bytes=object.storage->bytes;const auto at=object.offset+relative;
    for(unsigned n=0;n<4;++n){bytes->bytes.at(at+n)=static_cast<std::uint8_t>(value>>(24u-8u*n));bytes->initialized.at(at+n)=1;}
}
IgbObjectInstance object(std::uint32_t address,std::uint32_t offset=0){
    IgbObjectInstance result;result.storage=std::make_shared<IgbObjectStorage>();result.offset=offset;
    result.storage->original_address=address;result.storage->bytes=std::make_shared<IgbScratchBuffer>();
    result.storage->bytes->bytes.assign(offset+0x50,0xcc);result.storage->bytes->initialized.assign(offset+0x50,1);
    result.field=std::make_shared<IgbDecodeRuntimeField>();result.field->physical_storage=IgbFieldStorageBinding{result.storage,offset};
    result.storage->bindings.push_back({offset,{},{},{},{},result.field});
    result.field->vtable_word=0x80473f24;result.field->reference_word=0x7a800005;
    put(result,0,0x80473f24);put(result,4,0x7a800005);return result;
}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::int32_t count=0){
    auto result=std::make_shared<IgbRuntimeMetaType>();result->fields=std::make_shared<IgbDecodeFieldRegistry>();
    result->fields->count=count;result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    result->allocation_prefix=0;result->factory_redirect=0;result->native_size=0x34;result->type_word=0x80473f24;
    result->instance_count=0;result->instances=std::shared_ptr<IgbObjectInstanceList>{};result->class_flags=4;
    return result;
}
std::shared_ptr<IgbDecodeRuntimeField> descriptor(std::uint8_t policy=0){
    auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=0x12345678;
    field->flag_27=policy;field->flag_28=policy;field->flag_29=policy;return field;
}
// Reflected results and member virtual bodies are explicit prepared leaves.
// The tests below qualify original caller order, not those leaf implementations.
struct Fixture {
    IgbObjectInstance source=object(0x100000,16),destination=object(0x200000,8);
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(),base=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbReflectedCloneServices> services=std::make_shared<IgbReflectedCloneServices>();
    std::vector<IgbReflectedCloneCall> calls;
    std::function<MessengerCallResult(const IgbReflectedCloneCall&,IgbReflectedCloneOperation&)> action;
    std::function<void(const IgbReflectedCloneCall&,std::shared_ptr<IgbReflectedCloneOperation>&)> select;
    Fixture(){services->object_metadata=base;services->factory=[this](const IgbReflectedCloneCall& c){
        calls.push_back(c);auto op=std::make_shared<IgbReflectedCloneOperation>();
        op->resume=[this,c,out=op.get()]{
            if(c.kind==K::object_virtual && c.virtual_slot==0x58)out->metadata_result=meta;
            if(c.kind==K::tagged_pool)out->pool_result=pool;
            if(c.kind==K::reflected_factory)out->object_result=destination;
            if(action)return action(c,*out);return MessengerCallResult{true,0,{}};
        };if(select)select(c,op);return op;
    };}
};
void all_signed_policies(){
    for(const auto offset:{0x27u,0x28u,0x29u})for(unsigned raw=0;raw<256;++raw){
        auto services=std::make_shared<IgbReflectedCloneServices>();auto field=descriptor();
        const auto byte=static_cast<std::uint8_t>(raw);
        if(offset==0x27)field->flag_27=byte;else if(offset==0x28)field->flag_28=byte;else field->flag_29=byte;
        unsigned calls{};services->factory=[&](const IgbReflectedCloneCall& c){++calls;
            check(c.kind==K::field_virtual && c.field==field && c.virtual_slot==0x90+4*raw && c.vtable_word==0x12345678,
                "Signed policy selected the wrong member virtual or descriptor.");
            check(!c.destination.storage && !c.source.storage,"Member dispatch manufactured null object arguments.");
            auto op=std::make_shared<IgbReflectedCloneOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;};
        if(raw>=5)field->vtable_word.reset();
        IgbFieldCopyDispatchInvocation call(field,{},{},offset);
        check(call.resume(services).completed && calls==(raw<5?1u:0u),"Signed policy did not skip all unsupported bytes without a vtable read.");
        check(call.resume().completed && calls==(raw<5?1u:0u),"Completed policy dispatch replayed its member operation.");
    }
    auto field=descriptor();field->flag_28.reset();IgbFieldCopyDispatchInvocation late(field,{},{});
    check(!late.resume().completed,"Unknown copy policy fabricated a zero mode.");field->flag_28=std::uint8_t{255};
    check(late.resume().completed,"Late signed-negative policy required unnecessary services.");
}
void modes_and_original_order(){
    for(const auto mode:{0u,1u,2u}){
        Fixture f;f.base->fields->count=1;f.meta->fields->count=3;
        auto skipped=descriptor();skipped->flag_27.reset();skipped->flag_28.reset();skipped->flag_29.reset();
        auto first=descriptor(2),second=descriptor(4);f.meta->fields->backing->assign({skipped,first,second});
        IgbObjectCopyInvocation call(f.destination,f.source,mode);
        check(call.resume(f.services).completed,"Recognized object-copy pass did not finish.");
        std::vector<std::uint32_t> expected{0x58};if(mode==1){expected.push_back(0x48);expected.push_back(0x48);}
        expected.push_back(0x98);expected.push_back(0xa0);if(mode!=2)expected.push_back(mode==0?0x18:0x14);
        check(f.calls.size()==expected.size(),"Copy mode selected another number of hooks or fields.");
        for(std::size_t n=0;n<expected.size();++n)check(f.calls[n].virtual_slot==expected[n],"Original copy callback order changed.");
        for(const auto& c:f.calls)if(c.kind==K::field_virtual)check(c.destination.field==f.destination.field && c.source.field==f.source.field,
            "Member virtual lost the captured object pair.");
        if(mode==1)check(f.calls[1].object.field==f.destination.field && f.calls[2].object.field==f.source.field,
            "Deep copy reversed destination/source pre-copy hooks.");
        check(call.resume().completed && f.calls.size()==expected.size(),"Completed object copy replayed callbacks.");
    }
    for(const auto mode:{3u,4u,255u,0x7fffffffu,0x80000000u,0xffffffffu}){
        IgbObjectCopyInvocation call({},{},mode);check(call.resume().completed && !call.faulted(),"Unsupported signed copy mode touched an object or service.");
    }
}
void live_hooks_registry_and_backing(){
    Fixture f;auto selected=metadata(3),displaced=metadata(1);f.meta=selected;f.base->fields->count=1;
    auto first=descriptor(0),second=descriptor(1),replacement=descriptor(3);
    auto fields=std::make_shared<IgbDecodeFieldRegistry>();fields->count=3;
    fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();fields->backing->assign({descriptor(255),first,second});
    unsigned members{};
    f.action=[&](const auto& c,auto&)->MessengerCallResult{
        if(c.kind==K::object_virtual && c.virtual_slot==0x58){f.meta=displaced;put(f.destination,0,0x11111111);}
        if(c.kind==K::object_virtual && c.virtual_slot==0x48 && c.object.field==f.destination.field){
            check(c.vtable_word==0x11111111,"Destination hook reused the metadata getter's physical vtable.");put(f.source,0,0x22222222);
        }
        if(c.kind==K::object_virtual && c.virtual_slot==0x48 && c.object.field==f.source.field){
            check(c.vtable_word==0x22222222,"Source hook did not reload its physical vtable.");selected->fields=fields;
        }
        if(c.kind==K::field_virtual){++members;
            if(members==1){check(c.field==first,"Copy captured fields before the two pre-copy hooks.");
                selected->fields=displaced->fields;fields->count=0;f.base->fields->count=0;
                fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();fields->backing->assign({descriptor(255),descriptor(255),replacement});
                put(f.destination,0,0x33333333);
            }else check(c.field==replacement && c.virtual_slot==0x9c,"Copy did not reload backing while preserving its captured registry/count.");
        }
        if(c.kind==K::object_virtual && c.virtual_slot==0x14)check(c.vtable_word==0x33333333,"Final hook reused a stale physical vtable.");
        return {true,0,{}};
    };
    IgbObjectCopyInvocation call(f.destination,f.source,1);check(call.resume(f.services).completed && members==2,
        "Live registry/backing mutations changed the captured deep-copy loop range.");
    for(const auto mode:{0u,2u}){
        Fixture empty;empty.meta->fields->count=-1;empty.meta->fields->backing.reset();
        IgbObjectCopyInvocation negative(empty.destination,empty.source,mode);
        check(negative.resume(empty.services).completed && empty.calls.size()==(mode==0?2u:1u),"Negative captured count became an unsigned field traversal.");
    }
    Fixture wrap;wrap.base->fields->count=std::numeric_limits<std::int32_t>::min();
    wrap.meta->fields->count=std::numeric_limits<std::int32_t>::min()+1;wrap.meta->fields->backing->push_back(descriptor(0));
    IgbObjectCopyInvocation shifted(wrap.destination,wrap.source,2);
    check(shifted.resume(wrap.services).completed && wrap.calls.size()==2 && wrap.calls.back().kind==K::field_virtual,
        "Original slwi did not wrap the signed starting index to physical slot zero.");
}
void retained_operations_and_teardown(){
    Fixture f;f.meta->fields->count=1;const auto field=descriptor(2);f.meta->fields->backing->push_back(field);
    unsigned polls{},selected{};bool ready{};
    f.select=[&](const auto& c,auto& op){if(c.kind==K::field_virtual){++selected;op->resume=[&]{++polls;return MessengerCallResult{ready,0,"Prepared member copy"};};}};
    IgbObjectCopyInvocation call(f.destination,f.source,1);check(!call.resume(f.services).completed && polls==1 && selected==1,"Member copy did not retain its pending operation.");
    field->flag_28=std::uint8_t{4};field->vtable_word=0x87654321;f.meta->fields->backing.reset();ready=true;
    const auto original_factory=f.services->factory;f.services->factory={};
    check(!call.resume(f.services).completed && polls==2 && selected==1,"Pending member operation was reselected or the missing tail was completed.");
    f.services->factory=original_factory;check(call.resume(f.services).completed && polls==2 && selected==1,"Late tail binding replayed the completed member copy.");
    Fixture teardown;bool destroyed{};
    teardown.select=[&](const auto& c,auto& op){if(c.kind==K::object_virtual && c.virtual_slot==0x58){
        auto token=std::shared_ptr<int>(new int(0),[&](int* value){delete value;destroyed=true;put(teardown.destination,0,0x10203040);});
        op->metadata_result=teardown.meta;op->resume=[token]{return MessengerCallResult{true,0,{}};};
    }};
    IgbObjectCopyInvocation next(teardown.destination,teardown.source,1);
    check(next.resume(teardown.services).completed && destroyed && teardown.calls[1].vtable_word==0x10203040,
        "Next object virtual was selected before completed metadata operation teardown.");
    Fixture captured;captured.services->object_metadata.reset();captured.meta->fields->count=1;
    auto selected_field=descriptor(0);captured.meta->fields->backing->push_back(selected_field);
    IgbObjectCopyInvocation missing_base(captured.destination,captured.source,2);
    check(!missing_base.resume(captured.services).completed && captured.calls.size()==1,"Missing base metadata did not pause after destination metadata capture.");
    auto second=descriptor(3);captured.meta->fields->count=2;captured.meta->fields->backing->push_back(second);
    captured.meta->fields=metadata()->fields;captured.services->object_metadata=captured.base;
    check(missing_base.resume(captured.services).completed && captured.calls.size()==3 && captured.calls[1].field==selected_field && captured.calls[2].field==second,
        "Late base-global binding froze the later count read or replaced the captured registry.");
}
void physical_access_and_faults(){
    for(unsigned defect=0;defect<4;++defect){
        Fixture f;if(defect==0)f.destination.storage->bytes->released=true;
        if(defect==1)f.destination.storage->bytes->initialized[f.destination.offset]=0;
        if(defect==2)f.destination.storage->bytes->bytes.resize(f.destination.offset+3);
        if(defect==3)f.destination.storage->bindings.front().field=std::make_shared<IgbDecodeRuntimeField>();
        IgbObjectCopyInvocation call(f.destination,f.source,1);rejects([&]{call.resume(f.services);},"Invalid physical destination selected its metadata virtual.");
        check(call.faulted() && f.calls.empty(),"Physical access failure changed the callback prefix or lost fault state.");
        rejects([&]{call.resume(f.services);},"Faulted object copy could be resumed.");
    }
    auto owner=object(0x300000);auto services=std::make_shared<IgbReflectedCloneServices>();unsigned calls{};
    services->factory=[&](const auto&){++calls;return std::shared_ptr<IgbReflectedCloneOperation>{};};
    owner.field->flag_28=std::uint8_t{5};owner.storage->bytes->released=true;
    IgbFieldCopyDispatchInvocation skipped(owner.field,{},{});check(skipped.resume(services).completed && calls==0,
        "Unsupported canonical policy dereferenced an unused physical vtable.");
    owner.field->flag_28=std::uint8_t{0};IgbFieldCopyDispatchInvocation reached(owner.field,{},{});
    rejects([&]{reached.resume(services);},"Selected member virtual accepted released physical storage.");check(calls==0,"Invalid physical descriptor reached a virtual factory.");
    Fixture callback;callback.action=[](const auto&,auto&)->MessengerCallResult{throw std::runtime_error("Prepared callback failure");};
    IgbObjectCopyInvocation failed(callback.destination,callback.source,2);rejects([&]{failed.resume(callback.services);},"Member caller swallowed its callback failure.");
    check(failed.faulted() && callback.calls.size()==1,"Callback fault replayed the metadata prefix.");
    Fixture recursive;IgbObjectCopyInvocation reentered(recursive.destination,recursive.source,2);
    recursive.action=[&](const auto&,auto&)->MessengerCallResult{rejects([&]{reentered.resume(recursive.services);},"Recursive copy entry succeeded.");return {true,0,{}};};
    rejects([&]{reentered.resume(recursive.services);},"Caught recursive fault did not invalidate the outer copy.");check(reentered.faulted(),"Recursive copy failure was not retained.");
    Fixture null_metadata;null_metadata.meta.reset();IgbObjectCopyInvocation null(null_metadata.destination,null_metadata.source,2);
    rejects([&]{null.resume(null_metadata.services);},"Null returned metadata silently became an empty schema.");
    Fixture bad_backing;bad_backing.meta->fields->count=1;IgbObjectCopyInvocation unavailable(bad_backing.destination,bad_backing.source,2);
    rejects([&]{unavailable.resume(bad_backing.services);},"Out-of-range physical descriptor fabricated a member.");
}
void outer_reflection_and_null_results(){
    Fixture f;const auto selected=f.meta;auto later=metadata();bool pool_ready{};unsigned pool_polls{};
    f.select=[&](const auto& c,auto& op){
        if(c.kind==K::object_virtual && c.virtual_slot==0x58 && c.object.field==f.source.field){
            auto token=std::shared_ptr<int>(new int(0),[&](int* p){delete p;put(f.source,4,0xab000001);f.meta=later;});
            op->metadata_result=selected;op->resume=[token]{return MessengerCallResult{true,0,{}};};
        }
        if(c.kind==K::tagged_pool){check(c.handle==0xab,"Source pool tag was captured before metadata callback teardown.");
            op->pool_result=f.pool;op->resume=[&]{++pool_polls;return MessengerCallResult{pool_ready,0,"Prepared tagged pool"};};}
        if(c.kind==K::reflected_factory)check(c.metadata==selected && c.pool==f.pool,"Reflected allocation lost captured metadata/pool.");
    };
    IgbReflectedCloneInvocation call(f.source,2);rejects([&]{call.result_instance();},"Incomplete reflected clone exposed a result.");
    check(!call.resume(f.services).completed && pool_polls==1,"Outer clone did not suspend at its tagged pool query.");
    put(f.source,4,0xcd000001);pool_ready=true;
    check(call.resume(f.services).completed && pool_polls==2 && call.result_instance().field==f.destination.field,"Outer clone reloaded the selected pool query or lost the allocated result.");
    check(f.calls.size()==4 && f.calls[0].kind==K::object_virtual && f.calls[1].kind==K::tagged_pool &&
        f.calls[2].kind==K::reflected_factory && f.calls[3].object.field==f.destination.field,
        "680B4 did not execute metadata, tagged pool, allocation and destination copy in order.");
    check(call.resume().completed && f.calls.size()==4,"Completed outer clone replayed allocation.");
    for(const auto mode:{0u,1u,2u,3u,0xffffffffu}){
        Fixture null;null.destination={};IgbReflectedCloneInvocation clone(null.source,mode);
        if(mode<=2){rejects([&]{clone.resume(null.services);},"Recognized copy mode silently skipped a null reflected result.");
            check(clone.faulted() && null.calls.size()==3,"Null result dereference changed the outer allocation prefix.");}
        else check(clone.resume(null.services).completed && !clone.result_instance().storage && null.calls.size()==3,
            "Unsupported copy mode accessed the null reflected result.");
    }
    Fixture byte_tag;byte_tag.source.storage->bytes->initialized[byte_tag.source.offset+5]=0;
    byte_tag.source.storage->bytes->initialized[byte_tag.source.offset+6]=0;byte_tag.source.storage->bytes->initialized[byte_tag.source.offset+7]=0;
    IgbReflectedCloneInvocation byte_read(byte_tag.source,3);
    check(byte_read.resume(byte_tag.services).completed && byte_tag.calls[1].handle==0x7a,
        "Original lbz pool query read uninitialized lower packed-reference bytes.");
    Fixture abandoned;abandoned.meta->fields->count=1;abandoned.meta->fields->backing->push_back(descriptor(0));
    abandoned.select=[](const auto& c,auto& op){if(c.kind==K::field_virtual)op->resume=[] {return MessengerCallResult{false,0,"Prepared pending member"};};};
    {IgbReflectedCloneInvocation incomplete(abandoned.source,2);check(!incomplete.resume(abandoned.services).completed,"Abandonment fixture did not reach allocated pending copy.");}
    check(!abandoned.services->unfinished.empty() && abandoned.services->unfinished.back().field==abandoned.destination.field,
        "Abandoned reflected copy dropped ownership of the allocated destination.");
}
void actual_reflected_factory_composition(){
    Fixture f;f.destination=object(0x400000);f.destination.storage->bytes->initialized.assign(0x50,0);
    f.services->reflected=std::make_shared<IgbFileFactoryServices>();auto initializer=std::make_shared<IgbObjectInitServices>();
    f.services->reflected->initializer=initializer;initializer->object_metadata=f.base;
    initializer->runtime=std::make_shared<IgbRuntimeTypesServices>();auto core=std::make_shared<IgbRuntimeTypeRegistry>();
    core->enabled=1;core->field_initialization_enabled=1;core->vtable_offset=0;initializer->runtime->singleton=core;
    unsigned allocations{},initialization_calls{};
    initializer->factory=[&](const IgbObjectInitCall& c){++initialization_calls;auto op=std::make_shared<IgbObjectInitOperation>();
        op->resume=[&,c,out=op.get()]{if(c.kind==IgbObjectInitServiceKind::object_virtual && c.virtual_slot==0x58)out->metadata_result=f.meta;
            return MessengerCallResult{true,0,{}};};return op;};
    f.services->reflected->factory=[&](const IgbFileFactoryCall& c){
        check(c.kind==IgbFileFactoryServiceKind::allocate && c.metadata==f.meta && c.pool==f.pool && c.bytes==0x34,
            "Actual reflected factory changed the captured pool or allocation size.");++allocations;
        auto op=std::make_shared<IgbFileFactoryOperation>();op->allocation_result=f.destination.storage;op->resume=[] {return MessengerCallResult{true,0,{}};};return op;};
    f.action=[](const auto& c,auto&)->MessengerCallResult{if(c.kind==K::reflected_factory)throw std::runtime_error("Actual reflected service was bypassed");return {true,0,{}};};
    IgbReflectedCloneInvocation call(f.source,1);check(call.resume(f.services).completed,"680B4 did not compose actual6546C and667F4.");
    check(call.result_instance().field==f.destination.field && allocations==1 && initialization_calls>0 && f.meta->instance_count==1,
        "Reflected composition returned raw storage before initializer completion.");
    check(igb_buffer_word(f.destination.storage->bytes,0)==0x80473f24 && igb_buffer_word(f.destination.storage->bytes,4)==1 &&
        f.destination.field->construction_active_.expired() && !f.destination.field->construction_fault_,
        "Actual reflected construction lost physical/typed header or construction ownership.");
}
void adapters(){
    Fixture f;unsigned delegated{};const auto factory=make_native_igb_reflected_field_clone_factory(f.services,[&](const auto&){++delegated;return std::shared_ptr<IgbFieldCloneOperation>{};});
    IgbFieldCloneCall request;request.kind=IgbFieldCloneServiceKind::reflected_clone;request.source=f.source.field;request.argument=2;request.function=0x800680b4;
    const auto op=factory(request);check(op && op->resume().completed && op->allocation_result.storage==f.destination.storage && op->allocation_result.offset==f.destination.offset,
        "Field-clone adapter lost actual reflected allocation identity.");
    request.kind=IgbFieldCloneServiceKind::register_type;check(!factory(request) && delegated==1,"Reflected adapter consumed class registration.");
    request.kind=IgbFieldCloneServiceKind::reflected_clone;request.function=0x80068060;
    check(!factory(request) && delegated==2,"Reflected adapter consumed a different original entry.");request.function=0x800680b4;
    Fixture late;const auto physical=late.source.field->physical_storage;late.source.field->physical_storage.reset();
    auto late_request=request;late_request.source=late.source.field;const auto late_adapter=make_native_igb_reflected_field_clone_factory(late.services);
    const auto waiting=late_adapter(late_request);check(waiting && !waiting->resume().completed && late.calls.empty(),
        "Missing source binding fabricated a physical object or completed reflection.");
    late.source.field->physical_storage=physical;check(waiting->resume().completed && waiting->allocation_result.storage==late.destination.storage,
        "Late physical source binding could not resume the same reflected operation.");
    auto temporary=std::make_shared<IgbReflectedCloneServices>();std::weak_ptr<IgbReflectedCloneServices> weak=temporary;
    const auto expired=make_native_igb_reflected_field_clone_factory(temporary);temporary.reset();check(weak.expired(),"Reflected adapter created a permanent services ownership cycle.");
    request.kind=IgbFieldCloneServiceKind::reflected_clone;rejects([&]{expired(request);},"Expired reflected services manufactured a clone.");
    unsigned fallback{};auto initializer=std::make_shared<IgbObjectInitServices>();
    initializer->factory=[&](const IgbObjectInitCall& c){check(c.kind==IgbObjectInitServiceKind::object_virtual && c.virtual_slot==0x58,"Metadata adapter delegated an unverified object virtual.");
        auto result=std::make_shared<IgbObjectInitOperation>();result->metadata_result=f.meta;result->resume=[] {return MessengerCallResult{true,0,{}};};return result;};
    const auto virtuals=make_native_igb_reflected_clone_virtual_factory(initializer,[&](const auto&){++fallback;return std::shared_ptr<IgbReflectedCloneOperation>{};});
    for(const auto vtable:{0x80473f24u,0x80473e30u,0x8047693cu,0x80476088u,0x80470a44u})for(const auto slot:{0x48u,0x14u,0x18u}){
        IgbReflectedCloneCall c;c.kind=K::object_virtual;c.object=f.source;c.vtable_word=vtable;c.virtual_slot=slot;
        const auto leaf=virtuals(c);check(leaf && leaf->resume().completed,"Verified scalar/string BLR hook remained unbound.");
    }
    IgbReflectedCloneCall c;c.kind=K::object_virtual;c.object=f.source;c.vtable_word=0x80473f24;c.virtual_slot=0x58;
    const auto lookup=virtuals(c);check(lookup && lookup->resume().completed && lookup->metadata_result==f.meta,"Metadata adapter lost the actual getter result.");
    c.vtable_word=0x12345678;c.virtual_slot=0x48;check(!virtuals(c) && fallback==1,"Unknown class teardown was assumed to be a BLR.");
}
void strict_field_result(){
    for(bool wrong_kind:{false,true}){
        Fixture f;const auto original=f.destination.field;
        f.select=[&](const auto& c,auto& op){if(c.kind!=K::reflected_factory)return;
            op->object_result=f.destination;
            op->resume=[&,out=op.get()]{
                if(wrong_kind){out->object_result.field.reset();out->object_result.metadata=f.meta;
                    f.destination.storage->bindings.front().field.reset();f.destination.storage->bindings.front().metadata=f.meta;}
                else f.destination.storage->bindings.front().field=std::make_shared<IgbDecodeRuntimeField>();
                return MessengerCallResult{true,0,{}};
            };
        };
        const auto factory=make_native_igb_reflected_field_clone_factory(f.services);
        IgbFieldCloneCall request;request.kind=IgbFieldCloneServiceKind::reflected_clone;request.function=0x800680b4;
        request.source=f.source.field;request.argument=3;const auto operation=factory(request);
        rejects([&]{operation->resume();},"Field adapter exported a replaced or non-field reflected identity.");
        check(!operation->allocation_result.storage,"Failed field result was published to the clone wrapper.");
        f.destination.storage->bindings.front().field=original;f.destination.storage->bindings.front().metadata.reset();
        rejects([&]{operation->resume();},"Faulted field adapter recovered by rebinding an allocation.");
    }
}
void member_adapter_profiles(){
    unsigned fallback{};auto services=std::make_shared<IgbFieldMemberCopyServices>();
    const auto factory=make_native_igb_reflected_member_copy_factory(services,[&](const auto&){++fallback;return std::shared_ptr<IgbReflectedCloneOperation>{};});
    for(const auto table:{0x80475b60u,0x80476848u,0x80477054u,0x80473f24u,0x80473e30u,0x80476630u,0x80470a44u}){
        for(const auto slot:{0x90u,0x94u,0x98u}){
            auto source=object(0x500000),destination=object(0x600000);auto field=descriptor();field->offset=0x2d;field->size=2;
            source.storage->bytes->bytes[0x2d]=0x12;source.storage->bytes->bytes[0x2e]=0x34;
            IgbReflectedCloneCall c;c.kind=K::field_virtual;c.field=field;c.source=source;c.destination=destination;c.vtable_word=table;c.virtual_slot=slot;
            const auto op=factory(c);check(op && op->resume().completed && destination.storage->bytes->bytes[0x2d]==0x12 &&
                destination.storage->bytes->bytes[0x2e]==0x34 && destination.storage->bytes->bytes[0x2c]==0xcc && destination.storage->bytes->bytes[0x2f]==0xcc,
                "Known scalar member adapter did not execute the exact two-byte raw copy.");
        }
    }
    for(const auto table:{0x80475b60u,0x80473f24u,0x80473e30u,0x8047693cu,0x804762fcu,0x80476088u,0x80470a44u}){
        IgbReflectedCloneCall c;c.kind=K::field_virtual;c.vtable_word=table;c.virtual_slot=0xa0;
        const auto op=factory(c);check(op && op->resume().completed,"Verified no-copy BLR read absent fields or objects.");
    }
    IgbReflectedCloneCall c;c.kind=K::field_virtual;c.vtable_word=0x12345678;c.virtual_slot=0x90;
    check(!factory(c) && fallback==1,"Unknown member table was assumed to be a raw scalar copy.");
    c.vtable_word=0x80473f24;c.virtual_slot=0x91;check(!factory(c) && fallback==2,"Unaligned member virtual slot was accepted.");
    c.virtual_slot=0x8c;check(!factory(c) && fallback==3,"Member adapter consumed an unrelated field operation.");
    c.kind=K::object_virtual;c.virtual_slot=0x90;check(!factory(c) && fallback==4,"Member adapter consumed an object virtual.");
    const auto unmapped=make_native_igb_reflected_member_copy_factory(services);c.kind=K::field_virtual;c.vtable_word=0x80476088;c.virtual_slot=0x90;c.field=descriptor();
    const auto memory=unmapped(c);check(memory && !memory->resume().completed,"Memory deep-copy prerequisite fabricated a completed member operation.");
    unsigned prepared{};services->factory=[&](const IgbFieldMemberCopyCall& call){++prepared;check(call.function==0x80062824,"Memory adapter selected the wrong original member entry.");
        auto op=std::make_shared<IgbFieldMemberCopyOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;};
    check(memory->resume().completed && prepared==1,"Unbound member prerequisite could not acquire its explicit late endpoint.");
}
void concrete_field_copy_hooks(){
    for(const auto table:{0x80476088u,0x80470a44u,0x80477054u}){
        auto init_services=std::make_shared<IgbFieldInitServices>();
        init_services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
        init_services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        const auto kind=table==0x80476088?IgbMetaFieldKind::memory_reference:
            table==0x80477054?IgbMetaFieldKind::unsigned_short:IgbMetaFieldKind::unsigned_byte;
        auto cell=std::make_shared<IgbRegistrationMetaSlot>();
        init_services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(kind)]=cell;
        auto initializer=std::make_shared<IgbObjectInitServices>();
        initializer->factory=make_native_igb_field_init_factory(init_services);
        unsigned delegated{};
        const auto native_hooks=make_native_igb_reflected_clone_virtual_factory(initializer,[&](const auto&){
            ++delegated;return std::shared_ptr<IgbReflectedCloneOperation>{};});
        IgbReflectedCloneCall request;request.kind=K::object_virtual;request.vtable_word=table;request.virtual_slot=0x58;
        const auto getter=native_hooks(request);
        check(getter && !getter->resume().completed && delegated==0,"New class getter fabricated an unrepresented metadata cell.");
        auto actual=metadata();*cell=actual;
        check(getter->resume().completed && getter->metadata_result==actual,"Retained plain metadata getter missed its late canonical binding.");
        *cell=std::shared_ptr<IgbRuntimeMetaType>{};const auto null_getter=native_hooks(request);
        check(null_getter && null_getter->resume().completed && !null_getter->metadata_result,
            "Plain metadata getter registered a class instead of returning represented null.");
        // Actual BLRs do not read their receiver or require an initializer.
        const auto leaves=make_native_igb_reflected_clone_virtual_factory({});
        for(const auto slot:{0x48u,0x14u,0x18u}){request.virtual_slot=slot;const auto leaf=leaves(request);
            check(leaf && leaf->resume().completed && leaf->resume().completed,"Verified new class BLR required a receiver or replayed work.");}
        request.virtual_slot=0x4c;check(!native_hooks(request) && delegated==1,"Adjacent unverified object virtual was assumed to be a BLR.");
        request.virtual_slot=0x48;request.kind=K::field_virtual;
        check(!native_hooks(request) && delegated==2,"Object hook adapter consumed a member virtual.");
        for(const auto mode:{0u,1u,2u}){
            auto source=object(0x500000),destination=object(0x600000);put(source,0,table);put(destination,0,table);
            source.field->vtable_word=destination.field->vtable_word=table;
            auto member=descriptor(0);member->vtable_word=0x80470a44;member->offset=0x26;member->size=1;
            actual=metadata(1);actual->fields->backing->push_back(member);*cell=actual;
            source.storage->bytes->bytes[0x26]=0x51;source.field->flag_26=0x51;
            auto members=std::make_shared<IgbFieldMemberCopyServices>();
            const auto native=make_native_igb_reflected_member_copy_factory(members,native_hooks);
            auto services=std::make_shared<IgbReflectedCloneServices>();services->object_metadata=metadata();
            std::vector<std::uint32_t> slots;
            services->factory=[&](const IgbReflectedCloneCall& c){slots.push_back(c.virtual_slot);return native(c);};
            IgbObjectCopyInvocation copy(destination,source,mode);
            check(copy.resume(services).completed && destination.storage->bytes->bytes[0x26]==0x51 && destination.field->flag_26==0x51 &&
                destination.storage->bytes->bytes[0x25]==0xcc && destination.storage->bytes->bytes[0x27]==0xcc,
                "New object hook profile did not compose actual unsigned-byte member copying.");
            const std::vector<std::uint32_t> expected=mode==1?std::vector<std::uint32_t>{0x58,0x48,0x48,0x90,0x14}:
                mode==0?std::vector<std::uint32_t>{0x58,0x90,0x18}:std::vector<std::uint32_t>{0x58,0x90};
            check(slots==expected && delegated==2,"New class hooks changed original mode-specific dispatch order.");
            check(copy.resume().completed && slots==expected,"Completed class copy replayed its hooks or member.");
        }
    }
}
}
int main(){try{all_signed_policies();modes_and_original_order();live_hooks_registry_and_backing();retained_operations_and_teardown();
    physical_access_and_faults();outer_reflection_and_null_results();actual_reflected_factory_composition();adapters();strict_field_result();member_adapter_profiles();
    concrete_field_copy_hooks();
    std::cout<<"IGB reflected clone tests passed: "<<checks<<" checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
