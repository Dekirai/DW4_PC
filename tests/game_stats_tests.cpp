#include "dw4/action_event_runtime.hpp"
#include "dw4/database.hpp"
#include "dw4/game_data.hpp"
#include "dw4/game_database.hpp"
#include "dw4/game_stats.hpp"
#include "dw4/resources.hpp"
#include "database_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const char* message) {
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
using Fields=std::map<std::string,std::int32_t>;
MessengerDatabase database(const std::vector<Fields>& rows) {
    std::set<std::string> columns;for(const auto& row:rows)for(const auto& [name,value]:row){(void)value;columns.insert(name);}
    std::vector<std::optional<std::string>> names;for(const auto& name:columns)names.push_back(name);
    std::vector<std::int32_t> values;
    for(const auto& row:rows)for(const auto& name:columns) {
        const auto found=row.find(name);values.push_back(found==row.end()?0:found->second);
    }
    database_fixture::Fixture fixture;fixture.ints(names,values);
    return std::make_shared<const DatabaseDefinition>(fixture.definition());
}
Fields shinka_fields() {
    Fields result{{"MHP_INI",100},{"MMP_INI",50},{"STR_INI",10},{"AC_INI",20},
        {"INT_INI",30},{"MND_INI",40},{"DEX_INI",50},{"RES_FIRE",5},{"RES_ICE",6},
        {"RES_THUNDER",7},{"RES_DARK",8}};
    for(unsigned i=1;i<=6;++i) {
        const auto name="TECH"+std::to_string(i);result[name]=static_cast<std::int32_t>(i-1);result[name+"_LEVEL"]=1;
        for(unsigned rank=1;rank<=5;++rank)result[name+"_SKILL"+std::to_string(rank)]=static_cast<std::int32_t>(rank*10-9);
    }
    return result;
}
GameDatabaseCache cache(Fields fields=shinka_fields()) {
    return {database({std::move(fields)}),database({{{"SKILL",6}},{{"SKILL",7}},{{"SKILL",8}},
        {{"SKILL",6}},{{"SKILL",7}},{{"SKILL",8}}})};
}
auto ints(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataIntList>(name)->data;}
auto items(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataObjectList>(name)->data;}
std::int32_t value(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataInt>(name)->data;}
void put(const std::shared_ptr<GameDataList>& row,const char* name,std::int32_t number) {row->require<GameDataInt>(name)->data=number;}
using Service=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,const char* name,Service service) {
    const auto id=ControllerObjectId::allocate_native();
    events.register_native_controller(id,{[]()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [service=std::move(service)] {return MessengerManager{service,{}};}},"DATABASE",name,0);
}
MessengerDatabase weapon_database(bool two_rows=false) {
    database_fixture::Fixture fixture;
    const std::vector<std::optional<std::string>> names{"MAXHP","MAXMP","TSUYOSA","MAMORI","KASHIKOSA","SEISHIN","SUBAYASA",
        "RES_FIRE","RES_ICE","RES_THUNDER","RES_DARK","DELAY","MEICHURITSU","CHIMEIRITSU","MAHIRITSU","CHOHATURITSU",
        "BOUGYORITSU","MAHITEIKOU","DOKUTEIKOU","BITRITSU","EXPRITSU","LUCKRITSU","HPDRAIN","MPDRAIN","SKILL"};
    std::vector<std::int32_t> values(names.size()*(two_rows?2:1),1);values[names.size()-1]=4;
    if(two_rows)values.back()=8;
    fixture.ints(names,values);
    fixture.strings({"MODEL","ZOKUSEI","SHUSEINOURYOKU"},two_rows?
        std::vector<std::optional<std::string>>{"model_a","fire","TSUYOSA","model_b","ice","MAMORI"}:
        std::vector<std::optional<std::string>>{"model_a","fire","TSUYOSA"});
    return std::make_shared<const DatabaseDefinition>(fixture.definition());
}

void health_and_base_fields() {
    for(std::int32_t source=0;source<4;++source)for(const auto hp:{0,-1,5,500}) {
        GameDataState data;ActionEventRuntime events;auto caches=cache();const auto row=data.player(source);
        put(row,"LEVEL",1);put(row,"HP",hp);put(row,"MP",hp==500?99:0);
        auto counts=ints(row,"UPCNT");for(std::size_t i=0;i<counts->size();++i)(*counts)[i]=static_cast<std::int32_t>(i+1);
        (*ints(row,"UPDATA"))[6]=7;(*ints(row,"UPDATA"))[4]=13;(*ints(row,"DOWNDATA"))[4]=4;
        put(row,"ATTACKDELAY",876);put(row,"WEAPONRECNO",765);
        GameStatsRuntime runtime(source,0);auto& messenger=events.messenger();
        check(runtime.resume(data,caches,messenger,events).completed && runtime.completed() && !runtime.faulted(),"Modifier-disabled stat calculation did not complete.");
        check(value(row,"MHP")==108 && value(row,"MMP")==52 && value(row,"HP")==((hp==0 || hp>108)?108:hp) &&
            value(row,"MP")==((hp==0 || hp==500)?52:0),"HP-zero/MP-zero or staged maximum publication differs.");
        check(value(row,"TSUYOSA")==22 && value(row,"MAMORI")==24 && value(row,"KASHIKOSA")==35 &&
            value(row,"SEISHIN")==46 && value(row,"SUBAYASA")==57,"Typed stat UPCNT or outer UP/DOWN mapping differs.");
        check(value(row,"HONOOTEIKO")==13 && value(row,"KOORITEIKO")==15 && value(row,"KAMINARITEIKO")==17 &&
            value(row,"YAMITEIKO")==19 && value(row,"NEXTEXP")==341,"Resistance UPCNT mapping or NEXTEXP differs.");
        check(value(row,"ATTACKDELAY")==876 && value(row,"WEAPONRECNO")==765,"Disabled modifiers executed the gated equipment tail.");
        put(row,"MHP",987);check(runtime.resume(data,caches,messenger,events).completed && value(row,"MHP")==987,"Completed stat invocation replayed publications.");
    }
    GameDataState data;ActionEventRuntime events;auto fields=shinka_fields();fields["MHP_INI"]=-3;fields["MMP_INI"]=-7;
    auto caches=cache(fields);const auto row=data.player(0);put(row,"LEVEL",999);
    GameStatsRuntime runtime(0,0);check(runtime.resume(data,caches,events.messenger(),events).completed &&
        value(row,"MHP")==-3 && value(row,"HP")==-3 && value(row,"MMP")==-7 && value(row,"MP")==-7 && value(row,"NEXTEXP")==0,
        "Maximum values gained a lower clamp or level999 lost its zero NEXTEXP branch.");
}

void retained_base_and_waza() {
    GameDataState data;ActionEventRuntime events;auto fields=shinka_fields();fields["TECH4"]=0;fields["TECH3_LEVEL"]=99;
    auto caches=cache(fields);const auto pc_tech=caches.pc_tech;caches.pc_tech.reset();
    const auto row=data.player(2);put(row,"LEVEL",2);(*ints(row,"SKILL"))[6]=25;(*ints(row,"SKILL"))[7]=35;
    const auto old=std::make_shared<GameDataWaza>(GameDataWaza{9,123,1,99});const auto waza=items(row,"WAZALIST");waza->push_back(old);
    bool ready=false;unsigned calls{};
    provider(events,"pcTech",[&](NativeMessenger& live,const MessengerDelivery&) {
        ++calls;check(waza->empty() && value(row,"MHP")==100 && value(row,"YAMITEIKO")==8,
            "Waza query ran before stat publications or replayed old list contents.");
        if(!ready)return MessengerCallResult{false,0,"pcTech paused"};
        (*ints(row,"SKILL"))[6]=45;
        live.buffer().add_database(pc_tech);return MessengerCallResult{true,1,{}};
    });
    GameStatsRuntime runtime(2,0);auto& messenger=events.messenger();
    check(!runtime.resume(data,caches,messenger,events).completed && calls==1 && waza->empty(),"Missing pcTech service did not pause after the Waza clear.");
    ready=true;check(runtime.resume(data,caches,messenger,events).completed && calls==2 && items(row,"WAZALIST")==waza &&
        old->record_no==123 && old->is_equipped==1,"Waza continuation lost list identity or changed retained old items.");
    const std::array<std::pair<std::int32_t,std::int32_t>,4> expected{{{0,4},{1,4},{0,4},{4,7}}};
    check(waza->size()==expected.size(),"Technique level/skill gates emitted unexpected entries.");
    for(std::size_t i=0;i<expected.size();++i) {
        const auto item=std::get<std::shared_ptr<GameDataWaza>>((*waza)[i]);
        check(item->kind_no==9 && item->base_no==expected[i].first && item->record_no==expected[i].second && !item->is_equipped,
            "Technique group order, descending rank or explicit Waza defaults differ.");
    }

    GameDataState reset_data;ActionEventRuntime reset_events;GameDatabaseCache reset_caches;
    const auto old_row=reset_data.player(1);put(old_row,"LEVEL",1);put(old_row,"HP",5);
    const auto captured=ints(old_row,"UPCNT");const auto db=database({shinka_fields(),shinka_fields()});
    reset_caches.pc_tech=pc_tech;unsigned fetches{};bool resume_ready=false;
    provider(reset_events,"shinka",[&](NativeMessenger& live,const MessengerDelivery&) {
        if(++fetches==1) {
            reset_data.reset_player(1);const auto new_row=reset_data.player(1);put(new_row,"LEVEL",1);
            (*ints(new_row,"UPCNT"))[1]=20;(*captured)[0]=7;
            old_row->require<GameDataIntList>("UPCNT")->data=std::make_shared<std::vector<std::int32_t>>(11,99);
        }
        if(!resume_ready)return MessengerCallResult{false,0,"shinka paused"};
        live.buffer().add_database(db);return MessengerCallResult{true,1,{}};
    });
    GameStatsRuntime reset(1,0);auto& reset_messenger=reset_events.messenger();
    check(!reset.resume(reset_data,reset_caches,reset_messenger,reset_events).completed && value(old_row,"MHP")==0,
        "Base query published HP before its retained database completed.");
    resume_ready=true;check(reset.resume(reset_data,reset_caches,reset_messenger,reset_events).completed &&
        value(old_row,"MHP")==107 && value(old_row,"MMP")==70 && value(reset_data.player(1),"MHP")==0 &&
        value(old_row,"HP")==5 && value(old_row,"MP")==0 && fetches==2,
        "Old outer row, captured UPCNT list/live element or later current-row base lookup was lost.");
}

void complete_modifiers_and_tail() {
    GameDataState data;ActionEventRuntime events;auto caches=cache();const auto row=data.player(0);put(row,"LEVEL",1);put(row,"HP",5);
    const auto power=std::make_shared<GameDataWeapon>(GameDataWeapon{0,0,1,3,0});
    const auto main=std::make_shared<GameDataWeapon>(GameDataWeapon{0,0,1,5,0});
    const auto sub=std::make_shared<GameDataWeapon>(GameDataWeapon{0,0,1,7,0});
    *items(row,"POWERSOCKET")={power,std::monostate{},power};*items(row,"POWERPOCKET")={power,power};
    *items(row,"MAINSLOT")={main};*items(row,"MAINPOCKET")={main};*items(row,"SUBSLOT")={sub};
    const auto names=row->require<GameDataStringList>("POWERSOCKETNAME")->data;names->push_back("old");
    const auto db=weapon_database();std::set<std::uint64_t> sends;unsigned calls{};bool ready=false;std::uint64_t pause{};
    GameStatsRuntime runtime(0,255);auto& messenger=events.messenger();
    provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++calls;const bool fresh=sends.insert(delivery.dispatch_id).second;
        if(fresh && sends.size()==3) {
            check(value(row,"MHP")==102 && value(row,"HP")==5 && value(row,"MMP")==0,
                "MP item query did not observe previously committed HP publications.");
            put(row,"HP",0);
            rejects([&]{runtime.resume(data,caches,messenger,events);},"Recursive stat resume was accepted inside a real provider.");
        }
        if(fresh && sends.size()==5)pause=delivery.dispatch_id;
        if(delivery.dispatch_id==pause && !ready)return MessengerCallResult{false,0,"weapon paused"};
        live.buffer().add_database(db);return MessengerCallResult{true,1,{}};
    });
    check(!runtime.resume(data,caches,messenger,events).completed && calls==5 && sends.size()==5 &&
        value(row,"MMP")==52 && value(row,"MP")==0 && !runtime.faulted(),"Saved earlier HP-zero flag changed or modifier suspension replayed prior writes.");
    GameDatabaseCache other=caches;
    rejects([&]{runtime.resume(data,other,messenger,events);},"Suspended stats accepted another cache owner.");
    ready=true;check(runtime.resume(data,caches,messenger,events).completed && sends.size()==69 && calls==70,
        "Full modifier/tail query count differs or a paused query was duplicated.");
    check(value(row,"TSUYOSA")==24 && value(row,"MAMORI")==27 && value(row,"KASHIKOSA")==36 &&
        value(row,"SEISHIN")==46 && value(row,"SUBAYASA")==56 && value(row,"HONOOTEIKO")==8,
        "Modifier order, conditional revision or pocket sums differ.");
    check(value(row,"ATTACKDELAY")==1 && value(row,"CHIMEIRITSU")==3 && value(row,"KAIHIRITSU")==1 &&
        value(row,"CHOHATURITSU")==1 && value(row,"ATTACKZOKUSEI")==0 && value(row,"HPDRAIN")==2 && value(row,"MPDRAIN")==2,
        "Combat tail columns, first element category or final value copy differ.");
    check(value(row,"WEAPONRECNO")==0 && value(row,"WEAPONSKILLNO")==4 && row->require<GameDataStringList>("POWERSOCKETNAME")->data==names &&
        *names==std::vector<std::optional<std::string>>{"model_a","model_a"},"Weapon metadata or retained socket name clear/rebuild differs.");
}

void live_weapon_tail_and_failures() {
    GameDataState data;ActionEventRuntime events;auto caches=cache();const auto row=data.player(0);put(row,"LEVEL",1);
    const auto weapon=std::make_shared<GameDataWeapon>();(*items(row,"POWERSOCKET"))[0]=weapon;
    const auto db=weapon_database(true);unsigned calls{};
    provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&) {
        if(++calls==9)weapon->record_no=1;
        live.buffer().add_database(db);return MessengerCallResult{true,1,{}};
    });
    GameStatsRuntime runtime(0,1);check(runtime.resume(data,caches,events.messenger(),events).completed && calls==10 &&
        value(row,"WEAPONRECNO")==0 && value(row,"WEAPONSKILLNO")==8 &&
        *row->require<GameDataStringList>("POWERSOCKETNAME")->data==std::vector<std::optional<std::string>>{"model_b"},
        "Tail record was not captured before the live post-query SKILL/MODEL record reads.");

    GameDataState unsupported_data;ActionEventRuntime unsupported_events;auto unsupported_cache=cache();const auto unsupported_row=unsupported_data.player(0);
    (*items(unsupported_row,"MAINSLOT"))[0]=std::make_shared<GameDataDisk>();GameStatsRuntime unsupported(0,1);
    auto& unsupported_messenger=unsupported_events.messenger();
    check(!unsupported.resume(unsupported_data,unsupported_cache,unsupported_messenger,unsupported_events).completed &&
        !unsupported.faulted() && value(unsupported_row,"MHP")==0,"Unknown item provider was fabricated or published uncompleted HP.");
    (*items(unsupported_row,"MAINSLOT"))[0]=std::monostate{};
    check(!unsupported.resume(unsupported_data,unsupported_cache,unsupported_messenger,unsupported_events).completed,
        "Unsupported selected item was replaced by a re-read list entry on retry.");

    for(const auto bad:{-1,4})rejects([&]{GameStatsRuntime invalid(bad,0);},"Invalid source RAM accepted.");
    GameDataState invalid_data;ActionEventRuntime invalid_events;auto invalid_cache=cache();put(invalid_data.player(0),"LEVEL",1000);
    GameStatsRuntime invalid(0,0);auto& invalid_messenger=invalid_events.messenger();
    rejects([&]{invalid.resume(invalid_data,invalid_cache,invalid_messenger,invalid_events);},"Unbounded original level table access fabricated a last band.");
    check(invalid.faulted() && value(invalid_data.player(0),"MHP")==0,"Failed base calculation published or remained replayable.");
    rejects([&]{invalid.resume(invalid_data,invalid_cache,invalid_messenger,invalid_events);},"Faulted stat calculation replayed.");

    GameDataState failed_data;ActionEventRuntime failed_events;auto failed_cache=cache();const auto failed_row=failed_data.player(0);
    (*items(failed_row,"MAINSLOT"))[0]=std::make_shared<GameDataWeapon>();unsigned effects{};
    provider(failed_events,"weapon",[&](NativeMessenger&,const MessengerDelivery&)->MessengerCallResult{++effects;throw std::runtime_error("uncertain provider");});
    GameStatsRuntime failed(0,1);auto& failed_messenger=failed_events.messenger();
    rejects([&]{failed.resume(failed_data,failed_cache,failed_messenger,failed_events);},"Throwing item provider did not fault stat owner.");
    rejects([&]{failed.resume(failed_data,failed_cache,failed_messenger,failed_events);},"Faulted item effect was retried.");
    check(failed.faulted() && effects==1,"Uncertain item effect replayed.");
}

void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);const auto doc=read_igb_document(resources.read("dng101b1/separate/usa_pack.igb"));
    const auto shinka=std::make_shared<const DatabaseDefinition>(decode_database(doc,1879,"dng101b1/separate/usa_pack.igb",8));
    const auto pc=std::make_shared<const DatabaseDefinition>(decode_database(doc,1592,"dng101b1/separate/usa_pack.igb",8));
    const auto weapons=std::make_shared<const DatabaseDefinition>(decode_database(doc,1643,"dng101b1/separate/usa_pack.igb",8));
    for(std::int32_t source=0;source<4;++source) {
        GameDataState data;ActionEventRuntime events;GameDatabaseCache caches{shinka,pc};const auto row=data.player(source);
        put(row,"LEVEL",1);std::fill(ints(row,"SKILL")->begin(),ints(row,"SKILL")->end(),1);
        const std::array<std::int32_t,3> records{266,275,274};
        for(std::size_t i=0;i<records.size();++i)(*items(row,"POWERSOCKET"))[i]=std::make_shared<GameDataWeapon>(GameDataWeapon{0,records[i],1,0,0});
        unsigned calls{};provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&){++calls;live.buffer().add_database(weapons);return MessengerCallResult{true,1,{}};});
        GameStatsRuntime runtime(source,1);check(runtime.resume(data,caches,events.messenger(),events).completed && calls==12,
            "Actual prepared weapon/stat path did not complete with its repeated item queries.");
        check(value(row,"MHP")==shinka->integer(0,"MHP_INI") && value(row,"HP")==value(row,"MHP") &&
            value(row,"MMP")==shinka->integer(0,"MMP_INI") && value(row,"MP")==value(row,"MMP") && value(row,"NEXTEXP")==341,
            "Actual level1 health/base publication differs.");
        const auto waza=items(row,"WAZALIST");check(waza->size()==1 && std::get<std::shared_ptr<GameDataWaza>>((*waza)[0])->base_no==0 &&
            std::get<std::shared_ptr<GameDataWaza>>((*waza)[0])->record_no==0,"Actual prepared Agumon level1 technique thresholds differ.");
        check(value(row,"WEAPONRECNO")==266 && value(row,"WEAPONSKILLNO")==0 &&
            *row->require<GameDataStringList>("POWERSOCKETNAME")->data==std::vector<std::optional<std::string>>{"w_common05","w_common04","w_common03"},
            "Actual weapon metadata/names differ from the retained tail queries.");
    }
}

int oracle() {
    std::int32_t source{},selector{},level{},initial{},growth{},increment{};
    constexpr std::array<const char*,7> ini{"MHP_INI","MMP_INI","STR_INI","AC_INI","INT_INI","MND_INI","DEX_INI"};
    constexpr std::array<const char*,7> grow{"MHP_GROW","MMP_GROW","STR_GROW","AC_GROW","INT_GROW","MND_GROW","DEX_GROW"};
    constexpr std::array<const char*,7> output{"MHP","MMP","TSUYOSA","MAMORI","KASHIKOSA","SEISHIN","SUBAYASA"};
    while(std::cin>>source>>selector>>level>>initial>>growth>>increment) {
        if(selector<0 || selector>=7)throw std::runtime_error("Invalid oracle selector.");
        GameDataState data;ActionEventRuntime events;Fields fields;fields[ini[selector]]=initial;fields[grow[selector]]=growth;
        auto caches=cache(fields);const auto row=data.player(static_cast<std::size_t>(source));put(row,"LEVEL",level);
        (*ints(row,"UPCNT"))[static_cast<std::size_t>(selector)]=increment;
        GameStatsRuntime runtime(source,0);
        try {
            if(!runtime.resume(data,caches,events.messenger(),events).completed)throw std::runtime_error("Unexpected oracle service boundary.");
            std::cout<<value(row,output[selector])<<' '<<value(row,"NEXTEXP")<<'\n';
        }catch(const GameDataError&){std::cout<<"E\n";}
    }
    return 0;
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--oracle")return oracle();
        health_and_base_fields();retained_base_and_waza();complete_modifiers_and_tail();live_weapon_tail_and_failures();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_game_stats_tests [EXTRACTED_ROOT | --oracle]");
        std::cout<<"Game stats checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Game stats after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
