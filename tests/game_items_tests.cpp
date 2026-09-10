#include "dw4/game_items.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/database_runtime.hpp"
#include "dw4/game_database.hpp"
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
template<class F> void rejects(F&& fn,const char* message) {
    bool rejected{};try {fn();}catch(const std::exception&) {rejected=true;}check(rejected,message);
}
std::shared_ptr<GameDataWeapon> weapon(std::int32_t record,std::uint8_t equipped=0) {
    return std::make_shared<GameDataWeapon>(GameDataWeapon{0,record,equipped,-7,3});
}
auto objects(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataObjectList>(name)->data;}
auto names(const std::shared_ptr<GameDataList>& row) {return row->require<GameDataStringList>("POWERSOCKETNAME")->data;}
MessengerDatabase weapon_database() {
    database_fixture::Fixture fixture;
    fixture.ints({"VALUE",""},{-4,11,1200,22,7,33});
    fixture.strings({"MODEL"},{"first",std::nullopt,"third"});
    return std::make_shared<const DatabaseDefinition>(fixture.definition("synthetic/weapon.igb",1));
}
MessengerDatabase shinka_database() {
    database_fixture::Fixture fixture;
    fixture.ints({"INI_WEAPON1","INI_WEAPON2","INI_WEAPON3"},{0,1,0,2,2,1});
    return std::make_shared<const DatabaseDefinition>(fixture.definition("synthetic/shinka.igb",2));
}
void provider(ActionEventRuntime& events,const char* name,
    std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)> call) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t> {return std::uint8_t{1};},
        [call=std::move(call)] {return MessengerManager{call,{}};}
    },"DATABASE",name,0);
}

void retained_queries() {
    ActionEventRuntime events;auto& messenger=events.messenger();auto database=weapon_database();
    auto item=weapon(0);unsigned calls{},prefix{};bool ready{};
    provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++calls;check(delivery.message.command=="GETDATABASE" && delivery.message.index==0,"Weapon query used another actual request.");
        if(prefix++==0)item->record_no=1;
        if(!ready)return MessengerCallResult{false,0,"test database pause"};
        live.buffer().add_database(database);return MessengerCallResult{true,1,{}};
    });
    GameWeaponQuery query(item,GameItemQueryKind::integer,"VALUE");
    rejects([&] {query.integer_value();},"Unfinished weapon query exposed an invented value.");
    check(!query.resume(messenger,events).completed && !query.completed() && calls==1 && !query.faulted(),"Weapon query did not retain a suspended delivery.");
    ActionEventRuntime alien;auto& alien_messenger=alien.messenger();
    rejects([&] {query.resume(alien_messenger,alien);},"Weapon query accepted another runtime owner.");
    ready=true;check(query.resume(messenger,events).completed && query.integer_value()==1200 && calls==2,"Weapon record was captured before the suspended database callback.");
    item->record_no=2;database.reset();
    check(query.resume(messenger,events).completed && query.integer_value()==1200 && calls==2,"Completed weapon result changed or replayed its provider.");
    rejects([&] {query.string_value();},"Integer query exposed a string result.");
    database=weapon_database();
    GameWeaponQuery next(item,GameItemQueryKind::string,"MODEL");
    check(next.resume(messenger,events).completed && next.string_value()=="third" && calls==3,"New item query reused a cached send.");
    item->record_no=1;GameWeaponQuery empty(item,GameItemQueryKind::string,"MODEL");
    check(empty.resume(messenger,events).completed && empty.string_value().empty() && calls==4,"Null string-cell name did not become the original empty string.");
    item->record_no=std::numeric_limits<std::int32_t>::min();
    GameWeaponQuery absent(item,GameItemQueryKind::integer,std::nullopt);
    check(absent.resume(messenger,events).completed && absent.integer_value()==0 && calls==5,"Absent integer column bypassed the send or invented a row check.");
    item->record_no=1;GameWeaponQuery empty_column(item,GameItemQueryKind::integer,std::string{});
    check(empty_column.resume(messenger,events).completed && empty_column.integer_value()==22,"Empty column name was treated as absent.");
    GameWeaponQuery moved(std::move(empty_column));
    check(moved.integer_value()==22 && !empty_column.completed() && empty_column.faulted(),"Moving a query lost its owned completed result.");
    rejects([&] {empty_column.resume(messenger,events);},"Moved-from query was resumed.");
}

void ordinary_equipment() {
    for(std::int32_t ram=0;ram<4;++ram) {
        GameDataState data;GameDatabaseCache caches;caches.shinka=shinka_database();
        ActionEventRuntime events;auto& messenger=events.messenger();auto database=weapon_database();
        const auto row=data.player(static_cast<std::size_t>(ram));const auto folder=objects(row,"DEVICEFOLDER");
        const auto socket=objects(row,"POWERSOCKET");const auto labels=names(row);
        const auto old=weapon(2,1);folder->push_back(old);socket->push_back(old);labels->push_back("old");
        GameDeviceFolderInitialization initialize(ram);
        check(initialize.resume(data,caches,messenger,events).completed && folder==objects(row,"DEVICEFOLDER") && folder->size()==3,"Initial folder replaced its list owner or missed a weapon.");
        std::array<std::shared_ptr<GameDataWeapon>,3> items;
        for(std::size_t i=0;i<3;++i) {
            items[i]=std::get<std::shared_ptr<GameDataWeapon>>(folder->at(i));
            check(items[i]->record_no==(i==1?1:0) && items[i]->kind_no==0 && items[i]->revision==0 && items[i]->pocket==0 && items[i]->is_equipped==0,"Initial weapon did not receive the original explicit field values.");
        }
        check(items[0]!=items[1] && items[0]!=items[2] && old->is_equipped==1,"Repeated initial records reused an owner or rewrote the old item.");
        unsigned calls{};
        provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&) {
            check(socket->size()==calls+1 && labels->size()==calls && items[calls]->is_equipped==0,"MODEL query did not observe append before name/equipped publication.");
            if(calls)check(items[calls-1]->is_equipped==1,"Previous equipment prefix was not complete before the next query.");
            ++calls;live.buffer().add_database(database);return MessengerCallResult{true,1,{}};
        });
        GamePowerSocketInitialization select(ram);
        check(select.resume(data,caches,messenger,events).completed && calls==3 && socket==objects(row,"POWERSOCKET") && labels==names(row),"Power selection did not retain original inner-list owners or fresh queries.");
        for(std::size_t i=0;i<3;++i)check(std::get<std::shared_ptr<GameDataWeapon>>(socket->at(i))==items[i] && items[i]->is_equipped==1,"Equipment did not preserve its device-folder alias.");
        check(*labels==std::vector<std::optional<std::string>>{"first","","first"},"Power names lost ordered empty-string semantics.");
        check(select.resume(data,caches,messenger,events).completed && initialize.resume(data,caches,messenger,events).completed && calls==3,"Completed equipment initialization replayed a prefix.");
    }
}

void nested_reset_and_live_entries() {
    GameDataState data;GameDatabaseCache caches;ActionEventRuntime events;auto& messenger=events.messenger();
    const auto old_row=data.player(0);const auto old_folder=objects(old_row,"DEVICEFOLDER");old_folder->push_back(weapon(2));
    auto shinka=shinka_database();auto database=weapon_database();unsigned shinka_calls{};bool ready{};
    const auto current=weapon(2);
    provider(events,"shinka",[&](NativeMessenger& live,const MessengerDelivery&) {
        if(++shinka_calls==1) {
            check(old_folder->empty(),"Initial folder was not cleared before shinka delivery.");
            old_row->require<GameDataInt>("DIGIMONNO")->data=1;
            old_row->require<GameDataObjectList>("DEVICEFOLDER")->data=std::make_shared<std::vector<GameDataObject>>();
            data.reset_player(0);objects(data.player(0),"DEVICEFOLDER")->push_back(current);
        }
        if(!ready)return MessengerCallResult{false,0,"nested reset complete, provider pending"};
        live.buffer().add_database(shinka);return MessengerCallResult{true,1,{}};
    });
    GameDeviceFolderInitialization folder(0);
    check(!folder.resume(data,caches,messenger,events).completed && old_folder->empty() && data.player(0)!=old_row,"Folder reset continuation was not retained.");
    GameDataState alien_data;GameDatabaseCache alien_cache;
    rejects([&] {folder.resume(alien_data,caches,messenger,events);},"Pending equipment accepted another generated data owner.");
    rejects([&] {folder.resume(data,alien_cache,messenger,events);},"Pending equipment accepted another manager's database cache.");
    ready=true;check(folder.resume(data,caches,messenger,events).completed && old_folder->size()==3 && std::get<std::shared_ptr<GameDataWeapon>>(old_folder->at(0))->record_no==0 && objects(old_row,"DEVICEFOLDER")->empty(),"Folder retry reloaded row, species or replaced inner list.");
    unsigned calls{};provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&) {++calls;live.buffer().add_database(database);return MessengerCallResult{true,1,{}};});
    GamePowerSocketInitialization socket(0);
    check(socket.resume(data,caches,messenger,events).completed && calls==1 && std::get<std::shared_ptr<GameDataWeapon>>(objects(data.player(0),"POWERSOCKET")->at(0))==current,"Separate power entry reused the preceding phase's captured old row.");
}

void suspended_power_mutations() {
    GameDataState data;GameDatabaseCache caches;ActionEventRuntime events;auto& messenger=events.messenger();
    const auto row=data.player(1);const auto folder=objects(row,"DEVICEFOLDER");const auto socket=objects(row,"POWERSOCKET");const auto labels=names(row);
    const auto first=weapon(0),second=weapon(0),replacement=weapon(2),extra=weapon(0);
    *folder={first,second};auto database=weapon_database();unsigned calls{},prefix{};bool ready{};
    provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&) {
        ++calls;
        if(prefix++==0) {
            check(socket->size()==1 && labels->empty() && first->is_equipped==0,"Pending power query published its later effects early.");
            first->record_no=1;first->kind_no=7;folder->at(1)=replacement;folder->push_back(extra);
            row->require<GameDataObjectList>("POWERSOCKET")->data=std::make_shared<std::vector<GameDataObject>>();
            data.reset_player(1);
        }
        if(!ready)return MessengerCallResult{false,0,"power provider pending"};
        live.buffer().add_database(database);return MessengerCallResult{true,1,{}};
    });
    GamePowerSocketInitialization select(1);
    check(!select.resume(data,caches,messenger,events).completed && socket->size()==1 && labels->empty(),"Suspended power query lost its appended prefix.");
    ready=true;check(select.resume(data,caches,messenger,events).completed && calls==3 && socket->size()==2 && labels->size()==2,"Power retry repeated an append or extended its captured iteration count.");
    check(first->is_equipped==1 && second->is_equipped==0 && replacement->is_equipped==1 && extra->is_equipped==0 && labels->at(0)=="" && labels->at(1)=="third", "Power query lost live record/next-element reads or rechecked an already selected kind.");
    check(objects(row,"POWERSOCKET")->empty() && *objects(data.player(1),"POWERSOCKET")==std::vector<GameDataObject>(3),"Power retry retargeted a replacement wrapper or player row.");
}

void selection_edges() {
    for(unsigned scenario=0;scenario<3;++scenario) {
        GameDataState data;GameDatabaseCache caches;ActionEventRuntime events;auto& messenger=events.messenger();
        const auto row=data.player(0);const auto folder=objects(row,"DEVICEFOLDER");const auto socket=objects(row,"POWERSOCKET");
        auto database=weapon_database();unsigned calls{};
        for(unsigned i=0;i<4;++i)folder->push_back(weapon(0));
        if(scenario==0)row->require<GameDataObjectList>("POWERSOCKET")->data=folder;
        if(scenario==1) {
            folder->insert(folder->begin(),std::make_shared<GameDataDisk>(GameDataDisk{4,0,8,0}));
            folder->insert(folder->begin(),std::make_shared<GameDataWaza>(GameDataWaza{9,0,6,0}));
        }
        provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&) {
            if(scenario==2 && calls==0)for(unsigned i=0;i<3;++i)socket->push_back(weapon(0));
            ++calls;live.buffer().add_database(database);return MessengerCallResult{true,1,{}};
        });
        GamePowerSocketInitialization select(0);check(select.resume(data,caches,messenger,events).completed,"Prepared power selection edge did not complete.");
        if(scenario==0)check(folder->empty() && calls==0,"Aliased folder/socket count was read before the clear.");
        else if(scenario==1)check(socket->size()==3 && calls==3,"Nonweapon kind skip or exact three-item early exit differs.");
        else check(socket->size()==7 && calls==4,"Original count==3 exit was broadened to count>=3 after a nested mutation.");
    }
}

void checked_failures() {
    rejects([] {GameDeviceFolderInitialization value(-1);},"Negative source RAM accepted.");
    rejects([] {GamePowerSocketInitialization value(4);},"Absent source RAM accepted.");
    rejects([] {GameWeaponQuery value({},GameItemQueryKind::integer,"VALUE");},"Null item accepted.");
    rejects([] {GameWeaponQuery value(weapon(0),GameItemQueryKind::string,std::string("MO\0DEL",6));},"Embedded NUL column accepted.");
    for(unsigned malformed=0;malformed<4;++malformed) {
        GameDataState data;GameDatabaseCache caches;ActionEventRuntime events;auto& messenger=events.messenger();
        const auto row=data.player(0);const auto folder=objects(row,"DEVICEFOLDER");const auto socket=objects(row,"POWERSOCKET");socket->clear();socket->push_back(weapon(1));
        if(malformed==0)folder->push_back(std::monostate{});
        else if(malformed==1)folder->push_back(std::make_shared<GameDataDisk>(GameDataDisk{0,0,0,0}));
        else if(malformed==2)row->require<GameDataObjectList>("DEVICEFOLDER")->data.reset();
        else folder->resize(65537);
        GamePowerSocketInitialization select(0);
        rejects([&] {select.resume(data,caches,messenger,events);},"Malformed device list fabricated a supported item.");
        check(select.faulted() && (malformed<2?socket->empty():socket->size()==1),"Checked failure lost its documented publication boundary.");
        rejects([&] {select.resume(data,caches,messenger,events);},"Faulted equipment operation replayed.");
    }
    for(bool throwing:{false,true}) {
        ActionEventRuntime events;auto& messenger=events.messenger();unsigned calls{};auto database=weapon_database();
        provider(events,"weapon",[&](NativeMessenger& live,const MessengerDelivery&)->MessengerCallResult {
            ++calls;if(throwing)throw std::runtime_error("test uncertain provider");
            live.buffer().add_database(database);return {true,1,{}};
        });
        GameWeaponQuery query(weapon(0),GameItemQueryKind::string,"MISSING");
        rejects([&] {query.resume(messenger,events);},"Absent string or uncertain provider fabricated a result.");
        rejects([&] {query.resume(messenger,events);},"Faulted item query replayed provider effects.");
        check(query.faulted() && calls==1,"Item fault did not retain its once-only outcome.");
    }
}

void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto doc=read_igb_document(resources.read("dng101b1/separate/usa_pack.igb"));
    for(std::int32_t ram=0;ram<4;++ram) {
        GameDataState data;GameDatabaseCache caches;ActionEventRuntime events;auto& messenger=events.messenger();
        DatabaseRuntime databases(events);databases.register_source(decode_database_managers(doc,"dng101b1/separate/usa_pack.igb",8));
        GameDeviceFolderInitialization folder(ram);GamePowerSocketInitialization socket(ram);
        check(folder.resume(data,caches,messenger,events).completed && socket.resume(data,caches,messenger,events).completed,"Actual Agumon equipment helpers did not complete.");
        const auto row=data.player(static_cast<std::size_t>(ram));const auto devices=objects(row,"DEVICEFOLDER"),slots=objects(row,"POWERSOCKET");
        const std::array<std::int32_t,3> records{266,275,274};const std::array<std::int32_t,3> skills{0,4,5};
        const std::array<std::string,3> models{"w_common05","w_common04","w_common03"};
        check(caches.shinka->owner_reference()==1879 && devices->size()==3 && slots->size()==3,"Actual initialization used another database or item count.");
        for(std::size_t i=0;i<3;++i) {
            const auto item=std::get<std::shared_ptr<GameDataWeapon>>(devices->at(i));
            check(item==std::get<std::shared_ptr<GameDataWeapon>>(slots->at(i)) && item->record_no==records[i] && item->is_equipped==1 && item->revision==0 && item->pocket==0 && names(row)->at(i)==models[i],"Actual initial weapon fields, alias or MODEL differs.");
            GameWeaponQuery query(item,GameItemQueryKind::integer,"SKILL");
            check(query.resume(messenger,events).completed && query.integer_value()==skills[i] && std::get<MessengerDatabase>(messenger.buffer().objects().at(0))->owner_reference()==1643,"Actual weapon SKILL query lost its database provenance.");
        }
        check(row->require<GameDataBool>("ISUSE")->data==0 && row->require<GameDataInt>("HP")->data==0,"Equipment helpers fabricated complete player initialization.");
    }
}
}
int main(int argc,char** argv) {
    try {
        retained_queries();ordinary_equipment();nested_reset_and_live_entries();suspended_power_mutations();selection_edges();checked_failures();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_game_items_tests [EXTRACTED_ROOT]");
        std::cout<<"Game item checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Game items after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
