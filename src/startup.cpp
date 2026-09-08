#include "dw4/startup.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/lua4.hpp"
#include "dw4/packages.hpp"
#include "dw4/scripts.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace dw4 {
namespace {
constexpr std::size_t max_queue_entries = 4096;
constexpr std::size_t max_queue_bytes = 4u*1024u*1024u;
constexpr std::size_t max_script_calls = 64;
[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("Boot: " + text); }
void token(std::string_view value, std::size_t limit, const char* label) {
    if (value.empty() || value.size() > limit || std::any_of(value.begin(),value.end(),[](unsigned char c) {
        return !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_';
    })) fail(std::string(label)+" must contain only ASCII letters, digits and underscores within its length limit.");
}
void logical_path(std::string_view path) {
    if (path.empty() || path.size() > 4096 || path.front() == '/' || path.back() == '/') fail("Invalid logical resource path.");
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') {
            const auto c = static_cast<unsigned char>(path[i]);
            if (c < 32 || c == 127 || c == '\\' || c == ':') fail("Resource path contains an unsupported character.");
            continue;
        }
        const auto part = path.substr(begin,i-begin);
        if (part.empty() || part == "." || part == "..") fail("Resource path contains a traversal or empty component.");
        begin = i+1;
    }
}
int integer(std::string_view value, int max_value) {
    int result{};
    const auto parsed = std::from_chars(value.data(),value.data()+value.size(),result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || result < 0 || result > max_value)
        fail("Inspection integer must be from 0 to "+std::to_string(max_value)+".");
    return result;
}
int argument_integer(const Lua4Value& value, int max_value) {
    const auto number = value.as_number();
    if (!std::isfinite(number) || number < 0 || number > max_value || std::floor(number) != number)
        fail("Native method index is outside its explicit inspection range.");
    return static_cast<int>(number);
}
struct Host {
    BootReport& report;
    std::size_t queue_bytes{}, attempted_calls{}, scanned_path_bytes{};
    std::set<std::string,std::less<>> queued_paths{};
    std::vector<Lua4Value> call(const Lua4HostCall& call) {
        if (++attempted_calls > 32768) fail("Combined native-call budget exceeded.");
        const auto& input = report.inputs;
        if (call.name == "gsub") {
            if (call.arguments.size() != 3) fail("Only three-argument literal gsub is supported.");
            const auto value = call.arguments[0].as_string();
            const auto pattern = call.arguments[1].as_string();
            const auto replacement = call.arguments[2].as_string();
            // The boot profile uses literal p_ -> q_ substitution. Lua patterns,
            // replacement captures, callbacks and optional limits are not inferred.
            if (pattern != "p_" || replacement != "q_" || value.size() > 128)
                fail("Unsupported gsub profile; only bounded p_ to q_ model-name substitution is implemented.");
            std::string result(value); std::size_t count = 0, at = 0;
            while ((at = result.find(pattern,at)) != std::string::npos) { result.replace(at,2,replacement); at += 2; ++count; }
            return {Lua4Value::string(std::move(result)),Lua4Value::number(static_cast<double>(count))};
        }
        if (!call.name.starts_with("be.") || call.arguments.empty() ||
            call.arguments[0].type() != Lua4Type::HostObject || call.arguments[0].host_name() != "be")
            fail("Unsupported native binding or receiver: "+call.name);
        const auto args = call.arguments.subspan(1);
        const auto arity = [&](std::size_t size) { if (args.size() != size) fail("Wrong argument count for "+call.name); };
        const auto scalar = [&](int value) { arity(0); return std::vector<Lua4Value>{Lua4Value::number(value)}; };
        if (call.name == "be.CFLclear") {
            arity(0); report.resources.clear(); queued_paths.clear(); queue_bytes = 0; return {};
        }
        if (call.name == "be.CFLsetName") {
            arity(1); const auto path = args[0].as_string();
            if (path.size() > 8u*1024u*1024u-scanned_path_bytes) fail("Cumulative common-resource path scan budget exceeded.");
            scanned_path_bytes += path.size(); logical_path(path);
            if (queued_paths.contains(path)) return {}; // Original strcmp deduplication keeps the first request.
            const auto request_bytes = path.size()+call.site.source.size()+call.site.prototype.size();
            if (report.resources.size() >= max_queue_entries || request_bytes > max_queue_bytes-queue_bytes)
                fail("Common-resource queue budget exceeded.");
            queue_bytes += request_bytes; queued_paths.emplace(path);
            report.resources.push_back({std::string(path),call.site.source,call.site.prototype,call.site.pc,std::nullopt,{},{}});
            return {};
        }
        if (call.name == "be.CFLgetFolderName") { arity(0); return {Lua4Value::string(report.folder)}; }
        if (call.name == "be.SYSgetDifficulty") return scalar(input.difficulty);
        if (call.name == "be.SYSgetCameraMode") return scalar(input.camera_mode);
        // These three getters are literal return-1 routines in the verified USA DOL.
        if (call.name == "be.SYSgetCountry" || call.name == "be.SYSgetArea" || call.name == "be.SYSgetIsPackOnly") return scalar(1);
        if (call.name == "be.SYSisJoinPlayer" || call.name == "be.SYSgetPlayerName") {
            arity(1); const auto& player = input.players.at(static_cast<std::size_t>(argument_integer(args[0],3)));
            if (call.name == "be.SYSisJoinPlayer") return {Lua4Value::number(player.joined ? 1 : 0)};
            if (!player.joined || player.model_name.empty()) fail("Player model query has no configured joined player.");
            return {Lua4Value::string(player.model_name)};
        }
        if (call.name == "be.SYSgetWeaponName") {
            arity(2); const auto& player = input.players.at(static_cast<std::size_t>(argument_integer(args[0],3)));
            if (!player.joined) fail("Weapon query has no configured joined player.");
            const auto& name = player.weapon_names.at(static_cast<std::size_t>(argument_integer(args[1],2)));
            return {name ? Lua4Value::string(*name) : Lua4Value{}};
        }
        fail("Native method is not implemented: "+call.name);
    }
};
} // namespace

struct BootRuntime::Impl {
    BootReport report;
    Host host{report};
    std::unique_ptr<Lua4Vm> vm;
    bool attempted = false;
};

BootRuntime::BootRuntime() : impl_(std::make_unique<Impl>()) {}
BootRuntime::~BootRuntime() = default;
BootRuntime::BootRuntime(BootRuntime&&) noexcept = default;
BootRuntime& BootRuntime::operator=(BootRuntime&&) noexcept = default;
const BootReport& BootRuntime::report() const {
    if (!impl_) fail("Cannot inspect a moved-from boot runtime.");
    return impl_->report;
}
bool BootRuntime::has_live_state() const noexcept {
    return impl_ && impl_->vm && impl_->report.boot_scripts_completed;
}
Lua4Value BootRuntime::global(std::string_view name) const {
    if (!has_live_state()) fail("Global inspection requires a successfully completed boot runtime.");
    return impl_->vm->get_global(name);
}
void BootRuntime::resolve_resources(const ResourceCatalog& catalog) {
    if (!impl_ || !impl_->attempted) fail("Resource resolution requires an executed boot runtime.");
    resolve_boot_requests(impl_->report,catalog);
}

void validate_boot_inputs(const BootInputs& input) {
    if (input.difficulty < 0 || input.difficulty > 2 || input.camera_mode < 0 || input.camera_mode > 1)
        fail("Inspection state is outside the supported boot profile.");
    for (const auto& player : input.players) {
        if (player.joined) token(player.model_name,128,"Joined player model");
        else if (!player.model_name.empty()) fail("An unjoined player cannot have a model name.");
        for (const auto& weapon : player.weapon_names) if (weapon) {
            if (!player.joined) fail("An unjoined player cannot have a weapon name.");
            token(*weapon,128,"Weapon name");
        }
    }
}

bool set_boot_input_option(BootInputs& input, std::string_view option, std::string_view value) {
    if (option == "--boot-difficulty") input.difficulty = integer(value,2);
    else if (option == "--boot-camera") input.camera_mode = integer(value,1);
    else if (option.starts_with("--boot-player-")) {
        if (option.size() != 15) fail("Use --boot-player-N with one slot digit from 0 to 3.");
        const auto index = integer(option.substr(14),3); token(value,128,"Player model");
        input.players[static_cast<std::size_t>(index)].joined = true;
        input.players[static_cast<std::size_t>(index)].model_name = value;
    } else if (option.starts_with("--boot-weapon-")) {
        const auto indices = option.substr(14);
        if (indices.size() != 3 || indices[1] != '-') fail("Use --boot-weapon-PLAYER-SLOT with player 0..3 and slot 0..2.");
        const auto player = integer(indices.substr(0,1),3), slot = integer(indices.substr(2,1),2);
        token(value,128,"Weapon name");
        input.players[static_cast<std::size_t>(player)].weapon_names[static_cast<std::size_t>(slot)] = value;
    } else return false;
    return true;
}

void BootRuntime::execute(std::span<const StartupSource> sources, const std::string& folder, const BootInputs& input) {
    if (!impl_) fail("Cannot execute a moved-from boot runtime.");
    if (impl_->attempted) fail("A boot runtime permits only one execution attempt.");
    impl_->attempted = true;
    auto& report = impl_->report;
    report.next_barrier = "Common resource loading, deferred controller registration and layer/text initialization remain pending. start.lua and first.lua have not run.";
    try {
        validate_boot_inputs(input); logical_path(folder);
        report.folder = folder; report.inputs = input;
        if (sources.empty() || sources.size() > 32) fail("Boot requires 1..32 explicitly ordered script sources.");
        const auto plan = build_igb_startup_plan(sources);
        report.registered_sources = sources.size();
        for (const auto& source : sources) report.registered_scripts += list_igb_scripts(*source.document).size();
        Lua4Limits limits; limits.max_instructions = 32768;
        impl_->vm = std::make_unique<Lua4Vm>(limits);
        auto& vm = *impl_->vm;
        vm.set_global("be",Lua4Value::host_object("be"));
        vm.set_global("gsub",Lua4Value::host_function("gsub"));
        vm.set_host_handler([state = impl_.get()](const Lua4HostCall& call) { return state->host.call(call); });
        for (const auto& phase : plan.phases) {
            if (phase.kind != StartupPhaseKind::functions && phase.kind != StartupPhaseKind::boot) break;
            if (!phase.lookup_complete) fail(phase.filename+": "+(phase.diagnostics.empty() ? "No complete embedded lookup." : phase.diagnostics.front()));
            for (const auto& call : phase.calls) {
                if (report.scripts.size() >= max_script_calls) fail("Boot script-call budget exceeded.");
                const auto& source = sources[call.source_index];
                BootScriptResult result; result.name = phase.filename; result.source = source.label;
                result.info_reference = call.script.info_reference; result.data_index = call.script.data_index;
                try {
                    if (!call.script.supported) fail(call.script.diagnostic);
                    const auto script = read_igb_script(*source.document,call.script.info_reference,call.script.data_index);
                    const auto program = read_lua4_chunk(script.bytes,source.label+" / "+script.info.name);
                    const auto run = vm.execute(program);
                    result.instructions = run.instructions; result.host_calls = run.host_calls;
                    result.completed = run.completed; result.diagnostic = run.diagnostic;
                    if (!run.completed) result.diagnostic += " ["+run.site.source+" / "+run.site.prototype+" / PC "+std::to_string(run.site.pc)+"]";
                    report.instructions += run.instructions; report.host_calls += run.host_calls;
                } catch (const std::exception& error) { result.diagnostic = phase.filename+" at "+source.label+": "+error.what(); }
                report.scripts.push_back(result);
                if (!result.completed) fail(result.diagnostic);
                if (report.instructions > 2u*1024u*1024u) fail("Combined boot instruction budget exceeded.");
            }
        }
        report.boot_scripts_completed = true;
    } catch (const std::exception& error) {
        report.diagnostic = error.what();
        impl_->vm.reset(); // Failed partial state is never exposed or resumed.
    }
}

BootReport execute_boot_scripts(std::span<const StartupSource> sources, const std::string& folder, const BootInputs& input) {
    BootRuntime runtime;
    runtime.execute(sources,folder,input);
    return runtime.report();
}

void resolve_boot_requests(BootReport& report, const ResourceCatalog& catalog) {
    report.resources_resolved = false;
    for (auto& request : report.resources) { request.location.reset(); request.catalog_path.clear(); request.diagnostic.clear(); }
    validate_resource_catalog(catalog);
    if (report.resources.size() > max_queue_entries) fail("Request list exceeds its resolution limit.");
    for (const auto& request : report.resources) logical_path(request.logical_path);
    // ResourceCatalog::find is linear. Index this pass once, preserving the first
    // entry when a catalog legitimately repeats the same exact key/location.
    std::vector<const CatalogResource*> index; index.reserve(catalog.resources.size());
    for (const auto& resource : catalog.resources) index.push_back(&resource);
    std::stable_sort(index.begin(),index.end(),[](const auto* a, const auto* b) { return a->logical_path < b->logical_path; });
    bool complete = report.boot_scripts_completed;
    for (auto& request : report.resources) {
        auto folded = request.logical_path;
        for (auto& c : folded) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c+('a'-'A'));
        const auto found = std::lower_bound(index.begin(),index.end(),folded,[](const auto* resource, const auto& key) { return resource->logical_path < key; });
        const auto* resource = found != index.end() && (*found)->logical_path == folded ? *found : nullptr;
        if (resource && resource->location) { request.location = resource->location; request.catalog_path = resource->logical_path; }
        else { request.diagnostic = "No readable original catalog entry after ASCII lowercase lookup."; complete = false; }
    }
    report.resources_resolved = complete;
}

BootReport inspect_package_boot(const MountedResources& resources, const std::string& path, const BootInputs& input) {
    BootReport report; report.package_path = path; report.inputs = input;
    try {
        validate_boot_inputs(input);
        const auto package = load_package_descriptor(resources,path);
        report.folder = package.name;
        if (package.members.empty() || package.members.size() > 32) fail("Initial package must contain 1..32 members.");
        std::vector<IgbDocument> documents; documents.reserve(package.members.size());
        std::vector<std::string> labels; labels.reserve(package.members.size());
        std::size_t bytes = 0;
        for (const bool preliminary : {true,false}) for (const auto& member : package.members) {
            if ((member.suffix == "/s") != preliminary) continue;
            if (!member.supported) fail(member.diagnostic);
            auto document = read_package_member(resources,member,64u*1024u*1024u);
            if (document.bytes().size() > 256u*1024u*1024u-bytes) fail("Combined initial package bytes exceed 256 MiB.");
            bytes += document.bytes().size(); documents.push_back(std::move(document));
            labels.push_back(path+" / member "+std::to_string(member.index)+" "+member.specifier);
        }
        std::vector<StartupSource> sources;
        for (std::size_t i = 0; i < documents.size(); ++i) sources.push_back({&documents[i],labels[i]});
        report = execute_boot_scripts(sources,package.name,input); report.package_path = path;
        resolve_boot_requests(report,resources.catalog());
    } catch (const std::exception& error) { report.diagnostic = error.what(); }
    return report;
}
} // namespace dw4
