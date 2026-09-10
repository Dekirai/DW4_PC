#include "dw4/script_registry.hpp"
#include "dw4/assets.hpp"
#include "dw4/lua4.hpp"
#include "script_fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace dw4;
using namespace dw4::test;
int checks{};
void check(bool condition, const std::string& reason) {
    ++checks; if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F&& operation, const std::string& reason) {
    try { operation(); } catch (const ScriptError&) { check(true,reason); return; }
    check(false,reason);
}
ScriptRegistration selected(const IgbDocument& document, std::uint32_t reference,
    std::uint32_t export_index = 0, std::uint32_t source = 4, std::string label = "fixture") {
    return {&document,std::move(label),source,export_index,reference};
}
ScriptRegistry single(const IgbDocument& document, std::uint32_t reference) {
    const std::array registrations{selected(document,reference)};
    return ScriptRegistry(registrations);
}
void synthetic() {
    ScriptFixture f;
    const auto a = f.add_info({{"hit_player.lua",{10}},{"hit_player.lua",{11}},
        {"HIT_player.lua",{12}},{"arbitrary/path.xyz",{}},{"",{13}}},"A");
    const auto b = f.add_info({{"hit_player.lua",{20}}},"B");
    const auto hidden = f.add_info({{"hit_player.lua",{30}}},"hidden",false);
    auto doc = f.document();
    const auto original_bytes = Bytes(doc.bytes().begin(),doc.bytes().end());
    const std::array registrations{selected(doc,b.reference,1),selected(doc,a.reference),
        selected(doc,b.reference,1),selected(doc,a.reference,0,99,"second load")};
    const ScriptRegistry registry(registrations);
    const auto hit = registry.lookup("hit_player.lua");
    check(hit.filename == "hit_player.lua" && hit.entries.size() == 4,
        "Retain one result for each explicitly supplied registration");
    const std::array<std::uint8_t,4> expected{20,10,20,10};
    for (std::size_t i = 0; i < hit.entries.size(); ++i) {
        const auto& entry = hit.entries[i];
        check(entry.registration.registration_index == i && entry.state == ScriptLookupState::match &&
            entry.script->bytes == Bytes{expected[i]},"Explicit order and repeated calls survive selection");
        check(entry.script->info.data_index == 0 && entry.script->info.info_export_index == registrations[i].info_export_index,
            "First match retains its exact data and export ordinal");
    }
    check(hit.entries[3].registration.source_occurrence == 99 &&
        hit.entries[3].registration.source_label == "second load" &&
        hit.entries[0].registration.source_occurrence == 4,"Repeated source loads retain qualified provenance");
    check(registry.registrations().size() == 4 && registry.registrations()[0].info_reference == b.reference,
        "Registry inspection exposes only explicit registrations");
    check(registry.lookup("HIT_player.lua").entries[0].state == ScriptLookupState::no_match &&
        registry.lookup("HIT_player.lua").entries[1].script->bytes == Bytes{12},
        "Case-sensitive arbitrary filename selection does not skip another registration");
    check(registry.lookup("arbitrary/path.xyz").entries[1].state == ScriptLookupState::match &&
        registry.lookup("arbitrary/path.xyz").entries[1].script->bytes.empty(),
        "Owned empty bytes remain storage matches without implying valid Lua");
    check(registry.lookup("").entries[1].script->bytes == Bytes{13},"An authored empty name matches exactly");
    for (const auto& entry : registry.lookup("missing.lua").entries)
        check(entry.state == ScriptLookupState::no_match && !entry.script,"Missing filename retains each registry miss");
    check(Bytes(doc.bytes().begin(),doc.bytes().end()) == original_bytes,"Registration and lookup preserve input bytes");
    rejects([&] { single(doc,hidden.reference); },"Hidden info cannot be registered through an unrelated export ordinal");
    auto wrong = selected(doc,a.reference,1);
    rejects([&] { ScriptRegistry bad(std::span(&wrong,1)); },"Reference must match the selected export ordinal");
    wrong = selected(doc,a.reference,999);
    rejects([&] { ScriptRegistry bad(std::span(&wrong,1)); },"Export ordinal is range checked");
    wrong = selected(doc,a.reference); wrong.document = nullptr;
    rejects([&] { ScriptRegistry bad(std::span(&wrong,1)); },"Null document cannot register a prepared owner");
    auto other_doc = f.document();
    auto ambiguous = registrations;
    ambiguous[1].document = &other_doc;
    rejects([&] { ScriptRegistry bad(ambiguous); },"One source occurrence cannot identify different documents");
    ambiguous = registrations; ambiguous[1].source_label = "alien source";
    rejects([&] { ScriptRegistry bad(ambiguous); },"One source occurrence cannot acquire a different label");
    rejects([&] { registry.lookup(std::string("hit_player.lua\0other",20)); },"Embedded NUL filename is rejected");
    rejects([&] { registry.lookup(std::string(4097,'x')); },"Filename is bounded before copying");
    wrong = selected(doc,a.reference); wrong.source_label = std::string(4097,'x');
    rejects([&] { ScriptRegistry bad(std::span(&wrong,1)); },"Source label is bounded before copying");
    check(ScriptRegistry{}.lookup("any.lua").entries.empty(),"Empty prepared registry performs no implicit discovery");

    f.contents(f.exported_info_list,{a.reference,a.reference,b.reference});
    auto duplicate_doc = f.document();
    const std::array duplicate_exports{selected(duplicate_doc,a.reference,1),selected(duplicate_doc,a.reference,0)};
    const auto duplicated = ScriptRegistry(duplicate_exports).lookup("hit_player.lua");
    check(duplicated.entries[0].script->info.info_export_index == 1 &&
        duplicated.entries[1].script->info.info_export_index == 0 &&
        duplicated.entries[0].script->info.info_reference == duplicated.entries[1].script->info.info_reference,
        "Duplicate same-reference exports retain their selected occurrence instead of read's first occurrence");

    auto malformed = f;
    malformed.scalar(a.data_references[0],3,null_ref);
    auto malformed_doc = malformed.document();
    const std::array bad_then_good{selected(malformed_doc,a.reference),selected(malformed_doc,b.reference,2)};
    const auto barriers = ScriptRegistry(bad_then_good).lookup("hit_player.lua");
    check(barriers.entries[0].state == ScriptLookupState::unsupported_match && barriers.entries[0].script &&
        barriers.entries[0].script->bytes.empty() && barriers.entries[0].script->info.data_index == 0 &&
        !barriers.entries[0].script->info.diagnostic.empty(),"Unsupported first match shadows later same-name bytes");
    check(barriers.entries[1].state == ScriptLookupState::match && barriers.entries[1].script->bytes == Bytes{20},
        "Plan preserves subsequent independent registration after an explicit earlier barrier");
    malformed = f; malformed.contents(a.list_reference,{null_ref,a.data_references[0]});
    malformed_doc = malformed.document();
    const auto unknown = single(malformed_doc,a.reference).lookup("hit_player.lua");
    check(unknown.entries[0].state == ScriptLookupState::name_barrier && unknown.entries[0].script->info.data_index == 0,
        "An unknown earlier name blocks a potentially shadowed later match");
    malformed.contents(a.list_reference,{a.data_references[0],null_ref});
    malformed_doc = malformed.document();
    check(single(malformed_doc,a.reference).lookup("hit_player.lua").entries[0].state == ScriptLookupState::match,
        "An uncertain row after the first match does not invalidate selection");
    malformed = f; malformed.scalar(a.data_references[2],3,null_ref);
    malformed_doc = malformed.document();
    check(single(malformed_doc,a.reference).lookup("").entries[0].state == ScriptLookupState::match,
        "A known different malformed payload does not shadow a later requested name");
    // Opaque storage is deliberately parsed only by the eventual executor.
    bool lua_rejected{};
    try { read_lua4_chunk(hit.entries[1].script->bytes); } catch (const Lua4Error&) { lua_rejected = true; }
    check(lua_rejected && hit.entries[1].script->info.data_index == 0,
        "Invalid first-match bytecode remains selected and cannot fall through to its duplicate");

    ScriptFixture empty_fixture;
    const auto empty = empty_fixture.add_info({},"empty");
    auto empty_doc = empty_fixture.document();
    const auto empty_registry = single(empty_doc,empty.reference);
    check(empty_registry.registrations().size() == 1 &&
        empty_registry.lookup("hit_player.lua").entries[0].state == ScriptLookupState::no_match,
        "An exported empty info still owns a distinct registry occurrence");
    const auto plain = empty_fixture.add(test_controller);
    empty_fixture.text(plain,2,"non-Lua");
    empty_fixture.contents(empty_fixture.exported_info_list,{plain});
    auto plain_doc = empty_fixture.document();
    rejects([&] { single(plain_doc,plain); },"A different exported info type is not a Lua registration");

    ScriptLookupPlan retained;
    {
        ScriptFixture temporary;
        const auto info = temporary.add_info({{"owned.lua",{1,2,3}}});
        auto temporary_doc = temporary.document();
        auto temporary_registry = single(temporary_doc,info.reference);
        retained = temporary_registry.lookup("owned.lua");
        temporary_doc = IgbDocument{};
        check(temporary_registry.lookup("owned.lua").entries[0].script->bytes == Bytes({1,2,3}),
            "Registry owns bytes after source document destruction");
    }
    check(retained.entries[0].script->bytes == Bytes({1,2,3}) && retained.entries[0].registration.source_label == "fixture",
        "A retained selection owns its bytes and qualified labels after registry destruction");

    std::vector<ScriptRegistration> too_many(4097,selected(doc,a.reference));
    rejects([&] { ScriptRegistry bad(too_many); },"Repeated registrations count toward the occurrence bound");
    too_many.resize(65);
    for (std::uint32_t i = 0; i < too_many.size(); ++i) too_many[i].source_occurrence = i;
    rejects([&] { ScriptRegistry bad(too_many); },"Distinct repeated source loads count toward the source bound");
    ScriptFixture many_rows;
    std::vector<ScriptFixture::Script> row_values(17,{"repeat.lua",{1}});
    const auto row_info = many_rows.add_info(row_values);
    auto row_doc = many_rows.document();
    std::vector<ScriptRegistration> repeated_rows(4096,selected(row_doc,row_info.reference));
    rejects([&] { ScriptRegistry bad(repeated_rows); },"Repeated registrations consume the aggregate 65,536 row bound");
    ScriptFixture large;
    const auto large_info = large.add_info({{"large.lua",Bytes(1024u*1024u,7)}});
    auto large_doc = large.document();
    std::vector<ScriptRegistration> repeated_bytes(65,selected(large_doc,large_info.reference));
    rejects([&] { ScriptRegistry bad(repeated_bytes); },"Every repeated byte occurrence consumes the 64 MiB ownership bound");
}
void original(const std::filesystem::path& archive_path) {
    const auto archive = assets::AfsArchive::open(archive_path);
    auto document = read_igb_document(archive.read(179));
    const std::array registrations{selected(document,2783,237,9,"ndmw entry 179"),
        selected(document,2783,237,9,"ndmw entry 179")};
    const ScriptRegistry registry(registrations);
    const auto plan = registry.lookup("hit_p_agumon.lua");
    check(plan.entries.size() == 2,"Original explicit repeated registration remains two invocations");
    for (const auto& entry : plan.entries) {
        check(entry.state == ScriptLookupState::match && entry.script->info.data_index == 26 &&
            entry.script->info.data_reference == 1700 && entry.script->info.data_object_reference == 2418 &&
            entry.script->info.memory_reference == 2675 && entry.script->bytes.size() == 3230,
            "Actual Hit selection retains independently recovered object and buffer provenance");
        const auto program = read_lua4_chunk(entry.script->bytes,"owned original hit");
        check(program.info().functions == 1 && program.info().instructions == 534,
            "Owned original selected bytes decode lazily through the existing Lua reader");
    }
    const auto direct = read_igb_script(document,2783,26);
    check(direct.bytes == plan.entries[0].script->bytes,"Registry bytes match the existing exact payload reader");
    document = IgbDocument{};
    check(registry.lookup("hit_p_agumon.lua").entries[0].script->bytes == direct.bytes,
        "Original source lifetime does not constrain retained registry lookup");
}
} // namespace
int main(int argc, char** argv) {
    try {
        synthetic(); const auto synthetic_checks = checks;
        if (argc == 2) original(std::filesystem::path(argv[1]));
        else if (argc != 1) throw std::runtime_error("Usage: dw4_script_registry_tests [ndmw.afs]");
        std::cout << "Script registry checks: " << checks << " (synthetic " << synthetic_checks << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
