#include "dw4/game_data.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace dw4 {
namespace {
using K=GameDataKind;
constexpr std::array<GameDataSchemaField,13> common_schema{{
    {K::integer,0,"NINZU"},{K::integer,0,"DIFFICULTY"},
    {K::integer,0,"CAMERAMODE"},{K::integer,0,"LIGHTMODE"},
    {K::boolean_list,1024,"FLAG"},{K::boolean_list,12,"FLAGFOLDER"},
    {K::integer_list,16,"COUNTER"},{K::string_list,4,"BKCHIPSTAGE"},
    {K::float_list,12,"BKCHIPPOS"},{K::integer_list,8,"BKCHIPDATA"},
    {K::string_list,4,"BKCHIPNAME"},{K::string,0,"GATESTAGENAME"},
    {K::integer,0,"GATESTAGENO"}
}};
constexpr std::array<GameDataSchemaField,64> player_schema{{
    {K::integer,0,"UNIQUE"},{K::object_list,0,"PLAYERNAME"},
    {K::integer,0,"XDATA"},{K::integer,0,"BIT"},
    {K::integer,0,"DIGIMONNO"},{K::integer,0,"LEVEL"},
    {K::integer_list,9,"SKILL"},{K::integer_list,11,"UPCNT"},
    {K::string,0,"DIGIMONNAME"},{K::integer,0,"EXP"},
    {K::boolean,0,"ISUSE"},{K::integer,0,"HP"},
    {K::integer,0,"MHP"},{K::integer,0,"MP"},
    {K::integer,0,"MMP"},{K::integer,0,"TSUYOSA"},
    {K::integer,0,"MAMORI"},{K::integer,0,"KASHIKOSA"},
    {K::integer,0,"SEISHIN"},{K::integer,0,"SUBAYASA"},
    {K::integer,0,"HONOOTEIKO"},{K::integer,0,"KOORITEIKO"},
    {K::integer,0,"KAMINARITEIKO"},{K::integer,0,"YAMITEIKO"},
    {K::object_list,0,"WAZALIST"},{K::integer,0,"NEXTEXP"},
    {K::integer,0,"ATTACKDELAY"},{K::integer,0,"MEICHURITSU"},
    {K::integer,0,"CHIMEIRITSU"},{K::integer,0,"MAHIRITSU"},
    {K::integer,0,"KAIHIRITSU"},{K::integer,0,"BOUGYORITSU"},
    {K::integer,0,"MAHITEIKOU"},{K::integer,0,"DOKUTEIKOU"},
    {K::integer,0,"BITRITSU"},{K::integer,0,"EXPRITSU"},
    {K::integer,0,"LUCKRITSU"},{K::integer,0,"ATTACKZOKUSEI"},
    {K::integer,0,"HPDRAIN"},{K::integer,0,"MPDRAIN"},
    {K::integer,0,"CHOHATURITSU"},{K::integer,0,"WEAPONRECNO"},
    {K::integer,0,"WEAPONSKILLNO"},{K::object_list,0,"DEVICEFOLDER"},
    {K::object_list,0,"DISKFOLDER"},{K::boolean_list,50,"CARDLIST"},
    {K::object_list,0,"BANKDEVICE"},{K::integer,0,"BANKBIT"},
    {K::object_list,3,"POWERSOCKET"},{K::string_list,0,"POWERSOCKETNAME"},
    {K::object_list,5,"POWERPOCKET"},{K::object_list,1,"MAINSLOT"},
    {K::object_list,5,"MAINPOCKET"},{K::object_list,1,"SUBSLOT"},
    {K::boolean_list,1024,"BASE_FLAG"},{K::boolean_list,12,"BASE_FLAGFOLDER"},
    {K::integer_list,16,"BASE_COUNTER"},{K::boolean_list,16,"BASE_ISUSE"},
    {K::integer_list,16,"BASE_LEVEL"},{K::integer_list,16,"BASE_EXP"},
    {K::integer_list,144,"BASE_SKILL"},{K::integer_list,176,"BASE_UPCNT"},
    {K::integer_list,11,"UPDATA"},{K::integer_list,7,"DOWNDATA"}
}};

void check_text(std::string_view value) {
    if(value.size()>4096 || value.find('\0')!=std::string_view::npos)
        throw GameDataError("Game-data text exceeds the checked string profile.");
}
const std::optional<std::string>& object_name(const GameDataObject& object) {
    return std::visit([](const auto& pointer)->const std::optional<std::string>& {
        if constexpr(!requires {pointer->name;})
            throw GameDataError("A named game-data row cannot contain a null or an unnamed item object.");
        else {
            if(!pointer)throw GameDataError("A named game-data row contains an absent wrapper.");
            if(pointer->name)check_text(*pointer->name);
            return pointer->name;
        }
    },object);
}

// Only this bounded native object profile is represented. The fixed generated
// graph is shallow; the depth/aggregate limits also reject cyclic host graphs.
struct ValidationBudget { std::size_t objects{},elements{},text{}; };
void validate_object(const GameDataObject& object,ValidationBudget& budget,std::size_t depth) {
    if(depth>32 || ++budget.objects>65536)
        throw GameDataError("Game-data objects exceed the checked allocation budget.");
    auto text=[&](const std::optional<std::string>& value) {
        if(!value)return;
        check_text(*value);
        if(value->size()>1048576-budget.text)
            throw GameDataError("Game-data text exceeds the aggregate allocation budget.");
        budget.text+=value->size();
    };
    std::visit([&](const auto& pointer) {
        using T=std::decay_t<decltype(pointer)>;
        if constexpr(std::is_same_v<T,ControllerObjectId>) {
            if(!pointer.valid())throw GameDataError("Game-data controller object mixes identity domains.");
        } else if constexpr(!std::is_same_v<T,std::monostate>) {
            if(!pointer)throw GameDataError("Use a null object slot, not an absent typed game-data wrapper.");
            if constexpr(std::is_same_v<T,std::shared_ptr<GameDataUnicode>>) {
                text(pointer->name);
                if(pointer->buffer) {
                    if(pointer->buffer->size()>65536-budget.elements)throw GameDataError("Unicode arrays exceed the game-data allocation budget.");
                    budget.elements+=pointer->buffer->size();
                }
            } else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataList>>) {
                for(const auto& value:pointer->entries())validate_object(value,budget,depth+1);
            } else if constexpr(requires {pointer->name;pointer->data;}) {
                text(pointer->name);
                if constexpr(std::is_same_v<T,std::shared_ptr<GameDataString>>)text(pointer->data);
                else if constexpr(!std::is_same_v<T,std::shared_ptr<GameDataInt>> &&
                                  !std::is_same_v<T,std::shared_ptr<GameDataBool>>) {
                    if(!pointer->data)throw GameDataError("A game-data array has no list owner.");
                    if(pointer->data->size()>65536-budget.elements)
                        throw GameDataError("Game-data arrays exceed the aggregate allocation budget.");
                    budget.elements+=pointer->data->size();
                    if constexpr(std::is_same_v<T,std::shared_ptr<GameDataStringList>>)
                        for(const auto& value:*pointer->data)text(value);
                    else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataFloatList>>) {
                        for(float value:*pointer->data)if(!std::isfinite(value))
                            throw GameDataError("A game-data float list contains a nonfinite value.");
                    } else if constexpr(std::is_same_v<T,std::shared_ptr<GameDataObjectList>>)
                        for(const auto& value:*pointer->data)validate_object(value,budget,depth+1);
                }
            }
        }
    },object);
}
template<class T> GameDataObject scalar(const GameDataSchemaField& field) {
    auto value=std::make_shared<T>();value->name=field.name;return value;
}
template<class T> GameDataObject array(const GameDataSchemaField& field) {
    auto value=std::make_shared<T>();value->name=field.name;value->data->resize(field.count);return value;
}
std::shared_ptr<GameDataList> make_row(std::span<const GameDataSchemaField> schema) {
    std::vector<GameDataObject> objects;objects.reserve(schema.size());
    for(const auto& field:schema) {
        switch(field.kind) {
        case K::integer:objects.push_back(scalar<GameDataInt>(field));break;
        case K::integer_list:objects.push_back(array<GameDataIntList>(field));break;
        case K::boolean:objects.push_back(scalar<GameDataBool>(field));break;
        case K::boolean_list:objects.push_back(array<GameDataBoolList>(field));break;
        case K::string:objects.push_back(scalar<GameDataString>(field));break;
        case K::string_list:objects.push_back(array<GameDataStringList>(field));break;
        case K::object_list:objects.push_back(array<GameDataObjectList>(field));break;
        case K::float_list:objects.push_back(array<GameDataFloatList>(field));break;
        }
    }
    return std::make_shared<GameDataList>(std::move(objects));
}
std::shared_ptr<GameDataList> make_player() {
    auto row=make_row(player_schema);
    auto levels=row->require<GameDataIntList>("BASE_LEVEL");
    std::fill(levels->data->begin(),levels->data->end(),1);
    auto skills=row->require<GameDataIntList>("BASE_SKILL");
    std::fill(skills->data->begin(),skills->data->end(),1);
    auto disks=row->require<GameDataObjectList>("DISKFOLDER");
    disks->data->reserve(12);
    for(std::int32_t index=0;index<12;++index)
        disks->data->push_back(std::make_shared<GameDataDisk>(GameDataDisk{4,index,0,0}));
    row->require<GameDataBoolList>("BASE_FLAG")->data->at(960)=1;
    return row;
}
void check_player(std::size_t index) {
    if(index>=GameDataState::player_count)throw GameDataError("The generated game-data player index is outside 0..3.");
}
} // namespace

std::span<const GameDataSchemaField> game_common_schema() noexcept { return common_schema; }
std::span<const GameDataSchemaField> game_player_schema() noexcept { return player_schema; }
GameDataList::GameDataList(std::vector<GameDataObject> entries):entries_(std::move(entries)) {
    if(entries_.size()>4096)throw GameDataError("The named game-data row exceeds the checked object budget.");
    ValidationBudget budget;
    for(const auto& entry:entries_) {
        object_name(entry);
        validate_object(entry,budget,0);
    }
}
const GameDataObject* GameDataList::find(std::string_view name) const {
    check_text(name);
    for(const auto& entry:entries_) {
        const auto& candidate=object_name(entry);
        if(candidate && *candidate==name)return &entry;
    }
    return nullptr;
}
GameDataState::GameDataState():common_(make_row(common_schema)) {
    for(auto& row:players_)row=make_player();
}
const std::shared_ptr<GameDataList>& GameDataState::player(std::size_t index) const {
    check_player(index);return players_[index];
}
void GameDataState::reset_player(std::size_t index) {
    check_player(index);
    auto row=make_player();
    auto common=index==0?make_row(common_schema):nullptr;
    // Checked allocation failures publish no partial reset. Original CE1C
    // clears common first; this differs only at a native allocation failure.
    if(common)common_->entries_.swap(common->entries_);
    players_[index]=std::move(row);
}
} // namespace dw4
