#include "dw4/igb_string_pool.hpp"

#include <algorithm>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbStringPoolError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
constexpr std::uint32_t maximum_slots=262144,maximum_name=1024*1024;
bool null_name(const IgbDependencyName& name) {return std::holds_alternative<std::monostate>(name);}
std::uint32_t alignment(const IgbDependencyName& name) {
    if(const auto source=std::get_if<IgbRuntimeNameSource>(&name))return source->offset&3u;
    return 0;
}
std::uint8_t read_byte(const IgbDependencyName& name,std::uint32_t offset) {
    if(const auto source=std::get_if<IgbRuntimeNameSource>(&name))
        return igb_buffer_bytes(source->buffer,static_cast<std::uint32_t>(source->offset+offset),1).front();
    if(const auto value=std::get_if<SystemStageString>(&name)) {
        if(!*value)fail("String operation reaches an unrepresented nonnull host string.");
        // Immutable host strings expose a four-byte padded, aligned native view.
        if(offset<(*value)->size())return static_cast<std::uint8_t>((**value)[offset]);
        if(static_cast<std::size_t>(offset)<(*value)->size()+4u)return 0;
    }
    fail("String operation reaches unavailable character storage.");
}
std::uint32_t read_word(const IgbDependencyName& name,std::uint32_t offset) {
    std::uint32_t value{};for(unsigned index=0;index<4;++index)value=(value<<8u)|read_byte(name,offset+index);
    return value;
}
// Actual strcmp8009D5D8, including aligned reads beyond a terminator and its
// unsigned word ordering. Only the sign is consumed by the table search.
int compare(const IgbDependencyName& left,const IgbDependencyName& right) {
    std::uint32_t offset{};auto a=read_byte(left,0),b=read_byte(right,0);
    if(a!=b)return static_cast<int>(a)-b;
    if(alignment(left)==alignment(right)) {
        const auto align=alignment(left);
        if(align!=0) {
            if(a==0)return 0;
            for(std::uint32_t index=0;index<3u-align;++index) {
                ++offset;a=read_byte(left,offset);b=read_byte(right,offset);
                if(a!=b)return static_cast<int>(a)-b;
                if(a==0)return 0;
            }
            ++offset;
        }
        for(;;) {
            if(offset>=maximum_name)fail("String comparison exceeds the native name profile.");
            const auto first=read_word(left,offset),second=read_word(right,offset);
            if(((first-0x01010101u)&0x80808080u)!=0) {
                a=read_byte(left,offset);b=read_byte(right,offset);break;
            }
            if(first!=second)return first<second?-1:1;
            offset+=4;
        }
    }
    for(;;) {
        if(a!=b)return static_cast<int>(a)-b;
        if(a==0)return 0;
        if(++offset>=maximum_name)fail("String comparison exceeds the native name profile.");
        a=read_byte(left,offset);b=read_byte(right,offset);
    }
}
std::uint32_t length(const IgbDependencyName& name) {
    for(std::uint32_t index=0;index<maximum_name;++index)if(read_byte(name,index)==0)return index;
    fail("String length exceeds the native name profile.");
}
std::shared_ptr<IgbStringEntry>& slot(const std::shared_ptr<IgbStringTable::Storage>& storage,std::uint32_t index) {
    if(!storage || index>=maximum_slots || index>=storage->size())fail("String table reaches an unavailable physical slot.");
    return (*storage)[index];
}
void write_byte(const IgbRuntimeNameSource& target,std::uint32_t offset,std::uint8_t value) {
    const auto position=static_cast<std::uint32_t>(target.offset+offset);
    if(!target.buffer || target.buffer->released || position>=target.buffer->bytes.size() || position>=target.buffer->initialized.size())
        fail("String copy reaches unavailable destination storage.");
    target.buffer->bytes[position]=value;target.buffer->initialized[position]=1;
}
}
void igb_bind_string_pool(const std::shared_ptr<IgbStringPoolServices>& services,
    const std::shared_ptr<ModelSceneObject>& owner,const std::shared_ptr<IgbStringPoolState>& state) {
    if(!services || !owner || !state)fail("String-pool binding requires live service, owner and state identities.");
    for(const auto& item:services->pools)if(item.owner.lock()==owner) {
        if(item.state!=state)fail("One string-pool identity has conflicting native state.");return;
    }
    services->pools.push_back({owner,state});
}
IgbStringSearchResult igb_string_table_search(const std::shared_ptr<IgbStringTable>& table,const IgbDependencyName& name) {
    if(!table)fail("String search dereferences a null table.");
    auto step=table->step;std::uint32_t lower{};
    while(step!=0) {
        const auto candidate=lower+step;
        if(table->count>=candidate) {
            const auto entry=slot(table->storage,candidate-1u);
            if(!entry)fail("String search dereferences a null entry header.");
            const auto ordering=compare(name,IgbDependencyName(entry->bytes));
            if(ordering==0) {
                if(!entry->owner || entry->owner->released)fail("String search reaches an unavailable header reference word.");
                ++entry->owner->references;return {entry,candidate-1u};
            }
            if(ordering>0)lower=candidate;
        }
        step>>=1u;
    }
    return {{},lower};
}
struct IgbStringInternInvocation::Impl {
    enum class Phase {entry,bind,lookup,lock,after_lock,search,unlock,after_unlock,lookup_done,
        allocate,after_allocate,copy,insert,initialize,insert_search,grow,after_grow,move,store,done,call};
    std::shared_ptr<ModelSceneObject> owner,container;
    std::shared_ptr<IgbStringPoolState> pool;
    std::shared_ptr<IgbStringTable> table;
    std::shared_ptr<IgbStringEntry> entry;
    std::shared_ptr<IgbInternedString> result;
    IgbDependencyName name;
    std::shared_ptr<LoaderDirectoryMutex> mutex;
    std::function<MessengerCallResult()> mutex_callback,callback;
    IgbStringPoolCall request;
    IgbStringPoolFactory factory;
    std::shared_ptr<IgbStringPoolOperation> operation;
    Phase phase=Phase::entry,after{},locked{},unlocked{};
    std::uint32_t bytes{},index{};
    bool running{},fault{},owns{};
};
IgbStringInternInvocation::IgbStringInternInvocation(std::shared_ptr<ModelSceneObject> owner,IgbDependencyName name)
    :impl_(std::make_unique<Impl>()) {impl_->owner=std::move(owner);impl_->name=std::move(name);}
IgbStringInternInvocation::~IgbStringInternInvocation() {
    if(impl_->owns && !completed()) {
        impl_->pool->fault_=true;
        if(impl_->entry)impl_->pool->unreleased_.push_back(impl_->entry);
    }
}
bool IgbStringInternInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbStringInternInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<IgbInternedString> IgbStringInternInvocation::result() const {
    if(!completed() || faulted())fail("Incomplete string interning has no returned string.");return impl_->result;
}
MessengerCallResult IgbStringInternInvocation::resume(std::shared_ptr<IgbStringPoolServices> services) {
    auto& s=*impl_;using P=Impl::Phase;using K=IgbStringPoolServiceKind;
    if(s.running || s.fault) {s.fault=true;if(s.owns)s.pool->fault_=true;fail("String interning requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Running {bool& v;Running(bool& v):v(v){v=true;}~Running(){v=false;}} running(s.running);
    const auto check=[&] {if(s.fault || (s.owns && s.pool->fault_))fail("String interning continuation was invalidated.");};
    const auto clear=[&] {s.callback={};s.operation.reset();s.factory={};s.request={};check();};
    const auto select=[&](K kind,P after)->IgbStringPoolCall& {
        clear();s.request.kind=kind;s.factory=services?services->factory:IgbStringPoolFactory{};
        s.phase=P::call;s.after=after;return s.request;
    };
    const auto lock=[&](P after) {s.locked=after;s.phase=P::lock;};
    const auto unlock=[&](P after) {s.unlocked=after;s.phase=P::unlock;};
    try {
        for(;;) {check();switch(s.phase) {
        case P::entry:
            if(null_name(s.name)) {s.phase=P::done;break;}
            if(!s.owner)fail("Nonnull string interning dereferences a null pool.");
            s.phase=P::bind;break;
        case P::bind:
            if(services)for(const auto& binding:services->pools)if(binding.owner.lock()==s.owner) {s.pool=binding.state;break;}
            if(!s.pool)return pending("String interning requires the captured pool's native state.");
            if(s.pool->fault_)fail("String pool contains an invalidated original prefix.");
            s.owns=true;s.phase=P::lookup;break;
        case P::lookup:s.table=s.pool->table;lock(P::search);break;
        case P::lock:
        case P::unlock: {
            const bool acquire=s.phase==P::lock;
            if(!services || !services->mutex)return pending("String table requires mutex global805622F4.");
            s.mutex=*services->mutex;
            if(!s.mutex) {s.phase=acquire?s.locked:s.unlocked;break;}
            s.mutex_callback=acquire?s.mutex->acquire:s.mutex->release;
            s.phase=acquire?P::after_lock:P::after_unlock;break;
        }
        case P::after_lock:
        case P::after_unlock: {
            const bool acquire=s.phase==P::after_lock;
            if(!s.mutex_callback)s.mutex_callback=acquire?s.mutex->acquire:s.mutex->release;
            if(!s.mutex_callback)return pending("String table requires its captured mutex virtual operation.");
            const auto result=s.mutex_callback();check();if(!result.completed)return result;
            s.mutex_callback={};s.mutex.reset();check();s.phase=acquire?s.locked:s.unlocked;break;
        }
        case P::search: {
            const auto found=igb_string_table_search(s.table,s.name);
            s.result=found.entry?found.entry->owner:nullptr;unlock(P::lookup_done);break;
        }
        case P::lookup_done:
            if(s.result) {s.phase=P::done;break;}
            s.bytes=length(s.name)+1u;s.phase=P::allocate;break;
        case P::allocate: {
            auto& request=select(K::allocate_string,P::after_allocate);request.arena=s.pool->arena;request.bytes=s.bytes;break;
        }
        case P::after_allocate:
            s.entry=std::move(s.operation->entry_result);s.container=std::move(s.operation->container_result);
            clear();s.phase=P::copy;break;
        case P::copy:
            if(!s.entry)fail("String copy dereferences a null arena result.");
            {
                const auto input=std::get_if<IgbRuntimeNameSource>(&s.name);
                const bool aliases=input && input->buffer==s.entry->bytes.buffer;
                if(!aliases) {
                    // Distinct native allocations have no comparable original
                    // addresses. Require complete ranges before this host copy;
                    // do not claim direction-dependent malformed memcpy traces.
                    for(std::uint32_t index=0;index<s.bytes;++index)(void)read_byte(s.name,index);
                    const auto& target=s.entry->bytes;
                    if(!target.buffer || target.buffer->released || target.offset>target.buffer->bytes.size() ||
                        s.bytes>target.buffer->bytes.size()-target.offset || target.offset>target.buffer->initialized.size() ||
                        s.bytes>target.buffer->initialized.size()-target.offset)
                        fail("String copy requires complete nonoverlapping native ranges.");
                }
                const bool reverse=aliases && input->offset<s.entry->bytes.offset;
                for(std::uint32_t iteration=0;iteration<s.bytes;++iteration) {
                    const auto index=reverse?s.bytes-iteration-1u:iteration;
                    write_byte(s.entry->bytes,index,read_byte(s.name,index));
                }
                if(s.entry->owner) {
                    // Cache only already-copied bytes. In particular, do not
                    // scan for a terminator beyond the captured memcpy length.
                    const auto copied=igb_buffer_bytes(s.entry->bytes.buffer,s.entry->bytes.offset,s.bytes);
                    const auto end=std::find(copied.begin(),copied.end(),std::uint8_t{});
                    s.entry->owner->value=std::make_shared<const std::string>(copied.begin(),end);
                    s.entry->owner->native_name=s.entry->bytes;
                }
            }
            s.phase=P::insert;break;
        case P::insert:s.table=s.pool->table;lock(P::initialize);break;
        case P::initialize:
            if(!s.entry->owner)fail("String insertion dereferences a null header.");
            s.entry->owner->container=s.container;s.entry->owner->references=1;
            s.phase=P::insert_search;break;
        case P::insert_search:
            s.index=igb_string_table_search(s.table,IgbDependencyName(s.entry->bytes)).index;s.phase=P::grow;break;
        case P::grow:
            if(!s.table)fail("String insertion dereferences a null table.");
            if(s.table->count==s.table->capacity) {
                const auto capacity=s.table->capacity;
                auto& request=select(K::resize_table,P::after_grow);request.table=s.table;request.storage=s.table->storage;
                request.capacity=capacity*2u;request.bytes=capacity*8u;
            }else s.phase=P::move;
            break;
        case P::after_grow:
            if(s.table->storage && s.table->storage!=s.operation->storage_result)s.pool->unreleased_.push_back(s.table->storage);
            s.table->storage=std::move(s.operation->storage_result);s.table->capacity=s.request.capacity;
            clear();s.phase=P::move;break;
        case P::move: {
            const auto count=s.table->count-s.index;
            if(count>maximum_slots)fail("String table memmove exceeds the native slot profile.");
            const auto storage=s.table->storage;
            for(std::uint32_t remaining=count;remaining!=0;--remaining) {
                const auto source=slot(storage,s.index+remaining-1u);
                auto& destination=slot(storage,s.index+remaining);
                if(destination)s.pool->unreleased_.push_back(destination);
                destination=source;
            }
            s.phase=P::store;break;
        }
        case P::store: {
            auto& target=slot(s.table->storage,s.index);
            if(target)s.pool->unreleased_.push_back(target);
            target=s.entry;++s.table->count;
            const auto twice=s.table->step*2u;
            if(twice<=s.table->count)s.table->step=s.table->step==0?1:twice;
            s.result=s.entry->owner;unlock(P::done);break;
        }
        case P::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(s.request.kind==K::allocate_string?
                "String interning requires arena allocation80073738.":"String insertion requires physical reallocation8005624C.");
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending("The reached string-pool operation is not bound.");
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending("The reached string-pool operation has no continuation.");
            {const auto result=s.callback();check();if(!result.completed)return result;s.phase=s.after;}
            break;
        case P::done:
            return {true,0,{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.pool->fault_=true;throw;}
}
IgbDependenciesFactory make_native_igb_string_factory(std::shared_ptr<IgbStringPoolServices> services,
    IgbDependenciesFactory remaining) {
    return [services=std::move(services),remaining=std::move(remaining)](const IgbDependenciesCall& request) {
        if(request.kind!=IgbDependenciesServiceKind::intern_name)return remaining?remaining(request):nullptr;
        auto name=request.name;
        if(const auto value=std::get_if<SystemStageString>(&name);value && services && services->strings)
            for(const auto& owner:services->strings->owners)if(owner && owner->value==*value && owner->native_name) {
                name=*owner->native_name;break;
            }
        const auto call=std::make_shared<IgbStringInternInvocation>(request.pool,std::move(name));
        auto operation=std::make_shared<IgbDependenciesOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[call,services,weak] {
            const auto result=call->resume(services);
            if(result.completed) {
                weak.lock()->string_result=call->result();
                if(call->result() && services && services->strings)igb_bind_interned_string(services->strings,call->result());
            }
            return result;
        };
        return operation;
    };
}
} // namespace dw4
