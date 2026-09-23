#include "dw4/igb_field_string_init.hpp"
#include "dw4/igb_string_pool.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
auto bytes(std::size_t size,std::uint8_t value=0){auto p=std::make_shared<IgbScratchBuffer>();p->bytes.assign(size,value);p->initialized.assign(size,1);return p;}
void word(const std::shared_ptr<IgbScratchBuffer>& b,std::uint32_t value,std::size_t offset=0){for(unsigned i=0;i<4;++i)b->bytes[offset+i]=static_cast<std::uint8_t>(value>>(24-i*8));}
auto header(std::uint32_t refs=1,const char* value="incoming"){auto h=std::make_shared<IgbInternedString>();h->references=refs;h->value=std::make_shared<const std::string>(value);return h;}
struct Fixture {
    std::shared_ptr<IgbFieldValuesServices> services=std::make_shared<IgbFieldValuesServices>();
    std::shared_ptr<IgbDecodeRuntimeField> descriptor=std::make_shared<IgbDecodeRuntimeField>(),target_field=std::make_shared<IgbDecodeRuntimeField>();
    IgbObjectInstance target;std::shared_ptr<IgbScratchBuffer> defaults=bytes(4);
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbInternedString> incoming=header(),source=header(7,"source"),old=header(1,"old");
    std::vector<K> trace;unsigned creations{},initializations{},interns{},releases{};bool release_ready=true,intern_ready=true;
    std::function<void(const IgbDependenciesCall&,IgbDependenciesOperation&)> action;
    Fixture(){
        target.storage=std::make_shared<IgbObjectStorage>();target.storage->original_address=0x100000;target.storage->bytes=bytes(0x50,0xcc);
        target.offset=8;target.field=target_field;target.storage->bindings.push_back({8,{},{},{},{},target_field});
        target_field->vtable_word=0x804762fc;descriptor->offset=0x0c;descriptor->default_bytes=IgbRuntimeFieldDefaultSource{defaults,0};
        descriptor->default_bytes_address=0x200000;igb_bootstrap_write_word({target.storage,target.offset},0x0c,0);
        services->fields=std::make_shared<IgbFieldOperationsServices>();services->fields->dependencies=std::make_shared<IgbDependenciesServices>();
        auto dep=services->fields->dependencies;dep->globals=std::make_shared<SystemStageServices>();dep->globals->string_pool=pool;
        old->container=pool;incoming->container=pool;source->container=pool;
        igb_bind_field_string(services,0x300008,incoming);igb_bind_field_string(services,0x400008,source);igb_bind_field_string(services,0x500008,old);
        dep->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            trace.push_back(c.kind);
            if(c.kind==K::create_string_pool){++creations;check(c.bytes==16,"Pool allocation lost its original size.");out->pool_result=pool;}
            else if(c.kind==K::initialize_string_pool){++initializations;check(c.pool==pool,"Pool initializer lost allocation identity.");out->pool_result=pool;}
            else if(c.kind==K::intern_name){++interns;if(!intern_ready)return MessengerCallResult{false,0,"prepared intern"};out->string_result=incoming;}
            else if(c.kind==K::release_string){++releases;if(!release_ready)return MessengerCallResult{false,0,"prepared release"};}
            else throw std::runtime_error("Unexpected string initialization dependency.");
            if(action)action(c,*out);return MessengerCallResult{true,0,{}};};return op;};
    }
    std::uint32_t output(std::uint32_t offset=0x0c)const{return igb_bootstrap_read_word({target.storage,target.offset},offset);}
    void destination(const std::shared_ptr<IgbInternedString>& value,std::uint32_t address){
        igb_bootstrap_write_word({target.storage,target.offset},descriptor->offset,address);target_field->name_string=value;
        target_field->name_string_address=address;target_field->name=value?value->value:SystemStageString{};
    }
};
void null_and_physical_layout(){
    Fixture f;const auto before=f.target.storage->bytes->bytes;f.services.reset();IgbFieldStringInitInvocation call(f.descriptor,f.target);
    check(call.resume().completed && f.output()==0,"Null default required a pool or dependency service.");
    check(f.target_field->name_string && !*f.target_field->name_string && f.target_field->name_string_address==0 && !f.target_field->name,"Null string did not synchronize canonical name identity.");
    check(f.target.storage->bytes->bytes==before && f.incoming->references==1,"Null assignment changed padding or unrelated reference state.");
    check(call.resume().completed,"Completed null initialization replayed.");
    Fixture name;name.destination(name.old,0x500008);IgbFieldStringInitInvocation clear(name.descriptor,name.target);
    check(clear.resume(name.services).completed && name.old->references==0 && name.releases==1 && name.interns==0,"Null default did not release old name without interning.");
    check(name.output()==0 && name.target_field->name_string_address==0 && !name.target_field->name,"Null default did not clear physical and canonical names.");
    Fixture pool;pool.descriptor->offset=0x40;igb_bootstrap_write_word({pool.target.storage,pool.target.offset},0x40,0);pool.target_field->word_40=0x99;
    IgbFieldStringInitInvocation clear_pool(pool.descriptor,pool.target);check(clear_pool.resume().completed && pool.target_field->word_40==0 && pool.output(0x40)==0,"Object pool-name pointer was not synchronized.");
    Fixture absent;absent.descriptor->default_bytes.reset();IgbFieldStringInitInvocation missing(absent.descriptor,absent.target);
    check(!missing.resume(absent.services).completed && absent.trace.empty(),"Unknown descriptor default was inferred null.");
    absent.descriptor->default_bytes=IgbRuntimeFieldDefaultSource{};rejects([&]{missing.resume(absent.services);},"Known null default-buffer dereference did not fault.");
    check(missing.faulted() && absent.trace.empty(),"Default-buffer fault skipped the original source-first order.");
}
void nonnull_and_destination_capture(){
    Fixture f;word(f.defaults,0x400008);f.destination(f.old,0x500008);f.incoming->references=8;auto displaced=f.target_field->name;
    f.action=[&](const IgbDependenciesCall& c,IgbDependenciesOperation&){if(c.kind==K::intern_name){
        check(c.pool==f.pool && igb_dependency_name(c.name)==f.source->value,"Interning changed the captured default string identity.");
        f.descriptor->offset=0x40;igb_bootstrap_write_word({f.target.storage,f.target.offset},0x40,0);
        f.descriptor->default_bytes=IgbRuntimeFieldDefaultSource{};
    }};
    IgbFieldStringInitInvocation call(f.descriptor,f.target);check(call.resume(f.services).completed,"Nonnull string initialization did not complete.");
    check(f.output()==0x500008 && f.output(0x40)==0x300008 && f.target_field->word_40==0x300008,"Destination offset was captured before interning.");
    check(f.incoming->references==8 && f.old->references==1 && f.source->references==7 && f.interns==1 && f.releases==0,"Wrapper retain and temporary release changed unrelated headers.");
    check(f.target_field->name==displaced,"Pool-name assignment overwrote canonical field name.");

    Fixture paused;word(paused.defaults,0x400008);paused.destination(paused.old,0x500008);paused.release_ready=false;
    IgbFieldStringInitInvocation waiting(paused.descriptor,paused.target);
    check(!waiting.resume(paused.services).completed && paused.old->references==0 && paused.incoming->references==2 && paused.output()==0x500008,"Pending old release lost incoming retain or prematurely published.");
    paused.descriptor->offset=0x40;paused.release_ready=true;paused.services->fields->dependencies->factory={};
    check(waiting.resume(paused.services).completed && paused.releases==2 && paused.interns==1 && paused.incoming->references==1,"Resumption replayed intern/retain/decrement or discarded selected release.");
    check(paused.output()==0x300008 && paused.target_field->name_string==paused.incoming && paused.target_field->name_string_address==0x300008 && paused.target_field->name==paused.incoming->value,"Captured destination or canonical name publication changed after suspension.");
    check(paused.output(0x40)==0xcccccccc,"Assignment used a changed descriptor offset after entry.");
}
void assignment_source_reread_and_aliases(){
    Fixture f;auto source=bytes(4);word(source,0x300008);f.destination(f.old,0x500008);auto replacement=header(12,"replacement");igb_bind_field_string(f.services,0x600008,replacement);
    f.action=[&](const IgbDependenciesCall& c,IgbDependenciesOperation&){if(c.kind==K::release_string){check(c.string==f.old,"Assignment released the incoming header.");word(source,0x600008);f.descriptor->offset=0x40;}};
    IgbFieldStringAssignInvocation assign(f.descriptor,f.target,{source,0});check(assign.resume(f.services).completed,"String assignment did not complete.");
    check(f.incoming->references==2 && f.old->references==0 && replacement->references==12,"Assignment retained the reread source instead of its first pointer.");
    check(f.output()==0x600008 && f.target_field->name_string==replacement && f.target_field->name==replacement->value,"Assignment failed to reread source after release.");
    for(const auto count:{0u,1u,0xffffffffu}){
        Fixture same;auto input=bytes(4);word(input,0x300008);same.incoming->references=count;same.destination(same.incoming,0x300008);
        IgbFieldStringAssignInvocation alias(same.descriptor,same.target,{input,0});check(alias.resume(same.services).completed,"Aliased string assignment failed.");
        check(same.incoming->references==count && same.releases==(count==0?1u:0u),"Aliased assignment skipped original full-word retain/release arithmetic.");
    }
    Fixture raw;auto input=bytes(4);word(input,0x300008);raw.destination(raw.old,0x500008);
    raw.action=[&](const IgbDependenciesCall& c,IgbDependenciesOperation&){if(c.kind==K::release_string)word(input,0x700008);};
    IgbFieldStringAssignInvocation unknown(raw.descriptor,raw.target,{input,0});check(unknown.resume(raw.services).completed && raw.output()==0x700008,"Raw reread pointer required an invented string dereference.");
    check(raw.target_field->name_string_address==0x700008 && !raw.target_field->name_string && !raw.target_field->name,"Unbound copied pointer was manufactured as a represented null header.");
}
void lazy_pool_and_missing_bindings(){
    Fixture f;word(f.defaults,0x400008);f.services->fields->dependencies->globals->string_pool.reset();auto displaced=std::make_shared<ModelSceneData>();std::weak_ptr<ModelSceneObject> weak=displaced;
    f.action=[&](const IgbDependenciesCall& c,IgbDependenciesOperation&){if(c.kind==K::initialize_string_pool)f.services->fields->dependencies->globals->string_pool=displaced;};
    IgbFieldStringInitInvocation lazy(f.descriptor,f.target);check(lazy.resume(f.services).completed && f.creations==1 && f.initializations==1 && f.interns==1,"Nonnull default skipped lazy pool setup.");
    displaced.reset();check(f.services->fields->dependencies->globals->string_pool==f.pool && !weak.expired(),"Pool raw publication lost or released its displaced owner.");
    check(f.trace==std::vector<K>({K::create_string_pool,K::initialize_string_pool,K::intern_name}),"Lazy setup call order changed.");

    Fixture late;word(late.defaults,0x400008);late.services->strings.clear();IgbFieldStringInitInvocation bind(late.descriptor,late.target);
    check(!bind.resume(late.services).completed && late.trace.empty(),"Missing source binding manufactured null input.");
    auto other_pool=std::make_shared<ModelSceneData>();late.services->fields->dependencies->globals->string_pool=other_pool;
    igb_bind_field_string(late.services,0x400008,late.source);late.action=[&](const IgbDependenciesCall& c,IgbDependenciesOperation&){if(c.kind==K::intern_name)check(c.pool==late.pool,"Late source binding reloaded an already captured pool.");};
    check(!bind.resume(late.services).completed && late.interns==1 && late.incoming->references==1 && late.output()==0,"Missing incoming arithmetic binding invented a pointer or retain.");
    igb_bind_field_string(late.services,0x300008,late.incoming);check(bind.resume(late.services).completed && late.interns==1 && late.output()==0x300008,"Late returned-address binding replayed interning.");

    Fixture no_chars;word(no_chars.defaults,0x400008);no_chars.source->value.reset();IgbFieldStringInitInvocation characters(no_chars.descriptor,no_chars.target);
    check(!characters.resume(no_chars.services).completed && no_chars.interns==0,"Nonnull source with unknown characters was converted into null.");
    no_chars.source->value=std::make_shared<const std::string>("late");check(characters.resume(no_chars.services).completed,"Late character binding did not resume.");
}
void native_interner_and_fault_prefixes(){
    Fixture f;word(f.defaults,0x400008);auto native=std::make_shared<IgbStringPoolServices>();native->mutex=std::shared_ptr<LoaderDirectoryMutex>{};
    auto pool=std::make_shared<IgbStringPoolState>();pool->table=std::make_shared<IgbStringTable>();pool->table->count=1;pool->table->step=1;pool->table->capacity=1;
    auto entry=std::make_shared<IgbStringEntry>();entry->owner=f.incoming;entry->owner->value=f.source->value;entry->bytes={bytes(12),0};
    std::copy(f.source->value->begin(),f.source->value->end(),entry->bytes.buffer->bytes.begin());entry->owner->native_name=entry->bytes;
    pool->table->storage=std::make_shared<IgbStringTable::Storage>(1,entry);igb_bind_string_pool(native,f.pool,pool);
    f.services->fields->dependencies->factory=make_native_igb_string_factory(native);IgbFieldStringInitInvocation actual(f.descriptor,f.target);
    check(actual.resume(f.services).completed && f.output()==0x300008 && f.incoming->references==2,"Actual54094 search/increment was not composed with assignment and temporary release.");
    check(f.target_field->name==f.source->value && f.trace.empty(),"Native interner fell back to a prepared success.");

    Fixture fault;auto input=bytes(4);word(input,0x300008);fault.target.storage->bytes->initialized[fault.target.offset+0x0c]=0;
    IgbFieldStringAssignInvocation invalid(fault.descriptor,fault.target,{input,0});rejects([&]{invalid.resume(fault.services);},"Uninitialized destination pointer did not fault.");
    check(fault.incoming->references==2 && fault.releases==0 && invalid.faulted(),"Destination read failure lost prior incoming retain.");
    Fixture released;word(released.defaults,0x400008);released.source->released=true;IgbFieldStringInitInvocation stale(released.descriptor,released.target);
    rejects([&]{stale.resume(released.services);},"Released source header was accepted.");check(released.interns==0 && stale.faulted(),"Released source ran interning.");
    Fixture recursion;word(recursion.defaults,0x400008);IgbFieldStringInitInvocation recursive(recursion.descriptor,recursion.target);
    recursion.action=[&](const IgbDependenciesCall&,IgbDependenciesOperation&){try{recursive.resume(recursion.services);}catch(const std::runtime_error&){};};
    rejects([&]{recursive.resume(recursion.services);},"Swallowed recursive initialization did not poison the outer call.");
    check(recursive.faulted() && recursion.output()==0,"Recursive fault published a destination.");
    Fixture abandoned;abandoned.intern_ready=false;word(abandoned.defaults,0x400008);std::weak_ptr<IgbObjectStorage> retained=abandoned.target.storage;
    {IgbFieldStringInitInvocation pending(abandoned.descriptor,abandoned.target);check(!pending.resume(abandoned.services).completed,"Prepared intern did not suspend.");}
    abandoned.target.storage.reset();check(!retained.expired(),"Abandoned pending wrapper lost target allocation lifetime.");
    abandoned.services.reset();check(retained.expired(),"Pending wrapper retained a permanent services cycle.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_string_init_tests");
    null_and_physical_layout();nonnull_and_destination_capture();assignment_source_reread_and_aliases();lazy_pool_and_missing_bindings();native_interner_and_fault_prefixes();
    std::cout<<"igb_field_string_init_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Field string initialization failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
