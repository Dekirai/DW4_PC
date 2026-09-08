#include "dw4/startup.hpp"
#include "dw4/scripts.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <iostream>
#include <stdexcept>
#include <variant>

namespace {
using dw4::test::Bytes;
int checks = 0;
void check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F action, const char* reason) {
    ++checks; try { action(); } catch (const std::exception&) { return; } throw std::runtime_error(reason);
}
// Independent tiny Lua4 chunk encoder; no original bytecode is embedded.
struct Code {
    std::vector<std::string> strings;
    std::vector<std::uint32_t> instructions;
    void string_op(std::uint32_t op, std::string value) {
        const auto index = static_cast<std::uint32_t>(strings.size()); strings.push_back(std::move(value));
        instructions.push_back(op | (index << 6));
    }
    void begin_call(const std::string& method) { string_op(12,"be"); string_op(16,method); }
    void end_call(std::uint32_t results = 0) { instructions.push_back(2 | (results << 6)); }
    void call(const std::string& method) { begin_call(method); end_call(); }
    void call(const std::string& method, const std::string& arg) { begin_call(method); string_op(7,arg); end_call(); }
    void number(int value) { instructions.push_back(6 | (static_cast<std::uint32_t>(value+33554431) << 6)); }
    Bytes bytes() const {
        Bytes result{0x1b,'L','u','a',0x40,1,4,4,4,32,6,9,8};
        const auto probe = std::bit_cast<std::uint64_t>(314159265.3589793);
        for (int i = 0; i < 8; ++i) result.push_back(static_cast<std::uint8_t>(probe >> (i*8)));
        const auto word = [&](std::uint32_t value) { dw4::test::push32(result,value); };
        word(0); word(0); word(0); result.push_back(0); word(16); // Source, line, params, vararg, stack.
        word(0); word(0); word(static_cast<std::uint32_t>(strings.size()));
        for (const auto& text : strings) { word(static_cast<std::uint32_t>(text.size()+1)); result.insert(result.end(),text.begin(),text.end()); result.push_back(0); }
        word(0); word(0); word(static_cast<std::uint32_t>(instructions.size()+1));
        for (auto instruction : instructions) word(instruction); word(0); return result;
    }
};
dw4::BootReport execute(const Code& functions, const Code& boot, const dw4::BootInputs& input = {}) {
    dw4::test::ScriptFixture fixture;
    fixture.add_info({{"func.lua",functions.bytes()},{"boot.lua",boot.bytes()}});
    const auto document = fixture.document();
    const std::array sources{dw4::StartupSource{&document,"Synthetic member"}};
    return dw4::execute_boot_scripts(sources,"SyntheticFolder",input);
}
void synthetic() {
    Code functions; functions.string_op(7,"Shared.igb"); functions.string_op(19,"saved_path");
    functions.call("CFLsetName","Discarded.igb");
    Code boot; boot.call("CFLclear"); boot.begin_call("CFLsetName"); boot.string_op(12,"saved_path"); boot.end_call();
    boot.call("CFLsetName","Shared.igb"); boot.call("CFLsetName","shared.igb");
    auto report = execute(functions,boot);
    check(report.boot_scripts_completed && report.scripts.size() == 2,"Execute both script phases with persistent globals");
    check(!report.resources_resolved && !report.next_barrier.empty(),"Execution does not claim loading or registration");
    check(report.resources.size() == 2 && report.resources[0].logical_path == "Shared.igb" && report.resources[1].logical_path == "shared.igb",
        "CFLclear removes old requests; exact dedup preserves case and order");
    check(report.resources[0].source.find("boot.lua") != std::string::npos && report.resources[0].prototype == "main","Queue retains executed call provenance");
    dw4::ResourceCatalog catalog; catalog.root_label = "Synthetic"; catalog.archives.push_back({"test.afs",1});
    catalog.resources.push_back({"shared.igb",0,dw4::ResourceLocation{0,0}});
    dw4::resolve_boot_requests(report,catalog);
    check(report.resources_resolved && report.resources[0].location == report.resources[1].location,"Original ASCII fold resolves both preserved case variants");
    check(report.resources[0].logical_path == "Shared.igb" && report.resources[0].catalog_path == "shared.igb","Lookup does not rewrite original request");
    auto stale = report; stale.resources[0].logical_path = "../bad.igb";
    rejects([&]{dw4::resolve_boot_requests(stale,catalog);},"Reject forged report path during re-resolution");
    check(!stale.resources_resolved && !stale.resources[0].location && !stale.resources[1].location,"Invalid re-resolution clears all previous success and locations");
    stale = report;
    rejects([&]{dw4::resolve_boot_requests(stale,{});},"Reject invalid catalog after a successful pass");
    check(!stale.resources_resolved && !stale.resources[0].location,"Catalog validation cannot preserve stale success");
    auto repeated_catalog = catalog; repeated_catalog.resources.push_back(catalog.resources[0]);
    dw4::resolve_boot_requests(report,repeated_catalog);
    check(report.resources_resolved,"Sorted resolution preserves valid identical repeated catalog keys");
    catalog.resources[0].logical_path = "Shared.igb";
    dw4::resolve_boot_requests(report,catalog);
    check(!report.resources_resolved && !report.resources[0].location,"Do not lowercase catalog keys or retain stale resolution");
    Code stopped; stopped.call("CFLclear"); stopped.call("CFLsetName","before.igb"); stopped.call("sd"); stopped.call("CFLsetName","after.igb");
    report = execute({},stopped);
    check(!report.boot_scripts_completed && report.resources.size() == 1 && report.resources[0].logical_path == "before.igb","Unknown engine effect stops without running later calls");
    check(report.diagnostic.find("be.sd") != std::string::npos && report.diagnostic.find("PC") != std::string::npos,"Stopped binding includes native name and bytecode site");
    for (const auto path : {"../outside.igb","/absolute.igb","a//b.igb","a\\b.igb","C:/outside.igb"}) {
        Code bad; bad.call("CFLsetName",path); check(!execute({},bad).boot_scripts_completed,"Reject invalid queue paths");
    }
    Code count; count.call("CFLsetName"); check(!execute({},count).boot_scripts_completed,"Reject wrong host arity");
    Code slot; slot.begin_call("SYSisJoinPlayer"); slot.number(4); slot.end_call();
    check(!execute({},slot).boot_scripts_completed,"Reject out-of-range native slot");
    Code constant; constant.begin_call("CFLsetName"); constant.string_op(12,"be"); constant.string_op(16,"CFLgetFolderName");
    constant.instructions.push_back(2 | (2u << 15) | (1u << 6)); constant.string_op(7,"/file.igb");
    constant.instructions.push_back(29 | (2u << 6)); constant.end_call();
    report = execute({},constant);
    check(report.boot_scripts_completed && report.resources.at(0).logical_path == "SyntheticFolder/file.igb","Folder getter returns supplied inherited name");
    dw4::BootInputs input;
    for (const auto option : {"--boot-difficulty","--boot-camera"}) {
        check(dw4::set_boot_input_option(input,option,"0"),"Accept explicit zero state");
        rejects([&]{dw4::set_boot_input_option(input,option,"-1");},"Reject negative state");
        rejects([&]{dw4::set_boot_input_option(input,option,"1x");},"Reject partially parsed state");
    }
    check(!dw4::set_boot_input_option(input,"--boot-country","0"),"USA literal getter cannot be overridden as state");
    check(dw4::set_boot_input_option(input,"--boot-player-0","p_test"),"Parse explicit joined player");
    check(dw4::set_boot_input_option(input,"--boot-weapon-0-2","w_test"),"Parse third weapon slot");
    dw4::validate_boot_inputs(input);
    check(input.players[0].joined && input.players[0].weapon_names[2] == "w_test","Keep player and weapon ordinals distinct");
    rejects([&]{dw4::set_boot_input_option(input,"--boot-player-4","p_test");},"Reject invalid option slot");
    rejects([&]{dw4::set_boot_input_option(input,"--boot-player-00","p_test");},"Reject duplicate-selector alias");
    rejects([&]{dw4::set_boot_input_option(input,"--boot-player-0","../escape");},"Reject path in model token");
    input.players[0].joined = false;
    rejects([&]{dw4::validate_boot_inputs(input);},"Reject unjoined state with model/weapons");
}
} // namespace
int main() {
    try { synthetic(); std::cout << "Startup tests passed: " << checks << " checks\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "Startup test failure: " << error.what() << '\n'; return 1; }
}
