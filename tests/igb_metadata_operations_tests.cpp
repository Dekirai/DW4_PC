#include "dw4/igb_metadata_operations.hpp"
#include "dw4/igb_string_pool.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbMetadataOperationKind;using S=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
SystemStageString text(std::string value){return std::make_shared<const std::string>(std::move(value));}
std::shared_ptr<ModelSceneObject> object(){return std::make_shared<ModelSceneData>();}
std::shared_ptr<IgbInternedString> string(std::string value,std::uint32_t refs=1){auto s=std::make_shared<IgbInternedString>();s->value=text(std::move(value));s->references=refs;s->container=object();return s;}
std::shared_ptr<IgbRuntimeMetaType> metadata(){
    auto m=std::make_shared<IgbRuntimeMetaType>();m->children=std::shared_ptr<IgbRuntimeTypeList>{};m->parent=std::shared_ptr<IgbRuntimeMetaType>{};
    m->type_word=0x804717f0;m->native_alignment=8;m->native_size=24;m->allocation_prefix=12;
    m->fields=std::make_shared<IgbDecodeFieldRegistry>();m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();return m;
}
struct Fixture {
    std::shared_ptr<IgbMetadataOperationsServices> services=std::make_shared<IgbMetadataOperationsServices>();
    std::shared_ptr<IgbRuntimeMetaType> parent=metadata(),child=metadata();
    std::shared_ptr<IgbRuntimeTypeList> children=std::make_shared<IgbRuntimeTypeList>();
    std::shared_ptr<ModelSceneObject> pool=object();
    std::shared_ptr<IgbInternedString> incoming=string("New");
    std::vector<IgbMetadataOperationCall> calls;
    std::vector<IgbDependenciesCall> strings;
    std::function<void(const IgbMetadataOperationCall&,IgbMetadataOperation&)> on_parent;
    std::function<void(const IgbDependenciesCall&,IgbDependenciesOperation&)> on_string;
    Fixture(){
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->dependencies=std::make_shared<IgbDependenciesServices>();services->dependencies->globals=std::make_shared<SystemStageServices>();
        services->dependencies->globals->string_pool=pool;children->backing=std::make_shared<IgbRuntimeTypeList::Storage>();
        services->factory=[this](const IgbMetadataOperationCall& request){auto op=std::make_shared<IgbMetadataOperation>();
            op->resume=[this,request,out=op.get()]{
                calls.push_back(request);
                switch(request.kind){
                case K::pool_for_handle:out->pool_result=pool;break;
                case K::create_children:out->children_result=children;break;
                case K::append_child:
                    if(!request.children)throw std::runtime_error("Prepared raw append dereferences a null list.");
                    request.children->backing->push_back(request.metadata);++request.children->count;break;
                case K::append_field:
                    if(!request.fields)throw std::runtime_error("Prepared raw append dereferences a null field list.");
                    request.fields->backing->push_back(request.field);++request.fields->count;break;
                }
                if(on_parent)on_parent(request,*out);return MessengerCallResult{true,0,{}};
            };return op;};
        services->dependencies->factory=[this](const IgbDependenciesCall& request){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,request,out=op.get()]{strings.push_back(request);
                switch(request.kind){
                case S::create_string_pool:case S::initialize_string_pool:out->pool_result=pool;break;
                case S::intern_name:out->string_result=incoming;break;
                case S::release_string:request.string->released=true;break;
                default:throw std::runtime_error("Unexpected metadata string operation.");
                }
                if(on_string)on_string(request,*out);return MessengerCallResult{true,0,{}};
            };return op;};
    }
};
void inheritance_and_live_fields(){
    IgbMetadataParentInvocation null({},{});check(null.resume().completed,"Null parent dereferenced the child.");
    Fixture f;auto first=std::make_shared<IgbDecodeRuntimeField>(),replacement=std::make_shared<IgbDecodeRuntimeField>();
    first->reference_word=0xffffffff;replacement->reference_word=17;
    f.parent->fields->backing->assign({first,nullptr});f.parent->fields->count=2;
    f.on_parent=[&](const auto& request,auto&){if(request.kind==K::append_field && f.child->fields->count==1){
        auto updated=std::make_shared<IgbDecodeFieldRegistry>();updated->count=-20;
        updated->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(IgbDecodeFieldRegistry::Storage{nullptr,replacement});f.parent->fields=updated;
    }};
    IgbMetadataParentInvocation call(f.child,f.parent);check(call.resume(f.services).completed,"Parent attachment did not complete.");
    check(f.calls.size()==5 && f.calls[0].handle==5 && f.calls[1].pool==f.pool,"Child-list creation lost actual lookup ordering.");
    check(f.child->parent && *f.child->parent==f.parent && f.child->type_word==f.parent->type_word &&
        f.child->native_alignment==8 && f.child->allocation_prefix==12 && f.child->native_size==24,"Inherited layout publication changed.");
    check(f.parent->reference_word==1 && f.child->reference_word==1 && f.children->count==1,"Raw child append added metadata references.");
    check(f.child->fields->count==2 && f.child->fields->backing->at(0)==first && f.child->fields->backing->at(1)==replacement,
        "Inherited-field loop did not capture count while reloading physical fields.");
    check(first->reference_word==0 && replacement->reference_word==18,"Field inheritance did not use full-word wrapping retains.");
    Fixture null_field;null_field.parent->children=null_field.children;null_field.parent->fields->count=1;null_field.parent->fields->backing->push_back(nullptr);
    IgbMetadataParentInvocation allowed(null_field.child,null_field.parent);check(allowed.resume(null_field.services).completed &&
        null_field.child->fields->count==1 && !null_field.child->fields->backing->front(),"Null field was rejected before raw append.");
    Fixture negative;negative.parent->children=negative.children;negative.parent->fields->count=-1;negative.parent->fields->backing.reset();
    IgbMetadataParentInvocation skip(negative.child,negative.parent);check(skip.resume(negative.services).completed && negative.calls.size()==1,
        "Negative captured count read unavailable field backing.");
}
void parent_failure_prefixes(){
    Fixture f;f.parent->children=f.children;auto field=std::make_shared<IgbDecodeRuntimeField>();f.parent->fields->count=1;f.parent->fields->backing->push_back(field);
    f.child->fields.reset();IgbMetadataParentInvocation bad(f.child,f.parent);rejects([&]{bad.resume(f.services);},"Null child field list appended successfully.");
    check(field->reference_word==2 && f.child->parent && *f.child->parent==f.parent && f.children->count==1,
        "Failure rolled back retain or earlier parent publication.");
    rejects([&]{bad.resume(f.services);},"Faulted attachment restarted.");
    Fixture null_child;null_child.parent->children=null_child.children;IgbMetadataParentInvocation missing({},null_child.parent);
    rejects([&]{missing.resume(null_child.services);},"Null child metadata accepted inherited stores.");
    check(null_child.children->count==1 && !null_child.children->backing->front(),"Null child failed before its original append.");
    Fixture partial;partial.parent->children=partial.children;partial.parent->type_word.reset();IgbMetadataParentInvocation delayed(partial.child,partial.parent);
    check(!delayed.resume(partial.services).completed && partial.child->parent && *partial.child->parent==partial.parent,
        "Unknown type word did not pause after the parent store.");
    auto live=metadata();live->native_alignment=16;live->allocation_prefix.reset();live->native_size=56;partial.child->parent=live;
    partial.parent->type_word=0x87654321;check(!delayed.resume(partial.services).completed && partial.child->type_word==0x87654321 && partial.child->native_alignment==16,
        "Layout fields did not distinguish captured parent from live reloaded parent.");
    live->allocation_prefix=32;check(delayed.resume(partial.services).completed && partial.child->native_size==56 && partial.child->allocation_prefix==32 && partial.calls.size()==1,
        "Late prefix binding replayed child append or lost live layout.");
    Fixture recursive;recursive.parent->children=recursive.children;IgbMetadataParentInvocation reentry(recursive.child,recursive.parent);
    recursive.on_parent=[&](const auto&,auto&){try{reentry.resume(recursive.services);}catch(const std::runtime_error&) {}};
    rejects([&]{reentry.resume(recursive.services);},"Swallowed recursive attachment completed.");
}
void retained_field_append(){
    Fixture f;f.parent->children=f.children;auto field=std::make_shared<IgbDecodeRuntimeField>();f.parent->fields->count=1;f.parent->fields->backing->push_back(field);
    const auto ordinary=f.services->factory;bool ready{};unsigned selected{},resumed{};
    f.services->factory=[&](const IgbMetadataOperationCall& request){if(request.kind!=K::append_field)return ordinary(request);
        ++selected;check(field->reference_word==2,"Field was not retained before the pending append.");
        auto op=std::make_shared<IgbMetadataOperation>();op->resume=[&,request]{++resumed;
            if(!ready)return MessengerCallResult{false,0,"Prepared append waits."};
            request.fields->backing->push_back(request.field);++request.fields->count;return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataParentInvocation call(f.child,f.parent);check(!call.resume(f.services).completed,"Pending field append completed.");
    const auto captured=f.child->fields;f.child->fields=std::make_shared<IgbDecodeFieldRegistry>();ready=true;
    check(call.resume(f.services).completed && selected==1 && resumed==2 && field->reference_word==2 && captured->count==1,
        "Resumed field append repeated retain or changed captured destination.");
    Fixture abandoned;abandoned.parent->children=abandoned.children;auto held=std::make_shared<IgbDecodeRuntimeField>();
    std::weak_ptr<IgbDecodeRuntimeField> weak=held;abandoned.parent->fields->count=1;abandoned.parent->fields->backing->push_back(held);held.reset();
    const auto normal=abandoned.services->factory;
    abandoned.services->factory=[&](const IgbMetadataOperationCall& request){if(request.kind!=K::append_field)return normal(request);
        abandoned.parent->fields->backing->clear();auto op=std::make_shared<IgbMetadataOperation>();op->resume=[]{return MessengerCallResult{false,0,"Prepared append waits."};};return op;};
    {IgbMetadataParentInvocation pending(abandoned.child,abandoned.parent);check(!pending.resume(abandoned.services).completed,"Retained field abandonment fixture did not suspend.");}
    check(!weak.expired() && weak.lock()->reference_word==2,"Abandonment discarded the retained incoming field after its source backing changed.");
}
void name_order_and_lifetime(){
    Fixture f;auto old=string("Old",1),replacement=string("Injected",11);igb_metadata_publish_name(f.child,old);
    std::weak_ptr<IgbInternedString> weak=replacement;
    f.on_string=[&](const auto& request,auto&){if(request.kind==S::release_string){
        check(request.string==old && old->references==0 && request.pool==old->container,"Old string release lost captured header.");
        igb_metadata_publish_name(f.child,replacement);replacement.reset();
    }};
    IgbMetadataNameInvocation call(f.child,text("Desired"));check(call.resume(f.services).completed,"Metadata naming did not complete.");
    check(f.child->name==f.incoming->value && f.child->name_string && *f.child->name_string==f.incoming &&
        f.strings.size()==2 && f.strings[0].kind==S::intern_name && f.strings[1].kind==S::release_string,
        "Naming changed intern/release/publication ordering.");
    check(!weak.expired() && weak.lock()->references==11 && f.incoming->references==1,"Raw publication released a replacement or retained the new guest string twice.");
    Fixture live;auto before=string("Before",8),during=string("During",2);igb_metadata_publish_name(live.child,before);
    live.on_string=[&](const auto& request,auto&){if(request.kind==S::intern_name)igb_metadata_publish_name(live.child,during);};
    IgbMetadataNameInvocation mutate(live.child,text("New"));check(mutate.resume(live.services).completed && before->references==8 && during->references==1,
        "Metadata naming captured the old pointer before interning.");
    Fixture zero;auto underflow=string("Underflow",0);igb_metadata_publish_name(zero.child,underflow);IgbMetadataNameInvocation wrap(zero.child,text("New"));
    check(wrap.resume(zero.services).completed && underflow->references==0xffffffff && zero.strings.size()==1,"String reference decrement did not wrap as a full word.");
    Fixture null;IgbMetadataNameInvocation bad({},text("New"));rejects([&]{bad.resume(null.services);},"Null naming receiver succeeded.");
    check(null.strings.size()==1 && null.strings[0].kind==S::intern_name,"Null naming receiver failed before original interning.");
}
void lazy_strings_and_pending_headers(){
    Fixture f;f.services->dependencies->globals->string_pool.reset();f.incoming.reset();IgbMetadataNameInvocation empty(f.child,std::monostate{});
    check(empty.resume(f.services).completed && f.strings.size()==3 && f.strings[0].bytes==16 &&
        f.strings[1].kind==S::initialize_string_pool && f.strings[2].kind==S::intern_name && !f.child->name,
        "Null desired name skipped original lazy pool initialization.");
    Fixture late;auto old=string("Old",3);late.child->name=old->value;IgbMetadataNameInvocation paused(late.child,text("New"));
    check(!paused.resume(late.services).completed && late.strings.size()==1 && old->references==3,"Unknown old header was fabricated or ignored.");
    auto changed=string("Changed",7);igb_metadata_publish_name(late.child,changed);
    igb_bind_interned_string(late.services->dependencies->strings,old);
    check(paused.resume(late.services).completed && old->references==2 && changed->references==7 && late.strings.size()==1,
        "Late header binding lost the captured old pointer or replayed interning.");
    Fixture delayed;bool ready{};unsigned selected{},resumed{};const auto ordinary=delayed.services->dependencies->factory;
    auto previous=string("Previous",1);igb_metadata_publish_name(delayed.child,previous);
    delayed.services->dependencies->factory=[&](const IgbDependenciesCall& request){if(request.kind!=S::release_string)return ordinary(request);
        ++selected;auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&]{++resumed;return MessengerCallResult{ready,0,{}};};return op;};
    IgbMetadataNameInvocation pending_release(delayed.child,text("New"));check(!pending_release.resume(delayed.services).completed && previous->references==0 && delayed.child->name==previous->value,
        "Pending release published the new name too early.");
    ready=true;check(pending_release.resume(delayed.services).completed && selected==1 && resumed==2 && previous->references==0,
        "Pending release repeated old reference decrement.");
    Fixture recursive;IgbMetadataNameInvocation reentry(recursive.child,text("New"));recursive.on_string=[&](const auto&,auto&){try{reentry.resume(recursive.services);}catch(const std::runtime_error&) {}};
    rejects([&]{reentry.resume(recursive.services);},"Swallowed recursive name failure completed.");
}
void native_interning_and_adapters(){
    Fixture f;auto strings=std::make_shared<IgbStringPoolServices>();strings->mutex=std::shared_ptr<LoaderDirectoryMutex>{};
    auto state=std::make_shared<IgbStringPoolState>();state->table=std::make_shared<IgbStringTable>();
    state->table->count=1;state->table->step=1;state->table->capacity=4;state->table->storage=std::make_shared<IgbStringTable::Storage>(4);
    auto entry=std::make_shared<IgbStringEntry>();entry->owner=string("Known",4);entry->bytes.buffer=std::make_shared<IgbScratchBuffer>();
    // The original aligned strcmp reads the whole word containing the null.
    entry->bytes.buffer->bytes={'K','n','o','w','n',0,0,0};entry->bytes.buffer->initialized.assign(8,1);entry->owner->native_name=entry->bytes;
    state->table->storage->at(0)=entry;igb_bind_string_pool(strings,f.pool,state);
    f.services->dependencies->strings=strings->strings;f.services->dependencies->factory=make_native_igb_string_factory(strings);
    IgbMetadataNameInvocation first(f.child,text("Known"));check(first.resume(f.services).completed && f.child->name==entry->owner->value && entry->owner->references==5,
        "Metadata naming did not compose native string-table search and retain.");
    IgbMetadataNameInvocation same(f.child,entry->owner->value);check(same.resume(f.services).completed && entry->owner->references==5,
        "Same-owner interning and old-name release did not balance.");
    auto factory=make_native_igb_metadata_operations_factory(f.services);IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::attach_parent;request.metadata=f.child;
    auto operation=factory(request);check(operation && operation->resume().completed,"Native registrar adapter did not preserve null-parent no-op.");
    request.kind=IgbTypeRegistrationServiceKind::set_name;request.name=text("Known");operation=factory(request);
    check(operation && operation->resume().completed && entry->owner->references==5,"Native registrar name adapter did not use canonical metadata.");
    request.kind=IgbTypeRegistrationServiceKind::defaults;check(!factory(request),"Metadata adapter swallowed unrelated defaults.");
}
} // namespace
int main(){try{inheritance_and_live_fields();parent_failure_prefixes();retained_field_append();name_order_and_lifetime();lazy_strings_and_pending_headers();native_interning_and_adapters();
    std::cout<<"Metadata operation checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
