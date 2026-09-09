#include "dw4/controllers.hpp"
#include "script_fixtures.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
using namespace dw4::test;
constexpr ControllerPrerequisites prepared{true, true};
int checks{};
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
IgbObject& object_at(IgbDocument& doc, std::uint32_t ref) { return doc.objects.at(doc.references.at(ref).value_index); }

// Build owned synthetic backing bytes with the existing IGB encoder, then
// describe the controller's decoded field views over those bytes. Controller
// tests operate on decoded graphs; serialized IGB parsing is tested separately.
struct Graph : ScriptFixture {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> rams;
    std::uint32_t ram(bool running = true, std::uint32_t data = null_ref) {
        auto bytes = words({99, running ? 1u : 0u, 17, 18, 19, 20, data});
        const auto storage = memory(std::move(bytes));
        const auto ref = add(lua_object); scalar(ref, 3, storage); rams.emplace_back(ref, storage); return ref;
    }
    std::uint32_t info(bool is_switch, const std::vector<std::uint32_t>& ram_refs, const std::string& label = "Info") {
        const auto ref = add(is_switch ? derived_info : lua_info); text(ref, 2, label);
        scalar(ref, 7, list(lua_list, ram_refs)); return ref;
    }
    void publish(std::vector<std::uint32_t> refs) { exports = std::move(refs); exported_info_list = list(info_list, exports); }
    IgbDocument document() const {
        auto doc = ScriptFixture::document();
        doc.metadata.object_types[lua_info].name = "beStaticInfo";
        doc.metadata.object_types[derived_info].name = "beSwitchCtrlInfo";
        doc.metadata.object_types[lua_list].name = "beBaseInfoRamList";
        doc.metadata.object_types[lua_object].name = "beBaseInfoRam";
        for (auto& object : doc.objects) if (object.type_index == lua_info || object.type_index == derived_info)
            for (auto& field : object.fields) if (field.slot == 7) field.slot = 6;
        for (const auto [ref, storage] : rams) {
            const auto offset = doc.memories.at(doc.references.at(storage).value_index).offset;
            auto& ram_object = object_at(doc, ref);
            ram_object.fields = {{2,1,offset,4},{5,4,offset+4,1},{6,1,offset+8,4},
                {7,1,offset+12,4},{8,1,offset+16,4},{9,1,offset+20,4},{10,2,offset+24,4}};
        }
        return doc;
    }
};
std::vector<StartupInfoAddress> deferred(const Graph& graph, std::uint32_t file = 0) {
    std::vector<StartupInfoAddress> result;
    for (std::uint32_t i = 0; i < graph.exports.size(); ++i) result.push_back({file, i, graph.exports[i]});
    return result;
}
ControllerInitialization prefix(const Graph& graph) {
    const auto doc = graph.document(); const std::array sources{StartupSource{&doc,"synthetic"}};
    return initialize_controller_prefix(sources, deferred(graph), prepared);
}
void actual_effects() {
    Graph graph; const auto data = graph.add(named_object);
    const auto shared = graph.ram(false, data), distinct = graph.ram();
    const auto stat = graph.info(false, {shared, distinct, shared}, "Map");
    const auto sw = graph.info(true, {shared}, "Gate"); graph.publish({stat, sw, stat});
    auto doc = graph.document(); const auto original = std::vector<std::uint8_t>(doc.bytes().begin(),doc.bytes().end());
    const std::array sources{StartupSource{&doc,"first"}};
    auto result = initialize_controller_prefix(sources,deferred(graph),prepared);
    check(result.completed && result.ordered_prefix && result.steps.size()==3 && !result.stopped_deferred_index,
        "Complete supported supplied prefix without claiming whole startup");
    check(result.rams.size()==2 && result.recipients.size()==7 && result.categories.size()==2,
        "RAM identity is unique while every recipient occurrence is retained");
    check(result.rams[0].object==ControllerObjectId{0,shared} && result.rams[0].ram_no==2 &&
        result.rams[0].parent_info==ControllerObjectId{0,stat} && result.rams[0].parent_manager==ControllerKind::static_info,
        "Final alias occurrence overwrites ordinal and parent links");
    check(!result.rams[0].is_run && result.rams[0].state==std::array<std::int32_t,4>{} &&
        result.rams[0].data_list==ControllerObjectId{0,data},
        "Reset native state but preserve false isRun and opaque RAM dataList");
    check(result.recipients[0].name=="Map" && result.recipients[0].instance_index==0 &&
        result.recipients[2].instance_index==2 && result.recipients[3].name=="Gate" && result.recipients[3].instance_index==0,
        "Messenger names and indices retain their registration-time values after RAM alias writes");
    check(result.categories[0].name=="STATICINFO" && result.categories[0].recipient_indices==std::vector<std::uint32_t>{0,1,2,4,5,6} &&
        result.categories[1].name=="SWITCHCTRL" && result.categories[1].recipient_indices==std::vector<std::uint32_t>{3},
        "Reuse first exact messenger category and append ordered recipient rows");
    check(result.managers[0].accepted_infos.size()==2 && result.managers[0].static_data_infos==
        std::vector<ControllerObjectId>(6,{0,stat}) && result.managers[1].accepted_infos.size()==1 &&
        result.managers[1].static_data_infos.empty(),"Static callback appends its info once per RAM occurrence; Switch does not");
    check(result.active_managers==std::vector<std::string>{"beMessenger","beStaticInfoCtl","beSwitchCtrl"} &&
        result.managers[0].is_update_run && result.managers[1].is_update_run,
        "Messenger activates first and controllers activate uniquely in acceptance order");
    check(result.registered_infos==deferred(graph) && result.steps[0].ram_indices==std::vector<std::uint32_t>{0,1,0},
        "Preserve accepted export occurrences and duplicate per-step RAM references");
    check(std::equal(original.begin(),original.end(),doc.bytes().begin()),"Callbacks never mutate original bytes");
    const std::array<std::uint32_t,2> selections{1,0};
    result=initialize_controller_callbacks(sources,deferred(graph),selections,prepared);
    check(result.completed && !result.ordered_prefix && result.active_managers==
        std::vector<std::string>{"beMessenger","beSwitchCtrl","beStaticInfoCtl"},"Explicit selection preserves caller order");
    const std::array aliases{StartupSource{&doc,"first"},StartupSource{&doc,"second"}};
    const std::array addresses{StartupInfoAddress{0,0,stat},StartupInfoAddress{1,0,stat}};
    result=initialize_controller_prefix(aliases,addresses,prepared);
    check(result.completed && result.rams.size()==4 && result.rams[0].object.file_index==0 && result.rams[2].object.file_index==1,
        "Identical document pointers and numeric RAM references in separate loads retain distinct identity");
    doc={};
    check(result.recipients[0].name=="Map" && result.rams[2].data_list==ControllerObjectId{1,data},
        "Controller state owns its data after source destruction");
}
void barriers() {
    Graph graph; const auto ram=graph.ram(); const auto stat=graph.info(false,{ram});
    const auto unknown=graph.add(test_controller); graph.text(unknown,2,"Sound barrier");
    const auto sw=graph.info(true,{ram}); graph.publish({stat,unknown,sw});
    auto result=prefix(graph);
    check(!result.completed && result.stopped_deferred_index==1u && result.steps.size()==2 &&
        result.steps[0].completed && !result.steps[1].completed,"Stop at first unsupported callback");
    check(result.registered_infos==std::vector<StartupInfoAddress>{{0,0,stat}} && result.recipients.size()==1 &&
        result.managers[1].accepted_infos.empty(),"Retain completed prefix without processing a later supported controller");
    auto doc=graph.document(); const std::array sources{StartupSource{&doc,"fixture"}};
    const std::array<std::uint32_t,1> selected{2};
    result=initialize_controller_callbacks(sources,deferred(graph),selected,prepared);
    check(result.completed && result.recipients.front().category=="SWITCHCTRL" && !result.ordered_prefix,
        "Explicit callback inspection is separate from ordered initialization");
    result=initialize_controller_prefix(sources,deferred(graph),{true,false});
    check(!result.completed && result.steps.empty() && result.active_managers.empty(),"Missing messenger prerequisites cannot fabricate initialized state");
    result=initialize_controller_prefix(sources,deferred(graph),{false,true});
    check(!result.completed && result.rams.empty(),"Require prepared system explicitly");
    graph.scalar(sw,7,null_ref); graph.publish({stat,sw}); result=prefix(graph);
    check(!result.completed && result.registered_infos.size()==1 && result.recipients.size()==1 &&
        result.rams[0].parent_info==ControllerObjectId{0,stat},"Null RAM-list failure preserves earlier state");
    graph.scalar(sw,7,graph.list(lua_list,{})); result=prefix(graph);
    check(result.completed && result.recipients.size()==1 && result.managers[1].is_update_run &&
        result.managers[1].accepted_infos.size()==1,"Empty RAM list is accepted and scheduled without messenger rows");
    graph.scalar(sw,7,graph.list(lua_list,{ram,null_ref})); result=prefix(graph);
    check(!result.completed && result.recipients.size()==1 && result.rams[0].parent_manager==ControllerKind::static_info &&
        result.steps.back().ram_indices.empty(),"Malformed later RAM cannot publish any effect from its callback");
    graph.text(stat,2,""); graph.publish({stat}); result=prefix(graph);
    check(result.completed && result.recipients.front().name.empty(),"Null or empty serialized info name registers the empty name");
    graph.text(stat,2,std::string("bad\0name",8)); result=prefix(graph);
    check(!result.completed && result.recipients.empty(),"Embedded NUL name is rejected without guessed C-string equality");
}
void validation() {
    Graph graph; const auto ram=graph.ram(); const auto stat=graph.info(false,{ram}); graph.publish({stat});
    auto doc=graph.document(); std::array sources{StartupSource{&doc,"fixture"}};
    auto addresses=deferred(graph);
    const auto rejected=[&] { const auto result=initialize_controller_prefix(sources,addresses,prepared);
        check(!result.completed && result.registered_infos.empty() && result.rams.empty(),"Reject malformed first callback atomically"); };
    addresses[0].reference_index=ram; rejected(); addresses=deferred(graph);
    addresses[0].export_index=1; rejected(); addresses=deferred(graph);
    addresses[0].file_index=1; rejected(); addresses=deferred(graph);
    doc.metadata.header.byte_order=IgbByteOrder::big; rejected(); doc=graph.document();
    object_at(doc,ram).fields[1].type_index=1; rejected(); doc=graph.document();
    object_at(doc,ram).fields[1].size=4; rejected(); doc=graph.document();
    object_at(doc,ram).fields.pop_back(); rejected(); doc=graph.document();
    doc.metadata.object_types[lua_object].name="DerivedBaseInfoRam"; rejected(); doc=graph.document();
    doc.metadata.object_types[lua_info].name="DerivedStaticInfo"; rejected(); doc=graph.document();
    doc.info_list_reference.reset(); rejected(); doc=graph.document();
    sources[0].document=nullptr; rejected(); sources[0].document=&doc;
    const std::vector<StartupSource> too_many_sources(129,{&doc,"alias"});
    check(!initialize_controller_prefix(too_many_sources,addresses,prepared).completed,"Bound loaded source occurrences");
    const std::vector<StartupInfoAddress> too_many_rows(65537,addresses[0]);
    check(!initialize_controller_prefix(sources,too_many_rows,prepared).completed,"Bound deferred rows before allocating selection order");
    const std::vector<std::uint32_t> too_many_selections(257,0);
    check(!initialize_controller_callbacks(sources,addresses,too_many_selections,prepared).completed,"Bound explicit selection count");
    const std::array<std::uint32_t,1> invalid_selection{1};
    check(!initialize_controller_callbacks(sources,addresses,invalid_selection,prepared).completed,"Reject out-of-range selected index");
    auto& payload=graph.records[graph.rams.front().second].data; payload[4]=2; doc=graph.document(); rejected(); payload[4]=1;
    graph.scalar(stat,7,graph.list(lua_list,std::vector<std::uint32_t>(257,ram))); doc=graph.document();
    const std::vector<std::uint32_t> visits(256,0);
    const auto bounded=initialize_controller_callbacks(sources,addresses,visits,prepared);
    check(!bounded.completed && bounded.recipients.size()==65535 && bounded.registered_infos.size()==255,
        "Combined repeated RAM visits stop before the next callback exceeds the budget");
    graph.text(stat,2,std::string(4096,'n'));
    graph.scalar(stat,7,graph.list(lua_list,std::vector<std::uint32_t>(2048,ram))); doc=graph.document();
    const auto text_limited=initialize_controller_prefix(sources,addresses,prepared);
    check(!text_limited.completed && text_limited.recipients.empty() && text_limited.diagnostic.find("budget")!=std::string::npos,
        "Bound copied recipient names before committing an oversized callback");
    graph.text(stat,2,"Info");
    const auto ram_list=graph.list(lua_list,{ram}); graph.scalar(stat,7,ram_list);
    graph.scalar(ram_list,3,65537); doc=graph.document(); rejected();
    graph.scalar(ram_list,3,0); doc=graph.document(); rejected();
    graph.scalar(ram_list,3,1); graph.scalar(ram_list,4,null_ref); doc=graph.document(); rejected();
    graph.scalar(stat,7,graph.list(lua_list,{stat})); doc=graph.document(); rejected();
    graph.scalar(stat,7,graph.list(lua_list,{ram}));
    graph.publish(std::vector<std::uint32_t>(4096,stat)); doc=graph.document();
    const std::vector<StartupSource> many_sources(17,{&doc,"same file"});
    std::vector<StartupInfoAddress> many_addresses;
    for(std::uint32_t i=0;i<17;++i) many_addresses.push_back({i,0,stat});
    const auto export_limited=initialize_controller_prefix(many_sources,many_addresses,prepared);
    check(!export_limited.completed && export_limited.registered_infos.size()==16 && export_limited.rams.size()==16,
        "Aggregate export bound counts repeated file loads and retains completed callbacks");
    graph.publish(std::vector<std::uint32_t>(4097,stat)); doc=graph.document(); addresses={{0,0,stat}}; rejected();
}
void session_adapter() {
    Graph graph; const auto stat=graph.info(false,{graph.ram()}); graph.publish({stat});
    StartupSession session;
    session.files.push_back({StartupFileStage::preliminary,0,"synthetic",{}, {},graph.document(),{}});
    session.files_loaded=session.info_routing_complete=true;
    const std::array sources{StartupSource{&session.files[0].document,"synthetic"}};
    session.infos=route_startup_infos(sources);
    auto result=initialize_controller_prefix(session,prepared);
    check(result.completed && result.registered_infos.size()==1 && !session.boot.has_live_state(),
        "Session adapter inspects aggregate routes without requiring or fabricating a boot execution");
    session.infos.deferred_infos[0].reference_index=0;
    if(stat==0) session.infos.deferred_infos[0].export_index=1;
    result=initialize_controller_prefix(session,prepared);
    check(!result.completed && result.steps.empty(),"Revalidate aggregate session routes instead of trusting its flags");
    session.infos=route_startup_infos(sources); session.files_loaded=false;
    check(!initialize_controller_prefix(session,prepared).completed,"Reject an incomplete loaded session");
}
} // namespace
int main() {
    try { actual_effects(); barriers(); validation(); session_adapter();
        std::cout<<"Controller tests passed: "<<checks<<" checks\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"Controller test failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
