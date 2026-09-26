#include "dw4/igb_gamecube_thread_adopt.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// Synthetic prepared OS contexts and tagged pools isolate the actual 77EFC
// caller. They do not establish OSInit, host scheduling, or pool construction.
namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto bytes(std::size_t size,std::uint8_t fill=0xcd,std::uint8_t known=0){auto value=std::make_shared<IgbScratchBuffer>();
    value->bytes.assign(size,fill);value->initialized.assign(size,known);return value;}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){auto value=std::make_shared<IgbObjectStorage>();
    value->original_address=address;value->bytes=bytes(size+0x10);return {value,0x10};}
bool known(IgbBootstrapPointer object,unsigned relative,unsigned width=4){const auto b=object.storage->bytes;const auto at=object.offset+relative;
    return at+width<=b->initialized.size() && std::all_of(b->initialized.begin()+at,b->initialized.begin()+at+width,[](auto v){return v!=0;});}
std::uint32_t word(IgbBootstrapPointer object,unsigned relative){return igb_bootstrap_read_word(object,relative);}
void pattern(IgbBootstrapPointer value){for(unsigned i=0;i<0x318;++i){const auto at=value.offset+i;
    value.storage->bytes->bytes[at]=static_cast<std::uint8_t>((i*37u+(i>>4u)*11u+3u)&255u);value.storage->bytes->initialized[at]=1;}}
struct Fixture {
    std::shared_ptr<IgbGamecubeThreadAdoptServices> services=std::make_shared<IgbGamecubeThreadAdoptServices>();
    IgbBootstrapPointer thread=raw(0x100000,0x388),source=raw(0x200000,0x318);
    std::shared_ptr<IgbRuntimeObject> runtime=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::thread);
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbMemoryFieldConstructCall> pool_calls;
    std::vector<IgbGamecubeThreadAdoptCall> free_calls;
    std::vector<std::shared_ptr<IgbMemoryFieldConstructOperation>> pool_operations;
    std::vector<std::shared_ptr<IgbGamecubeThreadAdoptOperation>> free_operations;
    std::function<MessengerCallResult(const IgbMemoryFieldConstructCall&)> pool_action;
    std::function<MessengerCallResult(const IgbGamecubeThreadAdoptCall&,const std::function<void()>&)> free_action;
    unsigned pool_polls{},free_polls{};
    Fixture(){
        runtime->physical_storage=IgbRuntimeObjectStorageBinding{thread.storage,thread.offset};IgbObjectBinding binding;
        binding.offset=thread.offset;binding.runtime_object=runtime;thread.storage->bindings.push_back(binding);
        igb_bootstrap_write_word(thread,0,0x80470110);igb_bootstrap_write_byte(thread,4,0xb7);igb_bootstrap_write_word(thread,0x20,0);
        pattern(source);services->os=std::make_shared<IgbOsThreadState>();services->os->current_thread_address=igb_bootstrap_address(source);
        services->os->mappings.push_back(source);services->memory=std::make_shared<IgbMemoryFieldConstructServices>();
        services->memory->factory=[this](const IgbMemoryFieldConstructCall& call){pool_calls.push_back(call);
            auto result=std::make_shared<IgbMemoryFieldConstructOperation>();result->resume=[this,call,out=result.get()]{++pool_polls;
                const auto state=pool_action?pool_action(call):MessengerCallResult{true,0,{}};if(state.completed)out->pool_result=pool;return state;};
            pool_operations.push_back(result);return result;};
        services->factory=[this](const IgbGamecubeThreadAdoptCall& call){free_calls.push_back(call);auto result=std::make_shared<IgbGamecubeThreadAdoptOperation>();
            result->resume_checked=[this,call](const std::function<void()>& guard){++free_polls;
                return free_action?free_action(call,guard):MessengerCallResult{true,0,{}};};
            result->resume=[out=result.get()]{return out->resume_checked({});};free_operations.push_back(result);return result;};
    }
};

// Independent expected instruction effects from original77EFC: paired reads
// are deliberately saved before either store. This also covers physical aliases.
void original_copy(std::vector<std::uint8_t>& memory,unsigned source,unsigned target){
    const auto group=[&](unsigned offset,unsigned width,unsigned count){std::array<std::uint8_t,8> saved{};
        for(unsigned i=0;i<width*count;++i)saved[i]=memory[source+offset+i];
        for(unsigned i=0;i<width*count;++i)memory[target+0x38+offset+i]=saved[i];};
    for(unsigned offset=0;offset<0x2c8;offset+=8)group(offset,4,2);
    group(0x2c8,2,1);group(0x2ca,2,1);
    for(unsigned offset=0x2cc;offset<=0x2dc;offset+=4)group(offset,4,1);
    group(0x2e0,4,2);group(0x2e8,4,2);group(0x2f0,4,1);group(0x2f4,4,2);group(0x2fc,4,2);
    group(0x304,4,1);group(0x308,4,1);group(0x30c,4,1);group(0x310,4,2);
}
void store_expected(std::vector<std::uint8_t>& bytes,unsigned at,std::uint32_t value){
    for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(24u-i*8u));}

void exact_copy_and_null_stack_skip_tag(){
    Fixture f;f.thread.storage->bytes->initialized[f.thread.offset+4]=0;
    const auto before=f.thread.storage->bytes->bytes,mask=f.thread.storage->bytes->initialized;
    IgbGamecubeThreadAdoptInvocation run(f.thread);check(run.resume(f.services).completed && run.completed() && !run.faulted(),
        "Null-stack adoption read the unknown tag or failed the prepared OS context.");
    check(word(f.thread,0x350)==igb_bootstrap_address(f.source) && word(f.thread,0x20)==0 && f.pool_calls.empty() && f.free_calls.empty(),
        "Current-thread adoption changed the master pointer or freed a null stack.");
    for(unsigned i=0;i<0x318;++i)check(f.thread.storage->bytes->bytes[f.thread.offset+0x38+i]==f.source.storage->bytes->bytes[f.source.offset+i] &&
        f.thread.storage->bytes->initialized[f.thread.offset+0x38+i],"Original inline OS-context copy missed a byte or halfword.");
    for(unsigned i=0;i<before.size();++i)if(!(i>=f.thread.offset+0x38 && i<f.thread.offset+0x354) &&
        !(i>=f.thread.offset+0x20 && i<f.thread.offset+0x24))check(f.thread.storage->bytes->bytes[i]==before[i] &&
            f.thread.storage->bytes->initialized[i]==mask[i],"Adoption changed unrelated thread/header or neighboring memory.");
    const auto completed=f.thread.storage->bytes->bytes;f.services->os->current_thread_address=0;
    check(run.resume().completed && f.thread.storage->bytes->bytes==completed,"Completed adoption replayed its OS load or copy.");
}

void separate_current_loads_and_late_mapping(){
    Fixture f;f.services->os->current_thread_address.reset();IgbGamecubeThreadAdoptInvocation run(f.thread);
    check(!run.resume(f.services).completed && !known(f.thread,0x350),"Unknown first current-thread cell fabricated a pointer publication.");
    f.services->os->current_thread_address=0x55667788;bool changed{};
    const auto after_first=[&]{if(!changed && known(f.thread,0x350)){changed=true;f.services->os=std::make_shared<IgbOsThreadState>();}};
    check(!run.resume(f.services,after_first).completed && changed && word(f.thread,0x350)==0x55667788 && !known(f.thread,0x38),
        "Second current-thread load reused the first result or required its unused source mapping.");
    f.services->os->current_thread_address=igb_bootstrap_address(f.source);
    check(!run.resume().completed && !known(f.thread,0x38),"Absent second source mapping was replaced by an invented context.");
    f.services->os->current_thread_address=0;f.services->os->mappings.push_back(f.source);
    check(run.resume().completed && word(f.thread,0x350)==0x55667788 && word(f.thread,0x38)==word(f.source,0),
        "Late source mapping recaptured the canonical pointer or changed the first publication.");
    Fixture zero;zero.services->os->current_thread_address=0;IgbGamecubeThreadAdoptInvocation null_source(zero.thread);
    rejects([&]{null_source.resume(zero.services);},"Known null second current-thread pointer became pending or an empty copy.");
    check(null_source.faulted() && word(zero.thread,0x350)==0 && !known(zero.thread,0x38),
        "Null source failure lost the first pointer store or changed the copied region.");
    Fixture unknown_target;unknown_target.services->os->current_thread_address.reset();IgbGamecubeThreadAdoptInvocation missing(IgbBootstrapPointer{});
    check(!missing.resume(unknown_target.services).completed && !missing.faulted(),"Unknown first global eagerly dereferenced the missing target.");
    unknown_target.services->os->current_thread_address=0x12345678;
    rejects([&]{missing.resume();},"Reached first pointer store accepted a missing target.");
}

void actual_pair_alias_order(){
    for(const unsigned relative:{0x34u,0x38u,0x3cu}){
        Fixture f;for(unsigned i=0;i<f.thread.storage->bytes->bytes.size();++i){
            f.thread.storage->bytes->bytes[i]=static_cast<std::uint8_t>(i*19u+(i>>3));f.thread.storage->bytes->initialized[i]=1;}
        igb_bootstrap_write_word(f.thread,0,0x80470110);igb_bootstrap_write_word(f.thread,0x20,0);
        IgbBootstrapPointer source{f.thread.storage,f.thread.offset+relative};f.services->os->current_thread_address=igb_bootstrap_address(source);
        f.services->os->mappings={source};auto expected=f.thread.storage->bytes->bytes;
        store_expected(expected,f.thread.offset+0x350,igb_bootstrap_address(source));original_copy(expected,source.offset,f.thread.offset);
        store_expected(expected,f.thread.offset+0x20,0);IgbGamecubeThreadAdoptInvocation run(f.thread);
        check(run.resume(f.services).completed && f.thread.storage->bytes->bytes==expected,
            "Aliased context copy lost the original paired-register read/write ordering.");
    }
}

void source_group_failures_preserve_exact_prefix(){
    struct Case{unsigned missing,prefix;};
    for(const auto c:std::array<Case,18>{{{0,0},{4,0},{8,8},{12,8},{0x2c4,0x2c0},{0x2c8,0x2c8},{0x2c9,0x2c8},{0x2ca,0x2ca},
        {0x2ce,0x2cc},{0x2dc,0x2dc},{0x2e4,0x2e0},{0x2ec,0x2e8},{0x2f0,0x2f0},{0x2f8,0x2f4},{0x300,0x2fc},
        {0x304,0x304},{0x30c,0x30c},{0x314,0x310}}}){
        Fixture f;igb_bootstrap_write_word(f.thread,0x20,0xfedcba98);
        f.source.storage->bytes->initialized[f.source.offset+c.missing]=0;IgbGamecubeThreadAdoptInvocation run(f.thread);
        rejects([&]{run.resume(f.services);},"Grouped adoption copy accepted an unknown reached source byte.");
        check(run.faulted() && word(f.thread,0x350)==igb_bootstrap_address(f.source) && word(f.thread,0x20)==0xfedcba98 &&
            f.pool_calls.empty() && f.free_calls.empty(),"Source-read fault lost master/stack prefix or selected stack release.");
        for(unsigned i=0;i<0x318;++i)check(f.thread.storage->bytes->initialized[f.thread.offset+0x38+i]==unsigned(i<c.prefix) &&
            f.thread.storage->bytes->bytes[f.thread.offset+0x38+i]==(i<c.prefix?f.source.storage->bytes->bytes[f.source.offset+i]:0xcd),
            "Source-read fault wrote part of its pair/halfword or discarded an earlier group.");
        f.source.storage->bytes->initialized[f.source.offset+c.missing]=1;
        rejects([&]{run.resume();},"Faulted grouped copy resumed after source repair.");
    }
}

void target_group_failures_preserve_exact_prefix(){
    // Truncate only after the first +350 store. Otherwise the original first
    // publication, rather than the intended grouped destination write, fails.
    struct Case{unsigned available,prefix;};
    for(const auto c:std::array<Case,8>{{{3,0},{7,4},{0x2c9,0x2c8},{0x2cb,0x2ca},
        {0x2cf,0x2cc},{0x2e7,0x2e4},{0x303,0x300},{0x317,0x314}}}){
        Fixture f;bool truncated{};IgbGamecubeThreadAdoptInvocation run(f.thread);
        const auto guard=[&]{if(!truncated && known(f.thread,0x350)){truncated=true;const auto size=f.thread.offset+0x38+c.available;
            f.thread.storage->bytes->bytes.resize(size);f.thread.storage->bytes->initialized.resize(size);}};
        rejects([&]{run.resume(f.services,guard);},"Grouped adoption copy accepted an incomplete destination word/halfword.");
        check(truncated && run.faulted() && f.pool_calls.empty(),"Truncated target reached stack release or missed its store fault.");
        for(unsigned i=0;i<c.available;++i)check(f.thread.storage->bytes->initialized[f.thread.offset+0x38+i]==unsigned(i<c.prefix) &&
            f.thread.storage->bytes->bytes[f.thread.offset+0x38+i]==(i<c.prefix?f.source.storage->bytes->bytes[f.source.offset+i]:0xcd),
            "Destination word/halfword failure changed the completed store prefix.");
        rejects([&]{run.resume();},"Failed destination store replayed its partial group.");
    }
}

void captured_source_and_target_identities(){
    for(bool source_side:{false,true})for(unsigned mutation=0;mutation<3;++mutation){
        Fixture f;IgbGamecubeThreadAdoptInvocation run(f.thread);const auto storage=source_side?f.source.storage:f.thread.storage;
        const auto owner=storage->bytes;const auto address=storage->original_address;bool changed{};std::vector<std::uint8_t> before,mask;
        const auto guard=[&]{if(!changed && known(f.thread,0x38,8)){changed=true;before=f.thread.storage->bytes->bytes;mask=f.thread.storage->bytes->initialized;
            if(mutation==0)storage->bytes=bytes(owner->bytes.size(),0x65,1);if(mutation==1)storage->original_address+=0x100;if(mutation==2)owner->released=true;}};
        rejects([&]{run.resume(f.services,guard);},"Grouped copy silently rebound a captured source or target identity.");
        const auto target=source_side?f.thread.storage->bytes:owner;
        check(changed && run.faulted() && target->bytes==before && target->initialized==mask && f.pool_calls.empty(),
            "Identity invalidation changed a later copy group or lost its completed prefix.");
        storage->bytes=owner;owner->released=false;storage->original_address=address;
        rejects([&]{run.resume();},"Identity fault revived after restoring the original context allocation.");
    }
    Fixture late;late.services->os->mappings.clear();IgbGamecubeThreadAdoptInvocation waiting(late.thread);
    check(!waiting.resume(late.services).completed && known(late.thread,0x350),"Late source fixture did not reach captured mapping suspension.");
    const auto owner=late.thread.storage->bytes;const auto before=owner->bytes,mask=owner->initialized;
    late.thread.storage->bytes=bytes(owner->bytes.size(),0xe5,1);late.services->os->mappings.push_back(late.source);
    rejects([&]{waiting.resume();},"Late source resolution redirected writes to a replacement target owner.");
    check(owner->bytes==before && owner->initialized==mask && late.thread.storage->bytes->bytes==std::vector<std::uint8_t>(owner->bytes.size(),0xe5),
        "Late-source identity fault changed original or replacement target storage.");
}

void fresh_stack_and_captured_free_operands(){
    Fixture f;bool adjusted{},pool_ready{},free_ready{};IgbGamecubeThreadAdoptInvocation run(f.thread);
    f.pool_action=[&](const IgbMemoryFieldConstructCall&){return MessengerCallResult{pool_ready,0,"Prepared tagged pool."};};
    f.free_action=[&](const IgbGamecubeThreadAdoptCall&,const std::function<void()>&){return MessengerCallResult{free_ready,0,"Prepared stack release."};};
    const auto guard=[&]{if(!adjusted && known(f.thread,0x34c)){adjusted=true;igb_bootstrap_write_word(f.thread,0x20,0xfedcba98);igb_bootstrap_write_byte(f.thread,4,0xff);}};
    check(!run.resume(f.services,guard).completed && adjusted && f.pool_calls.size()==1 && f.pool_calls[0].handle==0xff &&
        f.pool_calls[0].kind==IgbMemoryFieldConstructServiceKind::tagged_pool && word(f.thread,0x20)==0xfedcba98 && f.free_calls.empty(),
        "Stack free reused an early stack/tag operand or cleared the stack before pool selection returned.");
    igb_bootstrap_write_word(f.thread,0x20,0x12345678);igb_bootstrap_write_byte(f.thread,4,2);pool_ready=true;
    check(!run.resume().completed && f.free_calls.size()==1 && f.free_calls[0].address==0xfedcba98 && f.free_calls[0].virtual_slot==0x108 &&
        f.free_calls[0].pool==f.pool && word(f.thread,0x20)==0x12345678,"Selected free lost the captured full stack pointer/pool or used the wrong pool slot.");
    f.free_operations[0]->resume_checked=[](const std::function<void()>&)->MessengerCallResult{
        throw std::runtime_error("Replacement free callback must not run.");};
    f.services->factory={};f.services->memory->factory={};f.source.storage->bytes->released=true;free_ready=true;
    check(run.resume().completed && word(f.thread,0x20)==0 && f.free_polls==2 && f.pool_polls==2,
        "Pending free lost its selected continuation, replayed copying or used completed source storage.");
}

void late_services_and_conflicting_sources(){
    Fixture f;igb_bootstrap_write_word(f.thread,0x20,0x87654321);const auto pool=f.services->memory->factory;const auto free=f.services->factory;
    f.services->memory->factory={};f.services->factory={};IgbGamecubeThreadAdoptInvocation run(f.thread);
    check(!run.resume(f.services).completed && known(f.thread,0x34c) && word(f.thread,0x20)==0x87654321 && f.pool_calls.empty(),
        "Unbound tagged-pool service discarded the completed copy or fabricated release.");
    f.services->memory->factory=pool;check(!run.resume().completed && f.pool_calls.size()==1 && f.free_calls.empty(),
        "Unbound +108 release endpoint became completed free.");
    f.services->factory=free;check(run.resume().completed && word(f.thread,0x20)==0 && f.pool_polls==1 && f.free_polls==1,
        "Late lower services replayed completed copy/pool work or failed to clear the stack after release.");
    Fixture empty;igb_bootstrap_write_word(empty.thread,0x20,0x80003000);auto operation=std::make_shared<IgbGamecubeThreadAdoptOperation>();
    empty.services->factory=[operation](const IgbGamecubeThreadAdoptCall&){return operation;};IgbGamecubeThreadAdoptInvocation callback(empty.thread);
    check(!callback.resume(empty.services).completed && word(empty.thread,0x20)==0x80003000,"Missing retained free callback became a no-op.");
    operation->resume=[] {return MessengerCallResult{true,0,{}};};check(callback.resume().completed && word(empty.thread,0x20)==0,
        "Late legacy free callback failed to resume its retained operation.");
    Fixture null_pool;igb_bootstrap_write_word(null_pool.thread,0x20,0x80002000);null_pool.pool.reset();IgbGamecubeThreadAdoptInvocation bad_pool(null_pool.thread);
    rejects([&]{bad_pool.resume(null_pool.services);},"Known null tag lookup result was dereferenced as a valid +108 pool.");
    check(bad_pool.faulted() && known(null_pool.thread,0x34c) && word(null_pool.thread,0x20)==0x80002000 && null_pool.free_calls.empty(),
        "Null pool failure lost copied context/stack prefix or selected release.");
    Fixture conflict;auto duplicate=raw(conflict.source.storage->original_address,0x318);pattern(duplicate);
    conflict.services->os->mappings.push_back(duplicate);IgbGamecubeThreadAdoptInvocation mapped(conflict.thread);
    rejects([&]{mapped.resume(conflict.services);},"Conflicting physical mappings silently selected a current-thread source.");
    check(mapped.faulted() && known(conflict.thread,0x350) && !known(conflict.thread,0x38),
        "Conflicting source resolution lost first publication or started the copy.");
}

void free_fault_prefix_and_reentry(){
    for(unsigned mode=0;mode<4;++mode){Fixture f;igb_bootstrap_write_word(f.thread,0x20,0x80004000);IgbGamecubeThreadAdoptInvocation run(f.thread);unsigned caught{};
        f.free_action=[&](const IgbGamecubeThreadAdoptCall&,const std::function<void()>& guard)->MessengerCallResult{
            if(mode==0)throw std::runtime_error("Prepared free failure.");
            if(mode==1){try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}}
            if(mode==2)f.runtime->physical_storage.reset();
            if(mode==3){try{run.resume(f.services,guard);}catch(const std::runtime_error&){++caught;}}
            return {true,0,{}};};
        rejects([&]{run.resume(f.services);},"Failed or caught-recursive free cleared the thread stack.");
        check(run.faulted() && known(f.thread,0x34c) && word(f.thread,0x20)==0x80004000 && f.free_polls==1 &&
            ((mode!=1 && mode!=3) || caught==1),"Free failure changed copied context/stack prefix or missed recursive invalidation.");
        f.runtime->physical_storage=IgbRuntimeObjectStorageBinding{f.thread.storage,f.thread.offset};f.free_action={};
        rejects([&]{run.resume();},"Failed free revived after callback or binding repair.");check(f.free_polls==1,"Free failure replayed an irreversible release.");
    }
    for(unsigned boundary=0;boundary<3;++boundary){Fixture f;IgbGamecubeThreadAdoptInvocation run(f.thread);bool caught{};
        const auto guard=[&]{const bool first=known(f.thread,0x350),copied=known(f.thread,0x38);
            if(!caught && ((boundary==0 && !first) || (boundary==1 && first && !copied) || (boundary==2 && copied))){caught=true;
                try{run.resume(f.services);}catch(const std::runtime_error&){};}};
        rejects([&]{run.resume(f.services,guard);},"Caught borrowed-guard reentry performed another adoption store.");
        check(caught && run.faulted() && known(f.thread,0x350)==(boundary!=0) && known(f.thread,0x38)==(boundary==2) &&
            !known(f.thread,0x3c) && f.pool_calls.empty(),"Borrowed-guard recursion changed the immediately following publication/copy store.");
    }
}

void actual_startup_parent_guard(){
    Fixture f;igb_bootstrap_write_word(f.thread,4,2);igb_bootstrap_write_word(f.thread,0x20,0x80005000);
    auto startup=std::make_shared<IgbThreadStartupServices>();startup->pool_handle=4;startup->manager=IgbBootstrapPointer{};
    auto manager=raw(0x300000,0x80);igb_bootstrap_write_word(manager,0,0x804700a8);igb_bootstrap_write_word(manager,4,2);
    const auto native=make_native_igb_gamecube_thread_adopt_factory(f.services);unsigned inserted{},caught{};
    startup->factory=[&](const IgbThreadStartupCall& call){
        if(call.kind==IgbThreadStartupServiceKind::object_virtual && call.virtual_slot==0xdc)return native(call);
        auto op=std::make_shared<IgbThreadStartupOperation>();op->resume=[&,call,out=op.get()]{
            if(call.kind==IgbThreadStartupServiceKind::pool_for_handle)out->pool_result=f.pool;
            else if(call.kind==IgbThreadStartupServiceKind::create_manager)out->object_result=manager;
            else if(call.kind==IgbThreadStartupServiceKind::create_thread)out->object_result=f.thread;
            else if(call.kind==IgbThreadStartupServiceKind::object_virtual && call.virtual_slot==0x60)++inserted;
            else throw std::runtime_error("Unexpected prepared startup endpoint.");return MessengerCallResult{true,0,{}};};return op;};
    IgbThreadStartupInvocation run;f.free_action=[&](const IgbGamecubeThreadAdoptCall&,const std::function<void()>&)->MessengerCallResult{
        try{run.resume(startup);}catch(const std::runtime_error&){++caught;}return {true,0,{}};};
    rejects([&]{run.resume(startup);},"Real startup parent reentry did not stop its nested thread adoption.");
    check(caught==1 && run.faulted() && inserted==0 && startup->manager->storage==manager.storage &&
        igb_bootstrap_read_byte(f.thread,0x2c)==1 && known(f.thread,0x34c) && word(f.thread,0x20)==0x80005000 && word(f.thread,4)==2,
        "Caught real startup reentry cleared stack, inserted/released the thread, or lost its prior copy/publication prefix.");
    rejects([&]{run.resume(startup);},"Faulted actual startup replayed adoption.");check(f.free_polls==1,"Faulted startup repeated stack release.");
}

void exact_adapter_scope(){
    Fixture f;auto sentinel=std::make_shared<IgbThreadStartupOperation>();const auto factory=make_native_igb_gamecube_thread_adopt_factory(f.services,
        [&](const IgbThreadStartupCall&){return sentinel;});IgbThreadStartupCall call;call.kind=IgbThreadStartupServiceKind::object_virtual;
    call.object=f.thread;call.vtable_word=0x80470110;call.virtual_slot=0xdc;const auto operation=factory(call);
    check(operation && operation!=sentinel && operation->resume_checked && operation->resume_checked({}).completed,
        "Exact current-thread adoption adapter omitted its checked continuation.");
    for(const auto slot:{0x2cu,0x58u,0x60u,0xa8u}){call.virtual_slot=slot;check(factory(call)==sentinel,"Adoption adapter claimed another thread operation.");}
    call.virtual_slot=0xdc;call.vtable_word=0x804700a8;check(factory(call)==sentinel,"Adoption adapter claimed a different object's +DC.");
    call.vtable_word=0x80470110;call.kind=IgbThreadStartupServiceKind::create_thread;
    check(factory(call)==sentinel,"Adoption adapter replaced reflected thread construction.");
}
}
int main(){try{
    exact_copy_and_null_stack_skip_tag();separate_current_loads_and_late_mapping();actual_pair_alias_order();
    source_group_failures_preserve_exact_prefix();target_group_failures_preserve_exact_prefix();captured_source_and_target_identities();
    fresh_stack_and_captured_free_operands();late_services_and_conflicting_sources();free_fault_prefix_and_reentry();actual_startup_parent_guard();exact_adapter_scope();
    std::cout<<"igb_gamecube_thread_adopt_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"GameCube thread adoption tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
