#include "dw4/igb_type_mapping.hpp"
#include "dw4/igb_runtime_bootstrap.hpp"
#include "dw4/igb_decode.hpp"
#include "dw4/igb.hpp"
#include "dw4/loader_globals.hpp"
#include "dw4/loader_dependency.hpp"
#include "dw4/igb_string_pool.hpp"
#include "dw4/igb_file_factory.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace dw4;
using K=IgbTypeMappingServiceKind;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& fn,const char* reason) {
    ++checks;try {fn();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
struct Record {
    std::string name;
    std::uint32_t major{},minor{};
    std::int32_t parent{-1};
    std::uint32_t slots{};
    std::vector<std::array<std::uint16_t,3>> fields;
};
void integer(std::vector<std::uint8_t>& bytes,std::uint32_t value,unsigned width,bool little) {
    for(unsigned i=0;i<width;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(8u*(little?i:width-i-1u))));
}
std::shared_ptr<IgbScratchBuffer> records(const std::vector<Record>& items,bool mapping,bool little) {
    auto result=std::make_shared<IgbScratchBuffer>();auto& bytes=result->bytes;
    for(const auto& item:items) {
        integer(bytes,static_cast<std::uint32_t>(item.name.size()+1),4,little);
        integer(bytes,item.major,4,little);integer(bytes,item.minor,4,little);
        if(mapping) {integer(bytes,static_cast<std::uint32_t>(item.fields.size()),4,little);
            integer(bytes,std::bit_cast<std::uint32_t>(item.parent),4,little);integer(bytes,item.slots,4,little);}
    }
    for(const auto& item:items) {
        bytes.insert(bytes.end(),item.name.begin(),item.name.end());bytes.push_back(0);
        if(mapping)for(const auto& field:item.fields)for(const auto value:field)integer(bytes,value,2,little);
    }
    result->initialized.assign(bytes.size(),1);return result;
}
template<class T> void reserve(const std::shared_ptr<T>& list,std::int32_t capacity) {
    if(!list || capacity<0 || capacity>262144)throw std::runtime_error("Prepared allocation outside the fixture profile.");
    if(!list->backing)list->backing=std::make_shared<typename T::Storage>();
    list->backing->resize(static_cast<std::size_t>(capacity));list->capacity=capacity;
}
// Constructors, allocation, destruction, field operations and registration are
// explicit prepared endpoints. The caller, lookup, swaps and lists are native.
struct Fixture {
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbStreamState> io=std::make_shared<IgbStreamState>();
    std::shared_ptr<IgbDecodeTablesState> tables=std::make_shared<IgbDecodeTablesState>();
    std::shared_ptr<IgbTypeMappingState> state=std::make_shared<IgbTypeMappingState>();
    std::shared_ptr<IgbTypeMappingServices> services=std::make_shared<IgbTypeMappingServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> registry=std::make_shared<IgbRuntimeTypeRegistry>();
    std::vector<K> trace;
    std::vector<std::shared_ptr<IgbShortSlotList>> shorts;
    std::vector<std::shared_ptr<IgbRuntimeMetaType>> created_types;
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created_fields;
    std::vector<std::string> field_names;
    std::function<void(const IgbTypeMappingCall&,IgbTypeMappingOperation&)> on_call;
    std::function<void(const IgbTypeMappingCall&,std::shared_ptr<IgbTypeMappingOperation>&)> on_select;
    std::uint32_t alignment{4};
    Fixture(const std::vector<Record>& input={},std::uint32_t version=6,bool little=false) {
        io->file=file;file->pool_118=std::make_shared<ModelSceneData>();io->version=version;io->swapped=little;
        io->object_type_count=static_cast<std::uint32_t>(input.size());io->statuses->success=17;io->statuses->failure=91;
        io->int_field=std::make_shared<IgbEndianField>(IgbEndianField{4});
        io->short_field=std::make_shared<IgbEndianField>(IgbEndianField{7}); // The caller must use the fresh getter instead.
        tables->object_type_buffer=records(input,std::bit_cast<std::int32_t>(version)>=4,little);
        tables->field_types=std::make_shared<IgbDecodeFieldList>();
        tables->field_types->backing->push_back(std::make_shared<IgbDecodeRuntimeField>());
        tables->field_types->count=-1; // Dynamic selection reads physical backing, not logical count.
        registry->enabled=1;registry->types=std::make_shared<IgbRuntimeTypeList>();
        registry->types->backing=std::make_shared<IgbRuntimeTypeList::Storage>();
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=registry;
        services->runtime->name_field=std::make_shared<IgbRuntimeNameField>();services->allow_dynamic=0;
        services->factory=[this](const IgbTypeMappingCall& request) {
            auto operation=std::make_shared<IgbTypeMappingOperation>();const auto weak=std::weak_ptr(operation);
            operation->resume=[this,request,weak] {
                auto output=weak.lock();trace.push_back(request.kind);
                switch(request.kind) {
                case K::create_type_list:output->type_list_result=std::make_shared<IgbMappedTypeList>();break;
                case K::create_slot_map_list:output->slot_maps_result=std::make_shared<IgbSlotMapList>();break;
                case K::create_short_list:
                    output->short_list_result=std::make_shared<IgbShortSlotList>();shorts.push_back(output->short_list_result);break;
                case K::reserve_type_list:reserve(request.type_list,request.capacity);break;
                case K::reserve_slot_map_list:reserve(request.slot_maps,request.capacity);break;
                case K::reserve_short_list:reserve(request.short_list,request.capacity);break;
                case K::destroy_list:break;
                case K::short_converter:output->converter_result=std::make_shared<IgbEndianField>(IgbEndianField{2});break;
                case K::default_pool:output->pool_result=file->pool_118;break;
                case K::create_metatype:
                    output->metadata_result=std::make_shared<IgbRuntimeMetaType>();output->metadata_result->major=1;
                    output->metadata_result->fields=std::make_shared<IgbDecodeFieldRegistry>();
                    output->metadata_result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
                    created_types.push_back(output->metadata_result);break;
                case K::name_metatype:request.metadata->name=igb_runtime_read_name(*request.name_source);break;
                case K::parent_metatype:
                    request.metadata->fields->count=request.parent->fields->count;
                    *request.metadata->fields->backing=*request.parent->fields->backing;
                    request.metadata->native_size=request.parent->native_size;
                    request.metadata->native_alignment=request.parent->native_alignment;request.metadata->class_flags=0x20;break;
                case K::create_byte_field:case K::create_field:
                    output->field_result=std::make_shared<IgbDecodeRuntimeField>();output->field_result->flag_26=9;
                    created_fields.push_back(output->field_result);break;
                case K::initialize_field:field_names.push_back(*request.name);break;
                case K::field_alignment:break;
                case K::append_field:
                    request.metadata->fields->backing->push_back(request.field);++request.metadata->fields->count;
                    ++request.field->reference_word;break;
                case K::field_metadata:output->metadata_result=std::make_shared<IgbRuntimeMetaType>();break;
                case K::begin_registration:case K::register_metatype:break;
                }
                if(on_call)on_call(request,*output);
                return MessengerCallResult{true,request.kind==K::field_alignment?alignment:0,{}};
            };
            if(on_select)on_select(request,operation);return operation;
        };
    }
    std::shared_ptr<IgbRuntimeMetaType> add_type(std::string name,std::int32_t count) {
        auto result=std::make_shared<IgbRuntimeMetaType>();result->name=text(std::move(name));result->major=1;
        result->fields=std::make_shared<IgbDecodeFieldRegistry>();result->fields->count=count;
        result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
        for(std::int32_t i=0;i<count && i<64;++i)result->fields->backing->push_back(std::make_shared<IgbDecodeRuntimeField>());
        result->native_size=12;result->native_alignment=4;
        registry->types->backing->push_back(result);++registry->types->count;return result;
    }
    IgbTypeMappingInvocation call() {return {file,io,tables,state};}
};

void known_types() {
    for(const auto version:{0u,3u,4u,6u,0xffffffffu})for(bool little:{false,true}) {
        Fixture f({{"Root"},{"Child",0,0,0,2,{{0,0,4},{0,1,2}}},{"Leaf",1,0,1,5,{{0,1,1},{0,3,1},{0,4,1}}}},version,little);
        const auto root=f.add_type("Root",2),child=f.add_type("Child",5),leaf=f.add_type("Leaf",7);
        auto call=f.call();check(call.resume(f.services).completed && call.value()==17,"Known native types did not complete.");
        check(!f.tables->object_type_buffer && f.state->types->count==3 && f.state->types->reference_word==1,
            "Known mapping lost type publication, creator release or raw buffer clear.");
        check(*f.state->types->backing==IgbMappedTypeList::Storage({root,child,leaf}) && root->reference_word==2 && child->reference_word==2,
            "Known append lost canonical identity or actual incoming reference.");
        if(std::bit_cast<std::int32_t>(version)>=4) {
            check(f.state->slot_maps && f.state->slot_maps->count==3 && f.shorts.size()==4,"Mapped versions omitted per-record maps.");
            check(*f.state->slot_maps->backing->at(1)->backing==std::vector<std::int16_t>({2,3}) &&
                *f.state->slot_maps->backing->at(2)->backing==std::vector<std::int16_t>({3,6,7}),"Inherited field slots used wrong ancestor adjustments.");
            check(*f.shorts.front()->backing==std::vector<std::int16_t>({2,3,2}) && f.shorts.front()->reference_word==0 &&
                f.shorts.back()->reference_word==1,"Temporary adjustments or map creator references were not released.");
            check(std::count(f.trace.begin(),f.trace.end(),K::short_converter)==(little?3:0),"Fresh short converter skipped a zero-field type or used file+D0.");
        }else check(!f.state->slot_maps && f.shorts.empty(),"Legacy version reached extended descriptors or maps.");
        const auto size=f.trace.size();check(call.resume().completed && f.trace.size()==size,"Completed type mapping replayed callbacks.");
    }
}

void failure_status_and_lazy_names() {
    {Fixture f({{"Root"},{"Mismatch",2}});f.add_type("Root",2);f.add_type("Mismatch",3);
        f.on_call=[&](const auto& request,auto&) {if(request.kind==K::destroy_list)f.io->statuses->failure=123;};
        auto call=f.call();check(call.resume(f.services).completed && call.value()==91 && f.state->types->count==1 && f.tables->object_type_buffer,
            "Version mismatch rolled back earlier type or captured status after destruction.");}
    {Fixture f({{"Missing"}});f.registry->enabled=0;f.tables->object_type_buffer->initialized.assign(24,1);
        auto call=f.call();check(call.resume(f.services).completed && call.value()==91,"Disabled lookup read an unavailable type name.");}
    {Fixture f({{"Missing"}});f.tables->object_type_buffer->initialized.assign(24,1);
        auto call=f.call();check(call.resume(f.services).completed && call.value()==91,"Empty lookup read an unavailable type name.");}
    {Fixture f({{"Root"}});const auto root=f.add_type("Root",2);f.services->runtime->name_field.reset();
        auto call=f.call();check(!call.resume(f.services).completed,"Missing descriptor global did not suspend lookup.");
        const auto buffer=f.tables->object_type_buffer;buffer->bytes[24]='N';root->name=text("Noot");
        f.services->runtime->name_field=std::make_shared<IgbRuntimeNameField>();
        check(call.resume(f.services).completed && call.value()==17,"Suspended name lookup used an eagerly copied string.");}
    {Fixture f;f.on_call=[&](const auto& request,auto&) {if(request.kind==K::reserve_short_list)f.io->statuses->success.reset();};
        auto call=f.call();check(!call.resume(f.services).completed && !f.tables->object_type_buffer && f.shorts.front()->reference_word==1,
            "Absent success global did not suspend after clear and before temporary release.");
        f.io->statuses->success=77;f.on_call=[&](const auto& request,auto&) {if(request.kind==K::destroy_list)f.io->statuses->success=88;};
        check(call.resume(f.services).completed && call.value()==77 && f.shorts.front()->reference_word==0,"Final destruction changed the captured success word.");}
}

void retained_operations_and_faults() {
    {Fixture f;const auto old=f.state->types=std::make_shared<IgbMappedTypeList>();bool ready{};unsigned polls{};
        std::shared_ptr<IgbMappedTypeList> incoming;
        f.on_call=[&](const auto& request,auto& result) {if(request.kind==K::create_type_list)incoming=result.type_list_result;};
        f.on_select=[&](const auto& request,auto& operation) {
            if(request.kind!=K::destroy_list)return;
            operation->resume=[&] {++polls;return MessengerCallResult{ready,0,"Prepared final destruction."};};
        };
        auto call=f.call();check(!call.resume(f.services).completed && old->reference_word==0 && incoming->reference_word==2 && f.state->types==old,
            "Replacement published before the old owner's final destructor returned.");
        f.on_select={};ready=true;check(call.resume(f.services).completed && f.state->types==incoming && incoming->reference_word==1 && polls==2,
            "Pending final destructor was reselected or incoming reference was lost.");}
    {Fixture f({{"Root"}});const auto root=f.add_type("Root",2);
        f.on_call=[](const auto& request,auto&) {if(request.kind==K::reserve_type_list)request.type_list->backing->clear();};
        auto call=f.call();rejects([&]{call.resume(f.services);},"Short physical type backing was accepted.");
        check(call.faulted() && f.state->faulted() && f.state->types->count==1 && root->reference_word==2 && f.tables->object_type_buffer,
            "Failed type store rolled back the original count/reference prefix.");
        rejects([&]{call.resume(f.services);},"Faulted mapping restarted its published prefix.");}
    {Fixture f({{"Root",0,0,0,1,{{0,0,1}}}});f.add_type("Root",2);
        auto call=f.call();rejects([&]{call.resume(f.services);},"Cyclic serialized parent walk was silently accepted.");}
    {Fixture f({{"Root"},{"Child",0,0,-1,1,{{0,0,1}}}});f.add_type("Root",2);f.add_type("Child",3);
        auto call=f.call();rejects([&]{call.resume(f.services);},"Unrepresented parent descriptor memory was accepted.");}
    {Fixture f;f.services->factory={};{auto call=f.call();check(!call.resume(f.services).completed,"Missing mapping constructor fabricated a list.");}
        auto retry=f.call();rejects([&]{retry.resume(f.services);},"Abandoned mapping restarted the same file state.");}
    {Fixture f({{"Root"}});f.add_type("Root",2);auto call=f.call();
        f.on_call=[&](const auto&,auto&) {try {call.resume(f.services);}catch(const std::runtime_error&) {}};
        rejects([&]{call.resume(f.services);},"Swallowed recursive invocation did not invalidate the outer work.");}
}

void dynamic_types() {
    for(bool little:{false,true}) {
        Fixture f({{"Root"},{"Dynamic",0,0,0,7,{{0,2,4},{0,0,2},{0,4,1}}}},6,little);f.services->allow_dynamic=1;
        const auto root=f.add_type("Root",3);root->fields->backing->at(1)->flag_25=1;
        auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();unsigned registered{},appended{};
        f.on_call=[&](const auto& request,auto&) {
            if(request.kind==K::begin_registration) {check(request.registry==f.registry,"First registration lost its captured singleton.");f.services->runtime->singleton=replacement;}
            if(request.kind==K::register_metatype) {check(request.registry==replacement,"Second registration reused a stale singleton.");++registered;}
            if(request.kind==K::append_field && ++appended==5) {request.field->offset=40;request.field->size=7;}
        };
        auto call=f.call();check(call.resume(f.services).completed && call.value()==17 && registered==1,"Dynamic caller did not reach completed registration.");
        const auto type=f.created_types.at(0);check(*type->name=="Dynamic" && type->reference_word==2 && type->fields->count==8 &&
            type->native_size==48 && type->native_alignment==4 && type->class_flags==0x24,"Dynamic type layout lost inherited count, alignment, flags or post-append size reload.");
        check(f.field_names==std::vector<std::string>({"fakeField","fakeField","Generated","Generated","Generated"}),
            "Dynamic caller changed original field initialization names or generated gaps after a decreasing slot.");
        const std::array<std::uint16_t,5> ordinals{2,3,4,2,6};
        for(std::size_t i=0;i<ordinals.size();++i)check(f.created_fields[i]->ordinal==ordinals[i] && f.created_fields[i]->reference_word==2,
            "Dynamic logical slot or creator reference changed.");
        check(f.created_fields[0]->flag_26==0 && f.created_fields[1]->flag_26==0 && f.created_fields[2]->flag_26==9,
            "Gap byte-field flag clear leaked into real field initialization.");
        const auto map=f.state->slot_maps->backing->at(1);check(map->count==3 && map->backing->at(0)==5 && map->backing->at(1)==3 && map->backing->at(2)==7,
            "Dynamic slot map lost parent runtime adjustment.");
        check(type->fields->backing->at(0)==root->fields->backing->at(0),"Dynamic prepared parent endpoint lost inherited field identity.");
    }
    {Fixture f({{"Root"},{"Dynamic",0,0,0}});const auto root=f.add_type("Root",2);root->flag_1a=128;f.services->allow_dynamic=1;
        auto call=f.call();check(call.resume(f.services).completed && call.value()==91 && f.created_types.empty(),"Forbidden parent flag reached dynamic construction.");}
    {Fixture f({{"Missing"}},3);f.services->allow_dynamic=1;auto call=f.call();
        check(call.resume(f.services).completed && call.value()==91 && f.created_types.empty(),"Legacy unknown type entered dynamic construction.");}
}

void original_prefix(const std::filesystem::path& supplied) {
    auto input=supplied;if(std::filesystem::is_directory(input)) {
        if(std::filesystem::is_directory(input/"files"))input/="files";input/="ndmw.igb";
    }
    std::ifstream source(input,std::ios::binary);check(bool(source),"Original ndmw input could not be opened.");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());
    check(bytes.size()<=64u*1024u*1024u,"Original byte stream exceeds the native integration profile.");
    const auto serialized=read_igb_metadata(std::span<const std::uint8_t>(bytes));
    const auto bootstrap=make_original_ndmw_runtime_bootstrap();Fixture f;const auto name=text("ndmw.igb");
    f.file->filename=name;f.file->option_a4=0xffffffff;f.io->version=0;f.io->swapped=0;f.io->object_type_count=0;
    f.io->expected_cookie=0xfada;f.io->int_field.reset();f.io->short_field.reset();f.tables->field_types.reset();f.tables->object_type_buffer.reset();
    igb_bind_native_scratch_pool(f.io,f.file->pool_118);f.services->runtime=bootstrap.type_services();
    auto table_services=std::make_shared<IgbDecodeTablesServices>();table_services->field_registry=bootstrap.field_registry();
    table_services->alignment_registry=std::make_shared<IgbDecodeAlignmentRegistry>();
    (*table_services->alignment_registry)->names=std::make_shared<IgbDecodeNameList>();
    (*table_services->alignment_registry)->values=std::make_shared<IgbDecodeWordList>();
    table_services->factory=[bootstrap](const IgbDecodeTableCall& request) {
        auto operation=std::make_shared<IgbDecodeTableOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[bootstrap,request,weak] {
            auto output=weak.lock();using T=IgbDecodeTableServiceKind;
            switch(request.kind) {
            case T::field_metadata:output->metadata_result=bootstrap.field_metadata(request.field);break;
            case T::create_field_list:output->field_list_result=std::make_shared<IgbDecodeFieldList>();break;
            case T::create_alignment_list:output->alignment_list_result=std::make_shared<IgbDecodeWordList>();break;
            case T::resize_field_list:reserve(request.field_list,request.capacity);break;
            case T::resize_alignment_list:reserve(request.alignment_list,request.capacity);break;
            case T::field_registry:throw std::runtime_error("Recovered registration profile was unexpectedly replaced.");
            }
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto state=f.file->igb_decode=std::make_shared<IgbDecodeState>();state->stream=f.io;state->tables=f.tables;state->type_mapping=f.state;
    auto services=std::make_shared<IgbDecodeServices>();services->stream=make_igb_byte_stream_services(name,std::move(bytes));
    services->tables=table_services;services->type_mapping=f.services;unsigned samples{};
    auto timer=std::make_shared<NativeGameClock>([&] {++samples;return std::uint64_t{1000000000};});timer->state().stopped=0;services->clock=timer;
    services->factory=make_native_igb_stage_factory(services);IgbDecodeInvocation invocation(f.file,name);const auto result=invocation.resume(services);
    check(!result.completed && result.diagnostic.find("800505E0")!=std::string::npos,"Original known metatypes did not reach directory-path preparation.");
    check(f.file->loading==1 && f.file->succeeded_31==0 && samples==1 && !invocation.faulted(),"Original mapped types invented decoder completion or repeated timing.");
    check(f.state->types->count==20 && f.state->slot_maps->count==20 && !f.tables->object_type_buffer &&
        f.io->stream->position==serialized.metadata_bytes,"Original type mapping changed count, raw buffer release or stream cursor.");
    const std::array<std::int32_t,20> runtime_counts{2,3,7,13,14,11,11,13,10,5,5,5,5,4,6,4,5,7,5,5};
    std::size_t mapped_fields{},missing_fields{};
    for(std::size_t i=0;i<serialized.object_types.size();++i) {
        const auto& item=serialized.object_types[i];const auto type=f.state->types->backing->at(i);const auto map=f.state->slot_maps->backing->at(i);
        check(*type->name==item.name && igb_runtime_slot_count(type)==runtime_counts.at(i),"Original type resolved to an unproved runtime layout.");
        check(map->count==static_cast<std::int32_t>(item.fields.size()),"Original per-type mapping omitted serialized fields.");
        for(std::size_t j=0;j<item.fields.size();++j) {
            auto ancestor=i;const auto slot=std::bit_cast<std::int16_t>(item.fields[j].slot);
            while(slot<static_cast<std::int32_t>(serialized.object_types[ancestor].slots)) {
                const auto parent=serialized.object_types[ancestor].parent_index;
                check(parent>=0 && static_cast<std::size_t>(parent)<ancestor,"Original field ancestry is outside the recovered profile.");ancestor=static_cast<std::size_t>(parent);
            }
            const auto adjustment=runtime_counts.at(ancestor)-static_cast<std::int32_t>(serialized.object_types[ancestor].slots);
            check(map->backing->at(j)==static_cast<std::int16_t>(slot+adjustment),"Original inherited field mapping disagrees with independently parsed ancestry.");++mapped_fields;
        }
    }
    check(f.tables->field_types->count==48,"Original field table omitted an unmatched serialized occurrence.");
    for(std::size_t i=0;i<serialized.field_types.size();++i) {
        const auto field=f.tables->field_types->backing->at(i);
        if(field)check(*bootstrap.field_metadata(field)->name==serialized.field_types[i].name,"Original field metadata lost its recovered identity.");
        else {++missing_fields;check(serialized.field_types[i].name=="igVertexBlendShaderCapsInfoMetaField","An unexpectedly missing field class was silently accepted.");}
    }
    check(missing_fields==1 && f.created_types.empty(),"Original registration profile fabricated a missing field class or used dynamic fallback.");
    const auto calls=f.trace.size();check(!invocation.resume(services).completed && samples==1 && f.trace.size()==calls,
        "Pending directory-path preparation replayed completed original mappings.");
    services->paths=std::make_shared<IgbLoadingPathsServices>();services->paths->factory=make_native_igb_path_pool_factory();
    services->dependencies=std::make_shared<IgbDependenciesServices>();
    const auto dependency_bridge=std::make_shared<IgbDependenciesFactory>();
    services->dependencies->factory=make_native_igb_dependency_pool_factory([dependency_bridge](const IgbDependenciesCall& request) {
        return *dependency_bridge?(*dependency_bridge)(request):nullptr;
    });
    const auto dependencies=invocation.resume(services);
    check(!dependencies.completed && dependencies.diagnostic.find("8007042C")!=std::string::npos,
        "Original directory-path/dependency prefix did not reach the actual loader singleton operation.");
    check(state->paths->directory_path && igb_runtime_read_name({state->paths->directory_path,0})->empty(),
        "Bare original stream filename did not produce an empty dependency base path.");
    check(f.io->stream->position==5381 && state->dependencies->body_bytes==2453 && state->dependencies->body &&
        !state->dependencies->body->released && f.file->loading==1 && f.file->succeeded_31==0,
        "Original dependency table was misread, freed before dependency requests or reported fully loaded.");
    check(!invocation.resume(services).completed && f.io->stream->position==5381 && samples==1 && f.trace.size()==calls,
        "Pending dependency loading replayed the completed native header/types/path prefix.");
    auto native_strings=std::make_shared<IgbStringPoolServices>();
    native_strings->strings=services->dependencies->strings;native_strings->mutex=std::shared_ptr<LoaderDirectoryMutex>{};
    auto dependency_services=std::make_shared<LoaderDependencyServices>();
    dependency_services->directory=std::make_shared<LoaderDirectoryServices>();
    dependency_services->directory->filename_field=std::make_shared<LoaderDirectoryNameField>();
    services->dependencies->globals=std::make_shared<SystemStageServices>();
    dependency_services->globals=services->dependencies->globals;dependency_services->strings=services->dependencies->strings;
    const auto strings_factory=make_native_igb_string_factory(native_strings);
    const auto file_factory_bridge=std::make_shared<LoaderDependencyFactory>();
    dependency_services->factory=make_loader_dependency_string_factory(strings_factory,
        [file_factory_bridge](const LoaderDependencyCall& request){return *file_factory_bridge?(*file_factory_bridge)(request):nullptr;});
    const auto prepared_pool_factory=std::make_shared<IgbDependenciesFactory>();
    const auto prepared_pool_bridge=[prepared_pool_factory](const IgbDependenciesCall& request) {
        return *prepared_pool_factory?(*prepared_pool_factory)(request):nullptr;
    };
    *dependency_bridge=make_native_loader_globals_factory(bootstrap.type_services(),
        make_native_igb_string_factory(native_strings,make_native_loader_dependency_factory(dependency_services,prepared_pool_bridge)));
    check(!invocation.resume(services).completed && !f.file->loader.lock(),
        "Native loader getter invented an unrepresented ArkCore loader field.");
    // This prepared igResource owner is an integration input, not proof of its
    // reflected factory or surrounding ArkCore startup. The getter must use it
    // instead of the distinct direct global80562104.
    auto loader=std::make_shared<SystemStageLoader>();loader->directories=std::make_shared<LoaderDirectoryList>();
    loader->pool_38=f.file->pool_118;(*bootstrap.type_services()->singleton)->loader=loader;
    services->dependencies->globals->loader=std::make_shared<SystemStageLoader>();
    const auto uninitialized_pool=invocation.resume(services);
    check(!uninitialized_pool.completed && uninitialized_pool.diagnostic.find("80054140")!=std::string::npos &&
        f.file->loader.lock()==loader,"Recovered loader getter used the wrong global or skipped actual string-pool construction.");
    // The already-selected missing pool constructor cannot be bypassed by
    // changing its singleton. Supply its explicitly prepared allocation/init
    // endpoints while native code owns both interning table operations.
    auto pool_owner=std::make_shared<ModelSceneData>();auto pool=std::make_shared<IgbStringPoolState>();
    pool->table=std::make_shared<IgbStringTable>();pool->table->capacity=4;
    pool->table->storage=std::make_shared<IgbStringTable::Storage>(4);pool->arena=f.file->pool_118;
    igb_bind_string_pool(native_strings,pool_owner,pool);
    unsigned allocations{};
    native_strings->factory=[&allocations](const IgbStringPoolCall& request) {
        if(request.kind!=IgbStringPoolServiceKind::allocate_string)throw std::runtime_error("Original prefix unexpectedly grew its prepared string table.");
        auto operation=std::make_shared<IgbStringPoolOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[&allocations,request,weak] {
            ++allocations;const auto out=weak.lock();out->entry_result=std::make_shared<IgbStringEntry>();
            out->entry_result->owner=std::make_shared<IgbInternedString>();
            out->entry_result->bytes.buffer=std::make_shared<IgbScratchBuffer>();
            out->entry_result->bytes.buffer->bytes.resize(request.bytes+4);
            out->entry_result->bytes.buffer->initialized.assign(request.bytes+4,1);
            out->container_result=request.arena;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto prepare_pool=[pool_owner](const IgbDependenciesCall& request)->std::shared_ptr<IgbDependenciesOperation> {
        if(request.kind!=IgbDependenciesServiceKind::create_string_pool && request.kind!=IgbDependenciesServiceKind::initialize_string_pool)return {};
        auto operation=std::make_shared<IgbDependenciesOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[pool_owner,weak] {weak.lock()->pool_result=pool_owner;return MessengerCallResult{true,0,{}};};return operation;
    };
    *prepared_pool_factory=prepare_pool;
    const auto first_dependency=invocation.resume(services);
    check(!first_dependency.completed && first_dependency.diagnostic.find("8002EBEC")!=std::string::npos,
        "Original dependency name did not reach the actual synchronous file factory.");
    check(loader->primary_prefix && loader->primary_prefix->empty() && pool->table->count==1 && allocations==1 &&
        services->dependencies->strings->owners.size()==1 && services->dependencies->strings->owners[0]->references==1,
        "Original path interning did not preserve its single native allocation and header identity.");
    check(f.io->stream->position==5381 && f.file->loading==1 && f.file->succeeded_31==0 &&
        state->dependencies->body && !state->dependencies->body->released,
        "First dependency factory boundary consumed later tables or invented complete loading.");
    check(!invocation.resume(services).completed && allocations==1 && samples==1 && f.trace.size()==calls,
        "Pending dependency factory replayed completed metadata or string interning.");
    // Enter the actual wrapper without inventing registered igIGBFile metadata,
    // field defaults, or an already-constructed file as its result.
    const auto file_factory=std::make_shared<IgbFileFactoryServices>();
    *file_factory_bridge=make_native_igb_file_factory(file_factory);
    const auto registration=invocation.resume(services);
    check(!registration.completed && registration.diagnostic.find("8002F178")!=std::string::npos,
        "Native file wrapper did not reach the actual metadata registration boundary.");
    check(!invocation.resume(services).completed && !file_factory->file_metadata && allocations==1 && samples==1 && f.trace.size()==calls,
        "Pending file registration invented metadata or replayed completed original work.");
    check(f.io->stream->position==5381 && f.file->loading==1 && f.file->succeeded_31==0 &&
        state->dependencies->body && !state->dependencies->body->released,
        "Native factory wrapper prematurely consumed dependency data or published completion.");
    std::cout<<"Original native runtime types: 20 recovered metatypes, "<<mapped_fields<<" inherited field mappings, 47 matched field classes, 1 unmatched.\n";
    std::cout<<"Original directory path/dependency table: cursor5381, body2453 bytes; native getter/path interning/synchronous request/file wrapper reached registration8002F178.\n";
}
}
int main(int argc,char** argv) {
    if(argc>2) {std::cerr<<"Usage: dw4_igb_type_mapping_tests [EXTRACTED_ROOT]\n";return 2;}
    try {known_types();failure_status_and_lazy_names();retained_operations_and_faults();dynamic_types();
        if(argc==2)original_prefix(std::filesystem::path(argv[1]));std::cout<<"IGB type mapping checks: "<<checks<<"\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"IGB type mapping failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
