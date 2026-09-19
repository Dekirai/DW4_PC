#include "dw4/igb_type_mapping.hpp"

#include <algorithm>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbTypeMappingError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
std::int32_t signed_word(std::uint32_t value) {return std::bit_cast<std::int32_t>(value);}
std::int16_t narrow_short(std::uint32_t value) {return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));}
std::int32_t increment(std::int32_t value) {return signed_word(std::bit_cast<std::uint32_t>(value)+1);}
template<class T> void acquire(const std::shared_ptr<T>& owner) {if(owner)++owner->reference_word;}
template<class T> T& physical(const std::shared_ptr<std::vector<T>>& backing,std::int32_t index) {
    if(index<0 || index>=262144 || !backing || static_cast<std::size_t>(index)>=backing->size())
        fail("IGB type mapping reaches an unavailable physical list slot.");
    return (*backing)[static_cast<std::size_t>(index)];
}
std::int32_t growth(std::int32_t capacity,std::int32_t count) {
    capacity=std::max<std::int32_t>(capacity,4);
    while(capacity<count) {
        if(capacity>=262144)fail("IGB type list exceeds the bounded allocation profile.");
        capacity=capacity<1024 ? capacity*2:capacity+1024;
    }
    if(capacity>262144)fail("IGB type list exceeds the bounded allocation profile.");
    return capacity;
}
std::int16_t buffer_short(const std::shared_ptr<IgbScratchBuffer>& buffer,std::uint32_t offset) {
    const auto bytes=igb_buffer_bytes(buffer,offset,2);
    return narrow_short((static_cast<std::uint32_t>(bytes[0])<<8)|bytes[1]);
}
const char* boundary(IgbTypeMappingServiceKind kind) {
    using K=IgbTypeMappingServiceKind;
    switch(kind) {
    case K::create_type_list:return "IGB type mapping8004FC94 requires metatype-list factory80034380.";
    case K::create_slot_map_list:return "IGB type mapping requires slot-map-list factory80035244.";
    case K::create_short_list:return "IGB type mapping requires short-list factory80035418.";
    case K::reserve_type_list:case K::reserve_slot_map_list:case K::reserve_short_list:
        return "IGB type mapping requires captured list metadata/pool allocation8004155C.";
    case K::destroy_list:return "IGB type mapping reaches final list destruction80066E1C.";
    case K::short_converter:return "IGB type mapping requires fresh short-field singleton80025CD4.";
    case K::default_pool:return "IGB dynamic type construction requires default pool80058C80.";
    case K::create_metatype:return "IGB dynamic type construction requires metatype factory80029E64.";
    case K::name_metatype:return "IGB dynamic type construction requires name assignment80065788.";
    case K::parent_metatype:return "IGB dynamic type construction requires parent assignment80065640.";
    case K::create_byte_field:return "IGB dynamic type construction requires byte-field factory80037B1C.";
    case K::initialize_field:return "IGB dynamic field requires initialization80063BA8(name,0,-1,type).";
    case K::field_alignment:return "IGB dynamic field requires selected alignment virtual+6C.";
    case K::append_field:return "IGB dynamic type requires field registration80065844 and its field virtuals.";
    case K::field_metadata:return "IGB dynamic type requires the selected field instance metadata virtual+58.";
    case K::create_field:return "IGB dynamic type requires field instance factory8006546C.";
    case K::begin_registration:return "IGB dynamic type requires registry publication8003E450.";
    case K::register_metatype:return "IGB dynamic type requires registry notification8003E574.";
    }
    return "IGB type mapping requires its reached operation.";
}
}

struct IgbTypeMappingInvocation::Impl {
    enum class Phase {
        entry,after_type_factory,publish_types,drop_type_creator,reserve_types,version,
        map_factory,after_map_factory,publish_maps,drop_map_creator,reserve_maps,
        adjustment_factory,after_adjustment_factory,reserve_adjustments,swap_descriptors,capture,visit,lookup,found,
        append_known,known_map_factory,after_record_map_factory,record_map_reserve,append_record_map,field_bytes,
        after_short_converter,map_capture,map_visit,map_advance,map_delta,map_release,advance,
        missing,dynamic_parent,dynamic_parent_counts,dynamic_pool,after_dynamic_pool,dynamic_factory,
        after_dynamic_factory,dynamic_name,dynamic_parent_set,append_dynamic,dynamic_field_bytes,
        dynamic_visit,dynamic_gap,dynamic_byte_factory,after_byte_factory,initialize_gap,after_gap_init,
        field_metadata,after_field_metadata,field_pool,after_field_pool,field_factory,after_field_factory,
        initialize_real,after_real_init,field_alignment,after_alignment,append_field,after_append_field,
        dynamic_next,dynamic_finish,dynamic_register,dynamic_notify,dynamic_map_factory,
        after_dynamic_map_factory,success,status,release_adjustments,finish,
        append,append_after_resize,append_store,call,done
    };
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbStreamState> stream;
    std::shared_ptr<IgbDecodeTablesState> tables;
    std::shared_ptr<IgbTypeMappingState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::shared_ptr<IgbMappedTypeList> new_types;
    std::shared_ptr<IgbSlotMapList> new_maps;
    std::shared_ptr<IgbShortSlotList> adjustments,record_map,map_adjustments,map_output;
    std::shared_ptr<IgbRuntimeMetaType> metadata,parent,field_type;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<ModelSceneObject> default_pool;
    std::shared_ptr<IgbScratchBuffer> data;
    std::unique_ptr<IgbRuntimeTypeLookupInvocation> lookup;
    IgbTypeMappingCall request;
    IgbTypeMappingFactory factory;
    std::shared_ptr<IgbTypeMappingOperation> operation;
    std::function<MessengerCallResult()> callback;
    IgbMappingReference append_list;
    std::variant<std::monostate,std::shared_ptr<IgbRuntimeMetaType>,std::shared_ptr<IgbShortSlotList>,std::int16_t> append_value;
    Phase phase=Phase::entry,after=Phase::done,append_after=Phase::done;
    std::uint32_t descriptor{},names{},triples{},parent_descriptor{},descriptor_words{},field_halves{},returned{},result{},field_creations{};
    std::int32_t index{},field_index{},parent_index{},parent_difference{},logical_slot{},target_slot{},append_index{},append_count{};
    bool mapping{},dynamic{},gap{},success{},owns{},running{},fault{};
};
IgbTypeMappingInvocation::IgbTypeMappingInvocation(std::shared_ptr<LoaderRequestFile> file,std::shared_ptr<IgbStreamState> stream,
    std::shared_ptr<IgbDecodeTablesState> tables,std::shared_ptr<IgbTypeMappingState> state):impl_(std::make_unique<Impl>()) {
    impl_->file=std::move(file);impl_->stream=std::move(stream);impl_->tables=std::move(tables);impl_->state=std::move(state);
}
IgbTypeMappingInvocation::~IgbTypeMappingInvocation() {
    if(impl_->owns && !completed()) {
        impl_->state->fault_=true;
        for(const std::shared_ptr<void>& owner:{std::shared_ptr<void>(impl_->new_types),std::shared_ptr<void>(impl_->new_maps),
            std::shared_ptr<void>(impl_->adjustments),std::shared_ptr<void>(impl_->record_map),std::shared_ptr<void>(impl_->metadata),
            std::shared_ptr<void>(impl_->field)})if(owner)impl_->state->unreleased_.push_back(owner);
        std::visit([&](const auto& owner) {
            using T=std::decay_t<decltype(owner)>;
            if constexpr(std::is_same_v<T,std::shared_ptr<IgbRuntimeMetaType>> ||
                std::is_same_v<T,std::shared_ptr<IgbShortSlotList>>)
                if(owner)impl_->state->unreleased_.push_back(owner);
        },impl_->append_value);
    }
}
bool IgbTypeMappingInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbTypeMappingInvocation::faulted() const noexcept {return impl_->fault;}
std::uint32_t IgbTypeMappingInvocation::value() const {
    if(!completed() || faulted())fail("IGB type mapping result requires completed unfaulted work.");return impl_->result;
}
MessengerCallResult IgbTypeMappingInvocation::resume(std::shared_ptr<IgbTypeMappingServices> services) {
    auto& s=*impl_;using P=Impl::Phase;using K=IgbTypeMappingServiceKind;
    if(s.fault || s.running) {s.fault=true;if(s.owns)s.state->fault_=true;fail("IGB type mapping requires a nonrecursive unfaulted invocation.");}
    if(completed())return {true,s.result,{}};
    struct Guard {bool& flag;Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(s.running);
    const auto check=[&] {if(s.fault || (s.owns && s.state->fault_))fail("IGB type mapping was invalidated by recursive work.");};
    const auto clear_call=[&] {s.callback={};s.factory={};s.request={};s.operation.reset();check();};
    const auto select=[&](K kind,P after)->IgbTypeMappingCall& {
        clear_call();s.request.kind=kind;s.request.file=s.file;s.request.state=s.state;
        s.factory=services ? services->factory:IgbTypeMappingFactory{};
        s.after=after;s.phase=P::call;return s.request;
    };
    const auto word=[&](std::uint32_t relative) {return igb_buffer_word(s.data,s.descriptor+relative);};
    const auto release=[&](IgbMappingReference reference,P after) {
        bool final{};
        std::visit([&](const auto& owner) {
            using T=std::decay_t<decltype(owner)>;
            if constexpr(!std::is_same_v<T,std::monostate>)if(owner) {
                --owner->reference_word;final=(owner->reference_word&0x7fffffu)==0;
            }
        },reference);
        if(final)select(K::destroy_list,after).reference=std::move(reference);
        else s.phase=after;
    };
    const auto replace=[&](auto& destination,const auto& incoming) {
        if(destination && destination!=incoming && (destination->reference_word&0x7fffffu)!=0)
            s.state->unreleased_.push_back(destination);
        destination=incoming;
    };
    const auto reserve=[&](IgbMappingReference list,std::int32_t capacity,P after) {
        std::visit([&](const auto& owner) {
            using T=std::decay_t<decltype(owner)>;
            if constexpr(std::is_same_v<T,std::shared_ptr<IgbMappedTypeList>>) {
                auto& call=select(K::reserve_type_list,after);call.type_list=owner;call.capacity=capacity;
            }else if constexpr(std::is_same_v<T,std::shared_ptr<IgbSlotMapList>>) {
                auto& call=select(K::reserve_slot_map_list,after);call.slot_maps=owner;call.capacity=capacity;
            }else if constexpr(std::is_same_v<T,std::shared_ptr<IgbShortSlotList>>) {
                auto& call=select(K::reserve_short_list,after);call.short_list=owner;call.capacity=capacity;
            }else fail("IGB type mapping selects an absent list allocator.");
        },list);
    };
    const auto append=[&](IgbMappingReference list,auto value,P after) {
        if constexpr(!std::is_same_v<decltype(value),std::int16_t>)acquire(value);
        s.append_list=std::move(list);s.append_value=std::move(value);s.append_after=after;s.phase=P::append;
    };
    const auto finish_status=[&](bool success) {s.success=success;s.phase=P::status;};
    try {
        if(!s.file || !s.stream || !s.tables || !s.state)fail("IGB type mapping requires canonical file and field owners.");
        if(s.stream->file.lock()!=s.file)fail("IGB type mapping stream belongs to a different file.");
        if(!s.owns) {
            if(s.state->fault_)fail("A faulted IGB type prefix cannot restart.");
            if(!s.state->active_.expired())fail("Another pending invocation owns these IGB type fields.");
            s.state->active_=s.token;s.owns=true;
        }
        for(;;) {check();switch(s.phase) {
        case P::entry:select(K::create_type_list,P::after_type_factory).pool=s.file->pool_118;break;
        case P::after_type_factory:
            s.new_types=std::move(s.operation->type_list_result);clear_call();acquire(s.new_types);
            release(s.state->types,P::publish_types);break;
        case P::publish_types:replace(s.state->types,s.new_types);s.phase=P::drop_type_creator;break;
        case P::drop_type_creator:release(s.new_types,P::reserve_types);break;
        case P::reserve_types:
            clear_call();s.new_types.reset();
            if(!s.state->types)fail("IGB type mapping dereferences a null metatype list factory result.");
            if(signed_word(s.stream->object_type_count)>=s.state->types->count)
                reserve(s.state->types,signed_word(s.stream->object_type_count),P::version);
            else s.phase=P::version;
            break;
        case P::version:
            clear_call();s.mapping=signed_word(s.stream->version)>=4;s.descriptor_words=s.mapping?6u:3u;s.field_halves=s.mapping?3u:0u;
            s.phase=s.mapping?P::map_factory:P::swap_descriptors;break;
        case P::map_factory:select(K::create_slot_map_list,P::after_map_factory).pool=s.file->pool_118;break;
        case P::after_map_factory:
            s.new_maps=std::move(s.operation->slot_maps_result);clear_call();acquire(s.new_maps);
            release(s.state->slot_maps,P::publish_maps);break;
        case P::publish_maps:replace(s.state->slot_maps,s.new_maps);s.phase=P::drop_map_creator;break;
        case P::drop_map_creator:release(s.new_maps,P::reserve_maps);break;
        case P::reserve_maps:
            clear_call();s.new_maps.reset();
            if(!s.state->slot_maps)fail("IGB type mapping dereferences a null slot-map list factory result.");
            if(signed_word(s.stream->object_type_count)>=s.state->slot_maps->count)
                reserve(s.state->slot_maps,signed_word(s.stream->object_type_count),P::adjustment_factory);
            else s.phase=P::adjustment_factory;
            break;
        case P::adjustment_factory:select(K::create_short_list,P::after_adjustment_factory).pool=s.file->pool_118;break;
        case P::after_adjustment_factory:
            s.adjustments=std::move(s.operation->short_list_result);clear_call();s.phase=P::reserve_adjustments;break;
        case P::reserve_adjustments:
            if(!s.adjustments)fail("IGB type mapping dereferences a null adjustment-list factory result.");
            if(signed_word(s.stream->object_type_count)>=s.adjustments->count)
                reserve(s.adjustments,signed_word(s.stream->object_type_count),P::swap_descriptors);
            else s.phase=P::swap_descriptors;
            break;
        case P::swap_descriptors:
            clear_call();
            if(s.stream->swapped!=0) {
                if(!s.stream->int_field)fail("IGB type mapping dereferences a null integer converter.");
                igb_swap_words(s.tables->object_type_buffer,0,s.stream->int_field->width,
                    signed_word(s.descriptor_words*s.stream->object_type_count));
            }
            s.phase=P::capture;break;
        case P::capture:
            s.data=s.tables->object_type_buffer;s.names=s.descriptor_words*s.stream->object_type_count*4u;
            s.descriptor=0;s.index=0;s.phase=P::visit;break;
        case P::visit:
            if(s.index>=signed_word(s.stream->object_type_count)) {s.phase=P::success;break;}
            if(s.index>=262144)fail("IGB type iteration exceeds the native profile.");
            s.lookup=std::make_unique<IgbRuntimeTypeLookupInvocation>(s.data,s.names);s.phase=P::lookup;break;
        case P::lookup: {
            const auto result=s.lookup->resume(services ? services->runtime:nullptr);check();if(!result.completed)return result;
            s.metadata=s.lookup->result();s.lookup.reset();s.phase=P::found;break;
        }
        case P::found:
            if(!s.metadata) {s.phase=P::missing;break;}
            if((word(4)==0?1u:word(4))!=s.metadata->major || word(8)!=s.metadata->minor) {finish_status(false);break;}
            s.dynamic=false;s.phase=P::append_known;break;
        case P::append_known:append(s.state->types,s.metadata,s.mapping?P::known_map_factory:P::advance);break;
        case P::known_map_factory:select(K::create_short_list,P::after_record_map_factory).pool=s.file->pool_118;break;
        case P::after_record_map_factory:
            s.record_map=std::move(s.operation->short_list_result);clear_call();s.phase=P::record_map_reserve;break;
        case P::record_map_reserve: {
            const auto count=signed_word(word(12));
            if(!s.record_map)fail("IGB type mapping dereferences a null per-record map factory result.");
            if(count>=s.record_map->count)reserve(s.record_map,count,P::append_record_map);
            else s.phase=P::append_record_map;
            break;
        }
        case P::append_record_map:clear_call();append(s.state->slot_maps,s.record_map,P::field_bytes);break;
        case P::field_bytes: {
            const auto swapped=s.stream->swapped;
            s.triples=s.names+word(0);s.field_index=0;
            if(swapped!=0)select(K::short_converter,P::after_short_converter);
            else s.phase=P::map_capture;
            break;
        }
        case P::after_short_converter: {
            const auto converter=s.operation->converter_result;
            const auto count=signed_word(s.field_halves*word(12));
            if(!converter)fail("IGB type mapping dereferences a null fresh short converter.");
            igb_swap_words(s.data,s.triples,converter->width,count);
            clear_call();s.phase=s.dynamic?P::dynamic_visit:P::map_capture;break;
        }
        case P::map_capture:
            s.map_adjustments=s.adjustments;s.map_output=s.record_map;s.field_index=0;s.phase=P::map_visit;break;
        case P::map_visit: {
            if(s.field_index>=signed_word(word(12))) {s.phase=P::map_delta;break;}
            if(s.field_index>=262144)fail("IGB field mapping exceeds the native profile.");
            auto record=s.descriptor;auto current=s.index;
            auto total=signed_word(igb_buffer_word(s.data,record+20u));
            const auto slot=static_cast<std::int32_t>(buffer_short(s.data,s.triples+2u));
            unsigned visits{};
            while(slot<total) {
                if(++visits>262144)fail("IGB slot-parent walk exceeds the bounded traversal profile.");
                current=signed_word(igb_buffer_word(s.data,record+16u));
                record=std::bit_cast<std::uint32_t>(current)*s.descriptor_words*4u;
                total=signed_word(igb_buffer_word(s.data,record+20u));
            }
            if(!s.map_adjustments)fail("IGB slot mapping dereferences a null captured adjustment list.");
            const auto backing=s.map_adjustments->backing;
            if(!s.map_output)fail("IGB slot mapping dereferences a null captured output map.");
            const auto delta=physical(backing,current);
            append(s.map_output,narrow_short(std::bit_cast<std::uint32_t>(slot)+static_cast<std::uint32_t>(delta)),P::map_advance);break;
        }
        case P::map_advance:s.triples+=s.field_halves*2u;++s.field_index;s.phase=P::map_visit;break;
        case P::map_delta: {
            const auto count=igb_runtime_slot_count(s.metadata);
            append(s.adjustments,narrow_short(std::bit_cast<std::uint32_t>(count)-word(20)),P::map_release);break;
        }
        case P::map_release:release(s.record_map,P::advance);break;
        case P::advance:
            clear_call();s.record_map.reset();s.map_output.reset();s.map_adjustments.reset();
            s.names+=word(0);if(s.mapping)s.names+=s.field_halves*(word(12)*2u);
            s.descriptor+=s.descriptor_words*4u;++s.index;s.phase=P::visit;break;
        case P::missing:
            if(!s.mapping) {finish_status(false);break;}
            if(!services || !services->allow_dynamic)
                return pending("IGB unknown type requires original dynamic-registration flag8056213D.");
            if(*services->allow_dynamic==0) {finish_status(false);break;}
            s.dynamic=true;s.phase=P::dynamic_parent;break;
        case P::dynamic_parent: {
            const auto types=s.state->types;
            s.parent_index=signed_word(word(16));
            if(!types)fail("IGB dynamic type dereferences a null parent type list.");
            s.parent=physical(types->backing,s.parent_index);
            if(!s.parent)fail("IGB dynamic type dereferences null parent metadata.");
            const auto flag=s.parent->flag_1a;
            s.parent_descriptor=std::bit_cast<std::uint32_t>(s.parent_index)*s.descriptor_words*4u;
            if(flag!=0) {finish_status(false);break;}
            s.phase=P::dynamic_parent_counts;break;
        }
        case P::dynamic_parent_counts:
            s.logical_slot=igb_runtime_local_field_count(s.parent);
            s.parent_difference=signed_word(std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.parent))-
                std::bit_cast<std::uint32_t>(s.logical_slot));
            s.phase=P::dynamic_pool;break;
        case P::dynamic_pool:select(K::default_pool,P::after_dynamic_pool);break;
        case P::after_dynamic_pool:
            s.default_pool=std::move(s.operation->pool_result);clear_call();s.phase=P::dynamic_factory;break;
        case P::dynamic_factory:select(K::create_metatype,P::after_dynamic_factory).pool=s.default_pool;break;
        case P::after_dynamic_factory:
            s.metadata=std::move(s.operation->metadata_result);
            if(s.metadata)s.state->unreleased_.push_back(s.metadata); // No creator release in original8004FC94.
            clear_call();s.phase=P::dynamic_name;break;
        case P::dynamic_name: {
            auto& request=select(K::name_metatype,P::dynamic_parent_set);
            request.metadata=s.metadata;request.name_source=IgbRuntimeNameSource{s.data,s.names};break;
        }
        case P::dynamic_parent_set: {
            auto& request=select(K::parent_metatype,P::append_dynamic);
            request.metadata=s.metadata;request.parent=s.parent;break;
        }
        case P::append_dynamic:clear_call();append(s.state->types,s.metadata,P::dynamic_field_bytes);break;
        case P::dynamic_field_bytes: {
            const auto swapped=s.stream->swapped;
            s.triples=s.names+word(0);s.field_index=0;
            if(swapped!=0)select(K::short_converter,P::after_short_converter);
            else s.phase=P::dynamic_visit;
            break;
        }
        case P::dynamic_visit: {
            if(s.field_index>=signed_word(word(12))) {s.phase=P::dynamic_finish;break;}
            if(s.field_index>=262144)fail("IGB dynamic field iteration exceeds the native profile.");
            const auto slot=static_cast<std::int32_t>(buffer_short(s.data,s.triples+2u));
            const auto parent_total=signed_word(igb_buffer_word(s.data,s.parent_descriptor+20u));
            if(slot<parent_total) {
                s.triples+=s.field_halves*2u;++s.field_index;s.phase=P::dynamic_visit;break;
            }
            if(!s.adjustments)fail("IGB dynamic type dereferences a null adjustment list.");
            const auto adjustment=physical(s.adjustments->backing,s.parent_index);
            s.target_slot=signed_word(std::bit_cast<std::uint32_t>(slot)+static_cast<std::uint32_t>(adjustment)-
                std::bit_cast<std::uint32_t>(s.parent_difference));
            s.phase=P::dynamic_gap;break;
        }
        case P::dynamic_gap:
            s.phase=s.logical_slot<s.target_slot?P::dynamic_byte_factory:P::field_metadata;break;
        case P::dynamic_byte_factory:
            if(++s.field_creations>262144)fail("IGB dynamic field creation exceeds the bounded native profile.");
            select(K::create_byte_field,P::after_byte_factory).pool=s.file->pool_118;break;
        case P::after_byte_factory:
            s.field=std::move(s.operation->field_result);
            if(s.field)s.state->unreleased_.push_back(s.field); // The original keeps the creator reference.
            clear_call();
            if(!s.field)fail("IGB gap-field creation reaches a null field byte+26 store.");
            s.field->flag_26=0;s.gap=true;s.phase=P::initialize_gap;break;
        case P::initialize_gap: {
            auto& request=select(K::initialize_field,P::after_gap_init);request.metadata=s.metadata;request.field=s.field;
            request.name=std::make_shared<const std::string>("fakeField");break;
        }
        case P::after_gap_init:
            clear_call();s.field->ordinal=static_cast<std::uint16_t>(s.logical_slot);s.field->size=1;
            s.phase=P::field_alignment;break;
        case P::field_metadata: {
            const auto types=s.tables->field_types;
            const auto type_index=static_cast<std::int32_t>(buffer_short(s.data,s.triples));
            if(!types)fail("IGB dynamic type reads a null field-instance list.");
            const auto instance=physical(types->backing,type_index);
            if(!instance)fail("IGB dynamic type selects metadata virtual+58 on a null field instance.");
            select(K::field_metadata,P::after_field_metadata).field=instance;break;
        }
        case P::after_field_metadata:
            s.field_type=std::move(s.operation->metadata_result);clear_call();s.phase=P::field_pool;break;
        case P::field_pool:select(K::default_pool,P::after_field_pool);break;
        case P::after_field_pool:
            s.default_pool=std::move(s.operation->pool_result);clear_call();s.phase=P::field_factory;break;
        case P::field_factory: {
            if(++s.field_creations>262144)fail("IGB dynamic field creation exceeds the bounded native profile.");
            auto& request=select(K::create_field,P::after_field_factory);request.metadata=s.field_type;request.pool=s.default_pool;break;
        }
        case P::after_field_factory:
            s.field=std::move(s.operation->field_result);
            if(s.field)s.state->unreleased_.push_back(s.field);
            clear_call();s.gap=false;s.phase=P::initialize_real;break;
        case P::initialize_real: {
            auto& request=select(K::initialize_field,P::after_real_init);request.metadata=s.metadata;request.field=s.field;
            request.name=std::make_shared<const std::string>("Generated");break;
        }
        case P::after_real_init:
            clear_call();
            if(!s.field)fail("IGB generated field reaches a null ordinal store.");
            s.field->ordinal=static_cast<std::uint16_t>(s.target_slot);
            s.field->size=static_cast<std::uint16_t>(buffer_short(s.data,s.triples+4u));
            s.phase=P::field_alignment;break;
        case P::field_alignment:select(K::field_alignment,P::after_alignment).field=s.field;break;
        case P::after_alignment: {
            const auto align_minus_one=s.returned-1u;clear_call();
            if(!s.metadata)fail("IGB dynamic field alignment dereferences null class metadata.");
            const auto previous=s.metadata->native_alignment;
            s.metadata->native_alignment=static_cast<std::uint16_t>(
                signed_word(s.returned)>static_cast<std::int32_t>(previous)?s.returned:previous);
            s.field->offset=(s.metadata->native_size+align_minus_one)&~align_minus_one;
            s.phase=P::append_field;break;
        }
        case P::append_field: {
            auto& request=select(K::append_field,P::after_append_field);request.metadata=s.metadata;request.field=s.field;break;
        }
        case P::after_append_field: {
            clear_call();const auto offset=s.field->offset;
            if(s.gap)s.logical_slot=increment(s.logical_slot);
            else s.triples+=s.field_halves*2u;
            const auto size=s.field->size;
            if(!s.gap)s.logical_slot=increment(s.logical_slot);
            s.metadata->native_size=offset+size;
            s.phase=s.gap?P::dynamic_gap:P::dynamic_next;break;
        }
        case P::dynamic_next:++s.field_index;s.phase=P::dynamic_visit;break;
        case P::dynamic_finish: {
            if(!s.metadata)fail("IGB dynamic type finalization dereferences null metadata.");
            const auto alignment=static_cast<std::uint32_t>(s.metadata->native_alignment)-1u;
            s.metadata->native_size=(s.metadata->native_size+alignment)&~alignment;
            s.metadata->class_flags|=4u;s.phase=P::dynamic_register;break;
        }
        case P::dynamic_register:case P::dynamic_notify: {
            clear_call();
            if(!services || !services->runtime || !services->runtime->singleton)
                return pending("IGB dynamic registration requires original singleton global80562100.");
            const auto registry=*services->runtime->singleton;
            const bool notify=s.phase==P::dynamic_notify;
            auto& request=select(notify?K::register_metatype:K::begin_registration,
                notify?P::dynamic_map_factory:P::dynamic_notify);
            request.registry=registry;request.metadata=s.metadata;break;
        }
        case P::dynamic_map_factory:
            clear_call();s.triples=s.names+word(0);
            select(K::create_short_list,P::after_dynamic_map_factory).pool=s.file->pool_118;break;
        case P::after_dynamic_map_factory:
            s.record_map=std::move(s.operation->short_list_result);clear_call();
            append(s.state->slot_maps,s.record_map,P::map_capture);break;
        case P::append:
            std::visit([&](const auto& owner) {
                using T=std::decay_t<decltype(owner)>;
                if constexpr(std::is_same_v<T,std::monostate>)fail("IGB append has no selected list.");
                else {
                    if(!owner)fail("IGB append dereferences a null selected list.");
                    s.append_index=owner->count;
                    const auto capacity=owner->capacity;
                    if(s.append_index<capacity) {
                        owner->count=increment(s.append_index);s.phase=P::append_store;
                    }else {
                        // Object append80041970 reloads count; inline shorts
                        // preserve their earlier captured count instead.
                        if constexpr(!std::is_same_v<T,std::shared_ptr<IgbShortSlotList>>)
                            s.append_index=owner->count;
                        s.append_count=increment(s.append_index);
                        reserve(s.append_list,growth(owner->capacity,s.append_count),P::append_after_resize);
                    }
                }
            },s.append_list);break;
        case P::append_after_resize:
            clear_call();
            std::visit([&](const auto& owner) {
                using T=std::decay_t<decltype(owner)>;
                if constexpr(!std::is_same_v<T,std::monostate>)owner->count=s.append_count;
            },s.append_list);
            s.phase=P::append_store;break;
        case P::append_store:
            std::visit([&](const auto& owner) {
                using T=std::decay_t<decltype(owner)>;
                if constexpr(std::is_same_v<T,std::shared_ptr<IgbShortSlotList>>)
                    physical(owner->backing,s.append_index)=std::get<std::int16_t>(s.append_value);
                else if constexpr(!std::is_same_v<T,std::monostate>) {
                    auto& slot=physical(owner->backing,s.append_index);
                    if(slot)s.state->unreleased_.push_back(slot); // Original raw store has no old-owner release.
                    using Value=typename T::element_type::Storage::value_type;
                    slot=std::get<Value>(s.append_value);
                }
            },s.append_list);
            s.append_list={};s.append_value={};s.phase=s.append_after;break;
        case P::success:s.tables->object_type_buffer.reset();finish_status(true);break;
        case P::status: {
            const auto statuses=s.stream->statuses;
            if(!statuses)return pending("IGB type mapping requires its reached status-word owner.");
            const auto value=s.success?statuses->success:statuses->failure;
            if(!value)return pending("IGB type mapping requires its reached success/failure status word.");
            s.result=*value;s.phase=P::release_adjustments;break;
        }
        case P::release_adjustments:release(s.adjustments,P::finish);break;
        case P::finish:
            clear_call();s.adjustments.reset();s.state->active_.reset();s.owns=false;s.phase=P::done;
            return {true,s.result,{}};
        case P::call: {
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(boundary(s.request.kind));
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending(boundary(s.request.kind));
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending(boundary(s.request.kind));
            const auto result=s.callback();check();if(!result.completed)return result;
            s.returned=result.value;s.phase=s.after;break;
        }
        case P::done:return {true,s.result,{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.state->fault_=true;throw;}
}
} // namespace dw4
