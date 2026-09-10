#include "dw4/action_event_runtime.hpp"
#include "dw4/database_runtime.hpp"
#include "dw4/game_ram.hpp"
#include "dw4/resources.hpp"
#include "database_fixture.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool condition,const char* message) {++checks;if(!condition)throw std::runtime_error(message);}
MessengerDispatch skill(ActionEventRuntime& runtime,std::int32_t source,std::int32_t kind,
    std::int32_t first=0,std::int32_t second=0) {
    auto& messenger=runtime.messenger();messenger.select("GAMERAM","gameram",0);
    messenger.buffer().add_string("P2ESKILLUP");
    for(const auto value:{source,kind,first,second})messenger.buffer().add_int(value);
    return messenger.begin_send("FIGHTCALC");
}
void prepare_row(const std::shared_ptr<GameDataList>& row,std::int32_t level,std::int32_t selected,std::int32_t value) {
    row->require<GameDataInt>("LEVEL")->data=level;
    row->require<GameDataInt>("WEAPONSKILLNO")->data=selected;
    row->require<GameDataIntList>("SKILL")->data->at(static_cast<std::size_t>(selected))=value;
}
void waza(const std::shared_ptr<GameDataList>& row,std::int32_t base,std::int32_t record) {
    row->require<GameDataObjectList>("WAZALIST")->data->push_back(std::make_shared<GameDataWaza>(GameDataWaza{9,record,0,base}));
}
database_fixture::Fixture fixture() {
    database_fixture::Fixture value;value.text(value.info,2,"pcTech");value.chars({"SKILL"},{6,7,8});return value;
}
void ordinary_gates() {
    constexpr std::array levels{std::pair{-1,-30},std::pair{0,0},std::pair{1,30},std::pair{36,1080},
        std::pair{37,1100},std::pair{107,2500},std::pair{108,2510},std::pair{227,3700},std::pair{228,3708},
        std::pair{std::numeric_limits<std::int32_t>::min(),0},std::pair{std::numeric_limits<std::int32_t>::max(),1876}};
    for(const auto [level,ceiling]:levels)for(std::int32_t source=0;source<4;++source)for(const auto difference:{-1,0,1}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);const auto row=game.state().player(source);
        prepare_row(row,level,5,ceiling+difference);const auto values=*row->require<GameDataIntList>("SKILL")->data;
        auto send=skill(runtime,source,60000,0,0);const NativeMessenger& messenger=runtime.messenger();
        const auto result=runtime.resume(send);
        check(result.stop==(difference<0?MessengerStop::service_required:MessengerStop::completed),"Signed level ceiling or selected weapon skill comparison differs.");
        check(!game.faulted() && game.pending_count()==(difference<0?1u:0u) &&
            *row->require<GameDataIntList>("SKILL")->data==values && messenger.buffer().integers()==std::vector<std::int32_t>{source,60000,0,0} &&
            messenger.buffer().strings()==std::vector<std::optional<std::string>>{"P2ESKILLUP"} && messenger.buffer().objects().empty(),
            "Skill gate changed values, argument lists or its retained ownership.");
        // Once the below-ceiling prefix committed, retry may not reread a
        // changed level/skill and incorrectly turn a pending growth into a no-op.
        row->require<GameDataInt>("LEVEL")->data=0;
        row->require<GameDataIntList>("SKILL")->data->at(5)=10000;
        check(runtime.resume(send).stop==result.stop,"Retry replayed the committed skill selection/ceiling phase.");
    }
    for(const auto kind:{-40000,-80000,-90000,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto send=skill(runtime,0,kind,255,-1);
        check(runtime.resume(send).stop==MessengerStop::completed,"Signed category division incorrectly required a magic technique.");
    }
    for(const auto flags:std::array{std::pair{0,0},std::pair{1,0},std::pair{0,1},std::pair{-1,2}}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);prepare_row(game.state().player(0),1,0,0);
        auto send=skill(runtime,0,0,flags.first,flags.second);
        check(runtime.resume(send).stop==MessengerStop::service_required,"An input flag bypassed actual skill selection/probability prerequisites.");
    }
}
void database_selection_and_cache() {
    for(const auto category:{4,8,9})for(std::int32_t source=0;source<4;++source) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);DatabaseRuntime databases(runtime);auto sample=fixture();
        databases.register_source(decode_database_managers(sample.document(),"synthetic/pcTech.igb",7));
        const auto database=databases.sources()[0].infos()[0].database;
        const auto row=game.state().player(source);prepare_row(row,1,7,30);
        waza(row,40,1);waza(row,40,2); // First matching base wins, before kind/index aliases.
        auto send=skill(runtime,source,category*10000+40);check(runtime.resume(send).stop==MessengerStop::completed,
            "Magic selection did not use the first Waza record and signed pcTech SKILL field.");
        auto& messenger=runtime.messenger();check(messenger.selected().category=="DATABASE" && messenger.selected().name=="pcTech" &&
            messenger.selected().command=="GETDATABASE" && messenger.buffer().integers().empty() && messenger.buffer().strings().empty() &&
            messenger.buffer().objects().size()==1 && std::get<MessengerDatabase>(messenger.buffer().objects()[0])==database &&
            messenger.buffer_depth()==0 && game.pending_count()==0,"pcTech fetch restored private arguments, copied its database or retained a completed phase.");
        // Original manager+24 is now cached. No new selection/GETDATABASE or
        // buffer clearing occurs, even if the registered database RAM goes inactive.
        databases.set_active({7,sample.rams[0]},0);
        auto cached=skill(runtime,source,category*10000+40,1,1);
        check(runtime.resume(cached).stop==MessengerStop::completed && messenger.selected().category=="GAMERAM" &&
            messenger.buffer().objects().empty() && messenger.buffer().integers()==std::vector<std::int32_t>{source,category*10000+40,1,1},
            "Completed pcTech cache was refreshed or reselected instead of retaining the actual database owner.");
    }
}
void suspended_query_and_nested_reset() {
    ActionEventRuntime runtime;GameRamRuntime game(runtime);auto sample=fixture();
    auto database=std::make_shared<const DatabaseDefinition>(sample.definition());
    auto row=game.state().player(2);prepare_row(row,1,7,30);waza(row,40,1);
    const auto endpoint=ControllerObjectId::allocate_native();runtime.register_recipient(endpoint,"DATABASE","pcTech",0);
    auto send=skill(runtime,2,80040);const NativeMessenger& messenger=runtime.messenger();
    check(runtime.resume(send).stop==MessengerStop::service_required && game.pending_count()==1 &&
        messenger.selected().category=="DATABASE" && messenger.buffer().integers().empty(),"Missing DB provider did not retain its exact nested request.");
    unsigned calls{},effects{};bool ready{};
    runtime.register_native_controller(endpoint,{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&] {return MessengerManager{[&](NativeMessenger& live,const MessengerDelivery&) {
            if(++calls==1) {
                ++effects;live.select("GAMERAM","gameram",0);live.buffer().add_int(2);
                auto reset=live.begin_send("GAMEDATAINIT");check(runtime.resume(reset).stop==MessengerStop::completed,"Nested original row reset failed.");
                // Prefix fields were captured before this nested operation;
                // changing old LEVEL must not recompute the already-read ceiling.
                row->require<GameDataInt>("LEVEL")->data=100;
                row->require<GameDataObjectList>("WAZALIST")->data->clear();
            }
            if(!ready)return MessengerCallResult{false,0,"database callback pending"};
            live.buffer().add_database(database);return MessengerCallResult{true,1,{}};
        },{}};}});
    check(runtime.resume(send).stop==MessengerStop::service_required && calls==1 && effects==1 &&
        game.state().player(2)!=row,"Nested reset did not replace the live player row before DB completion.");
    ready=true;
    check(runtime.resume(send).stop==MessengerStop::completed && calls==2 && effects==1 && game.pending_count()==0 &&
        messenger.buffer().integers()==std::vector<std::int32_t>{2} && std::get<MessengerDatabase>(messenger.buffer().objects()[0])==database,
        "Database retry reparsed overwritten arguments, recomputed the ceiling, rematched Waza or used the replacement row.");
    check(runtime.resume(send).stop==MessengerStop::completed && calls==2,"Completed outer skill delivery replayed the nested database callback.");
}
void malformed_and_missing_prerequisites() {
    for(unsigned count=0;count<4;++count) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto& messenger=runtime.messenger();
        messenger.select("GAMERAM","gameram",0);messenger.buffer().add_string("P2ESKILLUP");
        for(unsigned i=0;i<count;++i)messenger.buffer().add_int(0);
        auto send=messenger.begin_send("FIGHTCALC");check(runtime.resume(send).stop==MessengerStop::faulted && game.faulted(),"Missing skill argument invented a zero/default flag.");
    }
    for(const auto index:{-1,4}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto send=skill(runtime,index,0);
        check(runtime.resume(send).stop==MessengerStop::faulted,"Skill ceiling gate bypassed invalid source-row lookup.");
    }
    for(const auto index:{-1,9}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);game.state().player(0)->require<GameDataInt>("WEAPONSKILLNO")->data=index;
        auto send=skill(runtime,0,0);check(runtime.resume(send).stop==MessengerStop::faulted,"Zero LEVEL bypassed invalid selected SKILL access.");
    }
    for(const auto category:{4,8,9}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto send=skill(runtime,0,category*10000);
        const auto result=runtime.resume(send);check(result.stop==MessengerStop::service_required && !game.faulted() &&
            result.diagnostic.find("WAZALIST")!=std::string::npos,"Empty WAZALIST fabricated technique zero or skipped the LEVEL0 lookup.");
    }
    for(unsigned bad=0;bad<5;++bad) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);const auto row=game.state().player(0);
        if(bad==0)row->require<GameDataObjectList>("WAZALIST")->data->push_back(std::monostate{});
        else if(bad==1)row->require<GameDataObjectList>("WAZALIST")->data->push_back(std::make_shared<GameDataDisk>());
        else {
            waza(row,0,0);const auto id=ControllerObjectId::allocate_native();runtime.register_recipient(id,"DATABASE","pcTech",0);
            runtime.set_external_services({{},[&,bad](ControllerObjectId)->MessengerManager {
                return {[&,bad](NativeMessenger& live,const MessengerDelivery&) {
                    if(bad==3)live.buffer().add_data_list(row);
                    if(bad==4)live.buffer().add_object({});
                    return MessengerCallResult{true,1,{}};
                },{}};
            }});
        }
        auto send=skill(runtime,0,80000);check(runtime.resume(send).stop==MessengerStop::faulted && game.faulted(),
            "Malformed Waza or completed missing/wrong/null pcTech result fabricated a skill value.");
        check(runtime.resume(send).stop==MessengerStop::faulted,"Uncertain skill manager was replayed after a terminal checked error.");
    }
}
void original_database(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto doc=read_igb_document(resources.read("dng101b1/separate/usa_pack.igb"));
    for(std::int32_t source=0;source<4;++source)for(const auto technique:std::array{std::pair{0,6},std::pair{40,7},std::pair{65,8},std::pair{80,8}}) {
        ActionEventRuntime events;GameRamRuntime game(events);DatabaseRuntime databases(events);
        databases.register_source(decode_database_managers(doc,"dng101b1/separate/usa_pack.igb",8));
        // Explicit prepared helper inputs derived from actual tables; this is
        // not execution of GAMESTART or its six-group stat calculation.
        const auto row=game.state().player(source);prepare_row(row,1,technique.second,30);waza(row,technique.first,technique.first);
        auto send=skill(events,source,80000+technique.first);check(events.resume(send).stop==MessengerStop::completed,"Actual pcTech database did not resolve prepared technique skill gate.");
        const auto& object=std::get<MessengerDatabase>(events.messenger().buffer().objects()[0]);
        check(object->owner_reference()==1592 && object->integer(technique.first,"SKILL")==technique.second &&
            object->row_count()==100 && row->require<GameDataIntList>("SKILL")->data->at(technique.second)==30,
            "Actual pcTech provenance, skill column or original no-growth result differs.");
    }
}
}
int main(int argc,char** argv) {
    try {
        ordinary_gates();database_selection_and_cache();suspended_query_and_nested_reset();malformed_and_missing_prerequisites();
        const auto synthetic=checks;
        if(argc==2)original_database(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_game_ram_skill_tests [EXTRACTED_ROOT]");
        std::cout<<"GAMERAM skill checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"GAMERAM skill after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
