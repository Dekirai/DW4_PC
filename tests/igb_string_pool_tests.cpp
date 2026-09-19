#include "dw4/igb_string_pool.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
using K=IgbStringPoolServiceKind;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& fn,const char* reason) {
    ++checks;try {fn();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
IgbRuntimeNameSource bytes(std::string value,std::uint32_t offset=0) {
    auto buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes.resize(offset+value.size()+4);
    std::copy(value.begin(),value.end(),buffer->bytes.begin()+offset);
    buffer->initialized.assign(buffer->bytes.size(),1);return {buffer,offset};
}
std::shared_ptr<IgbStringEntry> entry(std::string name,std::uint32_t references=1) {
    auto value=std::make_shared<IgbStringEntry>();value->owner=std::make_shared<IgbInternedString>();
    value->owner->value=text(name);value->owner->references=references;value->bytes=bytes(std::move(name));return value;
}
struct Fixture {
    std::shared_ptr<ModelSceneObject> owner=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbStringPoolState> state=std::make_shared<IgbStringPoolState>();
    std::shared_ptr<IgbStringPoolServices> services=std::make_shared<IgbStringPoolServices>();
    std::shared_ptr<ModelSceneObject> container=std::make_shared<ModelSceneData>();
    std::function<void(const IgbStringPoolCall&,IgbStringPoolOperation&)> mutate;
    std::vector<K> trace;
    Fixture() {
        state->table=std::make_shared<IgbStringTable>();state->table->capacity=2;
        state->table->storage=std::make_shared<IgbStringTable::Storage>(2);
        state->arena=std::make_shared<ModelSceneData>();services->mutex=std::shared_ptr<LoaderDirectoryMutex>{};
        igb_bind_string_pool(services,owner,state);
        services->factory=[this](const IgbStringPoolCall& request) {
            auto operation=std::make_shared<IgbStringPoolOperation>();const auto weak=std::weak_ptr(operation);
            operation->resume=[this,request,weak] {
                const auto out=weak.lock();trace.push_back(request.kind);
                if(request.kind==K::allocate_string) {
                    check(request.arena==state->arena,"String allocation captured the wrong arena.");
                    check(request.bytes>0 && request.bytes<1024*1024,"String allocation size is not strlen+1.");
                    out->entry_result=entry("");out->entry_result->owner->references=77;
                    out->entry_result->bytes.buffer->bytes.resize(request.bytes+4);
                    out->entry_result->bytes.buffer->initialized.assign(request.bytes+4,1);
                    out->container_result=container;
                }else {
                    check(request.bytes==request.capacity*4u,"String table allocation lost original byte count.");
                    out->storage_result=std::make_shared<IgbStringTable::Storage>(*request.storage);
                    out->storage_result->resize(request.capacity);
                }
                if(mutate)mutate(request,*out);return MessengerCallResult{true,0,{}};
            };
            return operation;
        };
    }
    std::shared_ptr<IgbInternedString> intern(const std::string& value) {
        IgbStringInternInvocation call(owner,text(value));check(call.resume(services).completed,"Prepared string intern did not complete.");
        check(call.resume(services).completed,"Completed string intern did not remain complete.");return call.result();
    }
};
void ordinary() {
    Fixture f;std::vector<std::shared_ptr<IgbInternedString>> values;
    for(const auto& name:{"zulu","alpha","delta","bravo","echo","charlie",""})values.push_back(f.intern(name));
    check(f.state->table->count==7 && f.state->table->capacity==8 && f.state->table->step==4,"Sorted table growth or step changed.");
    std::string prior;
    for(std::size_t index=0;index<7;++index) {
        const auto& item=(*f.state->table->storage)[index];
        check(item && item->owner && item->owner->value,"Inserted string lost header identity.");
        check(index==0 || prior<*item->owner->value,"String table ordering is incorrect.");prior=*item->owner->value;
        check(item->owner->references==1 && item->owner->container==f.container,"Inserted header publication changed.");
    }
    const auto before=f.trace.size();check(f.intern("alpha")==values[1],"Cached intern changed identity.");
    check(values[1]->references==2 && f.trace.size()==before,"Cached intern allocated or missed its retain.");
    values[1]->references=0xffffffffu;check(f.intern("alpha")->references==0,"String retain did not wrap the full word.");
    check(f.intern("Alpha")!=values[1],"Intern lookup is not case-sensitive.");
}
void search_edges() {
    auto table=std::make_shared<IgbStringTable>();
    check(!igb_string_table_search(table,IgbDependencyName{}).entry,"Empty search inspected its null input.");
    table->count=1;table->step=1;table->storage=std::make_shared<IgbStringTable::Storage>(1);
    rejects([&] {igb_string_table_search(table,text("a"));},"Null physical entry was accepted.");
    auto stored=entry("a");(*table->storage)[0]=stored;
    for(std::uint32_t offset=0;offset<4;++offset) {
        const auto source=bytes("a",offset);const auto found=igb_string_table_search(table,source);
        check(found.entry==stored && found.index==0,"Aligned/unaligned lookup changed the first match.");
    }
    auto short_source=bytes("a");short_source.buffer->bytes.resize(2);short_source.buffer->initialized.resize(2);
    rejects([&] {igb_string_table_search(table,short_source);},"Aligned compare skipped bytes beyond terminator.");
    auto early=bytes("b");early.buffer->bytes.resize(1);early.buffer->initialized.resize(1);
    check(igb_string_table_search(table,early).index==1,"First-byte mismatch inspected unavailable later bytes.");
    auto same=bytes("a",1);same.buffer->bytes.resize(3);same.buffer->initialized.resize(3);
    check(igb_string_table_search(table,same).entry==stored,"Unaligned compare invented aligned tail reads.");
    stored->bytes.buffer->released=true;
    rejects([&] {igb_string_table_search(table,text("a"));},"Released string storage was accepted.");
    stored->bytes.buffer->released=false;table->step=2;table->count=0;
    check(igb_string_table_search(table,IgbDependencyName{}).index==0,"Skipped search candidate read its input.");
}
void boundaries() {
    IgbStringInternInvocation null({},{});check(null.resume().completed && !null.result(),"Null intern required a pool or globals.");
    IgbStringInternInvocation bad({},text("a"));rejects([&] {bad.resume();},"Nonnull intern accepted a null pool.");
    Fixture f;IgbStringInternInvocation pending(f.owner,text("a"));
    check(!pending.resume().completed,"Unknown pool was invented.");
    f.services->mutex.reset();check(!pending.resume(f.services).completed,"Unknown lock global was treated as null.");
    f.services->mutex=std::shared_ptr<LoaderDirectoryMutex>{};const auto factory=f.services->factory;f.services->factory={};
    check(!pending.resume(f.services).completed,"Missing arena allocation completed.");
    f.services->factory=factory;check(pending.resume(f.services).completed,"Delayed allocator did not resume.");
    check(*pending.result()->value=="a" && f.state->table->count==1,"Delayed intern replayed table insertion.");
    Fixture abandoned;{IgbStringInternInvocation call(abandoned.owner,text("a"));abandoned.services->factory={};check(!call.resume(abandoned.services).completed,"Unbound allocation completed.");}
    check(abandoned.state->faulted(),"Abandoned allocation did not invalidate its original prefix.");
    IgbStringInternInvocation restart(abandoned.owner,text("a"));rejects([&] {restart.resume(abandoned.services);},"Abandoned original prefix restarted.");
}
void mutexes_and_mutation() {
    Fixture f;auto first=std::make_shared<LoaderDirectoryMutex>(),second=std::make_shared<LoaderDirectoryMutex>();
    unsigned locks{},unlocks{};f.services->mutex=first;
    first->acquire=[&] {++locks;if(locks==1)return MessengerCallResult{false,0,"wait"};f.services->mutex=second;return MessengerCallResult{true,0,{}};};
    first->release=[]()->MessengerCallResult {throw std::runtime_error("Stale unlock owner.");};
    second->release=[&] {++unlocks;return MessengerCallResult{true,0,{}};};
    second->acquire=[&] {++locks;return MessengerCallResult{true,0,{}};};
    IgbStringInternInvocation call(f.owner,text("a"));check(!call.resume(f.services).completed && locks==1,"Pending lock was not retained.");
    auto replacement=std::make_shared<LoaderDirectoryMutex>();f.services->mutex=replacement;
    check(call.resume(f.services).completed && locks==3 && unlocks==2,"Lock capture or live unlock selection changed.");
    Fixture changed;auto source=bytes("alpha");auto replacement_table=std::make_shared<IgbStringTable>();
    replacement_table->capacity=2;replacement_table->storage=std::make_shared<IgbStringTable::Storage>(2);
    changed.mutate=[&](const IgbStringPoolCall& request,IgbStringPoolOperation&) {
        if(request.kind==K::allocate_string) {source.buffer->bytes[0]='o';changed.state->table=replacement_table;}
    };
    IgbStringInternInvocation live(changed.owner,source);check(live.resume(changed.services).completed,"Mutable name intern did not complete.");
    check(*live.result()->value=="olpha" && replacement_table->count==1,"Intern copied an eagerly frozen name or reused the old table.");
    Fixture duplicate;auto duplicate_entry=entry("same",9);auto mutex=std::make_shared<LoaderDirectoryMutex>();unsigned acquired{};
    mutex->acquire=[&] {if(++acquired==2) {duplicate.state->table->count=1;duplicate.state->table->step=1;(*duplicate.state->table->storage)[0]=duplicate_entry;}return MessengerCallResult{true,0,{}};};
    mutex->release=[] {return MessengerCallResult{true,0,{}};};duplicate.services->mutex=mutex;
    auto new_value=duplicate.intern("same");
    check(duplicate_entry->owner->references==10 && duplicate.state->table->count==2 && new_value!=duplicate_entry->owner,
        "Insertion's second search skipped duplicate retain or suppressed original insertion.");
    Fixture unterminated;auto unstable=bytes("a");
    unterminated.mutate=[&](const IgbStringPoolCall& request,IgbStringPoolOperation& op) {
        if(request.kind==K::allocate_string) {
            unstable.buffer->bytes[1]='b';op.entry_result->bytes.buffer->bytes.resize(2);
            op.entry_result->bytes.buffer->initialized.resize(2);
        }
    };
    IgbStringInternInvocation raw(unterminated.owner,unstable);
    check(raw.resume(unterminated.services).completed && unterminated.state->table->count==1,
        "Empty insertion invented an extra terminator scan after the original bounded copy.");
    check(raw.result()->native_name && *raw.result()->value=="ab","Interned header lost its physical byte identity/cache.");
    rejects([&] {igb_runtime_read_name(*raw.result()->native_name);},"Unterminated native string was silently padded by its host cache.");
    Fixture overlap;auto shared=bytes("a");
    overlap.mutate=[&](const IgbStringPoolCall& request,IgbStringPoolOperation& op) {
        if(request.kind==K::allocate_string)op.entry_result->bytes={shared.buffer,1};
    };
    IgbStringInternInvocation overlapping(overlap.owner,shared);
    check(overlapping.resume(overlap.services).completed && shared.buffer->bytes[1]=='a' && shared.buffer->bytes[2]==0,
        "Aliased copy lost the original backward-copy direction.");
    Fixture nested;bool entered{};std::shared_ptr<IgbInternedString> inner;
    nested.mutate=[&](const IgbStringPoolCall& request,IgbStringPoolOperation&) {
        if(request.kind==K::allocate_string && !entered) {entered=true;inner=nested.intern("same");}
    };
    const auto outer=nested.intern("same");
    check(inner!=outer && inner->references==2 && outer->references==1 && nested.state->table->count==2,
        "Unlocked nested interning was blocked or lost insertion's second-search retain.");
}
void failures() {
    Fixture null_result;null_result.mutate=[](const IgbStringPoolCall&,IgbStringPoolOperation& op) {op.entry_result.reset();};
    IgbStringInternInvocation null_call(null_result.owner,text("a"));rejects([&] {null_call.resume(null_result.services);},"Null arena result did not fail at copy.");
    Fixture grow;grow.intern("a");grow.intern("b");grow.mutate=[](const IgbStringPoolCall& request,IgbStringPoolOperation& op) {if(request.kind==K::resize_table)op.storage_result.reset();};
    IgbStringInternInvocation grow_call(grow.owner,text("c"));rejects([&] {grow_call.resume(grow.services);},"Null realloc result did not reach physical failure.");
    check(!grow.state->table->storage && grow.state->table->capacity==4 && grow.state->table->count==2,"Realloc raw publication or failure prefix changed.");
    Fixture recursive;IgbStringInternInvocation recursive_call(recursive.owner,text("a"));
    recursive.mutate=[&](const IgbStringPoolCall&,IgbStringPoolOperation&) {rejects([&] {recursive_call.resume(recursive.services);},"Recursive call was accepted.");};
    rejects([&] {recursive_call.resume(recursive.services);},"Swallowed recursive failure let outer invocation continue.");
    check(recursive_call.faulted() && recursive.state->faulted(),"Recursive fault did not invalidate pool state.");
}
void adapter() {
    Fixture f;IgbDependenciesCall request;request.kind=IgbDependenciesServiceKind::intern_name;
    request.pool=f.owner;request.name=text("folder");auto factory=make_native_igb_string_factory(f.services);
    auto operation=factory(request);check(operation && operation->resume().completed,"Dependency adapter did not run native interning.");
    auto registry=std::make_shared<IgbInternedStringRegistry>();igb_bind_interned_string(registry,operation->string_result);
    check(registry->owners.size()==1 && *registry->owners[0]->value=="folder","Native result lost canonical ownership binding.");
    const auto original=operation->string_result;request.name=original->value;
    original->native_name->buffer->bytes[original->native_name->offset]='g';
    auto physical=factory(request);check(physical->resume().completed && physical->string_result==original && original->references==2,
        "String adapter compared a stale host cache instead of authoritative native bytes.");
    check(std::holds_alternative<IgbRuntimeNameSource>(igb_interned_string_name(original)),
        "Dependency restoration discarded native character ownership.");
    request.kind=IgbDependenciesServiceKind::release_string;check(!factory(request),"String adapter invented container release.");
}
}
int main() {
    try {ordinary();search_edges();boundaries();mutexes_and_mutation();failures();adapter();std::cout<<checks<<" string-pool checks passed.\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
