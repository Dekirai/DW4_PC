#include "database_fixture.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/assets.hpp"
#include "dw4/database_runtime.hpp"

#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using database_fixture::Fixture;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid DATABASE operation was accepted.");
}
IgbObject& object(IgbDocument& doc,std::uint32_t ref) {return doc.objects.at(doc.references.at(ref).value_index);}
void extra_info(IgbDocument& doc,std::uint32_t ref,std::uint32_t prototype) {
    auto& added=object(doc,ref);const auto& original=doc.object(prototype);
    added.type_index=original.type_index;added.fields.push_back(*doc.field(original,6));
}
DatabaseManagerProgram program(const IgbDocument& doc,std::uint32_t occurrence=7) {
    return decode_database_managers(doc,"synthetic/database.igb",occurrence);
}
MessengerDispatch send(ActionEventRuntime& events,std::string command="GETDATABASE",
    std::optional<std::string> name="synthetic_db",std::int32_t index=-1) {
    auto& messenger=events.messenger();messenger.select("DATABASE",std::move(name),index);
    return messenger.begin_send(std::move(command));
}
void complete(ActionEventRuntime& events,MessengerDispatch& dispatch) {
    check(events.resume(dispatch).stop==MessengerStop::completed,"DATABASE send did not complete.");
}
MessengerDatabase returned(ActionEventRuntime& events,std::size_t ordinal=0) {
    const auto& objects=events.messenger().buffer().objects();
    check(ordinal<objects.size(),"DATABASE omitted a retained object.");
    const auto* database=std::get_if<MessengerDatabase>(&objects[ordinal]);
    check(database && *database,"DATABASE returned an untyped object or null.");return *database;
}

void decoding_and_aliases() {
    Fixture f;f.chars({"SKILL"},{-5});
    f.add_ram(-9,0);f.add_ram(123,255);
    f.contents(f.ram_list,{f.rams[0],f.rams[1],f.rams[2],f.rams[0]});
    dw4::test::word(f.records[f.ram_storage[0]].data,8,77);
    const auto other=f.add(dw4::test::scene_info);f.text(other,2,"other");f.scalar(other,4,0);f.scalar(other,7,f.database);
    f.contents(f.exported_info_list,{f.info,other,f.info});
    auto doc=f.document();extra_info(doc,other,f.info);
    doc.references[f.rams[2]].value_index=doc.references[f.rams[0]].value_index;
    const auto p=program(doc);
    check(p.source_occurrence()==7 && p.infos().size()==3 && p.infos()[1].reference==other,
        "DATABASE lost ordered repeated info exports or source ownership.");
    check(p.infos()[0].rams.size()==4 && p.infos()[0].rams[2].reference==f.rams[2] &&
        p.infos()[0].rams[2].owner_reference==f.rams[0],"Raw RAM aliases lost physical identity.");
    check(p.infos()[0].rams[0].authored_words[0]==77 && p.infos()[0].rams[1].authored_index==-9,
        "Decoder lost authored signed RAM words.");
    check(p.infos()[0].database==p.infos()[1].database && p.infos()[0].database==p.infos()[2].database,
        "Repeated database pointers were independently reconstructed.");
    const auto copy=p;check(&copy.infos()==&p.infos(),"Program copy lost immutable shared ownership.");
    ActionEventRuntime events;DatabaseRuntime runtime(events);runtime.register_source(p);
    const auto& alias=runtime.ram({7,f.rams[2]});
    check(&alias==&runtime.ram({7,f.rams[0]}) && alias.index==3 && alias.info==ControllerObjectId{7,f.info} &&
        alias.manager==runtime.manager_id() && alias.words==std::array<std::int32_t,4>{},
        "Generic initialization failed final ordinal/parent/reset semantics.");
    check(runtime.manager_id().is_native() && !alias.id.is_native() && runtime.sources().size()==1,
        "Native manager and actual serialized RAM identities were conflated.");
    check(events.messenger().registration_count()==12,"Ordered info/RAM alias registrations were deduplicated.");
    auto all=send(events,"GETDATABASE",std::nullopt);complete(events,all);
    check(events.messenger().buffer().objects().size()==12,"Authored inactive RAM was incorrectly filtered without the enabled gate.");
    for(std::size_t i=0;i<12;++i)check(returned(events,i)==p.infos()[0].database,"GETDATABASE copied its immutable owner.");
    auto selected=send(events,"GETDATABASE","other",2);complete(events,selected);
    check(events.messenger().buffer().objects().size()==1 && returned(events)==alias.database,
        "Registration-time alias ordinal was overwritten with final RAM ordinal.");
    runtime.set_active({7,f.rams[2]},128);
    auto active=send(events,"GETDATABASE",std::nullopt);events.messenger().buffer().set_enabled_only(1);complete(events,active);
    check(events.messenger().buffer().objects().size()==9 && runtime.ram({7,f.rams[0]}).active==128,
        "Raw active-byte aliases or enabled-only filtering changed.");
    auto raw_gate=send(events,"GETDATABASE",std::nullopt);events.messenger().buffer().set_enabled_only(255);complete(events,raw_gate);
    check(events.messenger().buffer().objects().size()==12,"A non-one enabled gate incorrectly filtered authored RAMs.");
    rejects([&]{runtime.register_source(p);});
    check(events.messenger().registration_count()==12 && runtime.sources().size()==1,
        "Duplicate source failure partially registered DATABASE owners.");
    rejects([&]{runtime.ram({8,f.rams[0]});});
    auto detached=[] {Fixture local;return program(local.document(),8);}();
    runtime.register_source(detached);
    check(runtime.sources().size()==2 && runtime.ram({8,f.rams[0]}).id.file_index==8,
        "Repeated source paths were deduplicated across actual loaded occurrences.");
}

void nulls_types_and_final_parent() {
    Fixture f;f.records[f.info].values[2]=dw4::test::words({0});f.scalar(f.info,4,255);
    f.records[f.ram_storage[0]].data[4]=254;
    const auto other=f.add(dw4::test::scene_info);f.text(other,2,"");
    f.contents(f.exported_info_list,{f.info,other});
    auto doc=f.document();extra_info(doc,other,f.info);
    const auto p=program(doc);
    check(!p.infos()[0].name && p.infos()[1].name=="" && p.infos()[0].active==255 && p.infos()[0].rams[0].active==254,
        "DATABASE collapsed null/empty names or raw active bytes.");
    ActionEventRuntime events;DatabaseRuntime runtime(events);runtime.register_source(p);
    check(runtime.ram({7,f.rams[0]}).info==ControllerObjectId{7,other} && !runtime.ram({7,f.rams[0]}).database,
        "Shared RAM did not retain its final parent database, including null.");
    auto call=send(events,"GETDATABASE",std::nullopt);complete(events,call);
    check(events.messenger().buffer().objects().size()==2 &&
        std::holds_alternative<std::monostate>(events.messenger().buffer().objects()[0]) &&
        std::holds_alternative<std::monostate>(events.messenger().buffer().objects()[1]),
        "Authored null DB did not append actual null object occurrences.");
    auto empty=send(events,"GETDATABASE","");complete(events,empty);
    check(events.messenger().buffer().objects().size()==2,
        "Null info name did not register under the original empty-string name.");
    auto inherited=doc;auto derived=inherited.metadata.object_types.at(doc.object(f.info).type_index);
    derived.parent_index=static_cast<std::int32_t>(doc.object(f.info).type_index);derived.name="DerivedDbInfo";
    object(inherited,f.info).type_index=static_cast<std::uint32_t>(inherited.metadata.object_types.size());
    inherited.metadata.object_types.push_back(std::move(derived));
    check(program(inherited).infos().size()==1,"Generic exact DB type acceptance changed to inheritance matching.");
    auto aliases=doc;aliases.references[other].value_index=aliases.references[f.info].value_index;
    const auto shared=program(aliases);
    check(shared.infos().size()==2 && shared.infos()[1].reference==other && shared.infos()[1].owner_reference==f.info,
        "Info export aliases lost raw occurrence/canonical owner separation.");
}

void delivery_cache_and_lifetime() {
    Fixture f;ActionEventRuntime events;MessengerDatabase retained;
    {
        DatabaseRuntime runtime(events);auto p=program(f.document());retained=p.infos()[0].database;
        runtime.register_source(p);
    }
    auto get=send(events);events.messenger().buffer().add_int(41);events.messenger().buffer().add_object({});
    complete(events,get);
    check(returned(events,1)==retained && events.messenger().buffer().integers()==std::vector<std::int32_t>{41},
        "GETDATABASE cleared shared lists or lost destroyed-facade ownership.");
    complete(events,get);check(events.messenger().buffer().objects().size()==2,"Completed send replayed DB append.");
    for(const auto* command:{"getdatabase","UNKNOWN",""}) {
        auto unknown=send(events,command);complete(events,unknown);
        check(events.messenger().buffer().objects().empty(),"Unknown fallback command fabricated DB output.");
    }
    DatabaseRuntime second(events);second.register_source(program(f.document(),8));
    auto cached=send(events);complete(events,cached);
    check(events.messenger().buffer().objects().size()==1 && returned(events)==retained,
        "Later DB registration invalidated a populated recipient cache.");
    auto fresh=send(events,"GETDATABASE",std::nullopt);complete(events,fresh);
    check(events.messenger().buffer().objects().size()==2 && returned(events,0)==retained && returned(events,1)!=retained,
        "New cache key lost ordered, separately loaded DB owners.");
}

void captured_fallback_and_pause() {
    Fixture f;ActionEventRuntime events;const auto first=ControllerObjectId::allocate_native();
    int entered{};bool ready{};
    events.register_native_controller(first,{[]{return std::optional<std::uint8_t>{std::uint8_t{1}};},[&] {
        return MessengerManager{[&](NativeMessenger& messenger,const MessengerDelivery&) {
            ++entered;
            if(!ready)return MessengerCallResult{false,0,"prepared first recipient"};
            // Same key changes the live cache command, but the original send's
            // fallback text remains GETDATABASE for the later DB recipient.
            auto nested=messenger.begin_send("UNKNOWN");
            return MessengerCallResult{true,1,{}};
        },{}};
    }},"DATABASE","synthetic_db",0);
    DatabaseRuntime runtime(events);const auto p=program(f.document());runtime.register_source(p);
    auto outer=send(events);check(events.resume(outer).stop==MessengerStop::service_required,"Prepared preceding recipient did not suspend.");
    check(entered==1,"First callback invoked more than once before retry.");
    ready=true;complete(events,outer);
    check(entered==2 && events.messenger().selected().command=="UNKNOWN" && returned(events)==p.infos()[0].database,
        "DB used live structured command instead of captured fallback text.");
    complete(events,outer);check(entered==2 && events.messenger().buffer().objects().size()==1,
        "Completed recipient prefix repeated after resuming completed outer send.");
}

void malformed_and_atomic_registration() {
    Fixture f;const auto doc=f.document();
    auto bad=doc;bad.info_list_reference.reset();rejects([&]{program(bad);});
    bad=doc;bad.metadata.header.byte_order=IgbByteOrder::big;rejects([&]{program(bad);});
    rejects([&]{decode_database_managers(doc,"",7);});
    rejects([&]{decode_database_managers(doc,std::string("a\0b",3),7);});
    rejects([&]{program(doc,std::numeric_limits<std::uint32_t>::max());});
    for(const auto [ref,slot]:std::array<std::pair<std::uint32_t,std::uint16_t>,5>{
        std::pair{f.info,std::uint16_t{4}},std::pair{f.info,std::uint16_t{6}},
        std::pair{f.rams[0],std::uint16_t{2}},std::pair{f.rams[0],std::uint16_t{5}},
        std::pair{f.rams[0],std::uint16_t{9}}}) {
        bad=doc;auto* field=const_cast<IgbFieldValue*>(bad.field(object(bad,ref),slot));field->size+=1;
        rejects([&]{program(bad);});
        bad=doc;object(bad,ref).fields.push_back(*bad.field(object(bad,ref),slot));rejects([&]{program(bad);});
    }
    auto invalid=f;invalid.contents(invalid.ram_list,{dw4::test::null_ref});rejects([&]{program(invalid.document());});
    invalid=f;invalid.scalar(invalid.ram_list,3,4097);rejects([&]{program(invalid.document());});
    invalid=f;invalid.records[invalid.info_storage].data=dw4::test::words({dw4::test::null_ref});rejects([&]{program(invalid.document());});
    invalid=f;invalid.scalar(invalid.info,7,invalid.info);rejects([&]{program(invalid.document());});
    invalid=f;invalid.contents(invalid.exported_info_list,{invalid.database});rejects([&]{program(invalid.document());});

    ActionEventRuntime events;DatabaseRuntime runtime(events);const auto p=program(doc);
    const ControllerObjectId id{7,f.rams[0]};
    PreparedControllerRegistration collision{id,{[]{return std::optional<std::uint8_t>{std::uint8_t{1}};},[]{return MessengerManager{};}},p.source()};
    events.register_controllers(std::span(&collision,1),{});
    rejects([&]{runtime.register_source(p);});
    check(runtime.sources().empty() && events.messenger().registration_count()==0,
        "Provider collision partially committed DB sources or category rows.");
    rejects([&]{runtime.ram(id);});
    auto different=decode_database_managers(doc,"other/path.igb",7);
    rejects([&]{runtime.register_source(different);});

    ActionEventRuntime full;DatabaseRuntime waiting(full);
    std::vector<MessengerRegistration> rows(65536,{{8,9},"FULL",{},0});full.messenger().register_recipients(rows);
    rejects([&]{waiting.register_source(p);});
    check(waiting.sources().empty() && full.messenger().registration_count()==65536,
        "Full messenger registration leaked a DB provider or source owner.");
    // Reset only the messenger rows, then prove the rejected provider can be installed.
    full.messenger().reset();waiting.register_source(p);
    auto retry=send(full);complete(full,retry);check(returned(full)==p.infos()[0].database,
        "Failed atomic DB registration prevented a later valid retry.");
}

void original(const std::filesystem::path& root) {
    const auto archive=assets::AfsArchive::open(root/"files"/"area.afs");
    const auto doc=read_igb_document(archive.read(9));
    const auto p=decode_database_managers(doc,"area.afs/9",8);
    check(p.infos().size()==12,"Original USA pack DB info count changed.");
    const DatabaseInfoDefinition* tech{};const DatabaseInfoDefinition* shinka{};
    std::size_t rows{};for(const auto& info:p.infos()) {
        rows+=info.rams.size();if(info.name=="pcTech")tech=&info;if(info.name=="shinka")shinka=&info;
    }
    check(tech && shinka && tech->reference==1500 && tech->database_reference==1592u && shinka->reference==1897 &&
        shinka->database_reference==1879u,"Original pcTech/shinka source-qualified definitions changed.");
    check(tech->rams.size()==1 && tech->rams[0].reference==1516 && tech->rams[0].active==1,
        "Original pcTech RAM/active state changed.");
    ActionEventRuntime events;DatabaseRuntime runtime(events);runtime.register_source(p);
    check(events.messenger().registration_count()==rows,"Original database registration lost actual RAM occurrences.");
    for(const auto& info:p.infos()) for(std::size_t index=0;index<info.rams.size();++index) {
        auto get=send(events,"GETDATABASE",info.name,static_cast<std::int32_t>(index));complete(events,get);
        check(returned(events)==info.database,"Original GETDATABASE failed actual retained info->DB object edge.");
    }
    auto get=send(events,"GETDATABASE","pcTech",0);complete(events,get);
    const auto result=returned(events);
    check(result==tech->database && result->source_occurrence()==8 && result->row_count()==100 &&
        result->integer(0,"SKILL")==6 && result->integer(0,"HP")==-160,
        "Actual database delivery changed typed row lookup or selected object identity.");
    std::cout<<"Original area.afs entry9: "<<p.infos().size()<<" DB infos, "<<rows<<" RAM registration rows.\n";
}
}
int main(int argc,char** argv) {
    try {
        decoding_and_aliases();nulls_types_and_final_parent();delivery_cache_and_lifetime();
        captured_fallback_and_pause();malformed_and_atomic_registration();
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: database_runtime_tests [ORIGINAL_ROOT]");
        std::cout<<checks<<" database runtime checks passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"Database runtime test failure: "<<error.what()<<'\n';return 1;}
}
