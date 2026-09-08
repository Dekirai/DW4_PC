#include "dw4/startup_loading.hpp"
#include "script_fixtures.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
using namespace dw4;
using namespace dw4::test;
int checks{};
void check(bool condition, const char* reason) {
    ++checks; if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F&& action, const char* reason) {
    ++checks; try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(reason);
}
struct Graph : ScriptFixture {
    void publish(const std::vector<std::uint32_t>& refs) {
        exports = refs;
        if (exported_info_list == null_ref) exported_info_list = list(info_list,refs);
        else contents(exported_info_list,refs);
    }
    std::uint32_t named(std::uint32_t type, const std::string& label) {
        const auto ref = add(type); text(ref,2,label); return ref;
    }
    std::uint32_t scene(const std::string& root_name, bool has_camera = false) {
        const auto root = named(node,root_name);
        const auto ref = named(scene_info,"Scene"); scalar(ref,5,root);
        if (has_camera) {
            const auto camera_ref = named(camera,"Camera");
            scalar(ref,7,list(camera_list,{camera_ref}));
        }
        return ref;
    }
    Bytes bytes() const {
        const auto doc = document(); return {doc.bytes().begin(),doc.bytes().end()};
    }
};
StartupInfoRoutes route(const IgbDocument& document) {
    const std::array sources{StartupSource{&document,"synthetic"}};
    return route_startup_infos(sources);
}
void routing() {
    Graph graph;
    const auto late = graph.scene("ROOTATTR"); // Root is legitimate reference zero.
    const auto controller = graph.named(test_controller,"Controller");
    const auto held = graph.scene("rootattr");
    const auto info = graph.add_info({});
    graph.publish({late,controller,held,info.reference,controller});
    auto document = graph.document(); const auto bytes = graph.bytes();
    const auto routes = route(document);
    check(routes.lua_infos == std::vector<StartupInfoAddress>{{0,3,info.reference}},"Route exact exported Lua info immediately");
    check(routes.held_scenes == std::vector<StartupInfoAddress>{{0,2,held}},"ROOTATTR matching is exact and case sensitive");
    check(routes.late_scenes == std::vector<StartupInfoAddress>{{0,0,late}},"Accept named graph at reference zero");
    check(routes.deferred_infos == std::vector<StartupInfoAddress>{{0,1,controller},{0,4,controller},{0,0,late}},
        "Preserve repeated ordinary exports and append late scenes after all ordinary entries");
    check(std::equal(bytes.begin(),bytes.end(),document.bytes().begin(),document.bytes().end()),"Routing never changes serialized input");
    const std::array repeated{StartupSource{&document,"first"},StartupSource{&document,"second"}};
    const auto twice = route_startup_infos(repeated);
    check(twice.lua_infos == std::vector<StartupInfoAddress>{{0,3,info.reference},{1,3,info.reference}},
        "The same local reference in another source retains its file identity");
    check(twice.deferred_infos == std::vector<StartupInfoAddress>{{0,1,controller},{0,4,controller},
        {1,1,controller},{1,4,controller},{0,0,late},{1,0,late}},"Late scenes append only after every source's ordinary exports");
    Graph cameras;
    const auto camera_list_zero = cameras.list(camera_list,{null_ref});
    const auto camera_scene = cameras.named(scene_info,"Count-only routing");
    cameras.scalar(camera_scene,7,camera_list_zero); cameras.publish({camera_scene});
    check(camera_list_zero == 0 && route(cameras.document()).late_scenes.size() == 1,
        "Nonempty camera list at reference zero routes late without dereferencing a null graph");
    cameras.contents(camera_list_zero,{});
    rejects([&]{route(cameras.document());},"Empty cameras with null graph cannot establish original root-name routing");
    auto malformed = graph; malformed.scalar(late,5,null_ref);
    rejects([&]{route(malformed.document());},"Null cameras and null graph fail closed");
    malformed = graph; malformed.scalar(late,5,controller);
    rejects([&]{route(malformed.document());},"Reject non-node scene root");
    malformed = graph; malformed.scalar(late,7,controller);
    rejects([&]{route(malformed.document());},"Reject non-list camera field");
    cameras.scalar(camera_list_zero,2,2);
    rejects([&]{route(cameras.document());},"Validate camera list count/capacity before routing");
    cameras = Graph{};
    const auto oversized_list = cameras.list(camera_list,{});
    const auto oversized_scene = cameras.scene("ROOTATTR"); cameras.scalar(oversized_scene,7,oversized_list);
    cameras.scalar(oversized_list,3,65537); cameras.publish({oversized_scene});
    rejects([&]{route(cameras.document());},"Bound camera-list capacity before expanding references");
    malformed = graph; malformed.records[late].type = derived_scene;
    const auto derived = route(malformed.document());
    check(derived.late_scenes.empty() && derived.deferred_infos.front() == StartupInfoAddress{0,0,late},
        "A derived scene type receives no exact-scene special routing");
    malformed = graph; malformed.publish({controller});
    check(route(malformed.document()).late_scenes.empty() && route(malformed.document()).lua_infos.empty(),
        "Hidden scene and Lua objects are not scanned into routing lists");
    malformed = graph; malformed.publish({null_ref});
    rejects([&]{route(malformed.document());},"Reject null exported info");
    malformed = graph; malformed.publish({0});
    rejects([&]{route(malformed.document());},"Reject a node exported as info");
    malformed = graph; malformed.exported_info_list = null_ref;
    rejects([&]{route(malformed.document());},"Missing explicit export list has no object-scan fallback");
    malformed = graph;
    malformed.records[0].values[2] = Bytes(1024u*1024u+4); word(malformed.records[0].values[2],0,1024u*1024u);
    rejects([&]{route(malformed.document());},"Reject huge padded root name before scanning trimmed text");
    malformed = graph; malformed.text(0,2,std::string("ROOTATTR\0suffix",15));
    rejects([&]{route(malformed.document());},"Reject embedded-NUL root name instead of guessing C-string equality");
    const std::array null_sources{StartupSource{nullptr,"null"}};
    rejects([&]{route_startup_infos(null_sources);},"Reject null source document");
    const std::vector<StartupSource> too_many(129,{&document,"alias"});
    rejects([&]{route_startup_infos(too_many);},"Bound source occurrences even for shared document pointers");
    Graph rows; const auto repeated_info = rows.named(test_controller,"Controller");
    rows.publish(std::vector<std::uint32_t>(4096,repeated_info)); const auto row_doc = rows.document();
    const std::vector<StartupSource> expanded(17,{&row_doc,"alias"});
    rejects([&]{route_startup_infos(expanded);},"Bound cumulative expanded exports across source aliases");
    Graph names; const auto padded = names.named(test_controller,std::string(4096,'n'));
    names.publish(std::vector<std::uint32_t>(1000,padded)); const auto name_doc = names.document();
    const std::array large_names{StartupSource{&name_doc,"A"},StartupSource{&name_doc,"B"},StartupSource{&name_doc,"C"}};
    rejects([&]{route_startup_infos(large_names);},"Bound repeated exported name copies across documents");
    names.records[padded].values[2] = Bytes(4100); word(names.records[padded].values[2],0,4096);
    const auto padded_doc = names.document();
    const std::array padded_sources{StartupSource{&padded_doc,"A"},StartupSource{&padded_doc,"B"},StartupSource{&padded_doc,"C"}};
    rejects([&]{route_startup_infos(padded_sources);},"Charge serialized padded export names even when decoded names are empty");
    Graph shared_cameras;
    const auto shared_list = shared_cameras.list(camera_list,std::vector<std::uint32_t>(65536,null_ref));
    const auto shared_scene = shared_cameras.named(scene_info,"Shared camera list");
    shared_cameras.scalar(shared_scene,7,shared_list); shared_cameras.publish({shared_scene});
    const auto camera_doc = shared_cameras.document();
    const std::vector<StartupSource> camera_aliases(17,{&camera_doc,"alias"});
    rejects([&]{route_startup_infos(camera_aliases);},"Bound cumulative camera-list traversal across repeated sources");
}

// Independent small Lua 4 encoder. Only synthetic globals and CFL calls are used.
struct Code {
    std::vector<std::string> strings;
    std::vector<std::uint32_t> instructions;
    void string_op(std::uint32_t opcode, std::string text) {
        const auto index = static_cast<std::uint32_t>(strings.size()); strings.push_back(std::move(text));
        instructions.push_back(opcode | (index << 6));
    }
    void global(const std::string& key, const std::string& value) { string_op(7,value); string_op(19,key); }
    void queue(const std::string& path) {
        string_op(12,"be"); string_op(16,"CFLsetName"); string_op(7,path); instructions.push_back(2);
    }
    Bytes bytes() const {
        Bytes result{0x1b,'L','u','a',0x40,1,4,4,4,32,6,9,8};
        const auto probe = std::bit_cast<std::uint64_t>(314159265.3589793);
        for (unsigned i = 0; i < 8; ++i) result.push_back(static_cast<std::uint8_t>(probe >> (i*8)));
        append(result,words({0,0,0})); result.push_back(0); append(result,words({16,0,0,static_cast<std::uint32_t>(strings.size())}));
        for (const auto& text : strings) { push32(result,static_cast<std::uint32_t>(text.size()+1)); name(result,text); }
        append(result,words({0,0,static_cast<std::uint32_t>(instructions.size()+1)}));
        for (auto value : instructions) push32(result,value); push32(result,0); return result;
    }
};
struct LoaderFixture {
    ResourceCatalog catalog;
    std::map<std::uint32_t,Bytes> assets;
    std::vector<std::pair<ResourceLocation,std::size_t>> reads;
    Graph descriptor, preliminary, ordinary, shared, other;
    std::uint32_t filenames{}, held{}, shared_late{}, shared_controller{}, other_camera{};
    LoaderFixture() {
        catalog.archives.push_back({"synthetic.afs",5});
        catalog.resources = {{"test.igb",0,ResourceLocation{0,0}},{"shared.igb",0,ResourceLocation{0,3}},
            {"alias.igb",0,ResourceLocation{0,3}},{"other.igb",0,ResourceLocation{0,4}}};
        const auto first_name = descriptor.named(named_object,"[0.2]");
        const auto second_name = descriptor.named(named_object,"[0.1]/s");
        filenames = descriptor.list(named_list,{first_name,second_name});
        const auto package = descriptor.named(file_info,"SyntheticFolder");
        descriptor.text(package,5,"DifferentSection"); descriptor.scalar(package,4,1); descriptor.scalar(package,6,filenames);
        descriptor.publish({package});
        Code functions; functions.global("retained","functions ran");
        preliminary.add_info({{"func.lua",functions.bytes()}});
        Code boot; boot.queue("Shared.igb"); boot.queue("alias.igb"); boot.queue("other.igb");
        ordinary.add_info({{"boot.lua",boot.bytes()}});
        held = ordinary.scene("BG01"); ordinary.publish({ordinary.exports[0],held});
        shared_late = shared.scene("ROOTATTR"); shared_controller = shared.named(test_controller,"Shared controller");
        shared.publish({shared_late,shared_controller});
        other_camera = other.scene("BG01",true);
        Code later; later.global("unexpected","must not execute");
        const auto late_info = other.add_info({{"func.lua",later.bytes()},{"boot.lua",later.bytes()}});
        other.publish({other_camera,late_info.reference});
        refresh();
    }
    void refresh() { assets = {{0,descriptor.bytes()},{1,preliminary.bytes()},{2,ordinary.bytes()},{3,shared.bytes()},{4,other.bytes()}}; }
    StartupAssetReader reader() {
        return [this](ResourceLocation at, std::size_t cap) {
            reads.emplace_back(at,cap);
            if (at.archive_index != 0) throw std::runtime_error("Unexpected synthetic archive");
            const auto& bytes = assets.at(at.entry_index);
            if (bytes.size() > cap) throw std::runtime_error("Synthetic reader cap exceeded");
            return bytes;
        };
    }
    StartupSession load() { return load_startup_session(catalog,reader(),"test.igb",{}); }
};
void loading() {
    LoaderFixture f; auto session = f.load();
    check(session.files_loaded && session.info_routing_complete && session.diagnostic.empty(),"Load and route complete synthetic initial/common sources");
    check(session.files.size() == 5 && f.reads.size() == 6,"Descriptor is not counted as a runtime source");
    const std::array<std::uint32_t,6> expected_reads{0,1,2,3,3,4};
    for (std::size_t i = 0; i < expected_reads.size(); ++i)
        check(f.reads[i].first == ResourceLocation{0,expected_reads[i]},"Read preliminary, ordinary and common FIFO occurrences in original order");
    check(f.reads[0].second == 4u*1024u*1024u && f.reads[1].second == 64u*1024u*1024u,
        "Supply independent descriptor and payload read caps before callback allocation");
    check(session.files[0].stage == StartupFileStage::preliminary && session.files[0].ordinal == 1 &&
        session.files[1].stage == StartupFileStage::package && session.files[1].ordinal == 0,
        "Keep original member ordinals after stage-order loading");
    check(session.files[2].stage == StartupFileStage::common && session.files[2].ordinal == 0 &&
        session.files[2].requested_path == "Shared.igb" && session.files[2].catalog_path == "shared.igb",
        "Keep original queue spelling separately from folded catalog identity");
    check(session.files[2].location == session.files[3].location && session.files[3].ordinal == 1,
        "Distinct requested aliases remain distinct owned file occurrences");
    check(session.files[2].document.bytes().data() != session.files[3].document.bytes().data(),
        "Aliased files own independent serialized buffers");
    check(session.infos.held_scenes == std::vector<StartupInfoAddress>{{1,1,f.held}},"Hold the ordinary non-camera scene separately");
    check(session.infos.late_scenes == std::vector<StartupInfoAddress>{{2,0,f.shared_late},{3,0,f.shared_late},{4,0,f.other_camera}},
        "Late scenes retain qualified aliases and cross-file source order");
    check(session.infos.deferred_infos == std::vector<StartupInfoAddress>{{2,1,f.shared_controller},{3,1,f.shared_controller},
        {2,0,f.shared_late},{3,0,f.shared_late},{4,0,f.other_camera}},"Append late scenes after all ordinary controller exports");
    check(session.infos.lua_infos.size() == 3,"Common Lua infos appear in routed ownership without rerunning func/boot");
    check(session.boot.report().boot_scripts_completed && session.boot.report().resources_resolved && session.boot.has_live_state(),
        "Retain actual successful boot runtime after byte loading");
    check(session.boot.global("retained").as_string() == "functions ran" && session.boot.global("unexpected").type() == Lua4Type::Nil,
        "Keep executed globals while leaving newly loaded func/boot chunks unexecuted");
    check(session.boot.report().folder == "SyntheticFolder" && !session.next_barrier.empty(),
        "Use inherited package name and retain the pending native initialization boundary");
    std::size_t total = 0, objects = 0, fields = 0;
    for (const auto& file : session.files) {
        total += file.document.bytes().size(); objects += file.document.objects.size();
        for (const auto& entry : file.document.entries) fields += entry.fields.size();
        for (const auto& object : file.document.objects) fields += object.fields.size();
    }
    check(session.total_bytes == total && session.object_count == objects && session.field_count == fields,
        "Account for every owned source occurrence and decoded object/directory field");
    f.assets.clear(); auto moved = std::move(session);
    check(moved.boot.global("retained").as_string() == "functions ran" && !moved.files[4].document.bytes().empty(),
        "Moved session survives destruction of external reader buffers");

    LoaderFixture missing; missing.assets.erase(4); auto failed = missing.load();
    check(!failed.files_loaded && !failed.info_routing_complete && failed.infos.deferred_infos.empty() && failed.infos.lua_infos.empty(),
        "Failed common read cannot publish complete routing or stale lists");
    check(failed.files.size() == 4 && failed.boot.has_live_state() && failed.diagnostic.find("other.igb") != std::string::npos,
        "Keep completed file prefix and executed boot state with failing request provenance");
    LoaderFixture bad_scene; bad_scene.shared.scalar(bad_scene.shared_late,5,null_ref); bad_scene.refresh();
    failed = bad_scene.load();
    check(failed.files_loaded && !failed.info_routing_complete && failed.infos.late_scenes.empty(),
        "Decoded payload completion stays distinct from failed final routing");
    LoaderFixture bad_initial; bad_initial.ordinary.scalar(bad_initial.held,5,null_ref); bad_initial.refresh();
    failed = bad_initial.load();
    check(!failed.files_loaded && !failed.info_routing_complete && !failed.boot.has_live_state() && bad_initial.reads.size() == 3,
        "Validate initial scene ownership before executing boot or reading common data");
    LoaderFixture unresolved; unresolved.catalog.resources.pop_back(); failed = unresolved.load();
    check(!failed.files_loaded && failed.files.size() == 2 && !failed.boot.report().resources_resolved,
        "Do not read a partially resolved or forged common queue");
    LoaderFixture bad_export; bad_export.other.publish({0}); bad_export.refresh(); failed = bad_export.load();
    std::size_t prefix_bytes = 0, prefix_objects = 0, prefix_fields = 0;
    for (const auto& file : failed.files) {
        prefix_bytes += file.document.bytes().size(); prefix_objects += file.document.objects.size();
        for (const auto& entry : file.document.entries) prefix_fields += entry.fields.size();
        for (const auto& object : file.document.objects) prefix_fields += object.fields.size();
    }
    check(!failed.files_loaded && failed.files.size() == 4 && failed.total_bytes == prefix_bytes &&
        failed.object_count == prefix_objects && failed.field_count == prefix_fields,
        "A rejected decoded file cannot increment counters for the retained successful prefix");
    LoaderFixture over_cap;
    failed = load_startup_session(over_cap.catalog,[](ResourceLocation, std::size_t cap) { return Bytes(cap+1); },"test.igb",{});
    check(!failed.files_loaded && failed.files.empty() && failed.diagnostic.find("byte limit") != std::string::npos,
        "Reject a reader that violates its cap before parsing returned bytes");
    failed = load_startup_session(over_cap.catalog,{},"test.igb",{});
    check(!failed.files_loaded && !failed.diagnostic.empty(),"Reject absent reader without claiming work completed");
    failed = load_startup_session(over_cap.catalog,over_cap.reader(),"TEST.igb",{});
    check(!failed.files_loaded && over_cap.reads.empty(),"Descriptor lookup is exact and performs no guessed fallback");
    LoaderFixture many;
    const auto repeated_member = many.descriptor.named(named_object,"[0.1]/s");
    many.descriptor.contents(many.filenames,std::vector<std::uint32_t>(33,repeated_member)); many.refresh(); failed = many.load();
    check(!failed.files_loaded && many.reads.size() == 1,"Bound initial member count before loading any member");
}
} // namespace
int main() {
    try { routing(); loading(); std::cout << "Startup loading tests passed: " << checks << " checks\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "Startup loading test failure after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
