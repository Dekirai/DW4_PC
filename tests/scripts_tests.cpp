#include "dw4/scripts.hpp"
#include "dw4/assets.hpp"
#include "script_fixtures.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4;
using namespace dw4::test;
int checks{};
void check(bool condition, const std::string& reason) {
    ++checks; if (!condition) throw std::runtime_error(reason);
}
template<class F> void throws(F&& operation, const std::string& reason) {
    try { operation(); } catch (const ScriptError&) { check(true,reason); return; }
    check(false,reason);
}
template<class F> void throws_igb(F&& operation, const std::string& reason) {
    try { operation(); } catch (const IgbError&) { check(true,reason); return; }
    check(false,reason);
}
StartupPlan plan(const IgbDocument& document) {
    const std::array sources{StartupSource{&document,"synthetic source"}};
    return build_igb_startup_plan(sources);
}
struct Fixture : ScriptFixture {
    Info primary, hidden;
    Fixture() {
        primary = add_info({{"boot.lua",{2,0,3}}, {"first.lua",{4}},
            {"func.lua",{1,0}}, {"start.lua",{3,0,1,2}}},"primary");
        hidden = add_info({{"hidden.lua",{9}}},"unexported",false);
    }
};
void synthetic() {
    Fixture f; auto doc = f.document();
    const Bytes original(doc.bytes().begin(),doc.bytes().end());
    const auto rows = list_igb_scripts(doc);
    check(rows.size() == 4,"Only explicit Lua-info exports are expanded");
    check(rows[0].name == "boot.lua" && rows[1].name == "first.lua" && rows[2].name == "func.lua" &&
        rows[3].name == "start.lua","Preserve data-list order rather than sorting startup names");
    const auto& first = rows[0];
    check(first.info_reference == f.primary.reference && first.info_export_index == 0 && first.data_index == 0 &&
        first.data_reference == f.primary.data_references[0] && first.data_object_reference == f.primary.data_objects[0] &&
        first.memory_reference == f.primary.memories[0] && first.info_name == "primary" && first.byte_size == 3,
        "Retain exact export, list, object, memory and label provenance");
    check(std::all_of(rows.begin(),rows.end(),[](const auto& row) {
        return row.supported && row.name_valid && row.diagnostic.empty() && !row.provenance.empty();
    }),"Opaque non-Lua bytes are valid storage without implying VM support");
    auto script = read_igb_script(doc,f.primary.reference,0);
    check(script.bytes == Bytes({2,0,3}),"Read exact owned bytes including NUL payload bytes");
    script.bytes[0] = 99;
    check(read_igb_script(doc,f.primary.reference,0).bytes == Bytes({2,0,3}),"Returned bytes do not alias document storage");
    check(std::equal(original.begin(),original.end(),doc.bytes().begin(),doc.bytes().end()),"Listing and reading preserve source bytes");
    throws([&]{read_igb_script(doc,f.hidden.reference,0);},"Reject a hidden info even when its script graph is valid");
    throws([&]{read_igb_script(doc,f.primary.reference,4);},"Reject out-of-range data-list index");
    const auto startup = plan(doc);
    check(startup.embedded_lookup_complete && startup.phases.size() == 4,"Resolve all four embedded startup phases");
    const std::array expected{"func.lua","boot.lua","start.lua","first.lua"};
    const std::array<std::uint32_t,4> indices{2,0,3,1};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        check(startup.phases[i].filename == expected[i] && startup.phases[i].lookup_complete &&
            startup.phases[i].calls.size() == 1 && startup.phases[i].calls[0].script.data_index == indices[i],
            "Resolve startup order independently of storage order");
    }
    check(startup.phases[0].native_prerequisites.size() == 3 && startup.phases[1].native_prerequisites.empty() &&
        startup.phases[2].native_prerequisites.size() == 4 && startup.phases[3].native_prerequisites.size() == 1,
        "Native barriers remain separate from successful embedded lookup");
    check(startup.phases[2].native_prerequisites[0].kind == StartupBarrierKind::common_resources_loaded &&
        startup.phases[3].native_prerequisites[0].kind == StartupBarrierKind::stage_enabled,
        "Preserve original barrier ordering between boot, start and first");

    auto aliases = f;
    aliases.scalar(f.primary.data_references[1],3,f.primary.data_objects[0]);
    auto aliased = aliases.document();
    check(list_igb_scripts(aliased)[1].name == "first.lua" &&
        read_igb_script(aliased,f.primary.reference,1).bytes == Bytes({2,0,3}),
        "Different script names may legitimately share one payload object");
    auto repeated = f;
    repeated.contents(repeated.exported_info_list,{f.primary.reference,f.primary.reference});
    doc = repeated.document(); const auto repeat_plan = plan(doc);
    check(list_igb_scripts(doc).size() == 8 && repeat_plan.phases[0].calls.size() == 2 &&
        repeat_plan.phases[0].calls[0].script.info_export_index == 0 &&
        repeat_plan.phases[0].calls[1].script.info_export_index == 1,
        "Repeated exports remain distinct ordered registry occurrences");

    ScriptFixture multiple;
    const auto a = multiple.add_info({{"func.lua",{10}},{"func.lua",{11}},{"boot.lua",{12}}},"A");
    const auto b = multiple.add_info({{"func.lua",{20}},{"first.lua",{21}},{"start.lua",{22}}},"B");
    auto multi_doc = multiple.document(); auto multi_plan = plan(multi_doc);
    check(multi_plan.embedded_lookup_complete && multi_plan.phases[0].calls.size() == 2 &&
        multi_plan.phases[0].calls[0].script.data_reference == a.data_references[0] &&
        multi_plan.phases[0].calls[1].script.data_reference == b.data_references[0],
        "Choose first same-name row within each info and continue to the next info");
    const std::array ordered_sources{StartupSource{&multi_doc,"registered first"},StartupSource{&aliased,"registered second"}};
    const auto combined = build_igb_startup_plan(ordered_sources);
    check(combined.phases[0].calls.size() == 3 && combined.phases[0].calls[0].source_index == 0 &&
        combined.phases[0].calls[1].source_index == 0 && combined.phases[0].calls[2].source_index == 1 &&
        combined.source_labels[1] == "registered second","Preserve explicit source registration order and labels");
    multiple.scalar(a.data_references[0],3,null_ref);
    multi_plan = plan(multiple.document());
    check(!multi_plan.phases[0].lookup_complete && multi_plan.phases[0].calls.size() == 2 &&
        !multi_plan.phases[0].calls[0].script.supported && multi_plan.phases[0].calls[0].script.data_index == 0,
        "Unsupported first match shadows a later same-name valid row");
    auto ambiguous = f;
    ambiguous.contents(f.primary.list_reference,{null_ref,f.primary.data_references[2]});
    const auto unknown = plan(ambiguous.document());
    check(!unknown.phases[0].lookup_complete && unknown.phases[0].calls.empty(),
        "Unknown earlier name prevents claiming a later row is the first match");
    ambiguous.contents(f.primary.list_reference,{f.primary.data_references[2],null_ref});
    const auto later_unknown = plan(ambiguous.document());
    check(later_unknown.phases[0].lookup_complete && later_unknown.phases[0].calls.size() == 1,
        "Unknown names after an established first match do not alter that selection");
    auto missing = f; missing.text(f.primary.data_references[2],2,"FUNC.lua");
    const auto missing_plan = plan(missing.document());
    check(!missing_plan.embedded_lookup_complete && missing_plan.phases[0].calls.empty() &&
        !missing_plan.phases[0].diagnostics.empty(),"Name matching is case sensitive and has no filename fallback");
    ScriptFixture empty; auto empty_doc = empty.document();
    check(list_igb_scripts(empty_doc).empty() && !plan(empty_doc).embedded_lookup_complete,
        "Missing info export table is empty storage, not implicit object scanning");
    empty.add_info({},"empty info");
    check(list_igb_scripts(empty.document()).empty(),"Empty exported script list is valid and has no matches");
    ScriptFixture zero; const auto zero_info = zero.add_info({{"zero.lua",{}}});
    check(read_igb_script(zero.document(),zero_info.reference,0).bytes.empty(),
        "An explicitly owned zero-byte buffer is valid storage, not valid bytecode");

    for (int kind = 0; kind < 10; ++kind) {
        auto bad = f;
        if (kind == 0) bad.exported_info_list = null_ref;
        if (kind == 1) bad.contents(bad.exported_info_list,{null_ref});
        if (kind == 2) bad.contents(bad.exported_info_list,{f.primary.data_references[0]});
        if (kind == 3) bad.records[f.primary.reference].type = derived_info;
        if (kind == 4) bad.scalar(f.primary.reference,7,null_ref);
        if (kind == 5) bad.scalar(f.primary.reference,7,bad.exported_info_list);
        if (kind == 6) bad.scalar(f.primary.list_reference,2,5);
        if (kind == 7) bad.scalar(f.primary.list_reference,4,0x80000000u);
        if (kind == 8) bad.scalar(bad.exported_info_list,3,4097);
        if (kind == 9) bad.scalar(f.primary.list_reference,3,65537);
        if (kind == 7) {
            throws_igb([&]{bad.document();},"IGB parser rejects external memory reference before script ownership lookup");
            continue;
        }
        throws([&]{read_igb_script(bad.document(),f.primary.reference,0);},"Reject malformed or unsupported ownership graph");
    }
    for (int kind = 0; kind < 10; ++kind) {
        auto bad = f;
        if (kind == 0) bad.contents(f.primary.list_reference,{null_ref});
        if (kind == 1) bad.records[f.primary.data_references[0]].type = derived_data;
        if (kind == 2) bad.scalar(f.primary.data_references[0],3,null_ref);
        if (kind == 3) bad.scalar(f.primary.data_references[0],3,0x80000000u);
        if (kind == 4) bad.records[f.primary.data_objects[0]].type = derived_object;
        if (kind == 5) bad.scalar(f.primary.data_objects[0],2,2);
        if (kind == 6) bad.scalar(f.primary.data_objects[0],2,4);
        if (kind == 7) bad.scalar(f.primary.data_objects[0],3,null_ref);
        if (kind == 8) bad.scalar(f.primary.data_objects[0],3,f.primary.data_objects[0]);
        if (kind == 9) bad.scalar(f.primary.data_objects[0],3,0x80000000u);
        if (kind == 3 || kind == 8 || kind == 9) {
            throws_igb([&]{bad.document();},"IGB parser rejects external or wrong-kind scalar references before script decoding");
            continue;
        }
        const auto invalid = list_igb_scripts(bad.document());
        check(!invalid[0].supported && !invalid[0].diagnostic.empty() && invalid[0].data_index == 0,
            "Malformed row remains an ordered unsupported descriptor");
        throws([&]{read_igb_script(bad.document(),f.primary.reference,0);},"Unsupported rows cannot expose payload bytes");
    }
    doc = f.document(); doc.metadata.header.byte_order = IgbByteOrder::big;
    throws([&]{list_igb_scripts(doc);},"Reject unverified big-endian script storage");
    doc = f.document(); doc.metadata.field_types[1].name = "igUnsignedIntMetaField";
    throws([&]{list_igb_scripts(doc);},"Validate ownership scalar field types, not just width");
    auto bad = f; bad.text(f.primary.data_references[0],2,std::string("boot.lua\0extra",14));
    check(!list_igb_scripts(bad.document())[0].name_valid,"Embedded NUL names cannot be compared as whole C++ strings");
    bad = f; bad.text(f.primary.data_references[0],2,std::string(4097,'a'));
    check(!list_igb_scripts(bad.document())[0].name_valid,"Bound decoded names to 4,096 bytes");
    bad = f;
    bad.records[f.primary.data_references[0]].values[2] = Bytes(1024u*1024u+4);
    word(bad.records[f.primary.data_references[0]].values[2],0,1024u*1024u);
    check(!list_igb_scripts(bad.document())[0].name_valid,"Bound serialized all-NUL names before trimming scans");
    bad = f;
    bad.records[f.primary.data_references[0]].values[2] = Bytes(4100);
    word(bad.records[f.primary.data_references[0]].values[2],0,4096);
    bad.contents(f.primary.list_reference,std::vector<std::uint32_t>(1100,f.primary.data_references[0]));
    const auto padded = list_igb_scripts(bad.document());
    check(padded.front().name_valid && !padded.back().name_valid,
        "Repeated padded aliases consume the cumulative serialized name budget");
    bad = f; bad.scalar(f.primary.data_objects[0],2,8u*1024u*1024u+1);
    check(!list_igb_scripts(bad.document())[0].supported,"Bound declared script size before obtaining payload");
    bad = f;
    bad.records[f.primary.memories[0]].data.resize(8u*1024u*1024u);
    bad.scalar(f.primary.data_objects[0],2,8u*1024u*1024u);
    bad.contents(f.primary.list_reference,std::vector<std::uint32_t>(9,f.primary.data_references[0]));
    const auto expanded = list_igb_scripts(bad.document());
    check(expanded.size() == 9 && expanded[7].supported && !expanded[8].supported,
        "Shared payload aliases count independently toward 64 MiB expansion");
    bad = f; bad.contents(f.primary.list_reference,std::vector<std::uint32_t>(32769,f.primary.data_references[0]));
    bad.contents(bad.exported_info_list,{f.primary.reference,f.primary.reference});
    throws([&]{list_igb_scripts(bad.document());},"Bound cumulative rows across repeated info exports");
    bad = f; bad.contents(bad.exported_info_list,std::vector<std::uint32_t>(2049,f.primary.reference));
    doc = bad.document();
    const std::array many_exports{StartupSource{&doc,"A"},StartupSource{&doc,"B"}};
    throws([&]{build_igb_startup_plan(many_exports);},"Bound cumulative exports across ordered source documents");
    const std::array null_sources{StartupSource{nullptr,"missing"}};
    throws([&]{build_igb_startup_plan(null_sources);},"Reject null source document");
    const std::array long_sources{StartupSource{&empty_doc,std::string(4097,'x')}};
    throws([&]{build_igb_startup_plan(long_sources);},"Bound source label before ownership copy");
    const std::vector<StartupSource> many_sources(65,{&empty_doc,"same source"});
    throws([&]{build_igb_startup_plan(many_sources);},"Bound source-list expansion even with shared document pointers");
}
void original(const std::filesystem::path& archive_path) {
    const auto archive = assets::AfsArchive::open(archive_path);
    const auto doc = read_igb_document(archive.read(179));
    const auto rows = list_igb_scripts(doc);
    check(rows.size() == 55,"Original dng101b1 member 1 exports 55 script records");
    std::set<std::uint32_t> objects;
    for (const auto& row : rows) {
        check(row.supported && row.name_valid && row.info_reference == 2783 && row.info_export_index == 237,
            "Every original script retains validated explicit export ownership");
        objects.insert(row.data_object_reference);
    }
    check(objects.size() < rows.size(),"Original shared bytecode objects remain distinct script records");
    const auto startup = plan(doc);
    check(startup.embedded_lookup_complete,"Original four-phase embedded selection resolves");
    const std::array<std::uint32_t,4> indices{9,6,53,8}, data{2391,1701,2654,1715},
        payloads{2392,1711,2655,1716}, memories{6578,2652,396,2431}, sizes{16849,7204,2099,1006};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& phase = startup.phases[i];
        check(phase.calls.size() == 1 && phase.calls[0].script.data_index == indices[i] &&
            phase.calls[0].script.data_reference == data[i] && phase.calls[0].script.data_object_reference == payloads[i] &&
            phase.calls[0].script.memory_reference == memories[i],"Original startup selection matches independently recorded graph");
        const auto chunk = read_igb_script(doc,2783,indices[i]);
        check(chunk.bytes.size() == sizes[i] && chunk.bytes[0] == 0x1b && chunk.bytes[1] == 'L' &&
            chunk.bytes[2] == 'u' && chunk.bytes[3] == 'a' && chunk.bytes[4] == 0x40,
            "Original owned buffer retains its exact size and Lua 4 signature");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        synthetic(); const auto synthetic_checks = checks;
        if (argc == 2) original(std::filesystem::path(argv[1]));
        else if (argc != 1) throw std::runtime_error("Usage: dw4_scripts_tests [ndmw.afs]");
        std::cout << "Embedded script checks: " << checks << " (synthetic " << synthetic_checks << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
