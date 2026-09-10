#include "dw4/action_event_runtime.hpp"
#include "dw4/database_runtime.hpp"
#include "dw4/game_ram.hpp"
#include "dw4/resources.hpp"
#include "database_fixture.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected,reason);
}
auto items(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataObjectList>(name)->data;}
auto ints(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataIntList>(name)->data;}
std::int32_t value(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataInt>(name)->data;}
void put(const std::shared_ptr<GameDataList>& row,const char* name,std::int32_t number) {row->require<GameDataInt>(name)->data=number;}
struct Databases {
    MessengerDatabase shinka,pc,weapon;
    Databases() {
        database_fixture::Fixture species;species.ints({"MHP_INI","MMP_INI"},{100,50});
        shinka=std::make_shared<const DatabaseDefinition>(species.definition());
        database_fixture::Fixture technique;technique.chars({"SKILL"},{6});
        pc=std::make_shared<const DatabaseDefinition>(technique.definition());
        database_fixture::Fixture equipment;equipment.ints({"MAXHP","MAXMP"},{7,2});
        equipment.strings({"MODEL","SHUSEINOURYOKU","ZOKUSEI"},{"test_weapon","MAMORI","fire"});
        weapon=std::make_shared<const DatabaseDefinition>(equipment.definition());
    }
};
using Service=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,const char* name,Service service) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [service=std::move(service)]{return MessengerManager{service,{}};}},"DATABASE",name,0);
}
void base_providers(ActionEventRuntime& events,const Databases& db,std::map<std::string,unsigned>& calls) {
    for(const auto& [name,owner]:std::array{std::pair{"shinka",db.shinka},std::pair{"pcTech",db.pc}})
        provider(events,name,[&,name,owner](NativeMessenger& messenger,const MessengerDelivery&){
            ++calls[name];messenger.buffer().add_database(owner);return MessengerCallResult{true,1,{}};
        });
}
MessengerDispatch packet(ActionEventRuntime& events,std::int32_t source,std::optional<std::uint8_t> modifiers) {
    auto& messenger=events.messenger();messenger.select("GAMERAM","gameram",0);messenger.buffer().add_int(source);
    if(modifiers)messenger.buffer().add_bool(*modifiers);
    return messenger.begin_send("RECALCPLAYER");
}
bool same_values(const MessengerBuffer& left,const MessengerBuffer& right) {
    return left.enabled_only()==right.enabled_only() && left.booleans()==right.booleans() && left.integers()==right.integers() &&
        left.floats()==right.floats() && left.integers64()==right.integers64() && left.vectors()==right.vectors() &&
        left.matrices()==right.matrices() && left.objects()==right.objects() && left.strings()==right.strings();
}

void optional_byte_and_packet_preservation() {
    const std::array<std::optional<std::uint8_t>,6> flags{std::nullopt,std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{128},std::uint8_t{255}};
    for(std::int32_t source=0;source<4;++source)for(const auto flag:flags) {
        ActionEventRuntime events;GameRamRuntime game(events);Databases db;std::map<std::string,unsigned> calls;base_providers(events,db,calls);
        const auto row=game.state().player(static_cast<std::size_t>(source));put(row,"LEVEL",1);
        auto warm=packet(events,source,std::uint8_t{0});
        check(events.resume(warm).stop==MessengerStop::completed && calls["shinka"]==1 && calls["pcTech"]==1,
            "RECALCPLAYER did not execute the base/Waza path through its shared manager caches.");
        put(row,"HP",17);put(row,"MP",0);put(row,"MHP",-1);put(row,"ATTACKDELAY",77);put(row,"WEAPONRECNO",123);
        const auto names=row->require<GameDataStringList>("POWERSOCKETNAME")->data;*names={"retained"};
        auto& messenger=events.messenger();messenger.select("GAMERAM","gameram",0);auto& buffer=messenger.buffer();
        buffer.add_int(source);buffer.add_int(999);buffer.add_int(-1);
        if(flag){buffer.add_bool(*flag);buffer.add_bool(*flag?std::uint8_t{0}:std::uint8_t{255});}
        buffer.set_enabled_only(255);buffer.add_float(-2.5f);buffer.add_int64(-1234567890123LL);
        buffer.add_vector({1,2,3});buffer.add_matrix({1,0,0,0,0,1,0,0,0,0,1,0,3,2,1,1});
        buffer.add_string(std::nullopt);buffer.add_string("ignored");buffer.add_data_list(row);buffer.add_object(std::nullopt);
        const auto before=buffer;const auto* identity=&buffer;const auto waza=items(row,"WAZALIST");const auto old_waza=(*waza)[0];
        auto send=messenger.begin_send("RECALCPLAYER");const auto result=events.resume(send);const NativeMessenger& live=messenger;
        const bool enabled=!flag || *flag!=0;
        check(result.stop==MessengerStop::completed && result.recipients_completed==1 && !game.pending_count() && !game.faulted(),
            "Optional modifier byte, ignored cardinalities or completed dispatch state differs.");
        check(value(row,"MHP")==100 && value(row,"HP")==17 && value(row,"MMP")==50 && value(row,"MP")==0 && value(row,"NEXTEXP")==341,
            "Recalculation was skipped for modifier zero or changed HP-zero versus MP-zero behavior.");
        check(value(row,"ATTACKDELAY")== (enabled?0:77) && value(row,"WEAPONRECNO")== (enabled?0:123) &&
            *names==(enabled?std::vector<std::optional<std::string>>{}:std::vector<std::optional<std::string>>{"retained"}),
            "Default modifier one, raw nonzero byte or first Boolean-only selection differs.");
        check(items(row,"WAZALIST")==waza && waza->size()==6 && (*waza)[0]!=old_waza,
            "Modifier-disabled recalculation omitted the actual Waza rebuild.");
        check(&live.buffer()==identity && same_values(before,live.buffer()) && messenger.buffer_depth()==0 &&
            messenger.selected().category=="GAMERAM" && messenger.selected().command=="RECALCPLAYER" && calls["shinka"]==1 && calls["pcTech"]==1,
            "Cached recalculation cleared/appended/restored packet values or refreshed its databases.");
        for(std::size_t other=0;other<4;++other)if(other!=static_cast<std::size_t>(source))
            check(value(game.state().player(other),"MHP")==0,"Recalculation wrote a different player row.");
        put(row,"MHP",987);
        check(events.resume(send).stop==MessengerStop::completed && value(row,"MHP")==987 && same_values(before,live.buffer()),
            "Completed RECALCPLAYER retry repeated effects or appended a result.");
    }
}

void paused_publication_and_packet_mutation() {
    ActionEventRuntime events;GameRamRuntime game(events);Databases db;std::map<std::string,unsigned> calls;base_providers(events,db,calls);
    auto& shared=events.messenger();
    const auto row=game.state().player(1);put(row,"LEVEL",1);(*items(row,"MAINSLOT"))[0]=std::make_shared<GameDataWeapon>();
    const auto unrelated=game.state().player(3);const auto hp=row->require<GameDataInt>("HP");
    std::set<std::uint64_t> queries;std::uint64_t paused{};unsigned effects{},provider_calls{};bool ready=false;
    provider(events,"weapon",[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
        ++provider_calls;const bool fresh=queries.insert(delivery.dispatch_id).second;
        if(fresh && queries.size()==2) {
            check(value(row,"MHP")==107 && hp->data==107 && value(row,"MMP")==0,
                "Paused MP query did not observe the committed MHP/HP prefix.");
            ++effects;paused=delivery.dispatch_id;
            messenger.select("GAMERAM","gameram",0);messenger.buffer().add_int(3);messenger.buffer().add_bool(0);
            messenger.buffer().add_string("overwritten command packet");
            auto nested=messenger.begin_send("GAMEDATAINIT");
            check(events.resume(nested).stop==MessengerStop::completed && game.state().player(3)!=unrelated,
                "Real nested row reset failed inside the database provider.");
            hp->data=0; // The already-read HP-zero decision must remain true.
        }
        if(delivery.dispatch_id==paused && !ready)return MessengerCallResult{false,0,"paused after HP publication"};
        messenger.buffer().add_database(db.weapon);return MessengerCallResult{true,1,{}};
    });
    auto send=packet(events,1,std::uint8_t{255});const auto result=events.resume(send);
    check(result.stop==MessengerStop::service_required && game.pending_count()==1 && !game.faulted() && effects==1 && provider_calls==2,
        "RECALCPLAYER did not retain its exact pending stat operation.");
    const NativeMessenger& messenger=shared;
    check(messenger.buffer().integers()==std::vector<std::int32_t>{3} && messenger.buffer().booleans()==std::vector<std::uint8_t>{0} &&
        messenger.selected().command=="GAMEDATAINIT","Nested packet mutation was hidden by a private buffer.");
    rejects([&]{events.messenger();},"Unrelated mutable messenger access bypassed the suspended delivery owner.");
    ready=true;check(events.resume(send).stop==MessengerStop::completed && game.pending_count()==0 && effects==1 && !game.faulted(),
        "Recalculation retry reparsed the changed command/source/byte or replayed the nested reset.");
    check(row==game.state().player(1) && hp==row->require<GameDataInt>("HP") && value(row,"MMP")==52 && value(row,"MP")==52 &&
        value(row,"HP")==0 && value(game.state().player(3),"MHP")==0 && value(row,"NEXTEXP")==341,
        "Captured source/modifier, old HP decision or actual row identity changed on resume.");
    check(value(row,"ATTACKDELAY")==0 && value(row,"WEAPONRECNO")==0 && messenger.buffer().integers().empty() &&
        messenger.buffer().objects().size()==1 && std::get<MessengerDatabase>(messenger.buffer().objects()[0])==db.weapon &&
        messenger.selected().category=="DATABASE" && messenger.selected().name=="weapon" && messenger.selected().command=="GETDATABASE",
        "Completed stat tail was skipped or final shared database state was restored/appended to.");
    const auto completed_calls=provider_calls;
    check(events.resume(send).stop==MessengerStop::completed && provider_calls==completed_calls && effects==1,
        "Completed outer RECALCPLAYER replayed an item provider.");
}

void checked_and_uncertain_failures() {
    for(const auto source:std::array<std::optional<std::int32_t>,3>{std::nullopt,-1,4}) {
        ActionEventRuntime events;GameRamRuntime game(events);Databases db;std::map<std::string,unsigned> calls;base_providers(events,db,calls);
        auto& messenger=events.messenger();messenger.select("GAMERAM","gameram",0);
        if(source)messenger.buffer().add_int(*source);messenger.buffer().add_bool(0);
        auto send=messenger.begin_send("RECALCPLAYER");
        check(events.resume(send).stop==MessengerStop::faulted && game.faulted() && calls.empty(),
            "Invalid source packet became a modifier-zero no-op or fabricated default RAM.");
        check(events.resume(send).stop==MessengerStop::faulted && calls.empty(),"Faulted invalid packet was retried.");
    }
    ActionEventRuntime events;GameRamRuntime game(events);Databases db;std::map<std::string,unsigned> calls;base_providers(events,db,calls);
    const auto row=game.state().player(0);put(row,"LEVEL",1);(*items(row,"MAINSLOT"))[0]=std::make_shared<GameDataWeapon>();
    unsigned effects{};provider(events,"weapon",[&](NativeMessenger& messenger,const MessengerDelivery&)->MessengerCallResult {
        if(++effects==2)throw std::runtime_error("uncertain MP item effect");
        messenger.buffer().add_database(db.weapon);return {true,1,{}};
    });
    auto send=packet(events,0,std::nullopt);
    check(events.resume(send).stop==MessengerStop::faulted && game.faulted() && effects==2 &&
        value(row,"MHP")==107 && value(row,"HP")==107 && value(row,"MMP")==0,
        "Uncertain item error rolled back the committed HP prefix or completed the command.");
    check(events.resume(send).stop==MessengerStop::faulted && effects==2,"Uncertain recalculation effects replayed.");

    ActionEventRuntime unsupported_events;GameRamRuntime unsupported_game(unsupported_events);std::map<std::string,unsigned> unsupported_calls;
    base_providers(unsupported_events,db,unsupported_calls);const auto unsupported_row=unsupported_game.state().player(0);
    (*items(unsupported_row,"MAINSLOT"))[0]=std::make_shared<GameDataDisk>();
    auto unsupported=packet(unsupported_events,0,std::nullopt);
    check(unsupported_events.resume(unsupported).stop==MessengerStop::service_required && unsupported_game.pending_count()==1 &&
        !unsupported_game.faulted() && value(unsupported_row,"MHP")==0,"Unsupported item provider fabricated a completed recalculation.");
    (*items(unsupported_row,"MAINSLOT"))[0]=std::monostate{};
    check(unsupported_events.resume(unsupported).stop==MessengerStop::service_required && unsupported_calls["shinka"]==1,
        "Unsupported item retry recreated its stat continuation from live state.");
}

void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);const auto doc=read_igb_document(resources.read("dng101b1/separate/usa_pack.igb"));
    ActionEventRuntime events;GameRamRuntime game(events);DatabaseRuntime databases(events);
    databases.register_source(decode_database_managers(doc,"dng101b1/separate/usa_pack.igb",8));
    for(std::int32_t source=0;source<4;++source) {
        auto& messenger=events.messenger();messenger.select("GAMERAM","gameram",0);messenger.buffer().add_int(source);
        messenger.buffer().add_string("p_agumon");messenger.buffer().add_game_object(std::monostate{});
        auto start=messenger.begin_send("GAMESTART");check(events.resume(start).stop==MessengerStop::completed,"Original object-route GAMESTART prerequisite failed.");
        const auto row=game.state().player(static_cast<std::size_t>(source));const auto socket=items(row,"POWERSOCKET");
        const auto folder=items(row,"DEVICEFOLDER");const auto waza=items(row,"WAZALIST");const auto old=(*waza)[0];
        const auto hp=row->require<GameDataInt>("HP");hp->data=7;put(row,"MP",0);put(row,"LEVEL",2);(*ints(row,"SKILL"))[6]=1000;
        const auto count=value(game.state().common(),"NINZU");auto recalc=packet(events,source,std::nullopt);
        check(events.resume(recalc).stop==MessengerStop::completed && !game.faulted() && !game.pending_count(),"Original RECALCPLAYER did not complete.");
        check(game.state().player(static_cast<std::size_t>(source))==row && hp==row->require<GameDataInt>("HP") && hp->data==7 &&
            value(row,"MP")==0 && value(row,"MHP")>7 && value(row,"NEXTEXP")==968 && value(game.state().common(),"NINZU")==count,
            "Original recalculation replaced owners, refilled zero MP or reran player-count initialization.");
        check(items(row,"WAZALIST")==waza && waza->size()==1 && (*waza)[0]!=old &&
            std::get<std::shared_ptr<GameDataWaza>>((*waza)[0])->base_no==0 && std::get<std::shared_ptr<GameDataWaza>>((*waza)[0])->record_no==2,
            "Original Agumon technique rank did not rebuild from changed live skill.");
        check(items(row,"POWERSOCKET")==socket && items(row,"DEVICEFOLDER")==folder && *socket==*folder && value(row,"WEAPONRECNO")==266 &&
            value(row,"WEAPONSKILLNO")==0 && *row->require<GameDataStringList>("POWERSOCKETNAME")->data==
            std::vector<std::optional<std::string>>{"w_common05","w_common04","w_common03"},"Original equipment aliases or tail queries changed.");
        put(row,"WEAPONSKILLNO",99);*row->require<GameDataStringList>("POWERSOCKETNAME")->data={"preserved"};
        auto base_only=packet(events,source,std::uint8_t{0});check(events.resume(base_only).stop==MessengerStop::completed &&
            value(row,"WEAPONSKILLNO")==99 && *row->require<GameDataStringList>("POWERSOCKETNAME")->data==std::vector<std::optional<std::string>>{"preserved"},
            "Original modifier-zero packet executed the equipment tail.");
    }
}
} // namespace
int main(int argc,char** argv) {
    try {
        optional_byte_and_packet_preservation();paused_publication_and_packet_mutation();checked_and_uncertain_failures();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_game_ram_recalc_tests [EXTRACTED_ROOT]");
        std::cout<<"GAMERAM recalculation checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"GAMERAM recalculation after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
