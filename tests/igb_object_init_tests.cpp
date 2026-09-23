#include "dw4/igb_object_init.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using K=IgbObjectInitServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
void put(const std::shared_ptr<IgbScratchBuffer>& bytes,std::uint32_t offset,std::uint32_t value){
    for(unsigned n=0;n<4;++n){bytes->bytes.at(offset+n)=static_cast<std::uint8_t>(value>>(24-8*n));bytes->initialized.at(offset+n)=1;}
}
std::shared_ptr<IgbRuntimeMetaType> metadata(unsigned fields=0){
    auto result=std::make_shared<IgbRuntimeMetaType>();result->allocation_prefix=0;result->type_word=0x804724c4;
    result->instance_count=0;result->instances=std::shared_ptr<IgbObjectInstanceList>{};result->class_flags=4;
    result->fields=std::make_shared<IgbDecodeFieldRegistry>();result->fields->count=static_cast<std::int32_t>(fields);
    result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    for(unsigned n=0;n<fields;++n){auto field=std::make_shared<IgbDecodeRuntimeField>();field->offset=n;field->default_mode=0;result->fields->backing->push_back(field);}
    return result;
}
// Prepared descriptors and virtual bodies test the actual initializer's
// ordering. They do not claim that igIGBFile field defaults are implemented.
struct Fixture {
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(),base=metadata();
    std::shared_ptr<IgbObjectStorage> storage=std::make_shared<IgbObjectStorage>();
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbObjectInitServices> services=std::make_shared<IgbObjectInitServices>();
    std::shared_ptr<ModelSceneObject> pool;
    std::vector<IgbObjectInitCall> trace;
    std::function<void(const IgbObjectInitCall&,IgbObjectInitOperation&,MessengerCallResult&)> after;
    std::function<void(const IgbObjectInitCall&,std::shared_ptr<IgbObjectInitOperation>&)> select;
    Fixture(){
        storage->original_address=0x10000000;storage->bytes=std::make_shared<IgbScratchBuffer>();
        storage->bytes->bytes.resize(0x400,0xcd);storage->bytes->initialized.resize(0x400);storage->bindings.push_back({0,file});
        core->vtable_offset=0;core->field_initialization_enabled=1;core->enabled=1;
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;services->object_metadata=base;
        services->factory=[this](const IgbObjectInitCall& call){
            auto operation=std::make_shared<IgbObjectInitOperation>();operation->resume=[this,call,out=operation.get()]{
                trace.push_back(call);MessengerCallResult result{true,0,{}};
                if(call.kind==K::object_virtual && call.virtual_slot==0x58)out->metadata_result=meta;
                if(call.kind==K::allocation_pool)out->pool_result=pool;
                if(after)after(call,*out,result);return result;
            };
            if(select)select(call,operation);return operation;
        };
    }
    IgbObjectInitInvocation call(){return {storage,0,meta};}
};
std::vector<std::uint32_t> object_slots(const Fixture& f){
    std::vector<std::uint32_t> result;for(const auto& call:f.trace)if(call.kind==K::object_virtual)result.push_back(call.virtual_slot);return result;
}
std::vector<std::uint32_t> field_slots(const Fixture& f){
    std::vector<std::uint32_t> result;for(const auto& call:f.trace)if(call.kind==K::field_virtual)result.push_back(call.virtual_slot);return result;
}
void headers_and_empty_ranges(){
    Fixture f;auto call=f.call();check(call.resume(f.services).completed,"Prepared empty-range initialization did not complete.");
    check(call.result()==f.file && !call.faulted() && !f.storage->faulted(),"Initialized layout lost its canonical file binding.");
    check(igb_buffer_word(f.storage->bytes,0)==0x804724c4 && igb_buffer_word(f.storage->bytes,4)==1 && f.file->reference_word==1,"Header stores changed byte order or refword.");
    check(object_slots(f)==std::vector<std::uint32_t>{0x58,0x58,0x58,0x0c,0x58,0x34,0x2c,0x24,0x58},"Fresh metadata/default virtual order changed.");
    check(field_slots(f).empty() && f.meta->instance_count==1,"Empty field ranges skipped the instance-count tail.");
    for(const auto& item:f.trace)if(item.kind==K::object_virtual){check(item.vtable_word==0x804724c4,"Virtual lookup did not read the physical type word.");
        if(item.virtual_slot==0x0c || item.virtual_slot==0x34)check(item.argument==1,"Field-pass completion lost its true argument.");}
    check(!f.file->igb_decode,"Prepared empty metadata manufactured a decoder.");
    const auto count=f.trace.size();check(call.resume({}).completed && f.trace.size()==count,"Completed initializer replayed original effects.");
    Fixture moved;put(moved.storage->bytes,0,0x11223344);moved.core->vtable_offset=12;auto offset=moved.call();
    check(offset.resume(moved.services).completed && igb_buffer_word(moved.storage->bytes,12)==0x804724c4,"Dynamic ArkCore type-word offset was ignored.");
    check(moved.trace.front().vtable_word==0x11223344,"Virtual call incorrectly used metadata type word instead of object vtable.");
}
void gates_and_default_modes(){
    for(unsigned flag:{0u,4u})for(unsigned enabled:{0u,1u})for(unsigned gate:{0u,1u}){
        Fixture f;f.meta=metadata(1);f.meta->class_flags=flag;f.core->enabled=static_cast<std::uint8_t>(enabled);f.core->field_initialization_enabled=static_cast<std::uint8_t>(gate);
        auto call=f.call();check(call.resume(f.services).completed,"First-pass gate corpus did not complete.");
        const bool skip=flag==0 && enabled==0 && gate!=0;
        const auto slots=field_slots(f);check(slots==(skip?std::vector<std::uint32_t>{0x7c,0xf0,0x70}:std::vector<std::uint32_t>{0x84,0x7c,0xf0,0x70}),"First-pass gate suppressed another pass or used the wrong condition.");
    }
    for(unsigned mode:{0u,1u,2u,3u,4u,127u,128u,255u})for(unsigned f0:{0u,1u,0x100u,0x101u}){
        Fixture f;f.meta=metadata(1);f.meta->fields->backing->front()->default_mode=static_cast<std::uint8_t>(mode);
        f.after=[&](const auto& c,auto&,auto& result){if(c.kind==K::field_virtual && c.virtual_slot==0xf0)result.value=f0;};
        auto call=f.call();check(call.resume(f.services).completed,"Signed default-mode corpus did not complete.");
        std::vector<std::uint32_t> expected{0x84,0x7c,0xf0};if((f0&255u)==0 && (mode==0 || mode==2 || mode==3))expected.push_back(mode==0?0x70:mode==2?0x78:0x74);
        check(field_slots(f)==expected,"F0 low-byte test or signed default-mode dispatch differs.");
    }
    Fixture missing;missing.meta=metadata(1);missing.meta->fields->backing->front()->default_mode.reset();auto call=missing.call();
    check(!call.resume(missing.services).completed && field_slots(missing).back()==0xf0,"Unrepresented mode did not pause after F0.");
    missing.meta->fields->backing->front()->default_mode=3;
    check(call.resume(missing.services).completed && field_slots(missing)==std::vector<std::uint32_t>{0x84,0x7c,0xf0,0x74},"Late field mode replayed F0.");
}
void captures_and_fresh_reads(){
    Fixture f;f.meta=metadata(3);f.base->fields->count=1;const auto captured=f.meta->fields;const auto replacement=metadata(3)->fields;
    const auto first=(*captured->backing)[1],second=(*replacement->backing)[2];
    f.after=[&](const auto& c,auto&,auto&){if(c.kind==K::field_virtual && c.virtual_slot==0x84 && c.field==first){
        captured->count=1;f.base->fields->count=0;captured->backing=replacement->backing;f.meta->fields=metadata()->fields;
    }};
    auto call=f.call();check(call.resume(f.services).completed,"Live backing/captured count fixture failed.");
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> seen;for(const auto& c:f.trace)if(c.kind==K::field_virtual)seen.push_back(c.field);
    check(seen==std::vector<std::shared_ptr<IgbDecodeRuntimeField>>{first,second},"First pass did not capture list/count while reloading physical backing.");
    Fixture fresh;auto final=metadata();final->instance_count=41;unsigned queries{};
    fresh.after=[&](const auto& c,auto& out,auto&){if(c.kind==K::object_virtual && c.virtual_slot==0x58 && ++queries==5)out.metadata_result=final;};
    auto tail=fresh.call();check(tail.resume(fresh.services).completed && final->instance_count==42 && fresh.meta->instance_count==0,"Final count used a prior metadata result.");
    Fixture mode;mode.meta=metadata(1);const auto field=mode.meta->fields->backing->front();
    mode.after=[&](const auto& c,auto&,auto&){if(c.kind==K::field_virtual && c.virtual_slot==0xf0){field->default_mode=2;mode.meta->fields->backing=metadata(1)->fields->backing;}};
    auto dispatch=mode.call();check(dispatch.resume(mode.services).completed,"F0 field capture fixture failed.");
    const auto writer=std::find_if(mode.trace.begin(),mode.trace.end(),[](const auto& c){return c.kind==K::field_virtual && c.virtual_slot==0x78;});
    check(writer!=mode.trace.end() && writer->field==field,"Default writer reloaded descriptor or captured mode before F0.");
}
void physical_ranges_and_nulls(){
    Fixture skip;skip.base->fields->count=4;skip.meta->fields->count=-1;skip.meta->fields->backing.reset();auto empty=skip.call();
    check(empty.resume(skip.services).completed && field_slots(skip).empty(),"Signed empty range accessed backing.");
    Fixture negative;negative.base->fields->count=-1;negative.meta=metadata(1);auto bad=negative.call();
    rejects([&]{bad.resume(negative.services);},"Negative physical field index was not rejected.");check(bad.faulted() && negative.storage->faulted(),"Field-range failure was not retained.");
    Fixture null;null.meta->class_flags=0;null.core->enabled=0;unsigned queries{};
    null.after=[&](const auto& c,auto& out,auto&){if(c.kind==K::object_virtual && c.virtual_slot==0x58 && ++queries==2)out.metadata_result.reset();};
    auto gated=null.call();check(gated.resume(null.services).completed,"Null metadata in skipped first pass was dereferenced.");
    Fixture no_gate;queries=0;no_gate.core->enabled=1;no_gate.after=[&](const auto& c,auto& out,auto&){if(c.kind==K::object_virtual && c.virtual_slot==0x58 && ++queries==2)out.metadata_result.reset();};
    auto dereference=no_gate.call();rejects([&]{dereference.resume(no_gate.services);},"Null metadata in executed first pass silently succeeded.");
    Fixture extent;extent.core->vtable_offset=0x3ff;auto short_write=extent.call();rejects([&]{short_write.resume(extent.services);},"Type-word write outside represented storage was accepted.");
    check(std::all_of(extent.storage->bytes->initialized.begin(),extent.storage->bytes->initialized.end(),[](auto n){return n==0;}),"Rejected word write partially published bytes.");
}
void pool_and_instance_tail(){
    for(bool physical:{false,true}){
        Fixture mutation;mutation.pool=std::make_shared<ModelSceneData>();
        mutation.after=[&](const auto& c,auto&,auto& result){if(c.kind==K::pool_index){
            if(physical)put(c.object.storage->bytes,c.object.offset+4,0x11234567);
            else igb_object_set_reference_word(c.object,0x11234567);
            check(igb_object_reference_word(c.object)==0x11234567,"Pool-index callback could not update the physical reference header.");result.value=0xaa;
        }};
        auto call=mutation.call();check(call.resume(mutation.services).completed && mutation.file->reference_word==0xaa234567 &&
            igb_buffer_word(mutation.storage->bytes,4)==0xaa234567,"Pool-byte store lost a callback's physical low24 update or left a stale typed reference.");
    }
    Fixture f;f.pool=std::make_shared<ModelSceneData>();f.meta->allocation_prefix=0x10000010;
    auto list=std::make_shared<IgbObjectInstanceList>();f.meta->instances=list;f.meta->instance_count=std::numeric_limits<std::uint32_t>::max();bool ready{};unsigned selects{},resumes{};
    f.after=[&](const auto& c,auto&,auto& result){if(c.kind==K::allocation_pool)check(c.address==0xfffffff0u,"Allocation-base subtraction failed 32-bit wrapping.");
        if(c.kind==K::pool_index){check(c.pool==f.pool,"Pool index used a fresh owner.");result.value=0x123456ab;}};
    f.select=[&](const auto& c,auto& op){if(c.kind==K::register_instance){++selects;check(c.instances==list && c.metadata==f.meta && c.object.file==f.file,"Instance-list append lost its captured owners.");
        op->resume=[&]{++resumes;return MessengerCallResult{ready,0,"Prepared list append."};};}};
    auto call=f.call();check(!call.resume(f.services).completed && f.meta->instance_count==0,"Wrapping instance count was deferred until append completion.");
    check(f.file->reference_word==0xab000001 && igb_buffer_word(f.storage->bytes,4)==0xab000001,"Pool index was not truncated to the top reference byte.");
    f.meta->instances=std::shared_ptr<IgbObjectInstanceList>{};ready=true;
    check(call.resume(f.services).completed && selects==1 && resumes==2 && f.meta->instance_count==0,"Pending append reselected owner or repeated count increment.");
    Fixture unknown;unknown.meta->instances.reset();auto late=unknown.call();check(!late.resume(unknown.services).completed && unknown.meta->instance_count==1,"Unknown instance-list field invented null.");
    unknown.meta->instances=std::shared_ptr<IgbObjectInstanceList>{};check(late.resume(unknown.services).completed && unknown.meta->instance_count==1,"Late instance-list state repeated increment.");
}
void pending_and_teardown(){
    Fixture f;f.meta=metadata(1);bool ready{};unsigned selected{},resumes{};
    f.select=[&](const auto& c,auto& op){if(c.kind==K::field_virtual && c.virtual_slot==0x84){++selected;op->resume=[&]{++resumes;return MessengerCallResult{ready,0,"Prepared field body."};};}};
    auto call=f.call();check(!call.resume(f.services).completed,"Prepared field body did not suspend.");
    f.services->factory={};ready=true;check(!call.resume(f.services).completed && selected==1 && resumes==2,"Pending field callback was replaced instead of retained.");
    rejects([&]{call.result();},"Pending initializer exposed its file.");
    Fixture teardown;bool destroyed{};const auto expected=0x01020304u;
    teardown.select=[&](const auto& c,auto& op){if(c.kind==K::object_virtual && c.virtual_slot==0x34){
        auto token=std::shared_ptr<int>(new int(0),[&](int* value){delete value;destroyed=true;put(teardown.storage->bytes,0,expected);});
        op->resume=[token]{return MessengerCallResult{true,0,{}};};
    }};
    auto next=teardown.call();check(next.resume(teardown.services).completed && destroyed,"Completed default callback was retained into the next call.");
    const auto found=std::find_if(teardown.trace.begin(),teardown.trace.end(),[](const auto& c){return c.kind==K::object_virtual && c.virtual_slot==0x2c;});
    check(found!=teardown.trace.end() && found->vtable_word==expected,"Next virtual loaded its vtable before releasing the completed callback.");
    Fixture missing;missing.services->runtime->singleton.reset();auto late=missing.call();check(!late.resume(missing.services).completed && missing.trace.empty(),"Missing ArkCore global fabricated initialization.");
    missing.services->runtime->singleton=missing.core;check(late.resume(missing.services).completed,"Late ArkCore binding could not resume.");
}
void faults_and_abandonment(){
    Fixture abandoned;abandoned.services->factory={};{auto call=abandoned.call();check(!call.resume(abandoned.services).completed,"Missing virtual body fabricated success.");}
    check(abandoned.storage->faulted(),"Abandoned initialization left allocation reusable.");
    auto retry=abandoned.call();rejects([&]{retry.resume(abandoned.services);},"Faulted allocation accepted a new initializer.");
    Fixture recursive;auto call=recursive.call();recursive.after=[&](const auto&,auto&,auto&){rejects([&]{call.resume(recursive.services);},"Recursive entry succeeded.");};
    rejects([&]{call.resume(recursive.services);},"Caught recursive fault did not invalidate outer continuation.");
    check(call.faulted() && recursive.storage->faulted(),"Recursive initialization did not retain failure state.");
    Fixture unbound;unbound.storage->bindings.clear();auto late=unbound.call();check(!late.resume(unbound.services).completed,"Unknown native layout binding was inferred.");
    unbound.storage->bindings.push_back({0,unbound.file});check(late.resume(unbound.services).completed,"Late native layout binding could not resume.");
    Fixture null_core;null_core.services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};null_core.meta->type_word.reset();auto order=null_core.call();
    check(!order.resume(null_core.services).completed,"Captured null ArkCore was dereferenced before the missing metadata+5C read.");
    null_core.meta->type_word=0;rejects([&]{order.resume(null_core.services);},"Represented null ArkCore did not fail at +394.");
}
void field_layout_and_lifetime(){
    // Prepared field virtuals exercise the same original initializer and its
    // physical/typed boundary without supplying a completed descriptor.
    for(const auto offset:{0u,16u}){
        Fixture f;const auto field=std::make_shared<IgbDecodeRuntimeField>();
        f.storage->bindings={{offset,{},{},{},{},field},{offset,{},{},{},{},field},{offset}};
        f.meta->type_word=0x804762fc;f.meta->allocation_prefix=offset;f.pool=std::make_shared<ModelSceneData>();
        f.file->reference_word=0xdeadbeef;
        f.after=[&](const auto& c,auto&,auto& status){
            check(c.object.field==field && !c.object.file && c.object.offset==offset,"Field initializer selected a file or another adjusted owner.");
            if(c.kind==K::allocation_pool)check(c.address==f.storage->original_address,"Field allocation-base helper lost its adjusted offset.");
            if(c.kind==K::pool_index){igb_object_set_reference_word(c.object,0x11234567);status.value=0xaa;}
            if(c.kind==K::object_virtual && c.virtual_slot==0x34)put(f.storage->bytes,offset,0x12345678);
        };
        IgbObjectInitInvocation call(f.storage,offset,f.meta);
        check(call.resume(f.services).completed,"Prepared reflected field initialization did not complete.");
        const auto result=call.result_instance();
        check(result.storage==f.storage && result.offset==offset && result.field==field && !result.file,"Typed field result lost its allocation or canonical owner.");
        check(field->reference_word==0xaa234567 && igb_buffer_word(f.storage->bytes,offset+4)==0xaa234567 &&
            field->vtable_word==0x12345678,"Field reference or fresh physical vtable publication differs.");
        check(field->physical_storage && field->physical_storage->storage.lock()==f.storage && field->physical_storage->offset==offset &&
            field->construction_active_.expired() && !field->construction_fault_,"Completed field retained a construction token or lost its physical binding.");
        check(f.file->reference_word==0xdeadbeef && (offset==0 || f.storage->bytes->bytes.front()==0xcd),"Field initialization changed an unrelated file or allocation prefix.");
        rejects([&]{call.result();},"A completed field was exposed as a null file.");
        check(!call.faulted() && call.result_instance().field==field,"Strict file result rejection invalidated the constructed field.");
    }
    Fixture late;late.storage->bindings.clear();auto field=std::make_shared<IgbDecodeRuntimeField>();auto call=late.call();
    check(!call.resume(late.services).completed && !late.storage->faulted(),"Absent field binding did not suspend before construction.");
    late.storage->bindings.push_back({0,{},{},{},{},field});const auto virtuals=late.services->factory;late.services->factory={};
    check(!call.resume(late.services).completed && !field->construction_active_.expired() && field->vtable_word==0x804724c4 && field->reference_word==1,
        "Late field binding did not enter and retain actual header initialization.");
    rejects([&]{call.result_instance();},"Pending field initialization exposed a constructed result.");
    auto competing=late.call();rejects([&]{competing.resume(late.services);},"A second initializer acquired active field storage.");
    check(!late.storage->faulted() && !field->construction_fault_,"Rejected competing initializer invalidated the existing owner.");
    late.services->factory=virtuals;check(call.resume(late.services).completed && call.result_instance().field==field,"Retained field initialization could not resume.");

    Fixture abandoned;auto unfinished=std::make_shared<IgbDecodeRuntimeField>();abandoned.storage->bindings={{0,{},{},{},{},unfinished}};abandoned.services->factory={};
    {auto pending=abandoned.call();check(!pending.resume(abandoned.services).completed,"Field abandonment fixture did not suspend.");}
    check(abandoned.storage->faulted() && unfinished->construction_fault_ && unfinished->construction_active_.expired(),"Abandoned field construction did not retain fault and release its token.");

    for(unsigned kind=0;kind<4;++kind){
        Fixture conflict;auto first=std::make_shared<IgbDecodeRuntimeField>();conflict.storage->bindings={{0,{},{},{},{},first}};
        if(kind==0)conflict.storage->bindings.push_back({0,conflict.file});
        if(kind==1)conflict.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        if(kind==2)conflict.storage->bindings.push_back({0,{},conflict.meta});
        if(kind==3)conflict.storage->bindings.front().file=conflict.file;
        auto invalid=conflict.call();rejects([&]{invalid.resume(conflict.services);},"Conflicting native field layout was accepted.");
        check(!first->construction_fault_ && std::all_of(conflict.storage->bytes->initialized.begin(),conflict.storage->bytes->initialized.end(),[](auto b){return b==0;}),
            "Rejected field binding published bytes or invalidated an unowned field.");
    }
    Fixture alias;auto elsewhere=std::make_shared<IgbDecodeRuntimeField>();elsewhere->physical_storage=IgbFieldStorageBinding{alias.storage,4};
    alias.storage->bindings={{0,{},{},{},{},elsewhere}};auto invalid=alias.call();
    rejects([&]{invalid.resume(alias.services);},"One canonical field was rebound to another physical offset.");
    for(unsigned mutation=0;mutation<3;++mutation){
        Fixture captured,other;auto owner=std::make_shared<IgbDecodeRuntimeField>();
        captured.storage->bindings={{16,{},{},{},{},owner}};
        const auto factory=captured.services->factory;captured.services->factory={};
        IgbObjectInitInvocation pending(captured.storage,16,captured.meta);
        check(!pending.resume(captured.services).completed && owner->physical_storage &&
            owner->physical_storage->storage.lock()==captured.storage && owner->physical_storage->offset==16,
            "Physical field mutation fixture did not suspend with its captured allocation binding.");
        const auto before=captured.storage->bytes->bytes,other_before=other.storage->bytes->bytes;
        if(mutation==0)owner->physical_storage.reset();
        if(mutation==1)owner->physical_storage=IgbFieldStorageBinding{captured.storage,20};
        if(mutation==2)owner->physical_storage=IgbFieldStorageBinding{other.storage,16};
        captured.services->factory=factory;
        rejects([&]{pending.resume(captured.services);},"Pending initializer accepted a removed or redirected physical field binding.");
        check(pending.faulted() && captured.storage->faulted() && owner->construction_fault_ &&
            captured.storage->bytes->bytes==before && other.storage->bytes->bytes==other_before && !other.storage->faulted(),
            "Physical field rebinding wrote additional bytes or faulted an unrelated allocation.");
        rejects([&]{pending.result_instance();},"Invalidated physical field binding exposed a constructed result.");
    }
    Fixture typed;typed.storage->bindings={{0,{},typed.meta}};auto metadata_instance=typed.call();
    check(metadata_instance.resume(typed.services).completed && metadata_instance.result_instance().metadata==typed.meta &&
        !metadata_instance.result_instance().file && !metadata_instance.result_instance().field,"Supported metadata layout lost its separate typed identity.");
    rejects([&]{metadata_instance.result();},"A completed metadata instance became a null file.");
    Fixture recursive;auto owner=std::make_shared<IgbDecodeRuntimeField>();recursive.storage->bindings={{0,{},{},{},{},owner}};auto outer=recursive.call();
    recursive.after=[&](const auto&,auto&,auto&){rejects([&]{outer.resume(recursive.services);},"Recursive field initialization succeeded.");};
    rejects([&]{outer.resume(recursive.services);},"Caught recursive field fault did not invalidate the outer continuation.");
    check(owner->construction_fault_ && recursive.storage->faulted(),"Recursive field failure lost canonical construction fault state.");
}
}
int main(){try{headers_and_empty_ranges();gates_and_default_modes();captures_and_fresh_reads();physical_ranges_and_nulls();pool_and_instance_tail();pending_and_teardown();faults_and_abandonment();field_layout_and_lifetime();
    std::cout<<"IGB object initialization: "<<checks<<" checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"IGB object initialization after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
