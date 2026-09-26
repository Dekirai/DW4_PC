#include "dw4/igb_metadata_layout.hpp"
#include "dw4/igb_pointer_list.hpp"

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
std::uint16_t halfword(const IgbBootstrapPointer& pointer,std::uint32_t offset){
    return static_cast<std::uint16_t>((std::uint16_t(igb_bootstrap_read_byte(pointer,offset))<<8u)|
        igb_bootstrap_read_byte(pointer,offset+1));
}
struct PhysicalField {
    IgbObjectInstance object;
    explicit PhysicalField(std::shared_ptr<IgbDecodeRuntimeField> value,std::uint32_t address=0x100000){
        object.offset=0x10;object.storage=std::make_shared<IgbObjectStorage>();object.storage->original_address=address;
        object.storage->bytes=std::make_shared<IgbScratchBuffer>();object.storage->bytes->bytes.assign(0x50,0xcd);
        object.storage->bytes->initialized.assign(0x50,0);object.field=std::move(value);
        object.field->physical_storage=IgbFieldStorageBinding{object.storage,object.offset};IgbObjectBinding binding;
        binding.offset=object.offset;binding.field=object.field;object.storage->bindings.push_back(binding);
        igb_bootstrap_write_word(pointer(),0,*object.field->vtable_word);igb_bootstrap_write_word(pointer(),8,object.field->offset);
        igb_bootstrap_write_halfword(pointer(),0x10,0x7788);igb_bootstrap_write_halfword(pointer(),0x12,object.field->ordinal);
        igb_bootstrap_write_halfword(pointer(),0x14,object.field->size);igb_bootstrap_write_halfword(pointer(),0x16,0x99aa);
        igb_bootstrap_write_word(pointer(),0x18,0x12345678);igb_bootstrap_write_byte(pointer(),0x25,object.field->flag_25);
    }
    IgbBootstrapPointer pointer()const{return {object.storage,object.offset};}
    std::uint16_t ordinal()const{return halfword(pointer(),0x12);}
    std::uint32_t owner()const{return igb_bootstrap_read_word(pointer(),0x18);}
};

void physical_ordinals_and_declaring_metadata(){
    Fixture f;auto inherited=field(),special=field(),local=field();special->flag_25=1;
    auto parent=metadata({inherited});inherited->declaring_type=parent;special->declaring_type=parent;local->declaring_type=parent;
    auto child=metadata({inherited,special,local});child->parent=parent;
    PhysicalField inherited_storage(inherited),special_storage(special,0x200000),local_storage(local,0x300000);
    const auto inherited_bytes=inherited_storage.object.storage->bytes->bytes;
    const auto inherited_mask=inherited_storage.object.storage->bytes->initialized;
    f.services->pointer_lists=std::make_shared<IgbPointerListServices>();f.services->pointer_lists->values.push_back({0x400010,child,{}});
    f.core->registration_callbacks=callbacks({0x80012340});unsigned notified{};
    f.services->factory=[&](const IgbMetadataLayoutCall& call){check(call.kind==K::registered_callback,"Physical layout unexpectedly selected a custom alignment.");
        auto operation=std::make_shared<IgbMetadataLayoutOperation>();operation->resume=[&]{++notified;
            check(local_storage.ordinal()==1 && local_storage.owner()==0x400010 && local->ordinal==1 && local->declaring_type->lock()==child &&
                (child->class_flags&4)!=0,"Registered callback ran before physical ordinal/owner publication and ready flag.");
            return MessengerCallResult{true,0,{}};};return operation;};
    IgbMetadataLayoutInvocation run(child);check(run.resume(f.services).completed && notified==1,
        "Bound layout did not complete physical ordinal and declaring-pointer stores.");
    check(inherited_storage.object.storage->bytes->bytes==inherited_bytes && inherited_storage.object.storage->bytes->initialized==inherited_mask &&
        inherited->ordinal==0xbeef && inherited->declaring_type->lock()==parent,"Identical inherited field did not skip both physical/canonical stores.");
    check(special_storage.ordinal()==0xbeef && special_storage.owner()==0x12345678 && special->ordinal==0xbeef && special->declaring_type->lock()==parent,
        "Special field received an ordinary local ordinal/declaring-pointer publication.");
    check(halfword(local_storage.pointer(),0x10)==0x7788 && halfword(local_storage.pointer(),0x14)==4 &&
        halfword(local_storage.pointer(),0x16)==0x99aa,"Ordinal publication overwrote adjacent type/size/padding halfwords.");
    const auto completed=local_storage.object.storage->bytes->bytes;check(run.resume().completed && notified==1 &&
        local_storage.object.storage->bytes->bytes==completed,"Completed bound layout replayed physical publication or callbacks.");
}

void physical_owner_address_pending_after_ordinal(){
    Fixture f;auto value=field();auto former=metadata();value->declaring_type=former;auto owner=metadata({value});PhysicalField storage(value);
    IgbMetadataLayoutInvocation run(owner);check(!run.resume(f.services).completed && storage.ordinal()==0 && value->ordinal==0 &&
        storage.owner()==0x12345678 && value->declaring_type->lock()==former && owner->class_flags==0,
        "Missing declaring metadata address did not suspend after the completed ordinal halfword store.");
    // A later observer changes the completed ordinal; resuming +18 must not
    // repeat the earlier original STH or its corresponding canonical update.
    igb_bootstrap_write_halfword(storage.pointer(),0x12,0x1357);value->ordinal=0x1357;
    f.services->pointer_lists=std::make_shared<IgbPointerListServices>();f.services->pointer_lists->values.push_back({0x500010,owner,{}});
    check(run.resume(f.services).completed && storage.ordinal()==0x1357 && value->ordinal==0x1357 && storage.owner()==0x500010 &&
        value->declaring_type->lock()==owner,"Late declaring pointer mapping replayed ordinal or lost the captured metadata owner.");
    Fixture inherited;auto base_field=field();auto parent=metadata({base_field});base_field->declaring_type=parent;
    auto child=metadata({base_field});child->parent=parent;PhysicalField unchanged(base_field);
    for(unsigned at=0;at<2;++at)unchanged.object.storage->bytes->initialized[unchanged.object.offset+0x12+at]=0;
    for(unsigned at=0;at<4;++at)unchanged.object.storage->bytes->initialized[unchanged.object.offset+0x18+at]=0;
    const auto before=unchanged.object.storage->bytes->bytes,mask=unchanged.object.storage->bytes->initialized;
    IgbMetadataLayoutInvocation skip(child);check(skip.resume(inherited.services).completed && unchanged.object.storage->bytes->bytes==before &&
        unchanged.object.storage->bytes->initialized==mask && base_field->ordinal==0xbeef && base_field->declaring_type->lock()==parent,
        "Inherited identity required an unused numeric owner mapping or initialized skipped publication bytes.");
}

void physical_publication_bounds_preserve_store_order(){
    for(bool fail_ordinal:{false,true}){
        Fixture f;auto value=field();value->vtable_word=7;auto former=metadata();value->declaring_type=former;auto owner=metadata({value});PhysicalField storage(value);
        f.services->pointer_lists=std::make_shared<IgbPointerListServices>();f.services->pointer_lists->values.push_back({0x600010,owner,{}});
        unsigned aligns{};f.services->factory=[&](const IgbMetadataLayoutCall& call){check(call.kind==K::field_alignment,"Bounds fixture selected a notification before field stores.");
            auto op=std::make_shared<IgbMetadataLayoutOperation>();op->resume=[&]{++aligns;owner->parent.reset();return MessengerCallResult{true,4,{}};};return op;};
        IgbMetadataLayoutInvocation run(owner);check(!run.resume(f.services).completed && aligns==1 && storage.ordinal()==0xbeef,
            "Bounds fixture did not suspend after alignment/flag processing and before ordinal publication.");
        // Resize after the earlier flag/size access boundary. A statically
        // short descriptor would fail earlier in the original instruction path.
        const auto extent=storage.object.offset+(fail_ordinal?0x13u:0x1bu);storage.object.storage->bytes->bytes.resize(extent);
        storage.object.storage->bytes->initialized.resize(extent);const auto before=storage.object.storage->bytes->bytes,mask=storage.object.storage->bytes->initialized;
        owner->parent=std::shared_ptr<IgbRuntimeMetaType>{};rejects([&]{run.resume(f.services);},"Unavailable physical ordinal/owner write was accepted.");
        check(run.faulted() && value->ordinal==(fail_ordinal?0xbeef:0) && value->declaring_type->lock()==former && owner->class_flags==0 && aligns==1,
            "Physical publication failure changed canonical owner, lost ordinal prefix or published metadata readiness.");
        if(fail_ordinal)check(storage.object.storage->bytes->bytes==before && storage.object.storage->bytes->initialized==mask,
            "Failed ordinal STH wrote one byte or changed its canonical ordinal.");
        else{check(storage.ordinal()==0,"Owner-word failure discarded the completed physical ordinal halfword.");
            for(unsigned at=0;at<extent;++at)if(at<storage.object.offset+0x12 || at>=storage.object.offset+0x14)
                check(storage.object.storage->bytes->bytes[at]==before[at] && storage.object.storage->bytes->initialized[at]==mask[at],
                    "Failed declaring-pointer STW wrote a partial word or modified unrelated physical bytes.");}
        storage.object.storage->bytes->bytes.resize(0x50);storage.object.storage->bytes->initialized.resize(0x50);
        rejects([&]{run.resume(f.services);},"Faulted physical publication revived after destination repair.");check(aligns==1,"Publication repair replayed alignment.");
    }
}

void physical_publication_conflict_and_binding_faults(){
    Fixture f;auto value=field();auto former=metadata();value->declaring_type=former;auto owner=metadata({value});PhysicalField storage(value);
    f.services->pointer_lists=std::make_shared<IgbPointerListServices>();f.services->pointer_lists->values={{0x500010,owner,{}},{0x600010,owner,{}}};
    IgbMetadataLayoutInvocation conflict(owner);rejects([&]{conflict.resume(f.services);},"Conflicting numeric identities published an arbitrary declaring metadata pointer.");
    check(conflict.faulted() && storage.ordinal()==0 && value->ordinal==0 && storage.owner()==0x12345678 && value->declaring_type->lock()==former,
        "Metadata address conflict lost the ordinal prefix or changed the declaring owner.");
    f.services->pointer_lists->values.pop_back();rejects([&]{conflict.resume(f.services);},"Metadata identity conflict revived after map repair.");
    for(unsigned invalid=0;invalid<3;++invalid){Fixture late;auto field_value=field();auto metadata_value=metadata({field_value});PhysicalField physical(field_value);
        IgbMetadataLayoutInvocation run(metadata_value);check(!run.resume(late.services).completed && physical.ordinal()==0,
            "Physical binding fixture did not stop between ordinal and declaring stores.");
        const auto bytes=physical.object.storage->bytes;const auto binding=field_value->physical_storage;
        const auto address=physical.object.storage->original_address;const auto before=bytes->bytes,mask=bytes->initialized;
        if(invalid==0)field_value->physical_storage.reset();if(invalid==1)bytes->released=true;if(invalid==2)physical.object.storage->original_address+=0x100;
        late.services->pointer_lists=std::make_shared<IgbPointerListServices>();late.services->pointer_lists->values.push_back({0x700010,metadata_value,{}});
        rejects([&]{run.resume(late.services);},"Pending declaring publication rebound or ignored an invalid captured physical field.");
        check(run.faulted() && bytes->bytes==before && bytes->initialized==mask && field_value->ordinal==0,
            "Invalid captured field changed physical publication prefix.");
        field_value->physical_storage=binding;bytes->released=false;physical.object.storage->original_address=address;
        rejects([&]{run.resume(late.services);},"Captured field identity fault resumed after restoration.");
    }
}
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
    physical_ordinals_and_declaring_metadata();physical_owner_address_pending_after_ordinal();physical_publication_bounds_preserve_store_order();physical_publication_conflict_and_binding_faults();
    std::cout<<"Metadata layout checks: "<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"Metadata layout failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
