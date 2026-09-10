#include "dw4/game_items.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/database.hpp"
#include "dw4/game_database.hpp"

#include <array>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t maximum_items=65536;
const std::string empty_diagnostic;
const std::shared_ptr<GameDataWeapon> absent_item;
[[noreturn]] void fail(const char* message) { throw GameDataError(message); }
void check_ram(std::int32_t ram) {
    if(ram<0 || ram>=static_cast<std::int32_t>(GameDataState::player_count))
        fail("Original equipment initialization requires an existing player RAM from zero through three.");
}
template<class T> void check_list(const std::shared_ptr<std::vector<T>>& list) {
    if(!list || list->size()>maximum_items)
        fail("Original equipment initialization requires an owned list with at most 65536 entries.");
}
template<class T> void check_append(const std::shared_ptr<std::vector<T>>& list) {
    check_list(list);
    if(list->size()==maximum_items)fail("Original equipment append exceeds the checked list bound.");
}
struct Invocation {
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    bool running{},fault{},complete{};
    std::string diagnostic;
    void bind(NativeMessenger& live,ActionEventRuntime& runtime) {
        if(fault)fail("Original item operation has an uncertain outcome; replay is forbidden.");
        if(running)fail("Original item operation cannot recursively resume itself.");
        if(messenger && (messenger!=&live || events!=&runtime))
            fail("Original item operation belongs to another messenger or runtime.");
        messenger=&live;events=&runtime;
    }
    MessengerCallResult finish() {
        complete=true;diagnostic.clear();return {true,1,{}};
    }
    MessengerCallResult pause(const MessengerCallResult& result) {
        diagnostic=result.diagnostic;return {false,0,diagnostic};
    }
};
struct Running {
    bool& flag;
    explicit Running(bool& value):flag(value) {flag=true;}
    ~Running() {flag=false;}
};
struct EquipmentInvocation : Invocation {
    std::int32_t ram;
    GameDataState* data{};
    GameDatabaseCache* caches{};
    std::shared_ptr<GameDataList> row;
    explicit EquipmentInvocation(std::int32_t source):ram(source) {check_ram(source);}
    void bind(GameDataState& state,GameDatabaseCache& cache,NativeMessenger& live,ActionEventRuntime& runtime) {
        Invocation::bind(live,runtime);
        if(data && (data!=&state || caches!=&cache))
            fail("Original equipment operation belongs to another game-data or database-cache owner.");
        data=&state;caches=&cache;
    }
};
std::int32_t item_kind(const GameDataObject& object) {
    if(const auto* value=std::get_if<std::shared_ptr<GameDataWeapon>>(&object);value && *value)return (*value)->kind_no;
    if(const auto* value=std::get_if<std::shared_ptr<GameDataDisk>>(&object);value && *value)return (*value)->kind_no;
    if(const auto* value=std::get_if<std::shared_ptr<GameDataWaza>>(&object);value && *value)return (*value)->kind_no;
    fail("Original DEVICEFOLDER contains an absent or unsupported item owner.");
}
}

struct GameWeaponQuery::Impl : Invocation {
    std::shared_ptr<GameDataWeapon> item;
    GameItemQueryKind kind;
    std::optional<std::string> column;
    GameDatabaseQuery query{"weapon"};
    std::int32_t integer{};
    std::string text;
    Impl(std::shared_ptr<GameDataWeapon> owner,GameItemQueryKind type,std::optional<std::string> name)
        :item(std::move(owner)),kind(type),column(std::move(name)) {
        if(!item)fail("Original weapon query requires its actual item owner.");
        if(kind!=GameItemQueryKind::integer && kind!=GameItemQueryKind::string)
            fail("Original weapon query has an unsupported value kind.");
        if(column && (column->size()>4096 || column->find('\0')!=std::string::npos))
            fail("Original weapon query requires a bounded column name without embedded NUL bytes.");
    }
};
GameWeaponQuery::GameWeaponQuery(std::shared_ptr<GameDataWeapon> item,GameItemQueryKind kind,std::optional<std::string> column)
    :impl_(std::make_unique<Impl>(std::move(item),kind,std::move(column))) {}
GameWeaponQuery::~GameWeaponQuery()=default;
GameWeaponQuery::GameWeaponQuery(GameWeaponQuery&&) noexcept=default;
GameWeaponQuery& GameWeaponQuery::operator=(GameWeaponQuery&&) noexcept=default;
MessengerCallResult GameWeaponQuery::resume(NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!impl_)fail("A moved-from weapon query cannot be resumed.");
    auto& state=*impl_;state.bind(messenger,events);
    if(state.complete)return {true,1,{}};
    Running running(state.running);
    try {
        const auto result=state.query.resume(messenger,events);
        if(!result.completed)return state.pause(result);
        const auto& database=state.query.database();
        const auto column=state.column ? std::optional<std::string_view>(*state.column):std::nullopt;
        // 803B06F0/803B0790 reload +0C after virtual+5C completes.
        if(state.kind==GameItemQueryKind::integer)state.integer=database->integer(state.item->record_no,column);
        else state.text=database->string(state.item->record_no,column);
        return state.finish();
    }catch(...) {state.fault=true;throw;}
}
bool GameWeaponQuery::completed() const noexcept {return impl_ && impl_->complete;}
bool GameWeaponQuery::faulted() const noexcept {return !impl_ || impl_->fault;}
const std::string& GameWeaponQuery::diagnostic() const noexcept {return impl_?impl_->diagnostic:empty_diagnostic;}
const std::shared_ptr<GameDataWeapon>& GameWeaponQuery::item() const noexcept {return impl_?impl_->item:absent_item;}
std::int32_t GameWeaponQuery::integer_value() const {
    if(!completed() || impl_->kind!=GameItemQueryKind::integer)fail("Original weapon integer query has no completed integer result.");
    return impl_->integer;
}
const std::string& GameWeaponQuery::string_value() const {
    if(!completed() || impl_->kind!=GameItemQueryKind::string)fail("Original weapon string query has no completed string result.");
    return impl_->text;
}

struct GameDeviceFolderInitialization::Impl : EquipmentInvocation {
    std::shared_ptr<std::vector<GameDataObject>> folder;
    std::int32_t species{};
    std::size_t index{};
    std::shared_ptr<GameDataWeapon> item;
    std::optional<GameDatabaseQuery> query;
    using EquipmentInvocation::EquipmentInvocation;
};
GameDeviceFolderInitialization::GameDeviceFolderInitialization(std::int32_t source)
    :impl_(std::make_unique<Impl>(source)) {}
GameDeviceFolderInitialization::~GameDeviceFolderInitialization()=default;
GameDeviceFolderInitialization::GameDeviceFolderInitialization(GameDeviceFolderInitialization&&) noexcept=default;
GameDeviceFolderInitialization& GameDeviceFolderInitialization::operator=(GameDeviceFolderInitialization&&) noexcept=default;
MessengerCallResult GameDeviceFolderInitialization::resume(GameDataState& data,GameDatabaseCache& caches,
    NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!impl_)fail("A moved-from folder initialization cannot be resumed.");
    auto& state=*impl_;state.bind(data,caches,messenger,events);
    if(state.complete)return {true,1,{}};
    Running running(state.running);
    try {
        if(!state.row) {
            const auto row=data.player(static_cast<std::size_t>(state.ram));
            const auto folder=row->require<GameDataObjectList>("DEVICEFOLDER")->data;
            const auto species=row->require<GameDataInt>("DIGIMONNO")->data;
            check_list(folder);
            state.row=row;state.folder=folder;state.species=species;
            // Publish the clear before either item allocation or shinka send.
            state.folder->clear();
        }
        constexpr std::array<const char*,3> columns{"INI_WEAPON1","INI_WEAPON2","INI_WEAPON3"};
        while(state.index<columns.size()) {
            if(!state.item)state.item=std::make_shared<GameDataWeapon>();
            if(state.query || !caches.shinka) {
                if(!state.query)state.query.emplace("shinka");
                const auto result=state.query->resume(messenger,events);
                if(!result.completed)return state.pause(result);
                caches.shinka=state.query->database();state.query.reset();
            }
            const auto record=caches.shinka->integer(state.species,columns[state.index]);
            *state.item=GameDataWeapon{0,record,0,0,0};
            check_append(state.folder);
            state.folder->push_back(state.item);
            state.item.reset();++state.index;
        }
        return state.finish();
    }catch(...) {state.fault=true;throw;}
}
std::int32_t GameDeviceFolderInitialization::source_ram() const noexcept {return impl_?impl_->ram:-1;}
bool GameDeviceFolderInitialization::completed() const noexcept {return impl_ && impl_->complete;}
bool GameDeviceFolderInitialization::faulted() const noexcept {return !impl_ || impl_->fault;}
const std::string& GameDeviceFolderInitialization::diagnostic() const noexcept {return impl_?impl_->diagnostic:empty_diagnostic;}

struct GamePowerSocketInitialization::Impl : EquipmentInvocation {
    std::shared_ptr<std::vector<GameDataObject>> folder,socket;
    std::shared_ptr<std::vector<std::optional<std::string>>> names;
    std::size_t count{},index{};
    std::optional<GameWeaponQuery> query;
    using EquipmentInvocation::EquipmentInvocation;
};
GamePowerSocketInitialization::GamePowerSocketInitialization(std::int32_t source)
    :impl_(std::make_unique<Impl>(source)) {}
GamePowerSocketInitialization::~GamePowerSocketInitialization()=default;
GamePowerSocketInitialization::GamePowerSocketInitialization(GamePowerSocketInitialization&&) noexcept=default;
GamePowerSocketInitialization& GamePowerSocketInitialization::operator=(GamePowerSocketInitialization&&) noexcept=default;
MessengerCallResult GamePowerSocketInitialization::resume(GameDataState& data,GameDatabaseCache& caches,
    NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!impl_)fail("A moved-from power-socket initialization cannot be resumed.");
    auto& state=*impl_;state.bind(data,caches,messenger,events);
    if(state.complete)return {true,1,{}};
    Running running(state.running);
    try {
        if(!state.row) {
            const auto row=data.player(static_cast<std::size_t>(state.ram));
            const auto folder=row->require<GameDataObjectList>("DEVICEFOLDER")->data;
            const auto socket=row->require<GameDataObjectList>("POWERSOCKET")->data;
            const auto names=row->require<GameDataStringList>("POWERSOCKETNAME")->data;
            check_list(folder);check_list(socket);check_list(names);
            state.row=row;state.folder=folder;state.socket=socket;state.names=names;
            state.socket->clear();state.names->clear();
            // The original reads DEVICEFOLDER count after both clears, which
            // matters when its inner list aliases POWERSOCKET's inner list.
            state.count=state.folder->size();
        }
        while(state.index<state.count) {
            if(!state.query) {
                check_list(state.socket);
                // Original equality, not a greater-than-or-equal capacity cap.
                if(state.socket->size()==3)return state.finish();
                check_list(state.folder);
                if(state.index>=state.folder->size())fail("Original DEVICEFOLDER storage no longer contains its captured iteration element.");
                const auto object=state.folder->at(state.index);
                if(item_kind(object)!=0) {++state.index;continue;}
                const auto* weapon=std::get_if<std::shared_ptr<GameDataWeapon>>(&object);
                if(!weapon || !*weapon)fail("Original kind-zero equipment requires its supported weapon dynamic type.");
                state.query.emplace(*weapon,GameItemQueryKind::string,"MODEL");
                check_append(state.socket);state.socket->push_back(*weapon);
            }
            const auto result=state.query->resume(messenger,events);
            if(!result.completed)return state.pause(result);
            check_append(state.names);
            state.names->emplace_back(state.query->string_value());
            state.query->item()->is_equipped=1;
            state.query.reset();++state.index;
        }
        return state.finish();
    }catch(...) {state.fault=true;throw;}
}
std::int32_t GamePowerSocketInitialization::source_ram() const noexcept {return impl_?impl_->ram:-1;}
bool GamePowerSocketInitialization::completed() const noexcept {return impl_ && impl_->complete;}
bool GamePowerSocketInitialization::faulted() const noexcept {return !impl_ || impl_->fault;}
const std::string& GamePowerSocketInitialization::diagnostic() const noexcept {return impl_?impl_->diagnostic:empty_diagnostic;}
} // namespace dw4
