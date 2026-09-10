#include "dw4/game_start.hpp"
#include "dw4/database.hpp"
#include "dw4/game_items.hpp"
#include "dw4/game_name.hpp"
#include "dw4/game_stats.hpp"

#include <algorithm>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw GameDataError(message);}
std::size_t player_index(std::int32_t value) {
    if(value<0 || value>=4)fail("Original player initialization requires an existing player row.");
    return static_cast<std::size_t>(value);
}
void text_check(const std::optional<std::string>& text) {
    if(text && (text->size()>4096 || text->find('\0')!=std::string::npos))
        fail("Original player initialization text exceeds the checked profile.");
}
struct Owners {
    GameDataState* data{};GameDatabaseCache* caches{};NativeMessenger* messenger{};ActionEventRuntime* events{};
    bool executing{},fault{},complete{};
    void check(GameDataState& d,GameDatabaseCache& c,NativeMessenger& m,ActionEventRuntime& e) {
        if(fault)fail("Uncertain player initialization cannot be replayed.");
        if(executing || (data && (data!=&d || caches!=&c || messenger!=&m || events!=&e)))
            fail("Retained player initialization requires the same non-recursive owners.");
        data=&d;caches=&c;messenger=&m;events=&e;
    }
};
struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}};
std::int32_t array_read(const GameDataList& row,std::string_view field,std::int32_t index) {
    const auto values=row.require<GameDataIntList>(field)->data;
    if(!values || index<0 || static_cast<std::size_t>(index)>=values->size())
        fail("Original BASE2CURRENT requires its actual source array element.");
    return values->at(static_cast<std::size_t>(index));
}
void copy_array(GameDataList& row,std::string_view source,std::string_view target,std::int32_t species,std::uint32_t count) {
    const auto base=std::bit_cast<std::uint32_t>(species)*count;
    for(std::uint32_t i=0;i<count;++i) {
        const auto value=array_read(row,source,std::bit_cast<std::int32_t>(base+i));
        const auto destination=row.require<GameDataIntList>(target)->data;
        if(!destination)fail("Original BASE2CURRENT destination has no array owner.");
        // The original setter silently omits out-of-range writes; its source
        // access happened first and was not protected by that destination gate.
        if(i<destination->size())destination->at(i)=value;
    }
}
MessengerCallResult acquire_shinka(std::optional<GameDatabaseQuery>& query,GameDatabaseCache& cache,
    NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!query && cache.shinka)return {true,1,{}};
    if(!query)query.emplace("shinka");
    const auto result=query->resume(messenger,events);
    if(!result.completed)return result;
    cache.shinka=query->database();query.reset();return {true,1,{}};
}
}

struct GameBaseToCurrent::Impl : Owners {
    std::int32_t source{},species{};
    std::shared_ptr<GameDataList> row;
    std::optional<GameDatabaseQuery> query;
};
GameBaseToCurrent::GameBaseToCurrent(std::int32_t source,std::int32_t species):impl_(std::make_unique<Impl>()) {
    impl_->source=source;impl_->species=species;
}
GameBaseToCurrent::~GameBaseToCurrent()=default;
GameBaseToCurrent::GameBaseToCurrent(GameBaseToCurrent&&) noexcept=default;
GameBaseToCurrent& GameBaseToCurrent::operator=(GameBaseToCurrent&&) noexcept=default;
bool GameBaseToCurrent::completed() const noexcept {return impl_ && impl_->complete;}
bool GameBaseToCurrent::faulted() const noexcept {return !impl_ || impl_->fault;}
MessengerCallResult GameBaseToCurrent::resume(GameDataState& data,GameDatabaseCache& caches,NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!impl_)fail("Moved player initialization has no retained work.");
    auto& state=*impl_;state.check(data,caches,messenger,events);if(state.complete)return {true,1,{}};
    Guard guard(state.executing);
    try {
        if(!state.row) {
            state.row=data.player(player_index(state.source));
            state.row->require<GameDataInt>("LEVEL");
            state.row->require<GameDataInt>("DIGIMONNO")->data=state.species;
        }
        const auto fetched=acquire_shinka(state.query,caches,messenger,events);
        if(!fetched.completed)return fetched;
        auto& row=*state.row;
        row.require<GameDataString>("DIGIMONNAME")->data=std::string(caches.shinka->string(state.species,"MODEL_NAME"));
        const auto level=array_read(row,"BASE_LEVEL",state.species);row.require<GameDataInt>("LEVEL")->data=level;
        const auto experience=array_read(row,"BASE_EXP",state.species);row.require<GameDataInt>("EXP")->data=experience;
        copy_array(row,"BASE_SKILL","SKILL",state.species,9);
        copy_array(row,"BASE_UPCNT","UPCNT",state.species,11);
        if(state.source==0) {
            const auto flags=row.require<GameDataBoolList>("BASE_FLAG")->data;
            data.common()->require<GameDataBoolList>("FLAG")->data=flags;
            const auto folders=row.require<GameDataBoolList>("BASE_FLAGFOLDER")->data;
            data.common()->require<GameDataBoolList>("FLAGFOLDER")->data=folders;
            const auto counters=row.require<GameDataIntList>("BASE_COUNTER")->data;
            data.common()->require<GameDataIntList>("COUNTER")->data=counters;
        }
        state.complete=true;return {true,1,{}};
    }catch(...){state.fault=true;throw;}
}

struct GameStartRuntime::Impl : Owners {
    enum class Phase { begin,species,base,device,power,stats,publication,name_row,name,done } phase=Phase::begin;
    std::int32_t source{},species{};
    std::optional<std::string> model;
    GameStartName input_name;
    std::shared_ptr<GameDataList> common,row,name_row;
    std::optional<GameDatabaseQuery> query;
    std::optional<GameBaseToCurrent> base;
    std::optional<GameDeviceFolderInitialization> device;
    std::optional<GamePowerSocketInitialization> power;
    std::optional<GameStatsRuntime> stats;
    std::shared_ptr<GameNameConverter> font;
};
GameStartRuntime::GameStartRuntime(std::int32_t source,std::optional<std::string> model,GameStartName name):impl_(std::make_unique<Impl>()) {
    text_check(model);
    if(const auto* text=std::get_if<std::optional<std::string>>(&name))text_check(*text);
    impl_->source=source;impl_->model=std::move(model);impl_->input_name=std::move(name);
}
GameStartRuntime::~GameStartRuntime()=default;
GameStartRuntime::GameStartRuntime(GameStartRuntime&&) noexcept=default;
GameStartRuntime& GameStartRuntime::operator=(GameStartRuntime&&) noexcept=default;
bool GameStartRuntime::completed() const noexcept {return impl_ && impl_->complete;}
bool GameStartRuntime::faulted() const noexcept {return !impl_ || impl_->fault;}
MessengerCallResult GameStartRuntime::resume(GameDataState& data,GameDatabaseCache& caches,NativeMessenger& messenger,
    ActionEventRuntime& events,const GameNameServices& names) {
    if(!impl_)fail("Moved GAMESTART has no retained work.");
    auto& state=*impl_;state.check(data,caches,messenger,events);if(state.complete)return {true,1,{}};
    Guard guard(state.executing);
    try {
        for(;;)switch(state.phase) {
        case Impl::Phase::begin:
            state.common=data.common();state.row=data.player(player_index(state.source));
            state.phase=Impl::Phase::species;break;
        case Impl::Phase::species: {
            const auto fetched=acquire_shinka(state.query,caches,messenger,events);
            if(!fetched.completed)return fetched;
            for(;state.species<16;++state.species) {
                const auto value=caches.shinka->string(state.species,"MODEL_NAME");
                if(state.model && *state.model==value) {
                    // Real authored names are nonempty. The empty input versus
                    // null-cell static-literal pointer relation is unverified.
                    if(value.empty()) {
                        const auto& group=caches.shinka->string_group();
                        const auto column=std::find_if(group.columns.begin(),group.columns.end(),[](const auto& name){return name.name=="MODEL_NAME";});
                        const auto index=static_cast<std::size_t>(state.species)*group.columns.size()+static_cast<std::size_t>(column-group.columns.begin());
                        if(!group.values.at(index).name)return {false,0,"Original GAMESTART empty interned name versus null MODEL_NAME cell requires its string-identity service."};
                    }
                    break;
                }
            }
            if(state.species==16)state.species=0;
            state.base.emplace(state.source,state.species);state.phase=Impl::Phase::base;break;
        }
        case Impl::Phase::base: {
            const auto result=state.base->resume(data,caches,messenger,events);if(!result.completed)return result;
            state.base.reset();state.device.emplace(state.source);state.phase=Impl::Phase::device;break;
        }
        case Impl::Phase::device: {
            const auto result=state.device->resume(data,caches,messenger,events);if(!result.completed)return result;
            state.device.reset();state.power.emplace(state.source);state.phase=Impl::Phase::power;break;
        }
        case Impl::Phase::power: {
            const auto result=state.power->resume(data,caches,messenger,events);if(!result.completed)return result;
            state.power.reset();state.stats.emplace(state.source,std::uint8_t{1});state.phase=Impl::Phase::stats;break;
        }
        case Impl::Phase::stats: {
            const auto result=state.stats->resume(data,caches,messenger,events);if(!result.completed)return result;
            state.stats.reset();state.phase=Impl::Phase::publication;break;
        }
        case Impl::Phase::publication: {
            state.row->require<GameDataBool>("ISUSE")->data=1;
            const auto used=state.row->require<GameDataBoolList>("BASE_ISUSE")->data;
            if(!used)fail("Original GAMESTART BASE_ISUSE has no array owner.");
            if(state.species>=0 && static_cast<std::size_t>(state.species)<used->size())used->at(static_cast<std::size_t>(state.species))=1;
            std::int32_t count{};
            for(std::size_t i=0;i<4;++i)if(data.player(i)->require<GameDataBool>("ISUSE")->data!=0)++count;
            state.common->require<GameDataInt>("NINZU")->data=count;
            state.phase=Impl::Phase::name_row;break;
        }
        case Impl::Phase::name_row:
            state.name_row=data.player(player_index(state.source));state.phase=Impl::Phase::name;break;
        case Impl::Phase::name: {
            GameDataObject object;
            if(const auto* captured=std::get_if<GameDataObject>(&state.input_name))object=*captured;
            else {
                if(!state.font) {
                    const auto resolver=names.font;
                    state.font=resolver?resolver():nullptr;
                    if(!state.font)return {false,0,"Original GAMESTART requires the actual same-context beFont conversion owner."};
                }
                const auto& text=std::get<std::optional<std::string>>(state.input_name);
                const auto converted=state.font->convert(text?std::optional<std::string_view>(*text):std::nullopt);
                object=make_player_name(*converted);
            }
            const auto target=state.name_row->require<GameDataObjectList>("PLAYERNAME")->data;
            if(!target)fail("Original GAMESTART PLAYERNAME has no list owner.");
            target->clear();target->push_back(std::move(object));
            state.phase=Impl::Phase::done;state.complete=true;return {true,1,{}};
        }
        case Impl::Phase::done:return {true,1,{}};
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
