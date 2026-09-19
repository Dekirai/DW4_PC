#include "dw4/igb_runtime_types.hpp"
#include "dw4/igb_decode_tables.hpp"
#include "dw4/igb_stream.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;
    try {operation();}catch(const IgbRuntimeTypesError&) {return;}
    throw std::runtime_error(reason);
}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
std::shared_ptr<IgbRuntimeMetaType> type(std::string name) {
    auto result=std::make_shared<IgbRuntimeMetaType>();result->name=text(std::move(name));return result;
}
struct Fixture {
    std::shared_ptr<IgbRuntimeTypesServices> services=std::make_shared<IgbRuntimeTypesServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> registry=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeTypeList> list=std::make_shared<IgbRuntimeTypeList>();
    std::shared_ptr<IgbRuntimeNameField> descriptor=std::make_shared<IgbRuntimeNameField>();
    explicit Fixture(std::initializer_list<std::shared_ptr<IgbRuntimeMetaType>> rows={}) {
        list->backing=std::make_shared<IgbRuntimeTypeList::Storage>(rows);
        list->count=static_cast<std::int32_t>(rows.size());list->capacity=-123;
        registry->enabled=1;registry->types=list;
        services->singleton=registry;services->name_field=descriptor;
    }
};
void first_match_and_strings() {
    static_assert(std::is_same_v<IgbDecodeRuntimeType,IgbRuntimeMetaType>);
    auto first=type("igObject"),second=type("igObject"),third=type("igNamedObject");
    Fixture f{first,second,third};
    for(const auto& request:{std::string("igObject"),std::string("igObject\0suffix",15)}) {
        IgbRuntimeTypeLookupInvocation call(text(request));
        check(call.resume(f.services).completed && call.result()==first,"Lookup lost the first matching runtime identity.");
        f.registry->enabled=0;f.list->backing->clear();
        check(call.resume().completed && call.result()==first,"Completed lookup replayed live globals.");
        f.registry->enabled=1;*f.list->backing={first,second,third};
    }
    for(const char* request:{"igobject","IGOBJECT","igObject/","missing",""}) {
        IgbRuntimeTypeLookupInvocation call(text(request));
        check(call.resume(f.services).completed && !call.result(),"Lookup changed exact C-string matching.");
    }
    first->name=text(std::string("igObject\0different",18));
    IgbRuntimeTypeLookupInvocation embedded(text("igObject"));
    check(embedded.resume(f.services).completed && embedded.result()==first,"Registered embedded NUL was compared as a string suffix.");
    first->name=text("");IgbRuntimeTypeLookupInvocation empty(text(""));
    check(empty.resume(f.services).completed && empty.result()==first,"Empty names were filtered.");
    auto canonical=std::make_shared<IgbDecodeTableOperation>();canonical->metadata_result=third;
    IgbRuntimeTypeLookupInvocation shared(text("igNamedObject"));
    check(shared.resume(f.services).completed && shared.result()==canonical->metadata_result,"Field metadata and object registry created separate canonical owners.");
    first->reference_word=0xff800004;third->reference_word=0x81000002;
    IgbRuntimeTypeLookupInvocation refs(text("igNamedObject"));refs.resume(f.services);
    check(first->reference_word==0xff800004 && third->reference_word==0x81000002,"Borrowed lookup edited packed reference words.");
}
void gate_and_reached_nulls() {
    auto row=type("T");Fixture f{row};
    for(unsigned byte=0;byte<256;++byte) {
        f.registry->enabled=static_cast<std::uint8_t>(byte);
        IgbRuntimeTypeLookupInvocation call(text("T"));
        check(call.resume(f.services).completed && call.result()==(byte?row:nullptr),"Singleton gate required one instead of any nonzero byte.");
    }
    f.registry->enabled=0;f.registry->types.reset();f.services->name_field.reset();
    IgbRuntimeTypeLookupInvocation disabled({});
    check(disabled.resume(f.services).completed && !disabled.result(),"Disabled registry touched descriptor, list, or requested name.");
    f.registry->enabled=1;f.registry->types=f.list;
    f.services->name_field=std::shared_ptr<IgbRuntimeNameField>{};f.list->backing.reset();
    for(auto count:{0,-1,std::numeric_limits<std::int32_t>::min()}) {
        f.list->count=count;IgbRuntimeTypeLookupInvocation call({});
        check(call.resume(f.services).completed && !call.result(),"Nonpositive signed count touched unavailable lookup operands.");
    }
    f.services->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    IgbRuntimeTypeLookupInvocation null_singleton({});
    rejects([&]{null_singleton.resume(f.services);},"Represented null singleton became missing integration.");
    check(null_singleton.faulted() && !null_singleton.completed(),"Null singleton did not preserve invocation fault.");
    f.services->singleton=f.registry;f.registry->types.reset();
    IgbRuntimeTypeLookupInvocation null_list({});
    rejects([&]{null_list.resume(f.services);},"Null registry list was treated as empty.");
    f.registry->types=f.list;f.list->count=1;
    IgbRuntimeTypeLookupInvocation null_descriptor(text("T"));
    rejects([&]{null_descriptor.resume(f.services);},"Null descriptor was treated as an unmatched name.");
    f.services->name_field=f.descriptor;
    IgbRuntimeTypeLookupInvocation no_backing(text("T"));
    rejects([&]{no_backing.resume(f.services);},"Missing physical backing was treated as no match.");
    f.list->backing=std::make_shared<IgbRuntimeTypeList::Storage>(1);
    IgbRuntimeTypeLookupInvocation null_row(text("T"));
    rejects([&]{null_row.resume(f.services);},"Null runtime type was filtered.");
    (*f.list->backing)[0]=row;row->name.reset();
    IgbRuntimeTypeLookupInvocation null_name(text("T"));
    rejects([&]{null_name.resume(f.services);},"Null stored name became an empty C string.");
    row->name=text("T");IgbRuntimeTypeLookupInvocation null_request({});
    rejects([&]{null_request.resume(f.services);},"Null requested name was accepted after reaching strcmp.");
    row->name.reset();
    rejects([&]{null_request.resume(f.services);},"A faulted invocation restarted from modified operands.");
    rejects([&]{static_cast<void>(null_request.result());},"Faulted lookup published a result.");
}
void captured_globals_and_live_list() {
    auto old_row=type("Old"),new_row=type("New");Fixture f{old_row};
    IgbRuntimeTypeLookupInvocation missing(text("Old"));
    rejects([&]{static_cast<void>(missing.result());},"Incomplete lookup published a result.");
    check(!missing.resume().completed && !missing.faulted(),"Missing global binding failed or fabricated a result.");
    auto services=std::make_shared<IgbRuntimeTypesServices>();
    check(!missing.resume(services).completed,"Disengaged singleton global became a null pointer.");
    services->singleton=f.registry;
    check(!missing.resume(services).completed,"Missing descriptor global did not remain pending.");
    const std::weak_ptr<IgbRuntimeTypeList> retained=f.list;
    auto replacement=std::make_shared<IgbRuntimeTypeList>();
    replacement->count=1;replacement->backing=std::make_shared<IgbRuntimeTypeList::Storage>(1,new_row);
    f.registry->types=replacement;f.registry->enabled=0;services->singleton.reset();
    f.list.reset();
    check(!retained.expired(),"Descriptor pause lost its selected registry when external owners were replaced.");
    services->name_field=f.descriptor;
    check(missing.resume(services).completed && missing.result()==old_row,"Descriptor pause recaptured admitted singleton/list.");
    f.list=retained.lock();

    f.registry->types=f.list;f.registry->enabled=1;f.services->name_field.reset();
    IgbRuntimeTypeLookupInvocation live(text("New"));
    check(!live.resume(f.services).completed,"Live-list test did not reach descriptor boundary.");
    f.list->count=2;f.list->backing=std::make_shared<IgbRuntimeTypeList::Storage>(IgbRuntimeTypeList::Storage{old_row,new_row});
    f.services->name_field=f.descriptor;
    check(live.resume(f.services).completed && live.result()==new_row,"Pending lookup captured count/backing before their original reads.");

    f.services->name_field.reset();IgbRuntimeTypeLookupInvocation zero({});
    check(!zero.resume(f.services).completed,"Missing descriptor read was skipped before reading logical count.");
    f.list->count=0;f.list->backing.reset();f.services->name_field=std::shared_ptr<IgbRuntimeNameField>{};
    check(zero.resume(f.services).completed && !zero.result(),"Live count zero still dereferenced a null descriptor.");
}
void offsets_and_bounds() {
    auto row=type("Name");Fixture f{row};
    for(std::uint32_t offset:{0u,0x10u,0x18u,0x20u,0xfffffffcu}) {
        f.descriptor->offset=offset;IgbRuntimeTypeLookupInvocation call(text("Name"));
        rejects([&]{call.resume(f.services);},"Unknown name-field offset was silently replaced with +1C.");
    }
    f.descriptor->offset=0x1c;f.list->count=2;
    IgbRuntimeTypeLookupInvocation early(text("Name"));
    check(early.resume(f.services).completed && early.result()==row,"Match validated an unreached later physical slot.");
    IgbRuntimeTypeLookupInvocation short_list(text("Absent"));
    rejects([&]{short_list.resume(f.services);},"Logical count hid a missing physical row.");
    f.list->count=1;row->name=text(std::string(4095,'a'));
    IgbRuntimeTypeLookupInvocation limit(text(std::string(4095,'a')));
    check(limit.resume(f.services).completed && limit.result()==row,"Maximum represented terminated name was rejected.");
    row->name=text(std::string(5000,'a'));
    IgbRuntimeTypeLookupInvocation mismatch(text("b"));
    check(mismatch.resume(f.services).completed && !mismatch.result(),"Long unread suffix was validated after an early mismatch.");
    IgbRuntimeTypeLookupInvocation long_name(text(std::string(5000,'a')));
    rejects([&]{long_name.resume(f.services);},"Unbounded equal C strings escaped the native profile.");
    row->name=text("A");f.list->count=262145;
    f.list->backing=std::make_shared<IgbRuntimeTypeList::Storage>(262145,row);
    IgbRuntimeTypeLookupInvocation too_many(text("B"));
    rejects([&]{too_many.resume(f.services);},"Registry visit bound was not enforced at the reached slot.");
}
void slot_counts() {
    auto metadata=type("Meta");
    rejects([&]{static_cast<void>(igb_runtime_slot_count({}));},"Slot-count helper accepted null metadata.");
    rejects([&]{static_cast<void>(igb_runtime_slot_count(metadata));},"Slot-count helper replaced a null field registry with zero.");
    metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();metadata->reference_word=0xff800003;
    for(const auto word:{0u,1u,2u,0x7fffffffu,0x80000000u,0xffffffffu}) {
        metadata->fields->count=std::bit_cast<std::int32_t>(word);
        check(std::bit_cast<std::uint32_t>(igb_runtime_slot_count(metadata))==word,"Slot-count helper constrained or normalized the raw word.");
    }
    check(!metadata->fields->backing && metadata->reference_word==0xff800003,"Slot count created backing or edited the owner reference.");
    const auto prior=metadata->fields;metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();
    metadata->fields->count=73;prior->count=12;
    check(igb_runtime_slot_count(metadata)==73,"Slot-count helper cached the old field owner.");
}
void local_fields() {
    auto metadata=type("RuntimeFields");metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();
    for(const auto count:{0,-1,std::numeric_limits<std::int32_t>::min()}) {
        metadata->fields->count=count;
        check(igb_runtime_local_field_count(metadata)==0,"Nonpositive field count read missing backing.");
    }
    metadata->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    for(unsigned byte=0;byte<256;++byte) {
        auto field=std::make_shared<IgbDecodeRuntimeField>();field->flag_25=static_cast<std::uint8_t>(byte);
        field->flag_26=0;field->reference_word=0xff800001;metadata->fields->backing->push_back(field);
    }
    metadata->fields->count=256;
    check(igb_runtime_local_field_count(metadata)==1,"Local-field helper used one-bit truth or flag+26.");
    (*metadata->fields->backing)[3]->flag_25=0;(*metadata->fields->backing)[3]->flag_26=255;
    check(igb_runtime_local_field_count(metadata)==2,"Local-field helper ignored a live flag+25 change.");
    check((*metadata->fields->backing)[3]->reference_word==0xff800001,"Local-field scan edited original reference words.");
    metadata->fields->count=-1;
    check(igb_runtime_field_at(metadata,255)==(*metadata->fields->backing)[255],"Physical field getter incorrectly validated logical count.");
    (*metadata->fields->backing)[255].reset();
    check(!igb_runtime_field_at(metadata,255),"Field getter filtered or dereferenced its null result.");
    metadata->fields->count=256;
    rejects([&]{static_cast<void>(igb_runtime_local_field_count(metadata));},"Local-field caller ignored null getter result.");
    metadata->fields->count=257;
    (*metadata->fields->backing)[255]=std::make_shared<IgbDecodeRuntimeField>();
    rejects([&]{static_cast<void>(igb_runtime_local_field_count(metadata));},"Local-field caller ignored short physical backing.");
    rejects([&]{static_cast<void>(igb_runtime_field_at(metadata,-1));},"Negative physical field index was accepted.");
    rejects([&]{static_cast<void>(igb_runtime_field_at(metadata,262144));},"Field physical bound was not enforced.");
    metadata->fields.reset();
    rejects([&]{static_cast<void>(igb_runtime_local_field_count(metadata));},"Local-field helper fabricated a missing field registry.");
    rejects([&]{static_cast<void>(igb_runtime_local_field_count({}));},"Local-field helper accepted null metadata.");
}
void lazy_name_sources() {
    auto row=type("Target");Fixture f{row};
    auto buffer=std::make_shared<IgbScratchBuffer>();
    f.registry->enabled=0;
    IgbRuntimeTypeLookupInvocation disabled(buffer,0xffffffffu);
    check(disabled.resume(f.services).completed && !disabled.result(),"Disabled registry eagerly consumed an unavailable scratch name.");
    f.registry->enabled=1;f.list->count=0;
    IgbRuntimeTypeLookupInvocation empty(buffer,0xffffffffu);
    check(empty.resume(f.services).completed && !empty.result(),"Empty registry eagerly consumed an unavailable scratch name.");
    f.list->count=1;buffer->bytes={'X'};buffer->initialized={1};
    IgbRuntimeTypeLookupInvocation mismatch(buffer,0);
    check(mismatch.resume(f.services).completed && !mismatch.result(),"Early name mismatch read an unavailable suffix or terminator.");
    f.services->name_field.reset();
    IgbRuntimeTypeLookupInvocation retained(buffer,0);
    check(!retained.resume(f.services).completed,"Lazy name lookup did not reach its descriptor pause.");
    buffer->bytes={'T','a','r','g','e','t',0};buffer->initialized.assign(7,1);
    f.services->name_field=f.descriptor;
    check(retained.resume(f.services).completed && retained.result()==row,"Pending lookup copied scratch bytes before their reached comparison.");
    auto copy=igb_runtime_read_name({buffer,0});
    check(copy && *copy=="Target","Explicit name reader changed the C string.");
    buffer->initialized[2]=0;IgbRuntimeTypeLookupInvocation incomplete(buffer,0);
    bool rejected{};try {incomplete.resume(f.services);}catch(const IgbStreamError&) {rejected=true;}
    check(rejected && incomplete.faulted(),"Reached unknown scratch bytes did not fault the lookup.");
}
}
int main() {
    try {
        first_match_and_strings();gate_and_reached_nulls();captured_globals_and_live_list();offsets_and_bounds();slot_counts();local_fields();lazy_name_sources();
        std::cout<<"IGB runtime type checks passed: "<<checks<<'\n';return 0;
    }catch(const std::exception& error) {
        std::cerr<<"IGB runtime type checks failed after "<<checks<<": "<<error.what()<<'\n';return 1;
    }
}
