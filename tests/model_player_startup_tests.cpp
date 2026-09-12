#include "dw4/model_player_startup.hpp"
#include "dw4/action_event_runtime.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation,const char* message) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
ModelPlayerStartDefaults defaults() {return ModelPlayerStartDefaults({"A","B","C","D"});}
ModelPlayerMapRules maps() {return {{"metal0","metal1","metal2","metal3","both"},{"lobby0","lobby1","lobby2","both"}};}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,std::string category,std::string name,int index,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},[hook=std::move(hook)]{return MessengerManager{hook,{}};}},
        std::move(category),std::move(name),index);
}
struct Fixture {
    GameDataState data;
    ActionEventRuntime events;
    ModelPlayerStartOwner owner;
    std::shared_ptr<GameDataList> row=data.player(0);
    std::shared_ptr<const std::string> current_map=std::make_shared<const std::string>("ordinary");
    unsigned recalculations{},map_reads{};
    Fixture() {
        auto matrix=std::make_shared<AnimationMatrix>(AnimationMatrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
        owner={std::make_shared<ModelScriptWork>(ControllerObjectId{3,101},"synthetic/startup.igb","player",0,matrix),
            {3,100},"synthetic/startup.igb",std::make_shared<std::optional<std::string>>("player"),std::make_shared<std::int32_t>(0)};
        row->require<GameDataBool>("ISUSE")->data=1;
        set("LEVEL",2);set("HP",7);set("MHP",13);set("MP",7);set("MMP",13);
        provider(events,"GAMERAM","game",0,[this](NativeMessenger&,const MessengerDelivery& delivery) {
            check(delivery.message.command=="RECALCPLAYER","Synthetic startup unexpectedly ran GAMESTART.");
            ++recalculations;return MessengerCallResult{true,1,{}};
        });
    }
    void set(const char* name,std::int32_t value) {row->require<GameDataInt>(name)->data=value;}
    ModelPlayerMapReader reader() {return [this]{++map_reads;return current_map;};}
    void ui() {
        for(const auto* name:{"lvnm","hpnm","mpnm"})provider(events,"NUMBERCTRL",name,0,
            [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
        for(const auto* name:{"hpmt","mpmt"})provider(events,"METERCTRL",name,0,
            [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    }
};
const ModelPlayerClock clock=[] {return std::uint64_t{100};};

void ordered_retained_sends() {
    Fixture f;f.current_map=std::make_shared<const std::string>("both");const auto old_map=f.current_map;
    const auto old_work=f.owner.work;const auto old_hp=old_work->hp();
    std::array<unsigned,6> calls{};
    const std::array<std::string,5> names{"lvnm","hpmt","hpnm","mpmt","mpnm"};
    const std::array<int,5> indices{0,1,2,3,0};
    std::vector<std::string> trace;
    for(std::size_t i=0;i<5;++i)provider(f.events,i==1 || i==3 ? "METERCTRL":"NUMBERCTRL",names[i],indices[i],
        [&,i](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            check(delivery.message.name==names[i] && delivery.message.index==indices[i] &&
                delivery.message.command==(i==1 || i==3 ? "SETPERCENT":"SETVALUE"),"Startup route/order differs.");
            if(++calls[i]==1) {
                trace.push_back(names[i]);
                check(messenger.buffer().objects().empty() && messenger.buffer().strings().empty(),"Startup did not clear old packet lists.");
                if(i==1 || i==3)check(messenger.buffer().integers().empty() && messenger.buffer().floats().size()==1 &&
                    std::abs(messenger.buffer().floats()[0]-(i==1 ? 900.0f/13.0f:53.0f))<0.00001f,"HP/MP percent arithmetic was conflated.");
                else check(messenger.buffer().floats().empty() && messenger.buffer().integers()==
                    std::vector<std::int32_t>{i==0 ? 2:i==2 ? 11:10},"Startup cached a stale scalar across callbacks.");
                if(i==0){f.data.reset_player(0);f.set("HP",9);}
                if(i==1)f.set("HP",11);
                if(i==3)f.set("MP",10);
                *f.owner.live_ram_no=i<4 ? indices[i+1]:2;
                messenger.buffer().add_string("callback prefix");
                return MessengerCallResult{false,0,"Synthetic recipient paused after its prefix."};
            }
            check(messenger.buffer().strings()==std::vector<std::optional<std::string>>{"callback prefix"},
                "Resuming a UI send rebuilt its packet or replayed callback effects.");
            return MessengerCallResult{true,1,{}};
        });
    provider(f.events,"PLAYER","player",2,[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
        check(delivery.message.command=="SETMETAL" && !delivery.message.name && delivery.message.index==2,
            "Startup metal send did not preserve its broadcast/live RAM route.");
        if(++calls[5]==1) {
            trace.push_back("metal");check(messenger.buffer().integers().empty() && messenger.buffer().floats().empty() &&
                messenger.buffer().strings().empty(),"SETMETAL fabricated arguments.");
            f.current_map=std::make_shared<const std::string>("ordinary");
            f.owner.work=std::make_shared<ModelScriptWork>(ControllerObjectId{3,102},"synthetic/startup.igb","other",0,
                std::make_shared<AnimationMatrix>(*old_work->matrix()));
            return MessengerCallResult{false,0,"Synthetic metal callback paused."};
        }
        return MessengerCallResult{true,1,{}};
    });
    ModelPlayerStartup startup(f.owner,f.row,defaults(),maps());auto& messenger=f.events.messenger();
    for(std::size_t i=0;i<6;++i)check(startup.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerStartupStop::service_required,
        "Startup skipped a retained recipient boundary.");
    check(startup.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerStartupStop::completed && startup.completed(),
        "Prepared original caller did not return after its map tail.");
    check(trace==std::vector<std::string>{"lvnm","hpmt","hpnm","mpmt","mpnm","metal"} &&
        std::all_of(calls.begin(),calls.end(),[](auto n){return n==2;}) && f.recalculations==1,"Startup replayed or reordered committed sends.");
    check(startup.captured_map()==old_map && f.map_reads==1 && old_work->original_is_main_lobby()==1 &&
        !f.owner.work->original_is_main_lobby() && startup.prefix().row()==f.row && f.row!=f.data.player(0),
        "Startup retargeted its captured map, ONE or player row after callbacks.");
    check(old_work->hp()==f.row->require<GameDataInt>("HP") && old_work->hp()!=old_hp,"Startup did not retain its original HP binding.");
    check(startup.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerStartupStop::completed && f.recalculations==1 && f.map_reads==1,
        "Completed caller repeated effects.");
}

void map_rules_and_preconditions() {
    for(const auto& map:std::vector<std::string>{"ordinary","metal0","metal1","metal2","metal3","both","lobby0","lobby1","lobby2","METAL0",""}) {
        Fixture f;f.ui();f.current_map=std::make_shared<const std::string>(map);f.owner.work->original_is_main_lobby()=std::uint8_t{255};
        unsigned metal{};provider(f.events,"PLAYER","player",0,[&](NativeMessenger&,const MessengerDelivery&){++metal;return MessengerCallResult{true,1,{}};});
        ModelPlayerStartup startup(f.owner,f.row,defaults(),maps());auto& messenger=f.events.messenger();
        check(startup.resume(messenger,f.events,clock).stop==ModelPlayerStartupStop::map_context_required && f.recalculations==1 &&
            !startup.faulted() && !f.map_reads,"Missing map field was guessed or faulted instead of retaining the caller.");
        ActionEventRuntime wrong;auto& foreign=wrong.messenger();
        rejects([&]{startup.resume(foreign,wrong,clock,f.reader());},"Retained startup changed its messenger owner.");
        rejects([&]{startup.resume(messenger,f.events,{});},"Startup accepted a missing prepared clock.");
        check(!startup.faulted() && startup.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerStartupStop::completed,
            "Rejected preflight poisoned the actual continuation.");
        const auto rules=maps();const bool is_metal=std::find(rules.metal().begin(),rules.metal().end(),map)!=rules.metal().end();
        const bool lobby=std::find(rules.main_lobby().begin(),rules.main_lobby().end(),map)!=rules.main_lobby().end();
        check(metal==unsigned(is_metal) && f.owner.work->original_is_main_lobby()==(lobby?1:255),
            "Map comparison was case-insensitive or overwrote a nonmatching existing lobby byte.");
    }
    Fixture f;f.ui();ModelPlayerStartup startup(f.owner,f.row,defaults(),maps());auto& messenger=f.events.messenger();
    rejects([&]{startup.resume(messenger,f.events,clock,[]{return std::shared_ptr<const std::string>{};});},"Null original map pointer was accepted.");
    check(startup.faulted() && f.recalculations==1,"Null-map fault lost its committed prefix.");
    rejects([&]{startup.resume(messenger,f.events,clock,f.reader());},"Faulted startup replayed uncertain effects.");
    rejects([]{ModelPlayerMapRules({std::string("x\0y",3),"b","c","d","e"},{"f","g","h","i"});},"Map names accepted embedded NUL.");
}

void arithmetic_and_failure_prefix() {
    check(original_player_hp_percent(7,13)==700.0f/13.0f && original_player_mp_percent(7,13)==53.0f,
        "HP fractions or MP truncation differ from the original instructions.");
    check(original_player_mp_percent(-7,13)==-53.0f && original_player_mp_percent(1073741824,13)==0,
        "MP multiplication did not wrap or division did not truncate toward zero.");
    rejects([]{original_player_mp_percent(1,0);},"Exceptional divw used a fabricated denominator.");
    rejects([]{original_player_mp_percent(536870912,-1);},"Exceptional wrapped INT_MIN/-1 divw was accepted.");
    check(std::isinf(original_player_hp_percent(1,0)) && std::isnan(original_player_hp_percent(0,0)),
        "HP helper silently clamped the original nonfinite arithmetic.");
    Fixture f;f.ui();f.set("MHP",0);unsigned levels{};
    provider(f.events,"NUMBERCTRL","lvnm",0,[&](NativeMessenger&,const MessengerDelivery&){++levels;return MessengerCallResult{true,1,{}};});
    ModelPlayerStartup startup(f.owner,f.row,defaults(),maps());auto& messenger=f.events.messenger();
    rejects([&]{startup.resume(messenger,f.events,clock,f.reader());},"Nonfinite HP escaped the checked scalar append boundary.");
    check(startup.faulted() && startup.phase()==ModelPlayerStartupPhase::hp_meter && levels==1 && f.recalculations==1 &&
        messenger.buffer().floats().empty(),"HP fault replayed or rolled back earlier level/recalculation effects.");
}

void dol_map_tables() {
    const auto path=std::filesystem::temp_directory_path()/("dw4_startup_maps_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".dol");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    std::vector<std::uint8_t> bytes(0x244);
    auto word=[&](std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(24-i*8));};
    word(0,0x100);word(0x48,0x80003100);word(0x90,4);word(0xe0,0x80003100);
    word(7*4,0x104);word(0x48+7*4,0x804e7c80);word(0x90+7*4,0x140);
    for(std::size_t i=0;i<9;++i){word(0x104+i*4,0x804e7ca4+static_cast<std::uint32_t>(i)*16);bytes[0x128+i*16]=static_cast<std::uint8_t>('A'+i);}
    auto write=[&]{std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));};
    write();const auto result=read_model_player_map_rules(path);
    check(result.metal()==std::array<std::string,5>{"A","B","C","D","E"} &&
        result.main_lobby()==std::array<std::string,4>{"F","G","H","I"},"DOL map table order or byte strings differ.");
    word(0x124,0x804e7ca4);write();check(read_model_player_map_rules(path).main_lobby()[3]=="A","Aliased DOL map pointer lost its string.");
    for(const auto pointer:{0u,0x80003100u,0x804e7dbfu}) {
        word(0x104,pointer);bytes.back()=255;write();
        rejects([&]{read_model_player_map_rules(path);},"DOL map pointer accepted null, executable or unterminated section-end data.");
    }
    word(0x104,0x804e7ca4);std::fill(bytes.begin()+0x128,bytes.begin()+0x168,std::uint8_t{255});write();
    rejects([&]{read_model_player_map_rules(path);},"DOL map string exceeded its bounded terminator search.");
    bytes.resize(0x128);write();rejects([&]{read_model_player_map_rules(path);},"Truncated DOL data section was accepted.");
}
}
int main() {
    try {ordered_retained_sends();map_rules_and_preconditions();arithmetic_and_failure_prefix();dol_map_tables();
        std::cout<<"Player startup checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Player startup after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
