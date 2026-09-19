#include "dw4/igb_decode_tables.hpp"
#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw IgbDecodeTablesError(reason);}
MessengerCallResult pending(const char* reason) {return {false,0,reason};}
std::int32_t signed_word(std::uint32_t value) {return std::bit_cast<std::int32_t>(value);}
std::int32_t increment(std::int32_t value) {return signed_word(std::bit_cast<std::uint32_t>(value)+1);}
template<class T> void release(std::shared_ptr<T>& owner) {
    if(owner && owner.use_count()==1)fail("IGB table reaches an unrepresented final runtime object destructor.");
    owner.reset();
}
template<class T> auto& physical(const std::shared_ptr<std::vector<T>>& backing,std::int32_t index) {
    if(index<0 || index>=262144 || !backing || static_cast<std::size_t>(index)>=backing->size())
        fail("IGB table reaches an unavailable physical runtime list slot.");
    return (*backing)[static_cast<std::size_t>(index)];
}
std::string name(const std::shared_ptr<IgbScratchBuffer>& buffer,std::uint32_t offset) {
    std::string value;
    for(std::size_t index=offset;value.size()<4096;++index) {
        const auto byte=igb_buffer_bytes(buffer,index,1).front();
        if(byte==0)return value;
        value.push_back(static_cast<char>(byte));
    }
    fail("IGB table name exceeds the bounded native C-string profile.");
}
std::int32_t growth(std::int32_t capacity,std::int32_t count) {
    capacity=std::max<std::int32_t>(capacity,4);
    while(capacity<count) {
        if(capacity>=262144)fail("IGB table list exceeds the native allocation profile.");
        capacity=capacity<1024 ? capacity*2:capacity+1024;
    }
    if(capacity>262144)fail("IGB table list exceeds the native allocation profile.");
    return capacity;
}
const char* boundary(IgbDecodeTableServiceKind kind) {
    switch(kind) {
    case IgbDecodeTableServiceKind::field_registry:return "IGB field table requires lazy runtime field registry80063B5C.";
    case IgbDecodeTableServiceKind::field_metadata:return "IGB field table requires the selected runtime field virtual+58.";
    case IgbDecodeTableServiceKind::create_field_list:return "IGB field table requires reflected igMetaFieldList factory80034010.";
    case IgbDecodeTableServiceKind::create_alignment_list:return "IGB alignment table requires reflected igIntList factory8003657C.";
    case IgbDecodeTableServiceKind::resize_field_list:
    case IgbDecodeTableServiceKind::resize_alignment_list:return "IGB table requires runtime list metadata/allocator8004155C.";
    }
    return "IGB table requires its reached operation.";
}
}
struct IgbDecodeTablesInvocation::Impl {
    enum class Phase {entry,ensure,after_ensure,field_read,registry,after_registry,publish_registry,field_prepare,
        field_factory,after_field_factory,alignment_header,alignment_header_result,alignment_body,
        alignment_registry,alignment_factory,after_alignment_factory,initial_capacity,visit,
        field_search,field_metadata,after_metadata,after_match,alignment_search,append,
        after_grow,store_append,advance,finish,release_names,release_values,call,done};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbStreamState> stream;
    std::shared_ptr<IgbDecodeTablesState> tables;
    IgbDecodeTableKind kind{};
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::unique_ptr<IgbBufferInvocation> buffer_call;
    std::shared_ptr<IgbScratchBuffer> data,header;
    std::shared_ptr<IgbDecodeFieldRegistry> registry;
    std::shared_ptr<IgbDecodeNameList> names;
    std::shared_ptr<IgbDecodeWordList> values,word_list;
    std::shared_ptr<IgbDecodeFieldList> field_list;
    std::shared_ptr<IgbDecodeRuntimeField> candidate;
    IgbDecodeTableCall request;
    IgbDecodeTableFactory factory;
    std::shared_ptr<IgbDecodeTableOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::entry,after=Phase::done;
    std::uint32_t ensure_status{},read_count{},name_offset{},descriptor_offset{},alignment{16},result{};
    std::int32_t registry_count{},index{},search{},append_index{},append_count{};
    bool owns{},running{},fault{},success{},initial_resize{};
};
IgbDecodeTablesInvocation::IgbDecodeTablesInvocation(std::shared_ptr<LoaderRequestFile> file,
    std::shared_ptr<IgbStreamState> stream,std::shared_ptr<IgbDecodeTablesState> tables,IgbDecodeTableKind kind):impl_(std::make_unique<Impl>()) {
    impl_->file=std::move(file);impl_->stream=std::move(stream);impl_->tables=std::move(tables);impl_->kind=kind;
}
IgbDecodeTablesInvocation::~IgbDecodeTablesInvocation() {
    if(impl_->owns && !completed()) {
        impl_->tables->fault_=true;
        for(const std::shared_ptr<void>& owner:{std::shared_ptr<void>(impl_->names),std::shared_ptr<void>(impl_->values),std::shared_ptr<void>(impl_->candidate)})
            if(owner)impl_->tables->unreleased_.push_back(owner);
    }
}
bool IgbDecodeTablesInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbDecodeTablesInvocation::faulted() const noexcept {return impl_->fault;}
std::uint32_t IgbDecodeTablesInvocation::value() const {
    if(!completed() || faulted())fail("IGB table result requires a completed, unfaulted invocation.");
    return impl_->result;
}
MessengerCallResult IgbDecodeTablesInvocation::resume(std::shared_ptr<IgbDecodeTablesServices> services) {
    auto& s=*impl_;
    if(s.running || s.fault) {s.fault=true;if(s.owns)s.tables->fault_=true;fail("IGB table requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.result,{}};
    struct Guard {bool& flag;Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(s.running);
    const auto check=[&] {if(s.fault || s.tables->fault_)fail("IGB table was invalidated by a recursive operation.");};
    const auto clear_call=[&] {s.callback={};s.factory={};s.request={};s.operation.reset();check();};
    const auto select=[&](IgbDecodeTableServiceKind kind,Impl::Phase after)->IgbDecodeTableCall& {
        clear_call();s.request.kind=kind;s.request.file=s.file;
        s.factory=services ? services->factory:IgbDecodeTableFactory{};
        s.after=after;s.phase=Impl::Phase::call;return s.request;
    };
    const auto status=[&](bool success)->std::optional<std::uint32_t> {
        if(!s.stream->statuses)return {};
        return success ? s.stream->statuses->success:s.stream->statuses->failure;
    };
    const auto finish=[&](bool success) {s.success=success;s.phase=Impl::Phase::finish;};
    const auto swap=[&](const std::shared_ptr<IgbScratchBuffer>& buffer,std::int32_t count) {
        const auto field=s.stream->int_field;
        if(!field)fail("IGB table dereferences a null integer endian-field owner.");
        igb_swap_words(buffer,0,field->width,count);
    };
    const auto request_resize=[&](bool field,std::int32_t capacity,Impl::Phase after) {
        auto& request=select(field ? IgbDecodeTableServiceKind::resize_field_list:IgbDecodeTableServiceKind::resize_alignment_list,after);
        request.field_list=s.field_list;request.alignment_list=s.word_list;request.capacity=capacity;
    };
    try {
        if(!s.file || !s.stream || !s.tables)fail("IGB table requires original file, stream-state and table-state owners.");
        if(s.stream->file.lock()!=s.file)fail("IGB table and stream state must share the same canonical file owner.");
        if(!s.owns) {
            if(s.tables->fault_)fail("A faulted IGB table prefix cannot restart.");
            if(!s.tables->active_.expired())fail("Another pending invocation owns this IGB table state.");
            s.tables->active_=s.token;s.owns=true;
        }
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::entry:
            if(s.kind==IgbDecodeTableKind::alignments)s.phase=Impl::Phase::alignment_header;
            else {
                const auto minimum=s.kind==IgbDecodeTableKind::field_types ? s.stream->field_type_bytes:s.stream->object_type_bytes;
                s.buffer_call=std::make_unique<IgbBufferInvocation>(s.stream,signed_word(minimum));s.phase=Impl::Phase::ensure;
            }
            break;
        case Impl::Phase::ensure: {
            const auto result=s.buffer_call->resume();check();if(!result.completed)return result;
            s.ensure_status=result.value;s.buffer_call.reset();s.phase=Impl::Phase::after_ensure;break;
        }
        case Impl::Phase::after_ensure: {
            const auto failure=status(false);if(!failure)return pending("IGB table requires the reached original failure-status word.");
            if(s.ensure_status==*failure) {s.result=*failure;s.phase=Impl::Phase::release_values;break;}
            if(s.kind==IgbDecodeTableKind::field_types) {s.tables->field_buffer=s.stream->buffer;s.phase=Impl::Phase::field_read;}
            else {
                s.tables->object_type_buffer=s.stream->buffer;
                const auto bytes=s.stream->object_type_bytes;
                const auto result=bytes==0 ? 1u:igb_read_elements(s.stream->stream,s.tables->object_type_buffer,0,bytes,1);
                finish(result==1);
            }
            break;
        }
        case Impl::Phase::field_read:
            (void)igb_read_elements(s.stream->stream,s.tables->field_buffer,0,s.stream->field_type_bytes,1);
            s.phase=Impl::Phase::registry;break;
        case Impl::Phase::registry:
            if(!services)return pending("IGB field table requires its represented runtime registry singleton.");
            if(services->field_registry) {s.registry=services->field_registry;s.phase=Impl::Phase::field_prepare;}
            else select(IgbDecodeTableServiceKind::field_registry,Impl::Phase::after_registry);
            break;
        case Impl::Phase::after_registry:
            s.registry=std::move(s.operation->registry_result);clear_call();s.phase=Impl::Phase::publish_registry;break;
        case Impl::Phase::publish_registry:
            if(!services)return pending("IGB field table requires its live runtime registry singleton for publication.");
            if(services->field_registry)s.tables->unreleased_.push_back(std::move(services->field_registry));
            services->field_registry=s.registry;s.phase=Impl::Phase::field_prepare;break;
        case Impl::Phase::field_prepare:
            if(!s.registry)fail("IGB field table dereferences a null runtime registry.");
            s.data=s.tables->field_buffer;s.registry_count=s.registry->count;
            s.name_offset=s.stream->field_type_count*12u;
            if(s.stream->swapped!=0)swap(s.data,signed_word(s.stream->field_type_count*3u));
            s.phase=Impl::Phase::field_factory;break;
        case Impl::Phase::field_factory:
            select(IgbDecodeTableServiceKind::create_field_list,Impl::Phase::after_field_factory).pool=s.file->pool_118;break;
        case Impl::Phase::after_field_factory:
            s.field_list=std::move(s.operation->field_list_result);
            if(s.field_list)s.tables->field_list_creation_refs.push_back(s.field_list);
            clear_call();
            try {release(s.tables->field_types);s.tables->field_types=s.field_list;}
            catch(...) {if(s.field_list)s.tables->unreleased_.push_back(s.field_list);throw;}
            s.phase=Impl::Phase::initial_capacity;break;
        case Impl::Phase::alignment_header:
            s.header=std::make_shared<IgbScratchBuffer>();s.header->bytes.resize(12);s.header->initialized.resize(12);
            s.read_count=igb_read_elements(s.stream->stream,s.header,0,12,1);s.phase=Impl::Phase::alignment_header_result;break;
        case Impl::Phase::alignment_header_result:
            if(s.read_count!=1) {finish(false);break;}
            if(s.stream->swapped!=0)swap(s.header,3);
            s.tables->alignment_bytes=signed_word(igb_buffer_word(s.header,0)-12u);
            s.tables->alignment_count=signed_word(igb_buffer_word(s.header,8));
            if(s.tables->alignment_count<=0) {finish(true);break;}
            s.tables->alignment_buffer=s.stream->buffer;s.phase=Impl::Phase::alignment_body;break;
        case Impl::Phase::alignment_body:
            s.read_count=igb_read_elements(s.stream->stream,s.tables->alignment_buffer,0,
                std::bit_cast<std::uint32_t>(s.tables->alignment_bytes),1);
            if(s.read_count!=1) {finish(false);break;}
            s.data=s.tables->alignment_buffer;
            if(s.stream->swapped!=0)swap(s.data,s.tables->alignment_count);
            s.phase=Impl::Phase::alignment_registry;break;
        case Impl::Phase::alignment_registry:
            if(!services || !services->alignment_registry)return pending("IGB alignment table requires the represented loader singleton.");
            if(!*services->alignment_registry)fail("IGB alignment table dereferences a null loader singleton.");
            s.names=(*services->alignment_registry)->names;s.values=(*services->alignment_registry)->values;
            s.name_offset=std::bit_cast<std::uint32_t>(s.tables->alignment_count)*4u;
            s.registry_count=s.names ? s.names->count:0;s.phase=Impl::Phase::alignment_factory;break;
        case Impl::Phase::alignment_factory:
            select(IgbDecodeTableServiceKind::create_alignment_list,Impl::Phase::after_alignment_factory).pool=s.file->pool_118;break;
        case Impl::Phase::after_alignment_factory:
            s.word_list=std::move(s.operation->alignment_list_result);clear_call();
            try {release(s.tables->alignments);s.tables->alignments=s.word_list;}
            catch(...) {if(s.word_list)s.tables->unreleased_.push_back(s.word_list);throw;}
            s.phase=Impl::Phase::initial_capacity;break;
        case Impl::Phase::initial_capacity: {
            const bool field=s.kind==IgbDecodeTableKind::field_types;
            if((field && !s.tables->field_types) || (!field && !s.tables->alignments))
                fail("IGB table dereferences a null reflected list factory result.");
            const auto count=field ? signed_word(s.stream->field_type_count):s.tables->alignment_count;
            const auto current=field ? s.tables->field_types->count:s.tables->alignments->count;
            if(count>=current) {
                s.field_list=field ? s.tables->field_types:nullptr;s.word_list=field ? nullptr:s.tables->alignments;
                request_resize(field,count,Impl::Phase::visit);
            }else s.phase=Impl::Phase::visit;
            break;
        }
        case Impl::Phase::visit: {
            // Drop completed allocator result before observing current count.
            if(s.operation)clear_call();
            const auto count=s.kind==IgbDecodeTableKind::field_types ? signed_word(s.stream->field_type_count):s.tables->alignment_count;
            if(s.index>=count) {
                if(s.kind==IgbDecodeTableKind::field_types)s.tables->field_buffer.reset();
                else s.tables->alignment_buffer.reset();
                finish(true);break;
            }
            if(s.index>=262144)fail("IGB table iteration exceeds the native profile.");
            s.search=0;s.candidate.reset();s.alignment=16;
            s.phase=s.kind==IgbDecodeTableKind::field_types ? Impl::Phase::field_search:Impl::Phase::alignment_search;break;
        }
        case Impl::Phase::field_search:
            if(s.search>=s.registry_count) {s.candidate.reset();s.phase=Impl::Phase::append;break;}
            s.candidate=physical(s.registry->backing,s.search);
            if(!s.candidate)fail("IGB field table dereferences a null registry field instance.");
            s.phase=Impl::Phase::field_metadata;break;
        case Impl::Phase::field_metadata:
            select(IgbDecodeTableServiceKind::field_metadata,Impl::Phase::after_metadata).field=s.candidate;break;
        case Impl::Phase::after_metadata: {
            const auto metadata=s.operation->metadata_result;
            if(!metadata || !metadata->name)fail("IGB field table compares a null metadata name.");
            const bool matched=std::strcmp(metadata->name->c_str(),name(s.data,s.name_offset).c_str())==0;
            clear_call();
            if(matched)select(IgbDecodeTableServiceKind::field_metadata,Impl::Phase::after_match).field=s.candidate;
            else {++s.search;s.phase=Impl::Phase::field_search;}
            break;
        }
        case Impl::Phase::after_match:clear_call();s.phase=Impl::Phase::append;break; // Second metadata result is ignored.
        case Impl::Phase::alignment_search:
            for(;s.search<s.registry_count;++s.search) {
                const auto item=physical(s.names->backing,s.search);
                if(!item)fail("IGB alignment table dereferences a null registered name owner.");
                const auto wanted=name(s.data,s.name_offset);
                if(std::strcmp(item->value ? item->value->c_str():"",wanted.c_str())==0) {
                    if(!s.values)fail("IGB alignment table dereferences a null matched value list.");
                    s.alignment=physical(s.values->backing,s.search);break;
                }
            }
            s.phase=Impl::Phase::append;break;
        case Impl::Phase::append: {
            const bool field=s.kind==IgbDecodeTableKind::field_types;
            s.field_list=field ? s.tables->field_types:nullptr;s.word_list=field ? nullptr:s.tables->alignments;
            if((field && !s.field_list) || (!field && !s.word_list))fail("IGB table append dereferences a null live output list.");
            s.append_index=field ? s.field_list->count:s.word_list->count;s.append_count=increment(s.append_index);
            const auto capacity=field ? s.field_list->capacity:s.word_list->capacity;
            if(s.append_index<capacity) {
                if(field)s.field_list->count=s.append_count;else s.word_list->count=s.append_count;
                s.phase=Impl::Phase::store_append;
            }else request_resize(field,growth(capacity,s.append_count),Impl::Phase::after_grow);
            break;
        }
        case Impl::Phase::after_grow:
            clear_call();if(s.field_list)s.field_list->count=s.append_count;else s.word_list->count=s.append_count;
            s.phase=Impl::Phase::store_append;break;
        case Impl::Phase::store_append:
            if(s.field_list) {
                auto& slot=physical(s.field_list->backing,s.append_index);
                if(slot)s.tables->unreleased_.push_back(std::move(slot));slot=s.candidate;
            }else physical(s.word_list->backing,s.append_index)=s.alignment;
            s.phase=Impl::Phase::advance;break;
        case Impl::Phase::advance:
            s.name_offset+=igb_buffer_word(s.data,s.descriptor_offset);
            s.descriptor_offset+=s.kind==IgbDecodeTableKind::field_types ? 12u:4u;
            ++s.index;s.phase=Impl::Phase::visit;break;
        case Impl::Phase::finish: {
            const auto value=status(s.success);if(!value)return pending("IGB table requires its reached original return-status word.");
            s.result=*value;s.phase=Impl::Phase::release_values;break;
        }
        case Impl::Phase::release_values:release(s.values);check();s.phase=Impl::Phase::release_names;break;
        case Impl::Phase::release_names:
            release(s.names);check();clear_call();s.tables->active_.reset();s.token.reset();s.owns=false;
            s.phase=Impl::Phase::done;return {true,s.result,{}};
        case Impl::Phase::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(boundary(s.request.kind));
            if(!s.operation) {
                s.operation=s.factory(s.request);check();
                if(!s.operation || !s.operation->resume)fail("IGB table operation factory returned no valid continuation.");
                s.callback=s.operation->resume;
            }
            {const auto result=s.callback();check();if(!result.completed)return result;}
            s.callback={};s.factory={};check();s.phase=s.after;break;
        case Impl::Phase::done:return {true,s.result,{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.tables->fault_=true;throw;}
}
} // namespace dw4
