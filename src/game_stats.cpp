#include "dw4/game_stats.hpp"

#include "dw4/action_event_runtime.hpp"
#include "dw4/database.hpp"
#include "dw4/game_data.hpp"
#include "dw4/game_database.hpp"
#include "dw4/game_items.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace dw4 {
namespace {
constexpr std::size_t list_limit=65536;
[[noreturn]] void fail(const char* message) {throw GameDataError(message);}
std::int32_t add(std::int32_t a,std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a)+std::bit_cast<std::uint32_t>(b));
}
std::int32_t subtract(std::int32_t a,std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a)-std::bit_cast<std::uint32_t>(b));
}
std::int32_t multiply(std::int32_t a,std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a)*std::bit_cast<std::uint32_t>(b));
}
template<class T> std::shared_ptr<std::vector<T>> bounded(std::shared_ptr<std::vector<T>> list) {
    if(!list || list->size()>list_limit)fail("Original stat calculation requires a bounded existing list.");
    return list;
}
template<class T> const T& element(const std::shared_ptr<std::vector<T>>& list,std::size_t index) {
    bounded(list);
    if(index>=list->size())fail("Original stat calculation indexed an unavailable list element.");
    return (*list)[index];
}
auto integers(const GameDataList& row,std::string_view name) {return bounded(row.require<GameDataIntList>(name)->data);}
auto objects(const GameDataList& row,std::string_view name) {return bounded(row.require<GameDataObjectList>(name)->data);}
std::int32_t read(const GameDataList& row,std::string_view name) {return row.require<GameDataInt>(name)->data;}
void publish(const GameDataList& row,std::string_view name,std::int32_t value) {row.require<GameDataInt>(name)->data=value;}
bool null_object(const GameDataObject& value) {
    return std::visit([](const auto& item) {
        if constexpr(std::is_same_v<std::decay_t<decltype(item)>,std::monostate>)return true;
        else if constexpr(requires {item.get();})return !item;
        else return false;
    },value);
}

// This private coroutine owns one synchronous original routine's locals. Its
// only suspension point is a retained original database service operation.
class Flow {
public:
    struct promise_type {
        std::function<bool()> wait;
        std::exception_ptr error;
        Flow get_return_object() {return Flow{std::coroutine_handle<promise_type>::from_promise(*this)};}
        std::suspend_always initial_suspend() const noexcept {return {};}
        std::suspend_always final_suspend() const noexcept {return {};}
        void return_void() const noexcept {}
        void unhandled_exception() noexcept {error=std::current_exception();}
    };
    explicit Flow(std::coroutine_handle<promise_type> handle):handle_(handle) {}
    Flow(Flow&& other) noexcept:handle_(std::exchange(other.handle_,{})) {}
    Flow(const Flow&)=delete;
    Flow& operator=(const Flow&)=delete;
    ~Flow() {if(handle_)handle_.destroy();}
    bool resume() {
        while(!handle_.done()) {
            auto& promise=handle_.promise();
            if(promise.wait) {
                if(!promise.wait())return false;
                promise.wait={};
            }
            handle_.resume();
            if(promise.error)std::rethrow_exception(promise.error);
        }
        return true;
    }
private:
    std::coroutine_handle<promise_type> handle_;
};
struct Await {
    std::function<bool()> operation;
    bool await_ready() const noexcept {return false;}
    void await_suspend(std::coroutine_handle<Flow::promise_type> handle) {handle.promise().wait=std::move(operation);}
    void await_resume() const noexcept {}
};

struct CachedInteger {
    MessengerDatabase& cache;
    std::string database_name,column;
    std::int32_t row{},value{};
    std::optional<GameDatabaseQuery> query;
    bool done{};
    CachedInteger(MessengerDatabase& owner,std::string database,std::int32_t record,std::string field)
        :cache(owner),database_name(std::move(database)),column(std::move(field)),row(record) {}
    bool resume(NativeMessenger& messenger,ActionEventRuntime& events,std::string& diagnostic) {
        if(done)return true;
        if(query || !cache) {
            if(!query)query.emplace(database_name);
            const auto result=query->resume(messenger,events);
            if(!result.completed) {diagnostic=result.diagnostic;return false;}
            cache=query->database();
        }
        value=cache->integer(row,column);done=true;return true;
    }
};

struct BaseValue {
    std::size_t source,selector;
    bool maximum;
    unsigned phase{};
    std::shared_ptr<std::vector<std::int32_t>> counts;
    std::int32_t species{},level{},initial{},growth{},value{};
    std::optional<CachedInteger> query;
    BaseValue(std::size_t ram,bool is_maximum,std::size_t field):source(ram),selector(field),maximum(is_maximum) {}
    bool resume(GameDataState& data,GameDatabaseCache& caches,NativeMessenger& messenger,
        ActionEventRuntime& events,std::string& diagnostic) {
        static constexpr std::array<std::string_view,7> initial_names{
            "MHP_INI","MMP_INI","STR_INI","AC_INI","INT_INI","MND_INI","DEX_INI"};
        static constexpr std::array<std::string_view,7> growth_names{
            "MHP_GROW","MMP_GROW","STR_GROW","AC_GROW","INT_GROW","MND_GROW","DEX_GROW"};
        const auto name_index=maximum?selector:selector+2;
        if(phase==0) {
            const auto row=data.player(source);
            species=read(*row,"DIGIMONNO");counts=integers(*row,"UPCNT");level=read(*row,"LEVEL");
            query.emplace(caches.shinka,"shinka",species,std::string(initial_names[name_index]));phase=1;
        }
        if(phase==1) {
            if(!query->resume(messenger,events,diagnostic))return false;
            initial=query->value;query.reset();
            query.emplace(caches.shinka,"shinka",species,std::string(growth_names[name_index]));phase=2;
        }
        if(phase==2) {
            if(!query->resume(messenger,events,diagnostic))return false;
            growth=query->value;query.reset();
            const auto increment=element(counts,maximum?selector:selector+2);
            constexpr std::array<std::int32_t,10> limits{5,10,20,30,50,70,110,150,230,999};
            constexpr std::array<std::int32_t,10> origins{1,5,10,20,30,50,70,110,150,230};
            constexpr std::array<std::int32_t,10> maximum_base{0,40,140,640,1440,3840,7440,17840,25040,34640};
            constexpr std::array<std::int32_t,10> maximum_slope{10,20,50,80,120,180,260,180,120,80};
            constexpr std::array<std::int32_t,10> stat_base{0,40,85,165,235,355,455,615,735,895};
            constexpr std::array<std::int32_t,10> stat_slope{10,9,8,7,6,5,4,3,2,1};
            const auto found=std::find_if(limits.begin(),limits.end(),[&](auto bound){return level<=bound;});
            if(found==limits.end())fail("Original stat level lookup exceeds the verified table ending at 999.");
            const auto index=static_cast<std::size_t>(found-limits.begin());
            const auto factor=add((maximum?maximum_base:stat_base)[index],
                multiply((maximum?maximum_slope:stat_slope)[index],subtract(level,origins[index])));
            value=std::min(add(initial,add(multiply(growth,factor)/100,increment)),maximum?99999:9999);
            phase=3;
        }
        return true;
    }
};

struct ItemValue {
    GameDataObject object;
    GameItemQueryKind kind;
    std::string column;
    std::optional<GameWeaponQuery> query;
    ItemValue(GameDataObject item,GameItemQueryKind type,std::string field)
        :object(std::move(item)),kind(type),column(std::move(field)) {}
    bool resume(NativeMessenger& messenger,ActionEventRuntime& events,std::string& diagnostic) {
        if(!query) {
            const auto* weapon=std::get_if<std::shared_ptr<GameDataWeapon>>(&object);
            if(!weapon || !*weapon) {
                diagnostic="Original stat item query requires the actual unsupported item type's database provider.";
                return false;
            }
            query.emplace(*weapon,kind,column);
        }
        const auto result=query->resume(messenger,events);
        if(!result.completed)diagnostic=result.diagnostic;
        return result.completed;
    }
    std::int32_t integer() const {return query->integer_value();}
    const std::string& string() const {return query->string_value();}
    std::int32_t revision() const {return query->item()->revision;}
};
enum class ModifierKind { power,power_pocket,main,main_pocket,sub };
struct ModifierSpec {ModifierKind kind;bool extra{};};
constexpr ModifierSpec P0{ModifierKind::power,false},P1{ModifierKind::power,true},
    PP{ModifierKind::power_pocket,false},M0{ModifierKind::main,false},M1{ModifierKind::main,true},
    MP{ModifierKind::main_pocket,false},S0{ModifierKind::sub,false},S1{ModifierKind::sub,true};
struct Modifier {
    std::shared_ptr<GameDataList> row;
    ModifierSpec spec;
    std::string column;
    std::int32_t value;
    std::size_t count{},index{};
    unsigned phase{};
    std::optional<ItemValue> query;
    Modifier(std::shared_ptr<GameDataList> owner,ModifierSpec effect,std::string field,std::int32_t input)
        :row(std::move(owner)),spec(effect),column(std::move(field)),value(input) {}
    bool resume(NativeMessenger& messenger,ActionEventRuntime& events,std::string& diagnostic) {
        constexpr std::array<std::string_view,5> names{"POWERSOCKET","POWERPOCKET","MAINSLOT","MAINPOCKET","SUBSLOT"};
        const auto name=names[static_cast<std::size_t>(spec.kind)];
        const bool pocket=spec.kind==ModifierKind::power_pocket || spec.kind==ModifierKind::main_pocket;
        if(phase==0) {count=objects(*row,name)->size();if(!pocket)count=std::min(count,std::size_t{1});phase=1;}
        while(index<count) {
            if(phase==1) {
                auto item=element(objects(*row,name),index);
                if(null_object(item)){++index;continue;}
                query.emplace(std::move(item),GameItemQueryKind::integer,column);phase=2;
            }
            if(phase==2) {
                if(!query->resume(messenger,events,diagnostic))return false;
                value=add(value,query->integer());
                if(spec.extra && spec.kind==ModifierKind::sub) {
                    auto item=query->object;query.reset();
                    query.emplace(std::move(item),GameItemQueryKind::string,"SHUSEINOURYOKU");phase=3;
                } else {
                    if(spec.extra)value=add(value,query->revision());
                    query.reset();++index;phase=1;
                }
            }
            if(phase==3) {
                if(!query->resume(messenger,events,diagnostic))return false;
                if(query->string()==column)value=add(value,query->revision());
                query.reset();++index;phase=1;
            }
        }
        return true;
    }
};
struct StatSpec {
    std::string_view output,column;
    int up{-1},down{-1};
    std::array<ModifierSpec,5> modifiers{};
    std::size_t count{};
};
constexpr std::array<StatSpec,11> stats{{
    {"MHP","MAXHP",6,-1,{M0,S0},2},{"MMP","MAXMP",-1,-1,{M0,S0},2},
    {"TSUYOSA","TSUYOSA",4,4,{P1,PP,S1},3},{"MAMORI","MAMORI",5,5,{M1,S1},2},
    {"KASHIKOSA","KASHIKOSA",-1,-1,{P0,PP,M0,MP,S1},5},
    {"SEISHIN","SEISHIN",-1,-1,{P0,PP,M0,MP,S0},5},
    {"SUBAYASA","SUBAYASA",0,0,{P0,PP,M0,MP,S1},5},
    {"HONOOTEIKO","RES_FIRE",7,-1,{M0,MP,S1},3},
    {"KOORITEIKO","RES_ICE",8,-1,{M0,MP,S1},3},
    {"KAMINARITEIKO","RES_THUNDER",9,-1,{M0,MP,S1},3},
    {"YAMITEIKO","RES_DARK",10,-1,{M0,MP,S1},3}
}};
struct TailSpec {
    StatSpec stat;
    int minimum{},maximum{};
    bool lower{},upper{};
};
constexpr std::array<TailSpec,11> tails{{
    {{"ATTACKDELAY","DELAY",-1,-1,{P0},1},0,70,false,true},
    {{"MEICHURITSU","MEICHURITSU",-1,-1,{P0},1}},
    {{"CHIMEIRITSU","CHIMEIRITSU",1,1,{P0,PP},2},1,0,true,false},
    {{"MAHIRITSU","MAHIRITSU",-1,-1,{P0},1}},
    {{"KAIHIRITSU","CHOHATURITSU",3,3,{MP},1},1,0,true,false},
    {{"BOUGYORITSU","BOUGYORITSU",-1,-1,{M0},1}},
    {{"MAHITEIKOU","MAHITEIKOU",2,2,{M0},1},1,0,true,false},
    {{"DOKUTEIKOU","DOKUTEIKOU",-1,-1,{M0},1}},
    {{"BITRITSU","BITRITSU",-1,-1,{S0},1}},
    {{"EXPRITSU","EXPRITSU",-1,-1,{S0},1}},
    {{"LUCKRITSU","LUCKRITSU",-1,-1,{S0},1}}
}};
} // namespace

struct GameStatsRuntime::Impl {
    std::size_t source;
    std::uint8_t modifiers;
    GameDataState* data{};
    GameDatabaseCache* caches{};
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::optional<Flow> flow;
    std::string diagnostic;
    bool running{},done{},fault{};
    Impl(std::size_t ram,std::uint8_t enabled):source(ram),modifiers(enabled) {}

    Flow execute() {
        const auto row=data->player(source);
        const auto species=read(*row,"DIGIMONNO"),level=read(*row,"LEVEL");
        bool hp_was_zero=false;
        for(std::size_t stat=0;stat<stats.size();++stat) {
            const auto& spec=stats[stat];std::int32_t value{};
            if(stat<7) {
                BaseValue base(source,stat<2,stat<2?stat:stat-2);
                co_await Await{[&]{return base.resume(*data,*caches,*messenger,*events,diagnostic);}};
                value=base.value;
            } else {
                CachedInteger query(caches->shinka,"shinka",species,std::string(spec.column));
                co_await Await{[&]{return query.resume(*messenger,*events,diagnostic);}};
                value=add(query.value,element(integers(*row,"UPCNT"),stat));
            }
            if(spec.up>=0)value=add(value,element(integers(*row,"UPDATA"),static_cast<std::size_t>(spec.up)));
            if(spec.down>=0)value=subtract(value,element(integers(*row,"DOWNDATA"),static_cast<std::size_t>(spec.down)));
            if(modifiers)for(std::size_t index=0;index<spec.count;++index) {
                Modifier modifier(row,spec.modifiers[index],std::string(spec.column),value);
                co_await Await{[&]{return modifier.resume(*messenger,*events,diagnostic);}};
                value=modifier.value;
            }
            value=stat<2?std::min(value,99999):std::clamp(value,1,9999);
            publish(*row,spec.output,value);
            if(stat==0) {
                const auto hp=read(*row,"HP");hp_was_zero=hp==0;
                if(hp_was_zero || hp>value)publish(*row,"HP",value);
            } else if(stat==1) {
                const auto mp=read(*row,"MP");
                if(hp_was_zero || mp>value)publish(*row,"MP",value);
            }
        }

        const auto waza=objects(*row,"WAZALIST");waza->clear();
        for(unsigned technique=1;technique<=6;++technique) {
            const auto prefix="TECH"+std::to_string(technique);
            CachedInteger base(caches->shinka,"shinka",species,prefix);
            co_await Await{[&]{return base.resume(*messenger,*events,diagnostic);}};
            CachedInteger skill(caches->pc_tech,"pcTech",base.value,"SKILL");
            co_await Await{[&]{return skill.resume(*messenger,*events,diagnostic);}};
            if(skill.value<0)fail("Original technique selected a negative player SKILL index.");
            const auto value=element(integers(*row,"SKILL"),static_cast<std::size_t>(skill.value));
            CachedInteger gate(caches->shinka,"shinka",species,prefix+"_LEVEL");
            co_await Await{[&]{return gate.resume(*messenger,*events,diagnostic);}};
            if(gate.value>level)continue;
            for(int rank=4;rank>=0;--rank) {
                CachedInteger threshold(caches->shinka,"shinka",species,prefix+"_SKILL"+std::to_string(rank+1));
                co_await Await{[&]{return threshold.resume(*messenger,*events,diagnostic);}};
                if(threshold.value>value)continue;
                if(waza->size()>=list_limit)fail("Original technique append exceeds the checked list bound.");
                waza->push_back(std::make_shared<GameDataWaza>(GameDataWaza{9,add(base.value,rank),0,base.value}));
                break;
            }
        }
        const auto experience=level==999?0:add(multiply(level,200),multiply(add(level,140),multiply(level,level)));
        publish(*row,"NEXTEXP",experience);
        if(!modifiers)co_return;

        for(std::size_t stat=0;stat<tails.size();++stat) {
            const auto& tail=tails[stat];const auto& spec=tail.stat;
            std::int32_t value=stat==0?element(integers(*row,"DOWNDATA"),6):0;
            if(spec.up>=0)value=add(value,element(integers(*row,"UPDATA"),static_cast<std::size_t>(spec.up)));
            if(spec.down>=0)value=subtract(value,element(integers(*row,"DOWNDATA"),static_cast<std::size_t>(spec.down)));
            for(std::size_t index=0;index<spec.count;++index) {
                Modifier modifier(row,spec.modifiers[index],std::string(spec.column),value);
                co_await Await{[&]{return modifier.resume(*messenger,*events,diagnostic);}};
                value=modifier.value;
            }
            if(tail.lower)value=std::max(value,tail.minimum);
            if(tail.upper)value=std::min(value,tail.maximum);
            publish(*row,spec.output,value);
        }
        publish(*row,"ATTACKZOKUSEI",-1);
        const auto pocket_count=objects(*row,"POWERPOCKET")->size();
        bool matched=false;
        for(std::size_t index=0;index<pocket_count && !matched;++index) {
            auto item=element(objects(*row,"POWERPOCKET"),index);
            if(null_object(item))continue;
            ItemValue query(std::move(item),GameItemQueryKind::string,"ZOKUSEI");
            co_await Await{[&]{return query.resume(*messenger,*events,diagnostic);}};
            constexpr std::array<std::string_view,4> names{"fire","ice","thunder","dark"};
            for(std::size_t kind=0;kind<names.size();++kind)if(query.string()==names[kind]) {
                publish(*row,"ATTACKZOKUSEI",static_cast<std::int32_t>(kind));matched=true;break;
            }
        }
        for(const auto name:{"HPDRAIN","MPDRAIN"}) {
            Modifier modifier(row,PP,name,0);
            co_await Await{[&]{return modifier.resume(*messenger,*events,diagnostic);}};
            publish(*row,name,modifier.value);
        }
        publish(*row,"CHOHATURITSU",read(*row,"KAIHIRITSU"));

        auto first=element(objects(*row,"POWERSOCKET"),0);
        std::int32_t weapon_record{},weapon_skill{};
        if(!null_object(first)) {
            // 803712E8 captures record before the item query; the query itself
            // reads the live record after its fresh database callback.
            const auto* weapon=std::get_if<std::shared_ptr<GameDataWeapon>>(&first);
            if(weapon && *weapon)weapon_record=(*weapon)->record_no;
            ItemValue query(std::move(first),GameItemQueryKind::integer,"SKILL");
            co_await Await{[&]{return query.resume(*messenger,*events,diagnostic);}};
            weapon_skill=query.integer();
        }
        publish(*row,"WEAPONRECNO",weapon_record);publish(*row,"WEAPONSKILLNO",weapon_skill);
        const auto socket=objects(*row,"POWERSOCKET");
        const auto names=bounded(row->require<GameDataStringList>("POWERSOCKETNAME")->data);
        names->clear();const auto count=socket->size();
        for(std::size_t index=0;index<count;++index) {
            auto item=element(socket,index);if(null_object(item))continue;
            ItemValue query(std::move(item),GameItemQueryKind::string,"MODEL");
            co_await Await{[&]{return query.resume(*messenger,*events,diagnostic);}};
            if(names->size()>=list_limit)fail("Original equipment name append exceeds the checked list bound.");
            names->emplace_back(query.string());
        }
    }
};

GameStatsRuntime::GameStatsRuntime(std::int32_t source_ram,std::uint8_t modifiers) {
    if(source_ram<0 || source_ram>=static_cast<std::int32_t>(GameDataState::player_count))
        fail("Original stat calculation requires source RAM zero through three.");
    impl_=std::make_unique<Impl>(static_cast<std::size_t>(source_ram),modifiers);
}
GameStatsRuntime::~GameStatsRuntime()=default;
GameStatsRuntime::GameStatsRuntime(GameStatsRuntime&&) noexcept=default;
GameStatsRuntime& GameStatsRuntime::operator=(GameStatsRuntime&&) noexcept=default;
MessengerCallResult GameStatsRuntime::resume(GameDataState& data,GameDatabaseCache& caches,
    NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!impl_)fail("Cannot resume a moved stat calculation.");
    auto& state=*impl_;
    if(state.running)fail("Original stat calculation cannot recursively resume itself.");
    if(state.data && (state.data!=&data || state.caches!=&caches || state.messenger!=&messenger || state.events!=&events))
        fail("Original stat calculation must resume with its retained owners.");
    if(state.fault)fail("Original stat calculation has an uncertain outcome; replay is forbidden.");
    if(state.done)return {true,1,{}};
    state.data=&data;state.caches=&caches;state.messenger=&messenger;state.events=&events;
    state.running=true;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{state.running};
    try {
        if(!state.flow)state.flow.emplace(state.execute());
        if(!state.flow->resume())return {false,0,state.diagnostic};
        state.done=true;state.diagnostic.clear();return {true,1,{}};
    }catch(...) {state.fault=true;throw;}
}
bool GameStatsRuntime::completed() const noexcept {return impl_ && impl_->done;}
bool GameStatsRuntime::faulted() const noexcept {return impl_ && impl_->fault;}
} // namespace dw4
