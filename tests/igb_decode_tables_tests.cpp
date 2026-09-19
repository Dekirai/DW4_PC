#include "dw4/igb_decode_tables.hpp"
#include "dw4/igb_stream.hpp"
#include "dw4/igb_decode.hpp"
#include "dw4/igb.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {++checks;try {operation();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
void word(std::vector<std::uint8_t>& bytes,std::uint32_t value,bool little=false) {
    for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(little ? i*8:(3-i)*8)));
}
std::vector<std::uint8_t> fields(const std::vector<std::string>& names,bool little=false) {
    std::vector<std::uint8_t> bytes;for(const auto& name:names) {word(bytes,static_cast<std::uint32_t>(name.size()+1),little);word(bytes,77,little);word(bytes,99,little);}
    for(const auto& name:names) {bytes.insert(bytes.end(),name.begin(),name.end());bytes.push_back(0);}return bytes;
}
std::vector<std::uint8_t> alignments(const std::vector<std::string>& names,bool little=false) {
    std::vector<std::uint8_t> body;for(const auto& name:names)word(body,static_cast<std::uint32_t>(name.size()+1),little);
    for(const auto& name:names) {body.insert(body.end(),name.begin(),name.end());body.push_back(0);}
    std::vector<std::uint8_t> bytes;word(bytes,static_cast<std::uint32_t>(12+body.size()),little);word(bytes,0x12345678,little);word(bytes,static_cast<std::uint32_t>(names.size()),little);
    bytes.insert(bytes.end(),body.begin(),body.end());return bytes;
}
// These are explicit prepared runtime registry/list-constructor/allocator
// endpoints. Stream bytes, table iteration, matching and publication are native.
struct Fixture {
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbStreamState> io=std::make_shared<IgbStreamState>();
    std::shared_ptr<IgbDecodeTablesState> state=std::make_shared<IgbDecodeTablesState>();
    std::shared_ptr<IgbDecodeTablesServices> services=std::make_shared<IgbDecodeTablesServices>();
    std::map<const IgbDecodeRuntimeField*,SystemStageString> metadata;
    std::vector<IgbDecodeTableServiceKind> trace;
    std::function<void(const IgbDecodeTableCall&,IgbDecodeTableOperation&)> on_call;
    std::function<void(const IgbDecodeTableCall&,std::shared_ptr<IgbDecodeTableOperation>&)> on_select;
    Fixture(std::vector<std::uint8_t> bytes={}) {
        io->file=file;file->pool_118=std::make_shared<ModelSceneData>();igb_bind_native_scratch_pool(io,file->pool_118);io->stream=igb_stream_from_bytes(text("synthetic.igb"),std::move(bytes));
        io->statuses->success=17;io->statuses->failure=91;
        io->buffer=std::make_shared<IgbScratchBuffer>();io->buffer->bytes.resize(4096);io->buffer->initialized.resize(4096);io->buffer_capacity=4096;
        io->int_field=std::make_shared<IgbEndianField>(IgbEndianField{4});
        services->field_registry=std::make_shared<IgbDecodeFieldRegistry>();services->field_registry->backing=std::make_shared<IgbDecodeFieldList::Storage>();
        services->alignment_registry=std::make_shared<IgbDecodeAlignmentRegistry>();
        (*services->alignment_registry)->names=std::make_shared<IgbDecodeNameList>();(*services->alignment_registry)->values=std::make_shared<IgbDecodeWordList>();
        services->factory=[this](const IgbDecodeTableCall& request) {
            auto operation=std::make_shared<IgbDecodeTableOperation>();auto weak=std::weak_ptr(operation);
            operation->resume=[this,request,weak] {
                auto out=weak.lock();trace.push_back(request.kind);
                switch(request.kind) {
                case IgbDecodeTableServiceKind::field_registry:
                    out->registry_result=std::make_shared<IgbDecodeFieldRegistry>();out->registry_result->backing=std::make_shared<IgbDecodeFieldList::Storage>();break;
                case IgbDecodeTableServiceKind::field_metadata:
                    out->metadata_result=std::make_shared<IgbDecodeRuntimeType>();out->metadata_result->name=metadata.at(request.field.get());break;
                case IgbDecodeTableServiceKind::create_field_list:out->field_list_result=std::make_shared<IgbDecodeFieldList>();break;
                case IgbDecodeTableServiceKind::create_alignment_list:out->alignment_list_result=std::make_shared<IgbDecodeWordList>();break;
                case IgbDecodeTableServiceKind::resize_field_list:
                    if(request.capacity<0 || request.capacity>262144)throw std::runtime_error("Prepared field allocation outside test profile.");
                    request.field_list->backing->resize(static_cast<std::size_t>(request.capacity));request.field_list->capacity=request.capacity;break;
                case IgbDecodeTableServiceKind::resize_alignment_list:
                    if(request.capacity<0 || request.capacity>262144)throw std::runtime_error("Prepared alignment allocation outside test profile.");
                    request.alignment_list->backing->resize(static_cast<std::size_t>(request.capacity));request.alignment_list->capacity=request.capacity;break;
                }
                if(on_call)on_call(request,*out);return MessengerCallResult{true,0,{}};
            };
            if(on_select)on_select(request,operation);return operation;
        };
    }
    std::shared_ptr<IgbDecodeRuntimeField> add_field(const char* name) {
        auto field=std::make_shared<IgbDecodeRuntimeField>();field->owner=std::make_shared<ModelSceneData>();metadata[field.get()]=text(name);
        services->field_registry->backing->push_back(field);++services->field_registry->count;return field;
    }
    void add_alignment(SystemStageString name,std::uint32_t value) {
        auto names=(*services->alignment_registry)->names;auto values=(*services->alignment_registry)->values;
        auto entry=std::make_shared<IgbDecodeName>();entry->value=std::move(name);names->backing->push_back(entry);values->backing->push_back(value);
        ++names->count;++values->count;
    }
    IgbDecodeTablesInvocation call(IgbDecodeTableKind kind) {return {file,io,state,kind};}
};
void field_mapping() {
    for(bool little:{false,true}) {
        const auto bytes=fields({"A","missing","A"},little);Fixture f(bytes);f.io->field_type_bytes=static_cast<std::uint32_t>(bytes.size());f.io->field_type_count=3;f.io->swapped=little;
        const auto first=f.add_field("A"),duplicate=f.add_field("A");unsigned metadata_calls{};
        f.on_call=[&](const auto& request,auto& out) {
            if(request.kind==IgbDecodeTableServiceKind::field_metadata) {
                ++metadata_calls;
                if(metadata_calls==2 || metadata_calls==6)out.metadata_result.reset(); // Matched second call result is ignored.
            }
        };
        auto call=f.call(IgbDecodeTableKind::field_types);check(call.resume(f.services).completed && call.value()==17,"Native field table did not complete.");
        const auto values=f.state->field_types->backing;
        check(f.state->field_types->count==3 && values->at(0)==first && !values->at(1) && values->at(2)==first && values->at(0)!=duplicate,
            "Field mapping lost first match, duplicate occurrence or unmatched null.");
        check(metadata_calls==6 && !f.state->field_buffer && f.io->stream->position==bytes.size(),"Field mapping missed duplicate metadata call or final pointer clear.");
        check(call.resume().completed && metadata_calls==6,"Completed field table replayed runtime calls.");
    }
}
void alignment_mapping() {
    for(bool little:{false,true}) {
        const auto bytes=alignments({"wide","missing","","wide"},little);Fixture f(bytes);f.io->swapped=little;
        f.add_alignment(text("wide"),0x80000020);f.add_alignment(text("wide"),3);f.add_alignment({},0xffffffff);
        (*f.services->alignment_registry)->values->count=-1; // Actual lookup uses physical corresponding slot.
        auto call=f.call(IgbDecodeTableKind::alignments);check(call.resume(f.services).completed && call.value()==17,"Native alignment mapping did not complete.");
        check(f.state->alignments->count==4 && *f.state->alignments->backing==std::vector<std::uint32_t>({0x80000020,16,0xffffffff,0x80000020}),
            "Alignment table changed first match, null-name fallback, raw word or default16.");
        check(f.state->alignment_count==4 && f.state->alignment_bytes==static_cast<std::int32_t>(bytes.size()-12) && !f.state->alignment_buffer,
            "Alignment header fields or final body-pointer clear changed.");
    }
    for(std::uint32_t count:{0u,0xffffffffu,0x80000000u}) {
        std::vector<std::uint8_t> bytes;word(bytes,4);word(bytes,0);word(bytes,count);Fixture f(bytes);
        const auto old=f.state->alignment_buffer=std::make_shared<IgbScratchBuffer>();f.services.reset();
        auto call=f.call(IgbDecodeTableKind::alignments);check(call.resume().completed && call.value()==17 && f.state->alignment_buffer==old && !f.state->alignments,
            "Nonpositive alignment count reached body/registry/list creation or cleared old pointer.");
        check(f.state->alignment_bytes==-8 && f.io->stream->position==12,"Signed header arithmetic changed.");
    }
}
void object_block_and_cohesive_sequence() {
    for(std::uint32_t size:{0u,1u,4u,9u}) {
        Fixture f({1,2,3,4});f.io->object_type_bytes=size;
        if(size==0)f.io->stream.reset();
        auto call=f.call(IgbDecodeTableKind::object_type_bytes);check(call.resume().completed && call.value()==(size>4 ? 91:17),"Object raw block status did not test exact one element.");
        check(f.state->object_type_buffer==f.io->buffer,"Object raw block did not retain actual scratch allocation alias.");
        if(size>0)check(f.io->stream->position==std::min(size,4u),"Object raw block changed concrete stream cursor.");
    }
    auto bytes=fields({"A"});const auto field_size=bytes.size();const auto align=alignments({"align"});bytes.insert(bytes.end(),align.begin(),align.end());
    bytes.insert(bytes.end(),{0x12,0x34,0x56,0x78});Fixture f(bytes);f.io->field_type_count=1;f.io->field_type_bytes=static_cast<std::uint32_t>(field_size);f.io->object_type_bytes=4;
    const auto original=f.add_field("A");f.add_alignment(text("align"),64);
    auto first=f.call(IgbDecodeTableKind::field_types);auto second=f.call(IgbDecodeTableKind::alignments);auto third=f.call(IgbDecodeTableKind::object_type_bytes);
    check(first.resume(f.services).completed && second.resume(f.services).completed && third.resume(f.services).completed,"Concrete early table sequence did not consume shared stream.");
    check(f.io->stream->position==bytes.size() && f.state->field_types->backing->at(0)==original && f.state->alignments->backing->at(0)==64 &&
        igb_buffer_word(f.state->object_type_buffer,0)==0x12345678,"Early table sequence mixed offsets, identities or scratch aliases.");
}
void captured_callbacks_live_fields_and_status() {
    const auto bytes=fields({"A","A"});Fixture f(bytes);f.io->field_type_count=2;f.io->field_type_bytes=static_cast<std::uint32_t>(bytes.size());const auto first=f.add_field("A");
    const auto replacement=std::make_shared<IgbDecodeRuntimeField>();replacement->owner=std::make_shared<ModelSceneData>();f.metadata[replacement.get()]=text("A");
    unsigned selected{},resumed{};bool ready{};auto factory=f.services->factory;
    f.on_select=[&](const auto& request,auto& operation) {
        if(request.kind!=IgbDecodeTableServiceKind::field_metadata || selected++!=0)return;
        auto weak=std::weak_ptr(operation);operation->resume=[&,weak,field=request.field] {
            ++resumed;if(!ready)return MessengerCallResult{false,0,"Prepared field metadata."};
            check(field==first,"Pending metadata recaptured replaced registry backing.");auto result=std::make_shared<IgbDecodeRuntimeType>();result->name=text("A");weak.lock()->metadata_result=result;return MessengerCallResult{true,0,{}};
        };
    };
    auto call=f.call(IgbDecodeTableKind::field_types);check(!call.resume(f.services).completed && f.state->field_buffer,"Missing metadata did not preserve raw buffer.");
    f.services->field_registry->backing=std::make_shared<IgbDecodeFieldList::Storage>(1,replacement);f.services->field_registry->count=0;
    f.services->factory={};ready=true;check(!call.resume(f.services).completed && resumed==2,"Pending metadata was redirected or completed missing second call.");
    f.services->factory=factory;check(call.resume(f.services).completed && f.state->field_types->backing->at(0)==first && f.state->field_types->backing->at(1)==replacement,
        "Field registry failed captured count or live backing reload.");
    Fixture status(fields({}));status.io->field_type_count=0;status.io->field_type_bytes=0;
    status.on_call=[&](const auto& request,auto&) {if(request.kind==IgbDecodeTableServiceKind::resize_field_list)status.io->statuses->success.reset();};
    auto pending_status=status.call(IgbDecodeTableKind::field_types);check(!pending_status.resume(status.services).completed && !status.state->field_buffer && status.state->field_types,
        "Missing final success word did not pause after pointer clear.");
    const auto visits=status.trace.size();status.io->statuses->success=123;check(pending_status.resume(status.services).completed && pending_status.value()==123 && status.trace.size()==visits,
        "Late status word replayed table effects or used stale status.");
}
void factory_reference_lifetimes() {
    Fixture fields_fixture;fields_fixture.io->field_type_count=0;fields_fixture.io->field_type_bytes=0;
    std::weak_ptr<IgbDecodeFieldList> retained;
    {auto call=fields_fixture.call(IgbDecodeTableKind::field_types);check(call.resume(fields_fixture.services).completed,"Empty field table did not finish.");retained=fields_fixture.state->field_types;}
    check(fields_fixture.state->field_list_creation_refs.size()==1 && fields_fixture.state->field_list_creation_refs.front()==fields_fixture.state->field_types,
        "Completed field helper lost the original factory creator reference.");
    fields_fixture.state->field_types.reset();check(!retained.expired(),"Field creator lifetime ended with the published field reference.");
    fields_fixture.state->field_list_creation_refs.clear();check(retained.expired(),"Synthetic field list retained an unexplained third reference.");
    Fixture alignment_fixture(alignments({"missing"}));std::weak_ptr<IgbDecodeWordList> released;
    {auto call=alignment_fixture.call(IgbDecodeTableKind::alignments);check(call.resume(alignment_fixture.services).completed,"Alignment table did not finish.");released=alignment_fixture.state->alignments;}
    alignment_fixture.state->alignments.reset();check(released.expired(),"Alignment helper retained a creator reference that original51954..74 releases.");
}
void reached_partial_failures() {
    {Fixture f(std::vector<std::uint8_t>(11));f.state->alignment_bytes=77;f.state->alignment_count=88;
        auto call=f.call(IgbDecodeTableKind::alignments);check(call.resume().completed && call.value()==91 && f.state->alignment_bytes==77 && f.state->alignment_count==88,
            "Short alignment header published fields before read success.");}
    {auto bytes=alignments({"x"});bytes.pop_back();Fixture f(bytes);auto call=f.call(IgbDecodeTableKind::alignments);
        check(call.resume().completed && call.value()==91 && f.state->alignment_buffer==f.io->buffer && !f.state->alignments,
            "Short alignment body cleared published pointer or reached runtime factory.");}
    {Fixture f(fields({"A"}));f.io->field_type_bytes=100;f.io->field_type_count=1;const auto candidate=f.add_field("A");
        auto call=f.call(IgbDecodeTableKind::field_types);check(call.resume(f.services).completed && call.value()==17 && f.state->field_types->backing->at(0)==candidate,
            "Field table incorrectly checked the ignored read result even when reached bytes exist.");}
    {Fixture f({0,0,0,2});f.io->field_type_bytes=100;f.io->field_type_count=1;f.add_field("A");
        auto call=f.call(IgbDecodeTableKind::field_types);rejects([&]{call.resume(f.services);},"Unknown scratch name bytes were fabricated as zeros.");
        check(call.faulted() && f.state->field_buffer && f.state->field_types && f.state->field_types->count==0,"Unknown byte failure lost reached table prefix.");}
    {Fixture f(fields({"A"}));f.io->field_type_bytes=14;f.io->field_type_count=1;
        f.on_call=[](const auto& request,auto&) {if(request.kind==IgbDecodeTableServiceKind::resize_field_list)request.field_list->backing->clear();};
        auto call=f.call(IgbDecodeTableKind::field_types);rejects([&]{call.resume(f.services);},"Short output backing was accepted.");
        check(f.state->field_types->count==1 && f.state->field_buffer,"Output failure rolled back published count or cleared buffer.");}
    {Fixture f(alignments({"x"}));f.add_alignment(text("x"),32);(*f.services->alignment_registry)->values.reset();
        auto call=f.call(IgbDecodeTableKind::alignments);rejects([&]{call.resume(f.services);},"Matched null alignment values were invented.");
        check(f.state->alignments && f.state->alignments->count==0 && f.state->alignment_buffer,"Matched value failure lost earlier fields.");}
    {Fixture f(alignments({"missing"}));(*f.services->alignment_registry)->names.reset();(*f.services->alignment_registry)->values.reset();
        auto call=f.call(IgbDecodeTableKind::alignments);check(call.resume(f.services).completed && f.state->alignments->backing->at(0)==16,
            "Null name list read unused null value list instead of default16.");}
    {Fixture f(fields({"A"}));f.io->field_type_bytes=14;f.io->field_type_count=1;f.services->factory={};
        {auto call=f.call(IgbDecodeTableKind::field_types);check(!call.resume(f.services).completed && f.state->field_buffer,"Missing list constructor silently completed.");}
        auto retry=f.call(IgbDecodeTableKind::field_types);rejects([&]{retry.resume(f.services);},"Abandoned table replayed published prefix.");}
}

void original_prefix(const std::filesystem::path& supplied) {
    auto input=supplied;
    if(std::filesystem::is_directory(input)) {
        if(std::filesystem::is_directory(input/"files"))input/="files";
        input/="ndmw.igb";
    }
    std::ifstream file(input,std::ios::binary);check(bool(file),"Original ndmw input could not be opened.");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    check(bytes.size()<=64u*1024u*1024u,"Original table input exceeded the represented byte-stream profile.");
    const auto metadata=read_igb_metadata(std::span<const std::uint8_t>(bytes));
    check(metadata.header.version==6,"Original table integration requires supplied v6 data.");
    Fixture f;const auto filename=text("ndmw.igb");f.file->filename=filename;f.file->option_a4=0xffffffff;
    f.io->stream.reset();f.io->buffer.reset();f.io->buffer_capacity=0;f.io->expected_cookie=0xfada;
    // Direct-table fixtures prepare this field themselves. The fresh file
    // profile instead lets the actual header acquire its singleton owners.
    f.io->int_field.reset();f.io->short_field.reset();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> prepared;
    // Deliberate prepared runtime registration. Serialized names guide fixture
    // construction; this does not claim original global registry initialization.
    for(const auto& field:metadata.field_types)prepared.push_back(f.add_field(field.name.c_str()));
    auto state=std::make_shared<IgbDecodeState>();state->stream=f.io;state->tables=f.state;f.file->igb_decode=state;
    auto services=std::make_shared<IgbDecodeServices>();services->stream=make_igb_byte_stream_services(filename,bytes);services->tables=f.services;
    unsigned samples{};auto timer=std::make_shared<NativeGameClock>([&] {++samples;return std::uint64_t{1000000000};});
    timer->state().stopped=0;services->clock=timer;services->factory=make_native_igb_stage_factory(services);
    IgbDecodeInvocation invocation(f.file,filename);const auto result=invocation.resume(services);
    check(!result.completed && result.diagnostic.find("8004FC94")!=std::string::npos,"Original decoder did not stop at actual runtime metatype construction.");
    check(f.file->loading==1 && f.file->succeeded_31==0 && !invocation.faulted() && samples==1,
        "Early original metadata invented loaded content, faulted, or resampled the timer.");
    check(f.io->version==6 && f.io->field_type_count==prepared.size() && f.io->object_type_count==metadata.object_types.size(),
        "Concrete original header disagrees with independently decoded serialized metadata.");
    check(f.state->field_types && f.state->field_types->count==static_cast<std::int32_t>(prepared.size()),
        "Original field-table caller did not publish every prepared runtime occurrence.");
    for(std::size_t i=0;i<prepared.size();++i)check(f.state->field_types->backing->at(i)==prepared[i],"Original field table changed prepared canonical identity.");
    check(f.state->alignment_count==4 && f.state->alignments && f.state->alignments->count==4 &&
        *f.state->alignments->backing==std::vector<std::uint32_t>({16,16,16,16}),"Original ndmw alignment entries did not use the explicit empty registry's default16.");
    const auto offset=metadata.metadata_bytes-metadata.header.object_type_buffer_size;
    const auto actual=igb_buffer_bytes(f.state->object_type_buffer,0,metadata.header.object_type_buffer_size);
    check(std::equal(actual.begin(),actual.end(),bytes.begin()+offset),"Original object-type block bytes were altered or read at a wrong cursor.");
    check(f.io->stream && f.io->stream->position==metadata.metadata_bytes && !f.state->field_buffer && !f.state->alignment_buffer,
        "Original early helpers did not reach the exact metadata boundary and clear transient pointers.");
    const auto calls=f.trace.size();check(!invocation.resume(services).completed && f.trace.size()==calls && samples==1 &&
        f.io->stream->position==metadata.metadata_bytes,"Pending original metatype construction replayed the completed native prefix.");
    std::cout<<"Original native metadata prefix: "<<prepared.size()<<" field mappings, "<<f.state->alignment_count
        <<" default alignment values, "<<metadata.object_types.size()<<" serialized object types, cursor "<<metadata.metadata_bytes<<"; pending8004FC94.\n";
}
}
int main(int argc,char** argv) {
    if(argc>2) {std::cerr<<"Usage: dw4_igb_decode_tables_tests [EXTRACTED_ROOT]\n";return 2;}
    try {field_mapping();alignment_mapping();object_block_and_cohesive_sequence();captured_callbacks_live_fields_and_status();factory_reference_lifetimes();reached_partial_failures();
        if(argc==2)original_prefix(std::filesystem::path(argv[1]));
        std::cout<<"IGB decode table checks: "<<checks<<"\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"IGB decode tables failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
