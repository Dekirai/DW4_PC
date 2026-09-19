#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <new>

namespace dw4 {
namespace {
constexpr std::size_t maximum_bytes=64u*1024u*1024u;
[[noreturn]] void fail(const char* message) {throw IgbStreamError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
void range(const std::shared_ptr<IgbScratchBuffer>& buffer,std::size_t offset,std::size_t length) {
    if(!buffer || buffer->released || offset>buffer->bytes.size() || length>buffer->bytes.size()-offset ||
        buffer->initialized.size()!=buffer->bytes.size())fail("IGB reaches an unavailable scratch-buffer range.");
}
std::optional<std::uint32_t> status(const IgbStreamState& state,bool success) {
    if(!state.statuses)return {};
    return success ? state.statuses->success:state.statuses->failure;
}
void claim(std::weak_ptr<void>& active,bool state_fault,const std::shared_ptr<void>& token,bool& owns) {
    if(state_fault)fail("A faulted IGB operation cannot restart its published prefix.");
    if(!owns) {
        if(!active.expired())fail("Another invocation owns this pending IGB operation.");
        active=token;owns=true;
    }
}
SystemStageString derived_name(const SystemStageString& filename) {
    if(!filename)fail("IGB filename derivation dereferences a null stream filename.");
    const auto length=std::strlen(filename->c_str());
    if(length==0)fail("Original IGB filename derivation reaches the byte before an empty filename.");
    auto offset=length-1;
    while(offset>0 && (*filename)[offset-1]!='/' && (*filename)[offset-1]!='\\')--offset;
    return std::make_shared<const std::string>(filename->c_str()+offset,length-offset);
}
}

std::shared_ptr<IgbByteStream> igb_stream_from_bytes(SystemStageString filename,
    std::vector<std::uint8_t> bytes,std::uint32_t alignment) {
    if(bytes.size()>maximum_bytes)fail("IGB byte stream exceeds the64MiB native input limit.");
    auto result=std::make_shared<IgbByteStream>();result->filename=std::move(filename);
    result->size=static_cast<std::int32_t>(bytes.size());result->alignment=alignment;
    result->bytes=std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));return result;
}
std::uint32_t igb_read_elements(const std::shared_ptr<IgbByteStream>& stream,
    const std::shared_ptr<IgbScratchBuffer>& buffer,std::size_t offset,std::uint32_t size,std::uint32_t count) {
    if(!stream || !stream->bytes)fail("IGB read dereferences an unavailable byte stream.");
    const auto length=static_cast<std::uint64_t>(size)*count;
    if(length>maximum_bytes)fail("IGB read exceeds the bounded native transfer profile.");
    range(buffer,offset,static_cast<std::size_t>(length));
    if(size==0 || count==0)return 0;
    if(stream->position>stream->bytes->size())fail("IGB byte stream cursor is outside its supplied data.");
    const auto copied=std::min<std::size_t>(static_cast<std::size_t>(length),stream->bytes->size()-stream->position);
    std::copy_n(stream->bytes->begin()+stream->position,copied,buffer->bytes.begin()+offset);
    std::fill_n(buffer->initialized.begin()+offset,copied,std::uint8_t{1});
    stream->position+=static_cast<std::uint32_t>(copied);
    return static_cast<std::uint32_t>(copied/size);
}
std::span<const std::uint8_t> igb_buffer_bytes(const std::shared_ptr<IgbScratchBuffer>& buffer,
    std::size_t offset,std::size_t length) {
    range(buffer,offset,length);
    if(std::find(buffer->initialized.begin()+offset,buffer->initialized.begin()+offset+length,0)!=
        buffer->initialized.begin()+offset+length)fail("IGB consumes unknown scratch bytes after a short read.");
    return std::span<const std::uint8_t>(buffer->bytes).subspan(offset,length);
}
std::uint32_t igb_buffer_word(const std::shared_ptr<IgbScratchBuffer>& buffer,std::size_t offset) {
    const auto bytes=igb_buffer_bytes(buffer,offset,4);std::uint32_t result{};
    for(const auto value:bytes)result=(result<<8u)|value;
    return result;
}
void igb_swap_words(const std::shared_ptr<IgbScratchBuffer>& buffer,std::size_t offset,
    std::uint16_t width,std::int32_t count) {
    if(width<=1 || count<=0)return;
    for(std::int32_t index=0;index<count;++index) {
        const auto displacement=static_cast<std::size_t>(index)*width;
        if(offset>maximum_bytes || displacement>maximum_bytes-offset)
            fail("IGB byte swap exceeds its bounded native buffer offset.");
        const auto start=offset+displacement;
        for(unsigned byte=0;byte<width/2u;++byte) {
            const auto left=start+byte,right=start+width-byte-1;
            // Original80064B70 reads both bytes before either store.
            const auto a=igb_buffer_bytes(buffer,left,1)[0],b=igb_buffer_bytes(buffer,right,1)[0];
            buffer->bytes[left]=b;buffer->bytes[right]=a;
        }
    }
}

void igb_bind_native_scratch_pool(const std::shared_ptr<IgbStreamState>& state,
    const std::shared_ptr<ModelSceneObject>& pool) {
    if(!state || !pool)fail("A native scratch-pool binding requires a nonnull state and pool identity.");
    for(const auto& binding:state->native_scratch_pools)if(binding.owner.lock()==pool)return;
    state->native_scratch_pools.push_back({pool});
}

struct IgbBufferInvocation::Impl {
    enum class Phase {entry,release,size,allocate,status,done};
    std::shared_ptr<IgbStreamState> state;
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbScratchBuffer> old_buffer;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::int32_t minimum{},capacity{};
    std::uint32_t alignment{},result{};
    Phase phase=Phase::entry;
    bool success{},owns{},running{},fault{};
};
IgbBufferInvocation::IgbBufferInvocation(std::shared_ptr<IgbStreamState> state,std::int32_t minimum):impl_(std::make_unique<Impl>()) {
    impl_->state=std::move(state);impl_->minimum=minimum;
}
IgbBufferInvocation::~IgbBufferInvocation() {if(impl_->owns && !completed())impl_->state->buffer_fault_=true;}
bool IgbBufferInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbBufferInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult IgbBufferInvocation::resume() {
    auto& s=*impl_;
    if(s.running || s.fault) {s.fault=true;if(s.owns)s.state->buffer_fault_=true;fail("IGB buffer operation requires a nonrecursive unfaulted owner.");}
    if(completed())return {true,s.result,{}};
    Running running(s.running);
    try {
        if(!s.state)fail("IGB buffer requires its file state.");
        auto& state=*s.state;claim(state.active_buffer_,state.buffer_fault_,s.token,s.owns);
        const auto bound=[&] {
            for(const auto& binding:state.native_scratch_pools)if(binding.owner.lock()==s.pool)return true;
            return false;
        };
        for(;;)switch(s.phase) {
        case Impl::Phase::entry:
            s.old_buffer=state.buffer;
            if(s.old_buffer && state.buffer_capacity>=s.minimum) {
                s.success=true;s.phase=Impl::Phase::status;break;
            }
            s.file=state.file.lock();
            if(!s.file)fail("IGB scratch operation reaches its unavailable file owner.");
            if(s.old_buffer) {
                s.pool=s.file->pool_118;
                if(!s.pool)fail("IGB scratch free dereferences a null memory pool.");
                s.phase=Impl::Phase::release;
            }else s.phase=Impl::Phase::size;
            break;
        case Impl::Phase::release:
            if(!bound())return pending("IGB scratch free awaits the captured pool virtual+108 binding.");
            // Original pool free leaves +108 pointing at the released
            // allocation until the replacement result is published.
            s.old_buffer->released=true;s.old_buffer.reset();s.pool.reset();
            s.phase=Impl::Phase::size;break;
        case Impl::Phase::size: {
            state.buffer_capacity=std::bit_cast<std::int32_t>(s.file->option_a4);
            if(state.buffer_capacity<0) {
                if(!state.stream)fail("IGB scratch sizing dereferences a null stream.");
                state.buffer_capacity=state.stream->size;
            }
            if(state.buffer_capacity<s.minimum)state.buffer_capacity=s.minimum;
            const auto rounded=(std::bit_cast<std::uint32_t>(state.buffer_capacity)+3u)&~3u;
            state.buffer_capacity=std::bit_cast<std::int32_t>(rounded);
            s.pool=s.file->pool_118;
            const auto stream=state.stream;
            if(!s.pool)fail("IGB scratch allocation dereferences a null memory pool.");
            if(!stream)fail("IGB scratch allocation dereferences stream alignment.");
            s.alignment=stream->alignment&0xffffu;s.capacity=state.buffer_capacity;
            s.phase=Impl::Phase::allocate;break;
        }
        case Impl::Phase::allocate:
            if(!bound())return pending("IGB scratch allocation awaits the captured pool virtual+DC binding.");
            // The explicit native allocator owns bytes, not guest addresses.
            // Keep the selected alignment argument even though vector storage
            // needs no exposed guest-address alignment contract.
            (void)s.alignment;
            if(s.capacity<0 || static_cast<std::uint32_t>(s.capacity)>maximum_bytes)
                fail("IGB scratch allocation exceeds the 64MiB represented allocation profile.");
            try {
                auto buffer=std::make_shared<IgbScratchBuffer>();
                buffer->bytes.resize(static_cast<std::uint32_t>(s.capacity));
                buffer->initialized.resize(static_cast<std::uint32_t>(s.capacity),0);
                state.buffer=std::move(buffer);
            }catch(const std::bad_alloc&) {state.buffer.reset();}
            s.success=bool(state.buffer);s.pool.reset();s.phase=Impl::Phase::status;break;
        case Impl::Phase::status: {
            const auto word=status(state,s.success);
            if(!word)return pending("IGB buffer awaits its reached success/failure status word.");
            s.result=*word;s.phase=Impl::Phase::done;state.active_buffer_.reset();s.owns=false;return {true,s.result,{}};
        }
        case Impl::Phase::done:return {true,s.result,{}};
        }
    }catch(...) {s.fault=true;if(s.owns)s.state->buffer_fault_=true;throw;}
}

std::shared_ptr<IgbStreamServices> make_igb_byte_stream_services(SystemStageString filename,
    std::vector<std::uint8_t> bytes) {
    if(!filename)fail("An exact byte-stream binding requires its original filename.");
    const auto source=igb_stream_from_bytes(filename,std::move(bytes));
    auto services=std::make_shared<IgbStreamServices>();
    services->open=[source](const auto& state,const auto& request,std::uint32_t mode) {
        auto operation=std::make_shared<IgbOpenOperation>();
        operation->resume=[source,state,request,mode,created=false]() mutable {
            if(!request || std::strcmp(request->c_str(),source->filename->c_str())!=0)
                return pending("Original file-manager path dispatch is not bound to these supplied bytes.");
            if(!created) {
                auto stream=std::make_shared<IgbByteStream>(*source);stream->mode=mode;
                state->stream=std::move(stream);created=true;
            }
            const auto word=status(*state,true);
            if(!word)return pending("Native byte-stream open awaits its original success status word.");
            return MessengerCallResult{true,*word,{}};
        };
        return operation;
    };
    return services;
}

struct IgbOpenInvocation::Impl {
    enum class Phase {entry,open,compare,name,intern,set_mode,status,done};
    std::shared_ptr<IgbStreamState> state;
    std::shared_ptr<LoaderRequestFile> file;
    SystemStageString filename,candidate;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    decltype(IgbStreamServices::open) factory;
    decltype(IgbStreamServices::intern_filename) intern_factory;
    std::shared_ptr<IgbOpenOperation> operation;
    std::shared_ptr<IgbStreamNameOperation> name_operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::entry;
    std::uint32_t open_result{},result{};
    bool success{},owns{},running{},fault{};
};
IgbOpenInvocation::IgbOpenInvocation(std::shared_ptr<IgbStreamState> state,SystemStageString filename):impl_(std::make_unique<Impl>()) {
    impl_->state=std::move(state);impl_->filename=std::move(filename);
}
IgbOpenInvocation::~IgbOpenInvocation() {if(impl_->owns && !completed())impl_->state->open_fault_=true;}
bool IgbOpenInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbOpenInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult IgbOpenInvocation::resume(std::shared_ptr<IgbStreamServices> services) {
    auto& s=*impl_;
    if(s.running || s.fault) {s.fault=true;if(s.owns)s.state->open_fault_=true;fail("IGB opening requires a nonrecursive unfaulted owner.");}
    if(completed())return {true,s.result,{}};
    Running running(s.running);
    const auto check=[&] {if(s.fault || s.state->open_fault_)fail("IGB opening was invalidated by its callback.");};
    try {
        if(!s.state)fail("IGB opening requires its file state.");
        claim(s.state->active_open_,s.state->open_fault_,s.token,s.owns);
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::entry:
            if(s.state->stream)s.phase=Impl::Phase::name;
            else if(s.filename)s.phase=Impl::Phase::open;
            else {s.success=false;s.phase=Impl::Phase::status;}
            break;
        case Impl::Phase::open: {
            if(!s.factory && services)s.factory=services->open;
            if(!s.factory)return pending("IGB requires original igFile creation and filename dispatch8004D6DC.");
            if(!s.operation) {s.operation=s.factory(s.state,s.filename,4);check();}
            if(!s.operation)return pending("IGB file opening is unavailable.");
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending("IGB file opening has no continuation.");
            const auto result=s.callback();check();if(!result.completed)return result;
            s.open_result=result.value;s.callback={};s.operation.reset();s.factory={};check();
            s.phase=Impl::Phase::compare;break;
        }
        case Impl::Phase::compare: {
            const auto failure=status(*s.state,false);
            if(!failure)return pending("IGB opening awaits the reached failure comparison word.");
            if(s.open_result==*failure) {
                if(s.state->stream && s.state->stream.use_count()==1)
                    fail("Failed IGB opening reaches an unrepresented final igFile destructor before clearing the stream.");
                s.state->stream.reset();s.success=false;s.phase=Impl::Phase::status;
            }
            else {
                s.state->owns_stream=1;
                if(s.state->stream)s.phase=Impl::Phase::name;
                else {s.success=false;s.phase=Impl::Phase::status;}
            }
            break;
        }
        case Impl::Phase::name:
            s.file=s.state->file.lock();
            if(!s.file)fail("IGB filename derivation reaches an unavailable directory owner.");
            if(s.file->filename && s.file->filename->c_str()[0]!=0)s.phase=Impl::Phase::set_mode;
            else {
                if(!s.state->stream)fail("IGB filename derivation dereferences a null stream.");
                s.candidate=derived_name(s.state->stream->filename);s.phase=Impl::Phase::intern;
            }
            break;
        case Impl::Phase::intern: {
            if(!s.intern_factory && services)s.intern_factory=services->intern_filename;
            if(!s.intern_factory)return pending("IGB filename derivation requires its string-pool acquisition/interning.");
            if(!s.name_operation) {s.name_operation=s.intern_factory(s.candidate);check();}
            if(!s.name_operation)return pending("IGB derived filename interning is unavailable.");
            if(!s.callback)s.callback=s.name_operation->resume;
            if(!s.callback)return pending("IGB filename interning has no continuation.");
            const auto result=s.callback();check();if(!result.completed)return result;
            s.file->filename=s.name_operation->result;
            s.callback={};s.name_operation.reset();s.intern_factory={};check();s.phase=Impl::Phase::set_mode;break;
        }
        case Impl::Phase::set_mode:
            if(!s.state->stream)fail("IGB mode assignment dereferences its reloaded null stream.");
            s.state->stream->mode=4;s.success=true;s.phase=Impl::Phase::status;break;
        case Impl::Phase::status: {
            const auto word=status(*s.state,s.success);
            if(!word)return pending("IGB opening awaits its reached return status word.");
            s.result=*word;s.phase=Impl::Phase::done;s.state->active_open_.reset();s.owns=false;return {true,s.result,{}};
        }
        case Impl::Phase::done:return {true,s.result,{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.state->open_fault_=true;throw;}
}

struct IgbHeaderInvocation::Impl {
    enum class Phase {ensure,compare,read,fields,validate,status,done};
    std::shared_ptr<IgbStreamState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::unique_ptr<IgbBufferInvocation> ensure;
    std::shared_ptr<IgbScratchBuffer> buffer;
    Phase phase=Phase::ensure;
    std::uint32_t allocation_result{},result{};
    bool success{},owns{},running{},fault{};
};
IgbHeaderInvocation::IgbHeaderInvocation(std::shared_ptr<IgbStreamState> state):impl_(std::make_unique<Impl>()) {impl_->state=std::move(state);}
IgbHeaderInvocation::~IgbHeaderInvocation() {if(impl_->owns && !completed())impl_->state->header_fault_=true;}
bool IgbHeaderInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbHeaderInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult IgbHeaderInvocation::resume(std::shared_ptr<IgbStreamServices> services) {
    auto& s=*impl_;
    if(s.running || s.fault) {s.fault=true;if(s.owns)s.state->header_fault_=true;fail("IGB header requires a nonrecursive unfaulted owner.");}
    if(completed())return {true,s.result,{}};
    Running running(s.running);
    try {
        if(!s.state)fail("IGB header requires its file state.");
        auto& state=*s.state;claim(state.active_header_,state.header_fault_,s.token,s.owns);
        for(;;)switch(s.phase) {
        case Impl::Phase::ensure:
            if(!s.ensure)s.ensure=std::make_unique<IgbBufferInvocation>(s.state,0xc0);
            if(const auto result=s.ensure->resume();!result.completed)return result;
            else s.allocation_result=result.value;
            s.ensure.reset();s.phase=Impl::Phase::compare;break;
        case Impl::Phase::compare: {
            const auto failure=status(state,false);
            if(!failure)return pending("IGB header awaits the reached buffer-failure comparison word.");
            if(s.allocation_result==*failure) {s.success=false;s.phase=Impl::Phase::status;}
            else s.phase=Impl::Phase::read;
            break;
        }
        case Impl::Phase::read: {
            const auto stream=state.stream;s.buffer=state.buffer;
            (void)igb_read_elements(stream,s.buffer,0,4,12); // Original ignores the read return value.
            s.phase=Impl::Phase::fields;break;
        }
        case Impl::Phase::fields: {
            if(!services)return pending("IGB header requires its native int/short metadata-field owners.");
            const auto publish=[&](auto& destination,auto incoming) {
                // The original acquires the returned owner before releasing
                // the old field, then publishes each field separately.
                if(destination && destination.use_count()==1) {
                    if(incoming)state.unpublished_fields_.push_back(std::move(incoming));
                    fail("IGB header reaches an unrepresented final metadata-field destructor before publication.");
                }
                destination=std::move(incoming);
            };
            publish(state.int_field,services->int_field);
            publish(state.short_field,services->short_field);
            s.phase=Impl::Phase::validate;break;
        }
        case Impl::Phase::validate: {
            if(igb_buffer_word(s.buffer,0x28)!=state.expected_cookie) {
                if(!state.int_field)fail("IGB header byte swap dereferences its null int metadata field.");
                igb_swap_words(s.buffer,0,state.int_field->width,12);
                if(igb_buffer_word(s.buffer,0x28)==state.expected_cookie)state.swapped=1;
            }else state.swapped=0;
            if(igb_buffer_word(s.buffer,0x28)!=state.expected_cookie) {s.success=false;s.phase=Impl::Phase::status;break;}
            const auto flags=igb_buffer_word(s.buffer,0x2c);state.version=flags&0xffffu;
            if(state.version>7) {s.success=false;s.phase=Impl::Phase::status;break;}
            state.flag_d4=static_cast<std::uint8_t>(flags>>31u);
            state.flag_d5=static_cast<std::uint8_t>((flags&0x40000000u)!=0);
            state.flag_d6=static_cast<std::uint8_t>((flags&0x20000000u)!=0);
            state.flag_d7=static_cast<std::uint8_t>(flags==1);
            state.flag_124=static_cast<std::uint8_t>((flags&0x10000000u)!=0);
            state.legacy_mode=state.flag_d7 ? 1:0;
            state.entry_bytes=igb_buffer_word(s.buffer,0);state.entry_count=igb_buffer_word(s.buffer,4);
            state.object_type_bytes=igb_buffer_word(s.buffer,8);state.object_type_count=igb_buffer_word(s.buffer,12);
            state.object_bytes=igb_buffer_word(s.buffer,16);state.object_count=igb_buffer_word(s.buffer,20);
            state.memory_reference_bytes=igb_buffer_word(s.buffer,24);state.memory_reference_count=igb_buffer_word(s.buffer,28);
            state.field_type_bytes=igb_buffer_word(s.buffer,32);state.field_type_count=igb_buffer_word(s.buffer,36);
            s.success=true;s.phase=Impl::Phase::status;break;
        }
        case Impl::Phase::status: {
            const auto word=status(state,s.success);
            if(!word)return pending("IGB header awaits its reached return status word.");
            s.result=*word;s.phase=Impl::Phase::done;state.active_header_.reset();s.owns=false;return {true,s.result,{}};
        }
        case Impl::Phase::done:return {true,s.result,{}};
        }
    }catch(...) {s.fault=true;if(s.owns)s.state->header_fault_=true;throw;}
}
} // namespace dw4
