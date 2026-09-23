#include "dw4/igb_object_init.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbObjectInitError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
std::int32_t increment(std::int32_t value) {return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)+1u);}
void synchronize_header(const IgbObjectInstance& object,std::uint32_t relative,std::uint32_t width) {
    const auto synchronize=[&](std::uint32_t header)->std::optional<std::uint32_t> {
        if(relative>=header+4u || static_cast<std::uint64_t>(relative)+width<=header)return {};
        const auto bytes=object.storage->bytes;const auto at=static_cast<std::uint64_t>(object.offset)+header;
        if(!bytes || bytes->released || at>bytes->bytes.size() || bytes->bytes.size()-at<4 ||
            at>bytes->initialized.size() || bytes->initialized.size()-at<4)return {};
        for(unsigned byte=0;byte<4;++byte)if(!bytes->initialized[static_cast<std::size_t>(at)+byte])return {};
        return igb_buffer_word(bytes,static_cast<std::size_t>(at));
    };
    if(const auto word=synchronize(0);word && object.field)object.field->vtable_word=*word;
    if(const auto word=synchronize(4)) {
        if(object.file)object.file->reference_word=*word;
        if(object.field)object.field->reference_word=*word;
    }
}
void write_byte(const IgbObjectInstance& object,std::uint32_t relative,std::uint8_t value) {
    const auto position=object.offset+relative;const auto bytes=object.storage->bytes;
    if(!bytes || bytes->released || position>=bytes->bytes.size() || position>=bytes->initialized.size())
        fail("Object initialization reaches unavailable physical write storage.");
    bytes->bytes[position]=value;bytes->initialized[position]=1;
    synchronize_header(object,relative,1);
}
void write_word(const IgbObjectInstance& object,std::uint32_t relative,std::uint32_t value) {
    const auto position=object.offset+relative;const auto bytes=object.storage->bytes;
    if(!bytes || bytes->released || position>bytes->bytes.size() || bytes->bytes.size()-position<4 ||
        position>bytes->initialized.size() || bytes->initialized.size()-position<4)
        fail("Object initialization reaches unavailable physical word storage.");
    for(unsigned byte=0;byte<4;++byte) {
        bytes->bytes[position+byte]=static_cast<std::uint8_t>(value>>(24u-8u*byte));bytes->initialized[position+byte]=1;
    }
    synchronize_header(object,relative,4);
}
std::shared_ptr<IgbDecodeRuntimeField> physical(const std::shared_ptr<IgbDecodeFieldRegistry>& list,std::int32_t index) {
    if(index<0 || index>=262144 || !list->backing || static_cast<std::size_t>(index)>=list->backing->size())
        fail("Object field initialization reaches an unavailable physical descriptor.");
    const auto field=(*list->backing)[static_cast<std::size_t>(index)];
    if(!field)fail("Object field initialization dereferences a null field.");return field;
}
}
std::uint32_t igb_object_reference_word(const IgbObjectInstance& object) {
    if(!object.storage || bool(object.file)==bool(object.field))fail("Object header access requires represented storage and one typed binding.");
    return igb_buffer_word(object.storage->bytes,object.offset+4u);
}
void igb_object_set_reference_word(const IgbObjectInstance& object,std::uint32_t value) {
    if(!object.storage || bool(object.file)==bool(object.field))fail("Object header access requires represented storage and one typed binding.");
    write_word(object,4,value);
}
struct IgbObjectInitInvocation::Impl {
    enum class Phase {bind,core,type_word,vtable_offset,header,base_metadata,after_base_metadata,allocation_prefix,
        allocation_pool,after_pool,pool_index,after_pool_index,pass,after_pass_metadata,gate,gate_fields,range,
        visit,after_field,default_mode,after_default,pass_end,tail_2c,tail_24,final_metadata,after_final_metadata,
        instance_count,instance_list,after_instance,call,done};
    IgbObjectInstance object;
    std::shared_ptr<IgbRuntimeMetaType> initial_metadata,metadata,base;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    IgbObjectInitCall request;
    IgbObjectInitFactory factory;
    std::shared_ptr<IgbObjectInitOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::bind,after=Phase::done;
    std::uint32_t type_word{},allocation_address{},returned{},visits{};
    std::int32_t index{},count{};
    unsigned pass{};
    bool owns{},running{},fault{};
};
IgbObjectInitInvocation::IgbObjectInitInvocation(std::shared_ptr<IgbObjectStorage> storage,std::uint32_t offset,
    std::shared_ptr<IgbRuntimeMetaType> metadata):impl_(std::make_unique<Impl>()) {
    impl_->object.storage=std::move(storage);impl_->object.offset=offset;impl_->initial_metadata=std::move(metadata);
}
IgbObjectInitInvocation::~IgbObjectInitInvocation() {
    if(impl_->owns && !completed()) {
        impl_->object.storage->fault_=true;
        if(impl_->object.file)impl_->object.storage->unreleased_.push_back(impl_->object.file);
        if(impl_->object.field) {
            impl_->object.field->construction_fault_=true;
            impl_->object.storage->unreleased_.push_back(impl_->object.field);
        }
    }
}
bool IgbObjectInitInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbObjectInitInvocation::faulted() const noexcept {return impl_->fault;}
IgbObjectInstance IgbObjectInitInvocation::result_instance() const {
    if(!completed() || faulted())fail("Incomplete object initialization has no constructed instance result.");return impl_->object;
}
std::shared_ptr<LoaderRequestFile> IgbObjectInitInvocation::result() const {
    const auto object=result_instance();
    if(object.field)fail("A constructed field cannot be returned through the file adapter.");return object.file;
}
MessengerCallResult IgbObjectInitInvocation::resume(std::shared_ptr<IgbObjectInitServices> services) {
    auto& s=*impl_;using P=Impl::Phase;using K=IgbObjectInitServiceKind;
    const auto invalidate=[&] {s.fault=true;if(s.owns) {s.object.storage->fault_=true;if(s.object.field)s.object.field->construction_fault_=true;}};
    if(s.running || s.fault) {invalidate();fail("Object initialization requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Running {bool& v;Running(bool& flag):v(flag){v=true;}~Running(){v=false;}} running(s.running);
    const auto check=[&] {if(s.fault || (s.owns && (s.object.storage->fault_ ||
        (s.object.field && s.object.field->construction_fault_))))fail("Object initialization continuation was invalidated.");};
    const auto clear=[&] {s.callback={};s.factory={};s.operation.reset();s.request={};check();};
    const auto select=[&](K kind,P after)->IgbObjectInitCall& {
        clear();s.request.kind=kind;s.request.object=s.object;s.request.metadata=s.metadata;
        s.factory=services?services->factory:IgbObjectInitFactory{};s.after=after;s.phase=P::call;return s.request;
    };
    const auto object_virtual=[&](std::uint32_t slot,P after,std::uint32_t argument=0) {
        // Releasing the completed callback may change the live object. Do it
        // before the next original vtable load, not inside select afterwards.
        clear();
        const auto word=igb_buffer_word(s.object.storage->bytes,s.object.offset);
        if(s.object.field)s.object.field->vtable_word=word;
        auto& request=select(K::object_virtual,after);request.virtual_slot=slot;request.vtable_word=word;request.argument=argument;
    };
    const auto field_virtual=[&](std::uint32_t slot,P after) {
        auto& request=select(K::field_virtual,after);request.field=s.field;request.virtual_slot=slot;
    };
    const auto core_available=[&] {return services && services->runtime && services->runtime->singleton.has_value();};
    try {
        for(;;) {check();switch(s.phase) {
        case P::bind: {
            if(!s.object.storage)fail("Object initialization has no represented allocation.");
            if(s.object.storage->fault_ || !s.object.storage->active_.expired())fail("Object allocation is faulted or already initializing.");
            unsigned selected_kind{};std::shared_ptr<void> selected_owner;
            s.object.file.reset();s.object.field.reset();
            for(const auto& binding:s.object.storage->bindings)if(binding.offset==s.object.offset) {
                const unsigned kind=(binding.file?1u:0u)|(binding.metadata?2u:0u)|(binding.fields?4u:0u)|
                    (binding.types?8u:0u)|(binding.field?16u:0u);
                if(kind && (kind&(kind-1u)))fail("Object offset has conflicting native layout kinds.");
                std::shared_ptr<void> owner;
                if(binding.file)owner=binding.file;else if(binding.metadata)owner=binding.metadata;
                else if(binding.fields)owner=binding.fields;else if(binding.types)owner=binding.types;else if(binding.field)owner=binding.field;
                if(kind && selected_kind && (kind!=selected_kind || owner!=selected_owner))fail("Object offset has conflicting native layout bindings.");
                if(kind) {selected_kind=kind;selected_owner=owner;}
                if(binding.file)s.object.file=binding.file;if(binding.field)s.object.field=binding.field;
            }
            if(!s.object.file && !s.object.field)return pending("Object initialization requires a supported typed binding at its adjusted allocation offset.");
            if(s.object.field) {
                const auto field=s.object.field;
                if(field->construction_fault_ || !field->construction_active_.expired())fail("Object field is faulted or already initializing.");
                if(field->physical_storage && (field->physical_storage->storage.lock()!=s.object.storage || field->physical_storage->offset!=s.object.offset))
                    fail("Object field has a conflicting physical allocation binding.");
                field->physical_storage=IgbFieldStorageBinding{s.object.storage,s.object.offset};field->construction_active_=s.token;
            }
            s.object.storage->active_=s.token;s.owns=true;s.phase=P::core;break;
        }
        case P::core:
            if(!core_available())return pending("Object initialization requires ArkCore global80562100.");
            s.core=*services->runtime->singleton;s.phase=P::type_word;break;
        case P::type_word:
            if(!s.initial_metadata)fail("Object initialization dereferences null input metadata at +5C.");
            if(!s.initial_metadata->type_word)return pending("Object initialization requires metadata type word+5C.");
            s.type_word=*s.initial_metadata->type_word;s.phase=P::vtable_offset;break;
        case P::vtable_offset:
            if(!s.core)fail("Object initialization dereferences null ArkCore at +394.");
            if(!s.core->vtable_offset)return pending("Object initialization requires captured ArkCore vtable offset+394.");
            write_word(s.object,*s.core->vtable_offset,s.type_word);s.phase=P::header;break;
        case P::header:
            write_word(s.object,4,0);write_word(s.object,4,1);
            // The original clears byte+05 bit7 after replacing the entire word.
            write_byte(s.object,5,0);s.phase=P::base_metadata;break;
        case P::base_metadata:object_virtual(0x58,P::after_base_metadata);break;
        case P::after_base_metadata:s.metadata=std::move(s.operation->metadata_result);clear();s.phase=P::allocation_prefix;break;
        case P::allocation_prefix:
            if(!s.metadata)fail("Object allocation-base helper dereferences null returned metadata.");
            if(!s.metadata->allocation_prefix)return pending("Object allocation-base helper requires live metadata prefix+20.");
            s.allocation_address=s.object.storage->original_address+s.object.offset-*s.metadata->allocation_prefix;
            s.phase=P::allocation_pool;break;
        case P::allocation_pool:select(K::allocation_pool,P::after_pool).address=s.allocation_address;break;
        case P::after_pool:
            s.pool=std::move(s.operation->pool_result);clear();
            if(s.pool)s.phase=P::pool_index;else {s.returned=0;s.phase=P::after_pool_index;}
            break;
        case P::pool_index:select(K::pool_index,P::after_pool_index).pool=s.pool;break;
        case P::after_pool_index:
            clear();write_byte(s.object,4,static_cast<std::uint8_t>(s.returned));
            s.pass=0;s.phase=P::pass;break;
        case P::pass:object_virtual(0x58,P::after_pass_metadata);break;
        case P::after_pass_metadata:
            s.metadata=std::move(s.operation->metadata_result);clear();
            s.phase=s.pass==0 && (!s.metadata || (s.metadata->class_flags&4u)==0)?P::gate:P::range;break;
        case P::gate:
            if(!core_available())return pending("First field pass requires its fresh ArkCore gate owner.");
            s.core=*services->runtime->singleton;s.phase=P::gate_fields;break;
        case P::gate_fields:
            if(!s.core)fail("First field pass dereferences null ArkCore gate owner.");
            if(!s.core->field_initialization_enabled)return pending("First field pass requires ArkCore gate byte+14.");
            s.phase=*s.core->field_initialization_enabled!=0 && s.core->enabled==0?P::pass_end:P::range;break;
        case P::range: {
            if(!services || !services->object_metadata)return pending("Field pass requires base igObject metadata global805622A4.");
            s.base=*services->object_metadata;
            if(!s.metadata)fail("Field pass dereferences null runtime metadata at +28.");
            s.fields=s.metadata->fields;
            if(!s.base)fail("Field pass dereferences null igObject metadata at +28.");
            const auto base_fields=s.base->fields;
            if(!s.fields)fail("Field pass dereferences null runtime field registry count.");
            s.count=s.fields->count;
            if(!base_fields)fail("Field pass dereferences null igObject field registry count.");
            s.index=base_fields->count;s.visits=0;s.phase=P::visit;break;
        }
        case P::visit:
            if(s.index>=s.count) {s.phase=P::pass_end;break;}
            if(++s.visits>262144)fail("Object field visits exceed the native bounded profile.");
            s.field=physical(s.fields,s.index);field_virtual(s.pass==0?0x84:s.pass==1?0x7c:0xf0,P::after_field);break;
        case P::after_field:
            clear();
            if(s.pass==2 && (s.returned&0xffu)==0)s.phase=P::default_mode;
            else {s.index=increment(s.index);s.phase=P::visit;}
            break;
        case P::default_mode: {
            if(!s.field->default_mode)return pending("Field defaults require the captured field's signed mode byte+24.");
            const auto mode=std::bit_cast<std::int8_t>(*s.field->default_mode);
            if(mode==0 || mode==2 || mode==3)field_virtual(mode==0?0x70:mode==2?0x78:0x74,P::after_default);
            else {s.index=increment(s.index);s.phase=P::visit;}
            break;
        }
        case P::after_default:clear();s.index=increment(s.index);s.phase=P::visit;break;
        case P::pass_end:
            if(s.pass==0) {s.pass=1;s.phase=P::pass;}
            else if(s.pass==1) {s.pass=2;object_virtual(0x0c,P::pass,1);}
            else object_virtual(0x34,P::tail_2c,1);
            break;
        case P::tail_2c:object_virtual(0x2c,P::tail_24);break;
        case P::tail_24:object_virtual(0x24,P::final_metadata,0);break;
        case P::final_metadata:object_virtual(0x58,P::after_final_metadata);break;
        case P::after_final_metadata:
            s.metadata=std::move(s.operation->metadata_result);clear();s.phase=s.metadata?P::instance_count:P::done;break;
        case P::instance_count:
            if(!s.metadata->instance_count)return pending("Object initialization requires the returned metadata's live instance count+2C.");
            ++*s.metadata->instance_count;s.phase=P::instance_list;break;
        case P::instance_list:
            if(!s.metadata->instances)return pending("Object initialization requires metadata instance-list field+30.");
            if(*s.metadata->instances)select(K::register_instance,P::after_instance).instances=*s.metadata->instances;
            else s.phase=P::done;
            break;
        case P::after_instance:clear();s.phase=P::done;break;
        case P::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending("Object initialization requires its selected native engine operation.");
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending("Object initialization reached an unbound virtual, allocator or instance-list operation.");
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending("Object initialization requires its retained operation continuation.");
            {const auto result=s.callback();check();if(!result.completed)return result;s.returned=result.value;s.phase=s.after;}
            break;
        case P::done:
            clear();s.object.storage->active_.reset();if(s.object.field)s.object.field->construction_active_.reset();s.owns=false;return {true,0,{}};
        }}
    }catch(...) {invalidate();throw;}
}
} // namespace dw4
