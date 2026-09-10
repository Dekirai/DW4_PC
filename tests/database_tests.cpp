#include "dw4/database.hpp"
#include "dw4/resources.hpp"
#include "database_fixture.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace dw4;
using database_fixture::Fixture;
using Bytes=database_fixture::Bytes;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {
    ++checks;try {operation();}catch(const DatabaseError&){return;}
    throw std::runtime_error("Invalid database graph/query was accepted.");
}
Fixture populated() {
    Fixture f;
    f.chars({"SKILL","DUP","DUP",std::nullopt,""},{-128,-4,99,55,11,127,3,98,56,12});
    f.shorts({"SKILL","HP","SHORT"},{300,-32768,-2,301,32767,2});
    f.ints({"SKILL","HP","INT"},{500,600,std::numeric_limits<std::int32_t>::min(),501,601,std::numeric_limits<std::int32_t>::max()});
    f.floats({"FLOAT"},{-0.0f,1.25f});f.bools({"PC","PC"},{255,1,128,0});
    f.strings({"NAME","TEXT"},{"first",std::nullopt,"second",std::string{}});return f;
}
void typed_queries() {
    static_assert(!std::is_default_constructible_v<DatabaseDefinition>);
    auto f=populated();const auto db=f.definition();
    check(db.source()=="synthetic/database.igb" && db.source_occurrence()==7 && db.reference()==f.database && db.owner_reference()==f.database,
        "Database lost explicit source/reference provenance.");
    check(db.row_count()==2 && db.char_group().columns.size()==5 && db.short_group().columns.size()==3 && db.int_group().columns.size()==3 &&
        db.float_group().columns.size()==1 && db.bool_group().columns.size()==2 && db.string_group().columns.size()==2,
        "Database did not decode every typed group.");
    check(db.integer(0,"SKILL")==-128 && db.integer(1,"SKILL")==127 && db.integer(0,"HP")==-32768 && db.integer(1,"HP")==32767,
        "Integer query lost char/short sign extension or type precedence.");
    check(db.integer(0,"INT")==std::numeric_limits<std::int32_t>::min() && db.integer(1,"INT")==std::numeric_limits<std::int32_t>::max(),
        "Integer query lost signed 32-bit data.");
    check(db.integer(0,"DUP")==-4 && db.integer(1,"DUP")==3 && db.integer(0,"")==11 && db.integer(1,"")==12,
        "Integer lookup lost first exact-name or empty-name behavior.");
    check(db.integer(0,"skill")==0 && db.integer(-1,"missing")==0 && db.integer(std::numeric_limits<std::int32_t>::max(),std::nullopt)==0 &&
        db.integer(0,"FLOAT")==0,"Missing integer columns or null queries acquired an invented fallback.");
    check(db.boolean(0,"PC")==255 && db.boolean(1,"PC")==128,"Bool lookup normalized a raw byte or selected a later duplicate.");
    check(db.string(0,"NAME")=="first" && db.string(1,"NAME")=="second" && db.string(0,"TEXT").empty() && db.string(1,"TEXT").empty(),
        "String query changed row-major addressing or nullable-cell behavior.");
    check(!db.string_group().values[1].name && db.string_group().values[3].name==std::optional<std::string>{std::string{}} &&
        !db.char_group().columns[3].name && db.char_group().columns[4].name==std::optional<std::string>{std::string{}},
        "Decoded null names/cells were normalized to non-null empty strings.");
    check(std::bit_cast<std::uint32_t>(db.float_group().values[0])==0x80000000u && db.float_group().values[1]==1.25f,
        "Float group lost its authored bits or was discarded because no float query exists.");
    const auto copy=db;const auto saved=db.string(0,"NAME");
    check(&copy.char_group()==&db.char_group() && copy.string(0,"NAME").data()==saved.data(),"Immutable database copy detached decoded ownership.");
    for(const auto row:{-1,2,std::numeric_limits<std::int32_t>::max()}) {
        rejects([&]{db.integer(row,"SKILL");});rejects([&]{db.boolean(row,"PC");});rejects([&]{db.string(row,"NAME");});
    }
    for(const auto name:{std::optional<std::string_view>{"missing"},std::optional<std::string_view>{std::nullopt}}) {
        rejects([&]{db.boolean(0,name);});rejects([&]{db.string(0,name);});
    }
    const std::string bad("SKILL\0extra",11);rejects([&]{db.integer(0,bad);});rejects([&]{db.boolean(0,bad);});rejects([&]{db.string(0,bad);});
    rejects([&]{db.integer(0,std::string(4097,'x'));});
}
void row_count_priority() {
    Fixture f;check(f.definition().row_count()==0,"All empty groups invented rows.");
    f.bools({"B"},{0,1,255,0,1,255});check(f.definition().row_count()==6,"Bool-only row count is incorrect.");
    f.floats({"F"},{1,2,3,4,5});check(f.definition().row_count()==5,"Float group did not precede bool for row count.");
    f.ints({"I"},{1,2,3,4});check(f.definition().row_count()==4,"Int group did not precede float for row count.");
    f.shorts({"S"},{1,2,3});check(f.definition().row_count()==3,"Short group did not precede int for row count.");
    f.chars({"C"},{1,2});check(f.definition().row_count()==2,"Char group did not precede short for row count.");
    f.strings({"N"},{"only"});const auto db=f.definition();
    check(db.row_count()==1 && db.integer(3,"I")==4 && db.boolean(5,"B")==255,
        "Row count priority incorrectly imposed one global row count on all typed queries.");
    f.strings({"N"},{});check(f.definition().row_count()==0,"A nonempty column group with zero rows did not determine row count.");
}
void canonical_aliases_and_owned_data() {
    Fixture f;const auto name=f.named("DUP");f.columns(0,std::vector<std::uint32_t>{name,name});f.data(0,{9,88,7,66},4);
    f.strings({"NAME"},{"shared","unused"});
    const auto original_memory=f.records.at(f.groups[0].data).values.at(4);
    std::uint32_t memory{};for(std::size_t i=0;i<4;++i)memory|=std::uint32_t(original_memory[i])<<(i*8);
    const auto memory_alias=f.memory(f.records.at(memory).data);f.scalar(f.groups[0].data,4,memory_alias);
    const auto columns_alias=f.list(dw4::test::named_list,{name,name});
    dw4::test::word(f.records.at(f.database_storage).data,0,columns_alias);
    auto doc=f.document();auto alias=[&](std::uint32_t reference) {
        const auto next=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references.at(reference));return next;
    };
    const auto database_alias=alias(f.database);
    doc.references.at(columns_alias)=doc.references.at(f.groups[0].columns);
    doc.references.at(memory_alias)=doc.references.at(memory);
    const auto string_cells=doc.object_list(f.groups[5].data);
    doc.references.at(*string_cells[1])=doc.references.at(*string_cells[0]);
    const auto db=decode_database(doc,database_alias,"alias.igb",3);
    check(db.reference()==database_alias && db.owner_reference()==f.database && db.char_group().columns_source.reference==columns_alias &&
        db.char_group().columns_source.owner_reference==f.groups[0].columns && db.char_group().data_source.memory_reference==memory_alias &&
        db.char_group().data_source.memory_owner_reference==memory,"Canonical database/list/memory provenance was not retained.");
    check(db.char_group().columns[0].owner_reference==name && db.char_group().columns[1].owner_reference==name &&
        db.integer(0,"DUP")==9 && db.integer(1,"DUP")==7,"Repeated physical column owner changed first-pointer lookup.");
    check(db.string_group().values[1].reference==*string_cells[1] && db.string_group().values[1].owner_reference==*string_cells[0] &&
        db.string(1,"NAME")=="shared","String-cell aliases lost raw/physical identity or payload sharing.");
    // A distinct reference alias must remain distinguishable in ordered cells.
    Fixture g;const auto a=g.named("COL"),b=g.named("different");g.columns(0,std::vector<std::uint32_t>{a,b});g.data(0,{5,100},2);
    auto graph=g.document();graph.references.at(b)=graph.references.at(a);
    const auto aliased=decode_database(graph,g.database,"physical.igb",5);
    check(aliased.char_group().columns[1].reference==b && aliased.char_group().columns[1].owner_reference==a && aliased.integer(0,"COL")==5,
        "Distinct serialized aliases were collapsed or treated as distinct physical column names.");
    const auto retained=populated().definition();check(retained.string(1,"NAME")=="second" && retained.integer(0,"HP")==-32768,
        "Definition retained borrowed IGB storage after document destruction.");
    auto mutated=populated();auto original=mutated.definition();mutated.chars({"SKILL"},{33});
    check(original.integer(0,"SKILL")==-128 && mutated.definition().integer(0,"SKILL")==33,"Decoded immutable values changed with a later fixture document.");
}
void malformed_graphs() {
    const auto f=populated();auto decode=[&](const IgbDocument& doc){return decode_database(doc,f.database,"bad.igb",2);};
    for(const auto source:{std::string{},std::string("x\0y",3),std::string(4097,'x')})rejects([&]{f.definition(source);});
    rejects([&]{f.definition("bad.igb",std::numeric_limits<std::size_t>::max());});
    auto doc=f.document();doc.metadata.header.byte_order=IgbByteOrder::big;rejects([&]{decode(doc);});
    for(const auto ref:{f.database,f.groups[0].columns,f.groups[0].data}) {
        auto graph=f.document();auto& object=graph.objects.at(graph.references.at(ref).value_index);
        object.type_index=dw4::test::object;rejects([&]{decode(graph);});
        graph=f.document();graph.objects.at(graph.references.at(ref).value_index).reference_index=f.info;
        rejects([&]{decode(graph);});
    }
    auto inconsistent=f.document();const auto memory=inconsistent.field_u32(inconsistent.object(f.groups[0].data),4);
    inconsistent.memories.at(inconsistent.references.at(memory).value_index).reference_index=f.database;
    rejects([&]{decode(inconsistent);});
    std::vector<std::uint32_t> targets{f.database};for(const auto& group:f.groups){targets.push_back(group.columns);targets.push_back(group.data);}
    for(const auto ref:targets)for(unsigned mutation=0;mutation<4;++mutation) {
        auto graph=f.document();auto& fields=graph.objects.at(graph.references.at(ref).value_index).fields;
        if(mutation==0)fields.back().size+=1;
        if(mutation==1)fields.back().type_index=0;
        if(mutation==2)fields.back().offset=graph.bytes().size();
        if(mutation==3)fields.push_back(fields.back());
        rejects([&]{decode(graph);});
    }
    for(std::size_t group=0;group<6;++group) {
        auto bad=f;dw4::test::word(bad.records[bad.database_storage].data,group*8,database_fixture::null_ref);rejects([&]{bad.definition();});
        bad=f;dw4::test::word(bad.records[bad.database_storage].data,group*8+4,database_fixture::null_ref);rejects([&]{bad.definition();});
        bad=f;bad.scalar(bad.groups[group].data,3,0);rejects([&]{bad.definition();});
        bad=f;bad.scalar(bad.groups[group].data,3,1048577);rejects([&]{bad.definition();});
        bad=f;bad.scalar(bad.groups[group].data,4,database_fixture::null_ref);rejects([&]{bad.definition();});
        bad=f;bad.scalar(bad.groups[group].columns,3,4097);rejects([&]{bad.definition();});
        bad=f;bad.columns(group,std::vector<std::uint32_t>{database_fixture::null_ref});rejects([&]{bad.definition();});
    }
    auto bad=f;bad.chars({"A","B"},{1,2,3});rejects([&]{bad.definition();});
    bad=f;bad.columns(0,std::vector<std::uint32_t>{});rejects([&]{bad.definition();});
    bad=f;bad.floats({"F"},{std::numeric_limits<float>::quiet_NaN()});rejects([&]{bad.definition();});
    bad=f;bad.chars({std::string("a\0b",3)},{1});rejects([&]{bad.definition();});
    bad=f;bad.strings({"N"},{std::string(4097,'x')});rejects([&]{bad.definition();});
    bad=f;bad.contents(bad.groups[5].data,{database_fixture::null_ref,database_fixture::null_ref});rejects([&]{bad.definition();});
    bad=f;const auto unnamed=bad.add(dw4::test::object);bad.columns(0,std::vector<std::uint32_t>{unnamed});rejects([&]{bad.definition();});
    // Counts may fit each list separately but exceed aggregate decoded storage.
    Fixture huge;huge.chars({"C"},std::vector<std::int8_t>(1048576));huge.bools({"B"},std::vector<std::uint8_t>(1048576));
    rejects([&]{huge.definition();});
    Fixture names;const auto repeated=names.named(std::string(4096,'x'));names.columns(0,std::vector<std::uint32_t>(1024,repeated));
    names.data(0,Bytes(1024),1024);rejects([&]{names.definition();});
}
void original(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);const std::string source="dng101b1/separate/usa_pack.igb";
    const auto doc=read_igb_document(mounted.read(source));
    const auto pc=decode_database(doc,1592,source,8),shinka=decode_database(doc,1879,source,8);
    check(pc.row_count()==100 && pc.reference()==1592 && pc.char_group().columns_source.reference==1575 &&
        pc.char_group().data_source.reference==1567 && pc.char_group().data_source.memory_reference==1534u &&
        pc.short_group().columns_source.reference==1576 && pc.short_group().data_source.memory_reference==1535u,
        "Original pcTech graph identity or row count differs.");
    check(pc.char_group().columns.size()==6 && pc.short_group().columns.size()==11 && pc.int_group().columns.empty() &&
        pc.float_group().columns.empty() && pc.bool_group().columns.size()==2 && pc.string_group().columns.size()==4,
        "Original pcTech typed column schema differs.");
    check(!pc.int_group().data_source.memory_reference && !pc.float_group().data_source.memory_reference &&
        pc.int_group().values.empty() && pc.float_group().values.empty(),"Original authored empty numeric groups were replaced or discarded.");
    check(pc.integer(0,"SKILL")==6 && pc.integer(0,"HP")==-160 && pc.integer(0,"MP")==16 && pc.string(0,"NAME")=="Petty Fire" &&
        pc.integer(40,"SKILL")==7 && pc.integer(40,"HP")==300 && pc.integer(65,"TIME")==1800,
        "Original pcTech queries lost actual signed values or names.");
    check(shinka.row_count()==16 && shinka.string(0,"NAME")=="Agumon" && shinka.string(1,"NAME")=="Veemon" &&
        shinka.integer(0,"MHP_INI")==180 && shinka.integer(0,"INI_WEAPON1")==266,"Original shinka graph or integer widths differ.");
    const std::array<std::int32_t,6> bases{0,80,40,30,35,75},skills{6,8,7,6,6,8},levels{1,12,22,12,23,23};
    for(std::size_t i=0;i<6;++i) {
        const auto column="TECH"+std::to_string(i+1);
        check(shinka.integer(0,column)==bases[i] && shinka.integer(0,column+"_LEVEL")==levels[i] &&
            pc.integer(bases[i],"SKILL")==skills[i],"Actual Agumon technique/database mapping differs.");
    }
    const std::array<std::int32_t,5> thresholds{1,931,1271,1671,2171};
    for(std::size_t i=0;i<5;++i)check(shinka.integer(0,"TECH3_SKILL"+std::to_string(i+1))==thresholds[i],"Original Agumon technique thresholds differ.");
    for(std::int32_t row=0;row<100;++row) {
        check(!pc.string(row,"NAME").empty() && pc.integer(row,"SKILL")>=6 && pc.integer(row,"SKILL")<=8,
            "Original pcTech row query has an invalid authored name/skill index.");
    }
    rejects([&]{pc.integer(100,"SKILL");});rejects([&]{shinka.string(16,"NAME");});
    std::cout<<"Original databases: pcTech=1592 rows=100 shinka=1879 rows=16 Agumon_tech_groups=6\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        typed_queries();row_count_priority();canonical_aliases_and_owned_data();malformed_graphs();
        if(args.size()==2)original(args[1]);else if(args.size()!=1)throw std::runtime_error("Usage: dw4_database_tests [EXTRACTED_ROOT]");
        std::cout<<"database: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"database after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);
}
