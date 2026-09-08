#include "dw4/startup.hpp"
#include "dw4/scripts.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
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
    std::vector<Code> children;
    void string_op(std::uint32_t op, std::string value) {
        const auto index = static_cast<std::uint32_t>(strings.size()); strings.push_back(std::move(value));
        instructions.push_back(op | (index << 6));
    }
    void begin_call(const std::string& method) { string_op(12,"be"); string_op(16,method); }
    void end_call(std::uint32_t results = 0) { instructions.push_back(2 | (results << 6)); }
    void call(const std::string& method) { begin_call(method); end_call(); }
    void call(const std::string& method, const std::string& arg) { begin_call(method); string_op(7,arg); end_call(); }
    void number(int value) { instructions.push_back(6 | (static_cast<std::uint32_t>(value+33554431) << 6)); }
    void append_function(Bytes& result) const {
        const auto word = [&](std::uint32_t value) { dw4::test::push32(result,value); };
        word(0); word(0); word(0); result.push_back(0); word(16); // Source, line, params, vararg, stack.
        word(0); word(0); word(static_cast<std::uint32_t>(strings.size()));
        for (const auto& text : strings) { word(static_cast<std::uint32_t>(text.size()+1)); result.insert(result.end(),text.begin(),text.end()); result.push_back(0); }
        word(0); word(static_cast<std::uint32_t>(children.size()));
        for (const auto& child : children) child.append_function(result);
        word(static_cast<std::uint32_t>(instructions.size()+1));
        for (auto instruction : instructions) word(instruction); word(0);
    }
    Bytes bytes() const {
        Bytes result{0x1b,'L','u','a',0x40,1,4,4,4,32,6,9,8};
        const auto probe = std::bit_cast<std::uint64_t>(314159265.3589793);
        for (int i = 0; i < 8; ++i) result.push_back(static_cast<std::uint8_t>(probe >> (i*8)));
        append_function(result); return result;
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
void runtime_lifecycle() {
    static_assert(!std::is_copy_constructible_v<dw4::BootRuntime>);
    static_assert(!std::is_copy_assignable_v<dw4::BootRuntime>);
    static_assert(std::is_nothrow_move_constructible_v<dw4::BootRuntime>);
    static_assert(std::is_nothrow_move_assignable_v<dw4::BootRuntime>);
    dw4::BootRuntime empty;
    check(!empty.has_live_state() && empty.report().scripts.empty(),"A default runtime is unexecuted");
    rejects([&]{empty.global("be");},"Unexecuted runtime exposes no globals");
    rejects([&]{empty.resolve_resources({});},"Unexecuted runtime cannot resolve resources");
    dw4::BootRuntime runtime = std::move(empty);
    check(!empty.has_live_state() && !runtime.has_live_state(),"Moving an unexecuted runtime transfers ownership");
    rejects([&]{empty.report();},"Moved-from runtime exposes no report");
    rejects([&]{empty.execute({},"Synthetic",{});},"Moved-from runtime cannot execute");
    const auto* address = &runtime.report();
    {
        Code child; child.string_op(7,"Retained.igb"); child.instructions.push_back(1); // RETURN from local zero.
        Code functions; functions.children.push_back(child); functions.instructions.push_back(48); // CLOSURE child zero.
        functions.string_op(19,"saved_function");
        functions.string_op(7,"owned value"); functions.string_op(19,"saved_value");
        functions.instructions.push_back(17); functions.string_op(19,"saved_table");
        Code boot; boot.call("CFLclear"); boot.begin_call("CFLsetName"); boot.string_op(12,"saved_function");
        boot.instructions.push_back(2 | (2u << 15) | (1u << 6)); boot.end_call();
        boot.number(42); boot.string_op(19,"boot_number");
        dw4::test::ScriptFixture fixture;
        fixture.add_info({{"func.lua",functions.bytes()},{"boot.lua",boot.bytes()}});
        auto document = fixture.document();
        const std::array sources{dw4::StartupSource{&document,"Temporary source"}};
        runtime.execute(sources,"Synthetic",{});
        check(runtime.has_live_state() && runtime.report().boot_scripts_completed,"Completed boot retains actual VM state");
        check(runtime.report().resources.at(0).logical_path == "Retained.igb","Boot calls the closure retained from func.lua");
    }
    check(runtime.global("saved_value").as_string() == "owned value","Globals survive destruction of source buffers and labels");
    check(runtime.global("saved_function").type() == dw4::Lua4Type::Function,"Function global remains owned after chunk destruction");
    check(runtime.global("saved_table").type() == dw4::Lua4Type::Table,"Table global remains owned after chunk destruction");
    check(runtime.global("boot_number").as_number() == 42,"Boot writes remain in the retained VM");
    check(runtime.global("be").host_name() == "be" && runtime.global("missing").type() == dw4::Lua4Type::Nil,"Strict host receiver and missing globals remain inspectable");
    dw4::BootRuntime moved(std::move(runtime));
    check(!runtime.has_live_state() && moved.has_live_state() && &moved.report() == address,"Move construction preserves the host/report address");
    rejects([&]{runtime.global("saved_value");},"Moved-from runtime exposes no stale VM handles");
    check(moved.global("saved_value").as_string() == "owned value","Move construction retains owned globals");
    rejects([&]{moved.execute({},"Other",{});},"Completed boot cannot execute a second time");
    check(moved.has_live_state() && moved.report().resources.size() == 1,"Rejected rerun leaves successful state unchanged");
    dw4::ResourceCatalog catalog; catalog.root_label = "Synthetic"; catalog.archives.push_back({"test.afs",1});
    catalog.resources.push_back({"retained.igb",0,dw4::ResourceLocation{0,0}});
    moved.resolve_resources(catalog);
    check(moved.report().resources_resolved && moved.report().resources[0].catalog_path == "retained.igb","Moved runtime resolves its retained report");
    dw4::BootRuntime assigned;
    {
        dw4::test::ScriptFixture fixture;
        fixture.add_info({{"func.lua",Code{}.bytes()},{"boot.lua",Code{}.bytes()}});
        const auto document = fixture.document();
        const std::array sources{dw4::StartupSource{&document,"Replaced source"}};
        assigned.execute(sources,"Replaced",{});
    }
    check(assigned.has_live_state(),"Move assignment can replace an existing live runtime");
    assigned = std::move(moved);
    check(!moved.has_live_state() && assigned.has_live_state() && &assigned.report() == address,"Move assignment preserves the retained host/report address");
    check(assigned.global("boot_number").as_number() == 42 && assigned.report().resources_resolved,"Move assignment transfers globals and resource resolution");
    rejects([&]{assigned.resolve_resources({});},"Invalid resolution still reports its error");
    check(assigned.has_live_state() && !assigned.report().resources_resolved && !assigned.report().resources[0].location,
        "Resolution failure clears stale locations without discarding successful Lua state");
    dw4::BootRuntime failed;
    {
        Code functions; functions.string_op(7,"partial value"); functions.string_op(19,"saved_value");
        Code stopped; stopped.call("CFLsetName","before.igb"); stopped.call("sd");
        dw4::test::ScriptFixture fixture;
        fixture.add_info({{"func.lua",functions.bytes()},{"boot.lua",stopped.bytes()}});
        const auto document = fixture.document();
        const std::array sources{dw4::StartupSource{&document,"Failing source"}};
        failed.execute(sources,"Synthetic",{});
    }
    check(!failed.has_live_state() && !failed.report().boot_scripts_completed && failed.report().scripts[0].completed,"A stopped boot exposes no live partial VM");
    check(failed.report().resources.size() == 1 && failed.report().diagnostic.find("be.sd") != std::string::npos,"Failure retains diagnostic queue and actual failed binding");
    rejects([&]{failed.global("saved_value");},"Partial globals from failed execution are not exposed");
    rejects([&]{failed.execute({},"Synthetic",{});},"Failed boot cannot be retried");
    dw4::BootRuntime moved_failure(std::move(failed));
    check(!moved_failure.has_live_state() && moved_failure.report().resources.size() == 1,"Moving a failure preserves diagnostics without reviving state");
    rejects([&]{moved_failure.execute({},"Synthetic",{});},"Moving a failure does not reset its execution guard");
    moved_failure.resolve_resources(catalog);
    check(!moved_failure.report().resources_resolved,"Diagnostic resolution cannot mark failed boot complete");
    dw4::BootRuntime invalid;
    invalid.execute({},"Synthetic",{});
    check(!invalid.has_live_state() && !invalid.report().diagnostic.empty(),"Validation failure leaves a stopped runtime");
    rejects([&]{invalid.execute({},"Synthetic",{});},"Validation failure also consumes the single attempt");
}
} // namespace
int main() {
    try { synthetic(); runtime_lifecycle(); std::cout << "Startup tests passed: " << checks << " checks\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "Startup test failure: " << error.what() << '\n'; return 1; }
}
