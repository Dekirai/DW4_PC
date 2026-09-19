#include "dw4/igb_runtime_types.hpp"
#include "dw4/igb_stream.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbRuntimeTypesError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
std::shared_ptr<IgbRuntimeMetaType> physical(
    const std::shared_ptr<IgbRuntimeTypeList::Storage>& backing,std::int32_t index) {
    if(index<0 || index>=262144 || !backing || static_cast<std::size_t>(index)>=backing->size())
        fail("Runtime type lookup reaches an unavailable physical registry slot.");
    return (*backing)[static_cast<std::size_t>(index)];
}
template<class Read> bool equal_c_strings(const SystemStageString& left,Read&& right) {
    if(!left)fail("Runtime type lookup compares a null registered name.");
    // Each native string has an accessible terminator. Preserve early mismatch
    // and embedded-NUL behavior without validating an unread trailing suffix.
    for(std::size_t index=0;index<4096;++index) {
        const auto a=index<left->size()?(*left)[index]:'\0';
        const auto b=right(index);
        if(a!=b)return false;
        if(a=='\0')return true;
    }
    fail("Runtime type lookup exceeds the represented C-string comparison bound.");
}
}

struct IgbRuntimeTypeLookupInvocation::Impl {
    enum class Phase {singleton,descriptor,search,done};
    SystemStageString name;
    std::optional<IgbRuntimeNameSource> source;
    std::shared_ptr<IgbRuntimeTypeRegistry> singleton;
    std::shared_ptr<IgbRuntimeTypeList> list;
    std::shared_ptr<IgbRuntimeNameField> descriptor;
    std::shared_ptr<IgbRuntimeMetaType> result;
    Phase phase=Phase::singleton;
    bool fault{};
};
IgbRuntimeTypeLookupInvocation::IgbRuntimeTypeLookupInvocation(SystemStageString name)
    :impl_(std::make_unique<Impl>()) {impl_->name=std::move(name);}
IgbRuntimeTypeLookupInvocation::IgbRuntimeTypeLookupInvocation(std::shared_ptr<IgbScratchBuffer> buffer,std::uint32_t offset)
    :impl_(std::make_unique<Impl>()) {impl_->source=IgbRuntimeNameSource{std::move(buffer),offset};}
IgbRuntimeTypeLookupInvocation::~IgbRuntimeTypeLookupInvocation()=default;
bool IgbRuntimeTypeLookupInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbRuntimeTypeLookupInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbRuntimeTypeLookupInvocation::result() const {
    if(!completed())fail("An incomplete runtime type lookup has no result.");
    return impl_->result;
}
MessengerCallResult IgbRuntimeTypeLookupInvocation::resume(std::shared_ptr<IgbRuntimeTypesServices> services) {
    auto& s=*impl_;
    if(s.fault)fail("Runtime type lookup requires an unfaulted invocation.");
    if(completed())return {true,0,{}};
    try {
        if(s.phase==Impl::Phase::singleton) {
            if(!services || !services->singleton)
                return pending("Runtime type lookup requires original singleton global80562100.");
            s.singleton=*services->singleton;
            if(!s.singleton)fail("Runtime type lookup dereferences a null registry singleton.");
            if(s.singleton->enabled==0) {s.phase=Impl::Phase::done;return {true,0,{}};}
            s.list=s.singleton->types;
            s.phase=Impl::Phase::descriptor;
        }
        if(s.phase==Impl::Phase::descriptor) {
            if(!services || !services->name_field)
                return pending("Runtime type lookup requires original name descriptor global8056176C.");
            s.descriptor=*services->name_field;
            s.phase=Impl::Phase::search;
        }
        if(!s.list)fail("Runtime type lookup reads the count of a null registry.");
        for(std::int32_t index=0;index<s.list->count;++index) {
            const auto backing=s.list->backing;
            if(!s.descriptor)fail("Runtime type lookup dereferences a null name descriptor.");
            const auto offset=s.descriptor->offset;
            const auto metadata=physical(backing,index);
            if(!metadata)fail("Runtime type lookup dereferences a null registered type.");
            if(offset!=0x1c)fail("Runtime type lookup reaches an unrepresented metadata string offset.");
            if(equal_c_strings(metadata->name,[&](std::size_t byte_index) {
                if(s.source)return static_cast<char>(igb_buffer_bytes(s.source->buffer,
                    s.source->offset+static_cast<std::uint32_t>(byte_index),1).front());
                if(!s.name)fail("Runtime type lookup compares a null requested name.");
                return byte_index<s.name->size()?(*s.name)[byte_index]:'\0';
            })) {
                // The original reloads backing and its row after strcmp.
                s.result=physical(s.list->backing,index);
                break;
            }
        }
        s.phase=Impl::Phase::done;
        return {true,0,{}};
    }catch(...) {s.fault=true;throw;}
}
SystemStageString igb_runtime_read_name(const IgbRuntimeNameSource& source) {
    std::string result;
    for(std::uint32_t index=0;index<4096;++index) {
        const auto byte=igb_buffer_bytes(source.buffer,source.offset+index,1).front();
        if(byte==0)return std::make_shared<const std::string>(std::move(result));
        result.push_back(static_cast<char>(byte));
    }
    fail("Runtime metadata name exceeds the represented C-string bound.");
}
std::int32_t igb_runtime_slot_count(const std::shared_ptr<IgbRuntimeMetaType>& metadata) {
    if(!metadata)fail("Runtime slot count dereferences null metadata.");
    const auto fields=metadata->fields;
    if(!fields)fail("Runtime slot count dereferences a null field registry.");
    return fields->count;
}
std::shared_ptr<IgbDecodeRuntimeField> igb_runtime_field_at(
    const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::int32_t index) {
    if(!metadata)fail("Runtime field access dereferences null metadata.");
    const auto fields=metadata->fields;
    if(!fields)fail("Runtime field access dereferences a null field registry.");
    const auto backing=fields->backing;
    if(index<0 || index>=262144 || !backing || static_cast<std::size_t>(index)>=backing->size())
        fail("Runtime field access reaches an unavailable physical registry slot.");
    return (*backing)[static_cast<std::size_t>(index)];
}
std::int32_t igb_runtime_local_field_count(const std::shared_ptr<IgbRuntimeMetaType>& metadata) {
    std::int32_t local{};
    for(std::int32_t index=0;index<igb_runtime_slot_count(metadata);++index) {
        const auto field=igb_runtime_field_at(metadata,index);
        if(!field)fail("Runtime local-field count dereferences a null field instance.");
        if(field->flag_25==0)++local;
    }
    return local;
}
} // namespace dw4
