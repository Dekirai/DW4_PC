#pragma once

#include "dw4/controllers.hpp"
#include "dw4/collision_math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dw4 {
class GameDataList;
class DatabaseDefinition;
class GameDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The name belongs to the beDataObj wrapper. Its payload and any referenced
// list retain separate, shared identities, including after a player-row reset.
struct GameDataInt { std::optional<std::string> name; std::int32_t data{}; };
struct GameDataBool { std::optional<std::string> name; std::uint8_t data{}; };
struct GameDataString { std::optional<std::string> name,data; };
template<class T> struct GameDataArray {
    std::optional<std::string> name;
    std::shared_ptr<std::vector<T>> data=std::make_shared<std::vector<T>>();
};
using GameDataIntList=GameDataArray<std::int32_t>;
using GameDataBoolList=GameDataArray<std::uint8_t>;
using GameDataStringList=GameDataArray<std::optional<std::string>>;
using GameDataFloatList=GameDataArray<float>;

// beNDMWItemDisk inherits beNDMWItemBase, not igNamedObject.
struct GameDataDisk {
    std::int32_t kind_no{},record_no{};
    std::uint8_t is_equipped{};
    std::int32_t count{};
};
// beNDMWItemWaza also inherits the unnamed ItemBase. Its final field is a
// technique base number, not the ItemDisk count. Population belongs to the
// original selected-player stat calculation, not the generated row defaults.
struct GameDataWaza {
    std::int32_t kind_no{},record_no{};
    std::uint8_t is_equipped{};
    std::int32_t base_no{};
};
struct GameDataWeapon {
    std::int32_t kind_no{},record_no{};
    std::uint8_t is_equipped{};
    std::int32_t revision{},pocket{};
};
// beUnicodeObj inherits igNamedObject. A fresh buffer is unallocated; actual
// conversion/name construction allocates its own array of 16-bit code units.
struct GameDataUnicode {
    std::optional<std::string> name;
    std::shared_ptr<std::vector<std::uint16_t>> buffer;
};
struct GameDataObjectList;
class ActorSceneAppearanceList;
class ActorSceneSkeleton;
class ModelPlayerWork;
using GameDataObject=std::variant<std::monostate,
    std::shared_ptr<GameDataInt>,std::shared_ptr<GameDataIntList>,
    std::shared_ptr<GameDataBool>,std::shared_ptr<GameDataBoolList>,
    std::shared_ptr<GameDataString>,std::shared_ptr<GameDataStringList>,
    std::shared_ptr<GameDataObjectList>,std::shared_ptr<GameDataFloatList>,
    std::shared_ptr<GameDataDisk>,std::shared_ptr<GameDataWaza>,std::shared_ptr<GameDataWeapon>,
    std::shared_ptr<GameDataUnicode>,ControllerObjectId,std::shared_ptr<const CollisionBox>,
    std::shared_ptr<GameDataList>,std::shared_ptr<const DatabaseDefinition>,
    std::shared_ptr<ActorSceneAppearanceList>,std::shared_ptr<const ActorSceneSkeleton>,
    std::shared_ptr<ModelPlayerWork>>;
struct GameDataObjectList : GameDataArray<GameDataObject> {};

enum class GameDataKind : std::uint8_t {
    integer=0,integer_list=1,boolean=2,boolean_list=3,string=4,
    string_list=5,object_list=6,float_list=7
};
struct GameDataSchemaField {
    GameDataKind kind;
    std::size_t count;
    std::string_view name;
};
std::span<const GameDataSchemaField> game_common_schema() noexcept;
std::span<const GameDataSchemaField> game_player_schema() noexcept;

// Ordered beDataObjList. Null names are skipped; empty names are real names.
// Lookup compares exact bytes and stops at the first matching name, before
// checking its type. Returning a shared wrapper never copies its live payload.
class GameDataList {
public:
    GameDataList()=default;
    explicit GameDataList(std::vector<GameDataObject> entries);
    const std::vector<GameDataObject>& entries() const noexcept { return entries_; }
    const GameDataObject* find(std::string_view name) const;
    template<class T> std::shared_ptr<T> find(std::string_view name) const {
        const auto* object=find(name);
        if(!object)return {};
        const auto* result=std::get_if<std::shared_ptr<T>>(object);
        if(!result || !*result)throw GameDataError("The first named game-data object has a different type.");
        return *result;
    }
    template<class T> std::shared_ptr<T> require(std::string_view name) const {
        auto result=find<T>(name);
        if(!result)throw GameDataError("The required named game-data object is absent.");
        return result;
    }
private:
    std::vector<GameDataObject> entries_;
    friend class GameDataState;
};

// Native 8036D224/8036CE1C generated state, before selection, save loading or
// stat/equipment calculation. This is not a complete initialized game session.
class GameDataState {
public:
    static constexpr std::size_t player_count=4;
    GameDataState();
    GameDataState(const GameDataState&)=delete;
    GameDataState& operator=(const GameDataState&)=delete;
    GameDataState(GameDataState&&)=delete;
    GameDataState& operator=(GameDataState&&)=delete;
    const std::shared_ptr<GameDataList>& common() const noexcept { return common_; }
    const std::shared_ptr<GameDataList>& player(std::size_t index) const;
    // Row zero also repopulates the SAME common-list object. Every reset
    // replaces the selected player row; retained old rows/wrappers stay alive.
    void reset_player(std::size_t index);
private:
    std::shared_ptr<GameDataList> common_;
    std::array<std::shared_ptr<GameDataList>,player_count> players_;
};
} // namespace dw4
