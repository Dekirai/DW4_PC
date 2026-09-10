#include "dw4/action_event_runtime.hpp"
#include "dw4/database_runtime.hpp"
#include "dw4/game_name.hpp"
#include "dw4/game_ram.hpp"
#include "dw4/resources.hpp"
#include "database_fixture.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected,reason);
}
auto ints(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataIntList>(name)->data;}
auto objects(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataObjectList>(name)->data;}
auto value(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataInt>(name)->data;}
struct Databases {
    MessengerDatabase shinka,weapon,pc;
    explicit Databases(std::size_t count=16) {
        database_fixture::Fixture species;
        std::vector<std::optional<std::string>> models;
        std::vector<std::int32_t> numbers;
        for(std::size_t i=0;i<count;++i) {
            models.emplace_back("species"+std::to_string(i));
            numbers.insert(numbers.end(),{0,1,2,100,50});
        }
        species.strings({"MODEL_NAME"},models);
        species.ints({"INI_WEAPON1","INI_WEAPON2","INI_WEAPON3","MHP_INI","MMP_INI"},numbers);
        shinka=std::make_shared<const DatabaseDefinition>(species.definition());
        database_fixture::Fixture items;items.strings({"MODEL"},{"weapon0","weapon1","weapon2"});items.ints({"SKILL"},{0,4,5});
        weapon=std::make_shared<const DatabaseDefinition>(items.definition());
        database_fixture::Fixture tech;tech.ints({"SKILL"},{6});pc=std::make_shared<const DatabaseDefinition>(tech.definition());
    }
};
using Service=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,const char* name,Service service) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [service=std::move(service)]{return MessengerManager{service,{}};}},"DATABASE",name,0);
}
void providers(ActionEventRuntime& events,const Databases& db,std::map<std::string,unsigned>& calls) {
    for(const auto& [name,database]:std::array{std::pair{"shinka",db.shinka},std::pair{"weapon",db.weapon},std::pair{"pcTech",db.pc}})
        provider(events,name,[&,name,database](NativeMessenger& messenger,const MessengerDelivery&){
            ++calls[name];messenger.buffer().add_database(database);return MessengerCallResult{true,1,{}};
        });
}
std::shared_ptr<GameNameConverter> font() {
    return std::make_shared<GameNameConverter>(GameNameTable({{65,65},{66,66},{67,67}}),std::make_shared<std::vector<std::uint8_t>>());
}
MessengerDispatch packet(ActionEventRuntime& events,std::int32_t source,
    const std::vector<std::optional<std::string>>& strings,const std::vector<GameDataObject>& objects={}) {
    auto& messenger=events.messenger();messenger.select("GAMERAM","gameram",0);messenger.buffer().add_int(source);
    for(const auto& text:strings)messenger.buffer().add_string(text);
    for(const auto& object:objects)messenger.buffer().add_game_object(object);
    return messenger.begin_send("GAMESTART");
}

void cardinality_and_species() {
    for(unsigned object_count=0;object_count<=2;++object_count)for(unsigned string_count=1;string_count<=3;++string_count) {
        ActionEventRuntime events;GameRamRuntime game(events);Databases db;std::map<std::string,unsigned> calls;providers(events,db,calls);
        const auto converter=font();game.set_name_services({[converter]{return converter;}});
        std::vector<std::optional<std::string>> strings(string_count,"ABC");strings[0]="species3";
        const bool selected=object_count==1 || string_count==2;
        auto send=packet(events,selected?1:99,strings,std::vector<GameDataObject>(object_count));
        check(events.resume(send).stop==MessengerStop::completed && !game.faulted() && !game.pending_count(),
            "GAMESTART packet cardinality or object-route precedence differs.");
        const auto row=game.state().player(1);
        check((row->require<GameDataBool>("ISUSE")->data!=0)==selected,"Ignored packet accessed or initialized an invalid player index.");
        if(selected) {
            check(value(row,"DIGIMONNO")==3 && value(game.state().common(),"NINZU")==1 && calls["shinka"]==1,
                "Selected species/common publication or manager cache differs.");
            const auto name=objects(row,"PLAYERNAME");
            check(name->size()==1 && (object_count==1?std::holds_alternative<std::monostate>(name->front()):
                std::holds_alternative<std::shared_ptr<GameDataUnicode>>(name->front())),"Name route was chosen after nested packet mutation.");
        } else check(calls.empty(),"Ignored cardinality queried a database.");
    }
    for(unsigned missing=0;missing<2;++missing) {
        ActionEventRuntime events;GameRamRuntime game(events);auto& messenger=events.messenger();messenger.select("GAMERAM","gameram",0);
        if(missing==0)messenger.buffer().add_string("species0");else messenger.buffer().add_int(99);
        auto send=messenger.begin_send("GAMESTART");
        check(events.resume(send).stop==MessengerStop::faulted && game.faulted(),"Mandatory packet reads were skipped for an ignored cardinality.");
    }
    for(const auto model:std::vector<std::optional<std::string>>{std::nullopt,"","absent","species15"}) {
        GameDataState data;ActionEventRuntime events;Databases db;GameDatabaseCache cache{db.shinka,db.pc};std::map<std::string,unsigned> calls;providers(events,db,calls);
        GameStartRuntime start(0,model,GameStartName{GameDataObject{}});
        check(start.resume(data,cache,events.messenger(),events,{}).completed && value(data.player(0),"DIGIMONNO")==
            (model=="species15"?15:0),"Ordered model lookup/null input/fallback species differs.");
    }
    GameDataState data;ActionEventRuntime events;Databases short_db(1);GameDatabaseCache cache{short_db.shinka,short_db.pc};
    GameStartRuntime incomplete(0,"absent",GameStartName{GameDataObject{}});
    rejects([&]{incomplete.resume(data,cache,events.messenger(),events,{});},"Absent species silently fell back before reading all sixteen rows.");
    check(incomplete.faulted() && !data.player(0)->require<GameDataBool>("ISUSE")->data,"Failed species lookup published a player.");
}

void base_arrays_and_aliases() {
    Databases db;
    for(std::int32_t source=0;source<4;++source) {
        GameDataState data;ActionEventRuntime events;GameDatabaseCache cache{db.shinka,{}};const auto row=data.player(source);
        (*ints(row,"BASE_LEVEL"))[2]=7;(*ints(row,"BASE_EXP"))[2]=12345;
        for(std::size_t i=0;i<9;++i)(*ints(row,"BASE_SKILL"))[18+i]=static_cast<std::int32_t>(10+i);
        for(std::size_t i=0;i<11;++i)(*ints(row,"BASE_UPCNT"))[22+i]=static_cast<std::int32_t>(30+i);
        const auto previous=data.common()->require<GameDataBoolList>("FLAG")->data;
        GameBaseToCurrent base(source,2);auto& messenger=events.messenger();
        check(base.resume(data,cache,messenger,events).completed && value(row,"DIGIMONNO")==2 && value(row,"LEVEL")==7 &&
            value(row,"EXP")==12345 && row->require<GameDataString>("DIGIMONNAME")->data=="species2","BASE2CURRENT scalar publication differs.");
        for(std::size_t i=0;i<9;++i)check((*ints(row,"SKILL"))[i]==10+static_cast<std::int32_t>(i),"BASE_SKILL slice differs.");
        for(std::size_t i=0;i<11;++i)check((*ints(row,"UPCNT"))[i]==30+static_cast<std::int32_t>(i),"BASE_UPCNT slice differs.");
        check(data.common()->require<GameDataBoolList>("FLAG")->data==(source==0?row->require<GameDataBoolList>("BASE_FLAG")->data:previous),
            "BASE2CURRENT RAM-zero common array alias differs.");
        if(source==0)check(data.common()->require<GameDataBoolList>("FLAGFOLDER")->data==row->require<GameDataBoolList>("BASE_FLAGFOLDER")->data &&
            ints(data.common(),"COUNTER")==ints(row,"BASE_COUNTER"),"Common flag-folder/counter arrays were copied.");
        row->require<GameDataInt>("LEVEL")->data=99;
        check(base.resume(data,cache,messenger,events).completed && value(row,"LEVEL")==99,"Completed BASE2CURRENT replayed source copies.");
    }
    GameDataState data;ActionEventRuntime events;GameDatabaseCache cache;const auto row=data.player(2);
    ints(row,"SKILL")->resize(2);(*ints(row,"BASE_SKILL"))[9]=77;bool ready=false;unsigned calls{};
    provider(events,"shinka",[&](NativeMessenger& messenger,const MessengerDelivery&){
        ++calls;check(value(row,"DIGIMONNO")==1,"DIGIMONNO was not written before database delivery.");
        if(!ready){data.reset_player(2);return MessengerCallResult{false,0,"paused shinka"};}
        messenger.buffer().add_database(db.shinka);return MessengerCallResult{true,1,{}};
    });
    GameBaseToCurrent base(2,1);auto& messenger=events.messenger();
    check(!base.resume(data,cache,messenger,events).completed && row!=data.player(2),"Base query did not suspend across a row replacement.");
    ready=true;check(base.resume(data,cache,messenger,events).completed && calls==2 && (*ints(row,"SKILL"))[0]==77 &&
        ints(row,"SKILL")->size()==2 && value(data.player(2),"DIGIMONNO")==0,"Retained base row or out-of-range destination setter differs.");
    GameDataState malformed;GameDatabaseCache cached{db.shinka,{}};GameBaseToCurrent bad(0,1);
    ints(malformed.player(0),"BASE_SKILL")->resize(9);ints(malformed.player(0),"SKILL")->clear();
    rejects([&]{bad.resume(malformed,cached,messenger,events);},"Empty destination hid an invalid original source read.");
    check(bad.faulted() && value(malformed.player(0),"DIGIMONNO")==1,"Checked base fault lost its earlier committed prefix.");
}

void retained_start_and_name() {
    GameDataState data;ActionEventRuntime events;Databases db;GameDatabaseCache cache;auto& messenger=events.messenger();
    const auto early=data.player(2);std::shared_ptr<GameDataList> middle;unsigned shinka_calls{},weapon_calls{},font_calls{};bool ready=false;
    data.player(1)->require<GameDataBool>("ISUSE")->data=128;
    provider(events,"shinka",[&](NativeMessenger& live,const MessengerDelivery&){
        ++shinka_calls;data.reset_player(2);middle=data.player(2);live.buffer().add_database(db.shinka);return MessengerCallResult{true,1,{}};
    });
    provider(events,"pcTech",[&](NativeMessenger& live,const MessengerDelivery&){live.buffer().add_database(db.pc);return MessengerCallResult{true,1,{}};});
    provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&){
        ++weapon_calls;
        check(!early->require<GameDataBool>("ISUSE")->data && value(data.common(),"NINZU")==0,"Player count was published before full equipment/stats success.");
        if(!ready)return MessengerCallResult{false,0,"paused weapon"};
        live.buffer().add_database(db.weapon);return MessengerCallResult{true,1,{}};
    });
    GameStartRuntime start(2,"species2",GameStartName{std::optional<std::string>{"ABCABCABC"}});
    check(!start.resume(data,cache,messenger,events,{}).completed && objects(middle,"POWERSOCKET")->size()==1,
        "Combined GAMESTART did not pause after the original equipment append.");
    ready=true;check(!start.resume(data,cache,messenger,events,{}).completed && !start.faulted() && shinka_calls==1,
        "Missing font completed GAMESTART or repeated the species database fetch.");
    check(early->require<GameDataBool>("ISUSE")->data==1 && !middle->require<GameDataBool>("ISUSE")->data &&
        early->require<GameDataBoolList>("BASE_ISUSE")->data->at(2)==1 && value(data.common(),"NINZU")==1 && value(middle,"MHP")==100,
        "Common retained row, live four-player count or later helper row access differs.");
    const auto name_list=objects(middle,"PLAYERNAME");const auto old=std::make_shared<GameDataInt>();old->data=55;*name_list={old};
    const auto committed_calls=weapon_calls;const auto converter=font();
    GameNameServices names{[&]{++font_calls;data.reset_player(2);return converter;}};
    check(start.resume(data,cache,messenger,events,names).completed && start.completed() && weapon_calls==committed_calls && font_calls==1 &&
        objects(middle,"PLAYERNAME")==name_list && old->data==55 && data.player(2)!=middle,"Name retry replayed common phases or lost its pre-font row/list owner.");
    const auto converted=std::get<std::shared_ptr<GameDataUnicode>>(name_list->at(0));
    check(*converted->buffer==std::vector<std::uint16_t>{65,66,67,65,66,67,65,66,0} &&
        objects(data.player(2),"PLAYERNAME")->empty(),"Name publication ignored the captured row or exact eight-unit truncation.");
    check(start.resume(data,cache,messenger,events,names).completed && font_calls==1,"Completed GAMESTART replayed name conversion.");
}

void opaque_objects_and_query_faults() {
    Databases db;const auto unicode=std::make_shared<GameDataUnicode>();unicode->name="real Unicode owner";
    const auto weapon=std::make_shared<GameDataWeapon>(GameDataWeapon{0,-7,255,-2,8});
    const std::vector<GameDataObject> payloads{std::monostate{},unicode,weapon,std::make_shared<GameDataInt>(),
        ControllerObjectId::allocate_native(),std::make_shared<const CollisionBox>(),std::make_shared<GameDataList>(),db.weapon};
    for(const auto& payload:payloads) {
        ActionEventRuntime events;GameRamRuntime game(events);std::map<std::string,unsigned> calls;providers(events,db,calls);
        const auto list=objects(game.state().player(0),"PLAYERNAME");*list={payload};
        auto send=packet(events,0,{"species0","ignored string","extra string"},{payload});
        check(events.resume(send).stop==MessengerStop::completed && objects(game.state().player(0),"PLAYERNAME")==list &&
            list->size()==1 && list->at(0)==payload,"Opaque name payload was copied, cast or lost during its aliased list clear.");
    }
    for(unsigned malformed=0;malformed<3;++malformed) {
        ActionEventRuntime events;auto& messenger=events.messenger();GameDatabaseQuery query("shinka");unsigned calls{};
        provider(events,"shinka",[&](NativeMessenger& live,const MessengerDelivery&){
            ++calls;if(malformed==1)live.buffer().add_object(std::nullopt);
            if(malformed==2)live.buffer().add_game_object(weapon);return MessengerCallResult{true,1,{}};
        });
        rejects([&]{query.database();},"Uncompleted query exposed a result.");
        rejects([&]{query.resume(messenger,events);},"Invalid current object zero was accepted as a database.");
        rejects([&]{query.resume(messenger,events);},"Faulted query replayed its uncertain provider.");
        check(query.faulted() && calls==1,"Faulted query repeated original delivery.");
    }
    MessengerBuffer buffer;rejects([&]{buffer.add_game_object(std::shared_ptr<GameDataUnicode>{});},"Typed null object bypassed messenger identity checks.");
    auto nested=std::make_shared<GameDataObjectList>();nested->name="OBJECTS";*nested->data={unicode,weapon,db.weapon};
    GameDataList row({nested,unicode});check(row.require<GameDataUnicode>("real Unicode owner")==unicode &&
        row.require<GameDataObjectList>("OBJECTS")->data->at(1)==GameDataObject{weapon},"Unicode named base or weapon signed fields lost their original identity.");
}

void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);const auto document=read_igb_document(resources.read("dng101b1/separate/usa_pack.igb"));
    ActionEventRuntime events;GameRamRuntime game(events);DatabaseRuntime databases(events);
    databases.register_source(decode_database_managers(document,"dng101b1/separate/usa_pack.igb",8));
    check(databases.sources()[0].infos().size()==12,"Original selected source did not supply twelve real database infos.");
    const auto converter=std::make_shared<GameNameConverter>(read_game_name_table(root/"sys"/"main.dol"),std::make_shared<std::vector<std::uint8_t>>());
    game.set_name_services({[converter]{return converter;}});
    for(std::int32_t source=0;source<4;++source) {
        const auto row=game.state().player(source);const auto names=objects(row,"PLAYERNAME");
        auto send=packet(events,source,{"p_agumon","Agumon"});
        const auto result=events.resume(send);if(result.stop!=MessengerStop::completed)throw std::runtime_error("Original GAMESTART: "+result.diagnostic);
        check(!game.faulted() && !game.pending_count() && value(game.state().common(),"NINZU")==source+1 &&
            row->require<GameDataBool>("ISUSE")->data==1 && row->require<GameDataBoolList>("BASE_ISUSE")->data->at(0)==1,
            "Original four-player GAMESTART did not finish and publish its live player count.");
        check(value(row,"LEVEL")==1 && value(row,"DIGIMONNO")==0 && row->require<GameDataString>("DIGIMONNAME")->data=="p_agumon" &&
            value(row,"HP")==value(row,"MHP") && value(row,"MHP")>0 && value(row,"MP")==value(row,"MMP") &&
            value(row,"NEXTEXP")==341,"Original player base/health initialization differs.");
        const auto folder=objects(row,"DEVICEFOLDER"),socket=objects(row,"POWERSOCKET");
        check(folder->size()==3 && *folder==*socket && folder!=socket,"Original initial weapon folder/socket aliases differ.");
        const std::array<std::int32_t,3> records{266,275,274};
        for(std::size_t i=0;i<3;++i) {
            const auto item=std::get<std::shared_ptr<GameDataWeapon>>(socket->at(i));
            check(item->kind_no==0 && item->record_no==records[i] && item->is_equipped==1 && item->revision==0 && item->pocket==0,
                "Original initial weapon record or equipped/revision/pocket fields differ.");
        }
        const auto waza=objects(row,"WAZALIST");
        check(waza->size()==1 && std::get<std::shared_ptr<GameDataWaza>>(waza->at(0))->record_no==0 &&
            value(row,"WEAPONRECNO")==266 && value(row,"WEAPONSKILLNO")==0,"Original selected Waza and first weapon metadata differ.");
        check(objects(row,"PLAYERNAME")==names && names->size()==1 &&
            *std::get<std::shared_ptr<GameDataUnicode>>(names->front())->buffer==std::vector<std::uint16_t>{65,103,117,109,111,110,0,0,0},
            "Original-table name conversion did not publish its retained player name list.");
    }
    // The selected Waza now allows the existing combat prefix to reach the
    // real growth/probability boundary instead of stopping at a missing Waza.
    auto& messenger=events.messenger();messenger.select("GAMERAM","gameram",0);messenger.buffer().add_string("P2ESKILLUP");
    for(const auto number:{0,80000,0,0})messenger.buffer().add_int(number);
    auto skill=messenger.begin_send("FIGHTCALC");const auto result=events.resume(skill);
    check(result.stop==MessengerStop::service_required && result.diagnostic.find("probability")!=std::string::npos &&
        !game.faulted(),"Original initialized skill did not reach the explicit remaining growth service.");
}
} // namespace
int main(int argc,char** argv) {
    try {
        cardinality_and_species();base_arrays_and_aliases();retained_start_and_name();opaque_objects_and_query_faults();
        const auto synthetic=checks;if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_game_start_tests [EXTRACTED_ROOT]");
        std::cout<<"Game start checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Game start after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
