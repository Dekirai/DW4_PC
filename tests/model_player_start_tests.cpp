#include "dw4/model_player_start.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/database_runtime.hpp"
#include "dw4/game_name.hpp"
#include "dw4/game_ram.hpp"
#include "dw4/resources.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const char* message) {
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
AnimationMatrix matrix() {return {1,0,0,0,0,1,0,0,0,0,1,0,4,5,6,1};}
ModelPlayerStartDefaults defaults() {return ModelPlayerStartDefaults({"TestA","TestB","TestC","TestD"});}
ModelPlayerStartOwner owner(std::int32_t ram=0) {
    auto work=std::make_shared<ModelScriptWork>(ControllerObjectId{7,100+static_cast<std::uint32_t>(ram)},
        "synthetic/player-start.igb","p_test",ram,std::make_shared<AnimationMatrix>(matrix()));
    return {std::move(work),{7,90},"synthetic/player-start.igb",
        std::make_shared<std::optional<std::string>>("p_test"),std::make_shared<std::int32_t>(ram)};
}
auto integers(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataIntList>(name)->data;}
auto booleans(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataBoolList>(name)->data;}
auto strings(const std::shared_ptr<GameDataList>& row,const char* name) {return row->require<GameDataStringList>(name)->data;}
std::vector<MessengerDeferredRecord> active(const NativeMessenger& messenger) {
    auto result=messenger.deferred_snapshot();
    std::erase_if(result,[](const auto& row){return !row.active;});return result;
}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,std::string category,std::optional<std::string> name,
    std::int32_t index,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [hook=std::move(hook)]{return MessengerManager{hook,{}};}},std::move(category),std::move(name),index);
}
void packet(const MessengerDelivery& delivery,const NativeMessenger& messenger,const char* command,std::int32_t index) {
    check(delivery.message.category=="GAMERAM" && !delivery.message.name && delivery.message.index==-1 &&
        delivery.message.command==command && messenger.buffer().integers()==std::vector<std::int32_t>{index},
        "Caller lost exact GAMERAM route or live integer packet.");
}

void ordered_retained_prefix() {
    GameDataState data;ActionEventRuntime events;auto prepared=owner();const auto captured=data.player(0);
    const auto hp=captured->require<GameDataInt>("HP"),mhp=captured->require<GameDataInt>("MHP");
    mhp->data=120;captured->require<GameDataInt>("MMP")->data=60;
    const auto up=integers(captured,"UPDATA"),down=integers(captured,"DOWNDATA");
    std::fill(up->begin(),up->end(),9);std::fill(down->begin(),down->end(),8);
    std::fill(booleans(captured,"BASE_ISUSE")->begin(),booleans(captured,"BASE_ISUSE")->end(),std::uint8_t{0});
    unsigned starts{},recalculations{},arms{},clock_reads{};bool start_ready=false,recalc_ready=false;
    std::uint64_t clock=50;std::vector<std::string> trace;
    provider(events,"GAMERAM","prepared-game",0,[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
        if(delivery.message.command=="GAMESTART") {
            ++starts;packet(delivery,messenger,"GAMESTART",0);
            check(messenger.buffer().strings()==std::vector<std::optional<std::string>>{"p_test","TestA"},
                "GAMESTART model/default names changed after original packet construction.");
            check(prepared.work->hp==hp && prepared.work->max_hp==mhp && hp->data==(starts==1?120:77),
                "Caller HP binding/refill was missing or repeated after suspension.");
            check(std::all_of(up->begin(),up->begin()+7,[](auto x){return x==0;}) && up->at(7)==9 &&
                std::all_of(down->begin(),down->begin()+6,[](auto x){return x==0;}) && down->at(6)==8,
                "The two short reset loops cleared the wrong array interval.");
            if(starts==1) {
                trace.push_back("GAMESTART prefix");data.reset_player(0);hp->data=77;
                *prepared.live_ram_no=2;*prepared.parent_name="new-info-name";
                *strings(captured,"POWERSOCKETNAME")={"retained-weapon"};
            }
            if(!start_ready)return MessengerCallResult{false,0,"prepared GAMESTART callback paused"};
            clock=900;trace.push_back("GAMESTART complete");return MessengerCallResult{true,1,{}};
        }
        ++recalculations;packet(delivery,messenger,"RECALCPLAYER",2);
        check(messenger.buffer().strings().empty() && messenger.buffer().objects().empty() && messenger.buffer().booleans().empty(),
            "RECALCPLAYER retained arguments from GAMESTART or added a modifier flag.");
        const auto queued=active(messenger);
        check(queued.size()==1 && queued[0].message.name=="retained-weapon" && queued[0].message.index==2 &&
            queued[0].deadline==900+16666666 && queued[0].arguments->integers().empty(),
            "GENERATION was not captured once before immediate recalculation with the live enqueue clock.");
        check(std::all_of(booleans(captured,"BASE_ISUSE")->begin(),booleans(captured,"BASE_ISUSE")->end(),[](auto x){return x==1;}) &&
            data.player(0)!=captured && data.player(0)->require<GameDataInt>("HP")->data==0,
            "Post-GAMESTART fill retargeted the current row or omitted old-row entries.");
        if(recalculations==1) {trace.push_back("RECALCPLAYER prefix");strings(captured,"POWERSOCKETNAME")->at(0)="later-name";}
        if(!recalc_ready)return MessengerCallResult{false,0,"prepared RECALCPLAYER callback paused"};
        trace.push_back("RECALCPLAYER complete");return MessengerCallResult{true,1,{}};
    });
    provider(events,"PLAYERARMS","retained-weapon",2,[&](NativeMessenger&,const MessengerDelivery& delivery) {
        ++arms;check(delivery.message.command=="GENERATE","Queued caller used a different weapon command.");
        trace.push_back("GENERATE test service");return MessengerCallResult{true,1,{}};
    });
    ModelPlayerStart start(prepared,captured,defaults());auto& messenger=events.messenger();
    const ModelPlayerClock live_clock=[&]{++clock_reads;return clock;};
    check(start.resume(messenger,events,live_clock).stop==ModelPlayerStartStop::service_required &&
        start.phase()==ModelPlayerStartPhase::game_start && active(messenger).empty() && !clock_reads,
        "Caller passed an incomplete GAMESTART or sampled enqueue time early.");
    check(start.binding().row()==captured && start.binding().hp()==hp && !start.prefix_completed(),"Caller did not retain its captured binding.");
    start_ready=true;
    check(start.resume(messenger,events,live_clock).stop==ModelPlayerStartStop::service_required &&
        start.phase()==ModelPlayerStartPhase::recalculate && !arms && clock_reads==1,
        "Caller delivered deferred GENERATE immediately or did not retain RECALCPLAYER.");
    ActionEventRuntime foreign;auto& foreign_messenger=foreign.messenger();
    rejects([&]{start.resume(foreign_messenger,foreign,live_clock);},"Suspended caller changed messenger/event owners.");
    check(!start.faulted(),"A rejected foreign owner poisoned the valid retained caller.");
    recalc_ready=true;clock=5000;
    check(start.resume(messenger,events,live_clock).stop==ModelPlayerStartStop::later_ui_required &&
        start.prefix_completed() && !start.faulted() && starts==2 && recalculations==2 && clock_reads==1 && !arms,
        "Completed prefix replayed earlier work or claimed weapon delivery/player completion.");
    hp->data=33;
    check(start.resume(messenger,events,live_clock).stop==ModelPlayerStartStop::later_ui_required && hp->data==33 &&
        recalculations==2 && active(messenger).size()==1,"Terminal checkpoint repeated prefix effects.");
    auto early=events.begin_deferred_update(900+16666666-1);
    check(events.resume(early).stop==MessengerStop::completed && !arms,"GENERATE ran before its exact deadline.");
    auto due=events.begin_deferred_update(900+16666666);
    check(events.resume(due).stop==MessengerStop::completed && arms==1 && active(messenger).empty(),"Queued caller was not delivered exactly once at equality.");
    check(trace==std::vector<std::string>{"GAMESTART prefix","GAMESTART complete","RECALCPLAYER prefix",
        "RECALCPLAYER complete","GENERATE test service"},"Caller/deferred event order differs from original.");
}

void branch_and_nullable_values() {
    for(const auto flag:{std::uint8_t{1},std::uint8_t{128},std::uint8_t{255}})
        for(const auto name:std::vector<std::optional<std::string>>{std::nullopt,"","weapon"}) {
            GameDataState data;ActionEventRuntime events;auto prepared=owner(1);const auto row=data.player(1);
            row->require<GameDataBool>("ISUSE")->data=flag;*booleans(row,"BASE_ISUSE")={2,3,4};
            *integers(row,"UPDATA")={7,8};integers(row,"DOWNDATA")->clear();*strings(row,"POWERSOCKETNAME")={name};
            unsigned calls{};provider(events,"GAMERAM","game",0,[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
                ++calls;packet(delivery,messenger,"RECALCPLAYER",1);return MessengerCallResult{true,1,{}};
            });
            ModelPlayerStart start(prepared,row,defaults());auto& messenger=events.messenger();
            check(start.resume(messenger,events,100).stop==ModelPlayerStartStop::later_ui_required && calls==1 &&
                *integers(row,"UPDATA")==std::vector<std::int32_t>{0,0} && integers(row,"DOWNDATA")->empty() &&
                *booleans(row,"BASE_ISUSE")==std::vector<std::uint8_t>{2,3,4},"Nonzero ISUSE did not skip only GAMESTART/fill or short lists grew.");
            const auto queued=active(messenger);check(queued.size()==1 && queued[0].message.name==name && queued[0].deadline==100+16666666,
                "Nullable/empty weapon name was lost or treated as an absent list element.");
        }
    for(const auto name:std::vector<std::optional<std::string>>{std::nullopt,""}) {
        GameDataState data;ActionEventRuntime events;auto prepared=owner();*prepared.parent_name=name;unsigned calls{},clock_reads{};
        provider(events,"GAMERAM","game",0,[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            ++calls;
            if(delivery.message.command=="GAMESTART")check(messenger.buffer().strings()[0]==std::optional<std::string>{""},
                "Null parent info name did not become an owned non-null empty string.");
            return MessengerCallResult{true,1,{}};
        });
        ModelPlayerStart start(prepared,data.player(0),defaults());auto& messenger=events.messenger();
        check(start.resume(messenger,events,ModelPlayerClock{[&]{++clock_reads;return 1;}}).stop==ModelPlayerStartStop::later_ui_required &&
            calls==2 && !clock_reads && active(messenger).empty(),"An empty weapon-name list scheduled a synthetic weapon or read an unused clock.");
    }
}

void malformed_ownership_and_faults() {
    GameDataState data;auto original=owner();
    for(unsigned variant=0;variant<7;++variant) {
        auto bad=original;
        if(variant==0)bad.work.reset();
        if(variant==1)bad.parent_name.reset();
        if(variant==2)bad.live_ram_no.reset();
        if(variant==3)bad.captured_parent_info.file_index=8;
        if(variant==4)bad.parent_source="other/source.igb";
        if(variant==5)bad.captured_parent_info=ControllerObjectId::allocate_native();
        if(variant==6)bad.captured_parent_info.reference_index=std::numeric_limits<std::uint32_t>::max();
        rejects([&]{ModelPlayerStart start(bad,data.player(0),defaults());},"Invalid captured work/info source ownership was accepted.");
    }
    rejects([&]{ModelPlayerStart start(original,{},defaults());},"Missing GETPLAYER row was accepted.");
    for(const auto value:{-1,1,4}) {
        auto bad=original;bad.live_ram_no=std::make_shared<std::int32_t>(value);
        rejects([&]{ModelPlayerStart start(bad,data.player(0),defaults());},"Initial live RAM mismatch/range was accepted.");
    }
    auto bad=original;bad.parent_name=std::make_shared<std::optional<std::string>>(std::string("a\0b",3));
    rejects([&]{ModelPlayerStart start(bad,data.player(0),defaults());},"Embedded NUL parent name was accepted.");
    ActionEventRuntime events;ModelPlayerStart no_clock(original,data.player(0),defaults());auto& messenger=events.messenger();
    const auto previous=original.work->hp;
    rejects([&]{no_clock.resume(messenger,events,ModelPlayerClock{});},"Missing explicit clock owner was accepted.");
    check(!no_clock.faulted() && original.work->hp==previous,"Clock preflight partially bound work.");
    for(unsigned variant=0;variant<3;++variant) {
        GameDataState malformed;ActionEventRuntime runtime;auto target=owner();const auto row=malformed.player(0);
        if(variant==0)row->require<GameDataIntList>("UPDATA")->data.reset();
        if(variant==1)integers(row,"DOWNDATA")->resize(65537);
        if(variant==2)row->require<GameDataBoolList>("BASE_ISUSE")->data.reset();
        provider(runtime,"GAMERAM","game",0,[](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
        ModelPlayerStart start(target,row,defaults());auto& native=runtime.messenger();
        rejects([&]{start.resume(native,runtime,1);},"Malformed owned list passed the caller's checked bounds.");
        check(start.faulted() && target.work->hp==row->require<GameDataInt>("HP"),"Checked later fault lost the earlier HP prefix.");
        rejects([&]{start.resume(native,runtime,1);},"Faulted caller replayed its committed prefix.");
    }
    GameDataState failing;ActionEventRuntime runtime;unsigned effects{};ModelPlayerStart start(owner(),failing.player(0),defaults());
    provider(runtime,"GAMERAM","game",0,[&](NativeMessenger&,const MessengerDelivery&)->MessengerCallResult {
        ++effects;throw std::runtime_error("uncertain prepared callback");
    });
    auto& native=runtime.messenger();rejects([&]{start.resume(native,runtime,1);},"Thrown callback did not fault the caller.");
    rejects([&]{start.resume(native,runtime,1);},"Uncertain callback was replayed.");check(effects==1 && start.faulted(),"Thrown callback ran twice.");
    ModelPlayerStart moved(std::move(no_clock));rejects([&]{no_clock.row();},"Moved caller exposed a row.");
    check(!no_clock.prefix_completed() && no_clock.faulted() && moved.owner().work==original.work,"Move lost owned caller identity.");
}

void default_table_validation() {
    rejects([&]{ModelPlayerStartDefaults value({std::string("x\0y",3),"b","c","d"});},"Default name embedded NUL passed.");
    rejects([&]{ModelPlayerStartDefaults value({std::string(65536,'x'),"b","c","d"});},"Default name allocation was unbounded.");
    const auto path=std::filesystem::temp_directory_path()/("dw4_player_defaults_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".dol");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    std::vector<std::uint8_t> bytes(0x204);
    auto word=[&](std::size_t at,std::uint32_t value) {for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(24-i*8));};
    word(0,0x100);word(0x48,0x80003100);word(0x90,4);word(0xe0,0x80003100);
    word(7*4,0x104);word(0x48+7*4,0x804e7c70);word(0x90+7*4,0x100);
    for(std::size_t i=0;i<4;++i) {word(0x104+i*4,0x804e7c80+static_cast<std::uint32_t>(i)*16);bytes[0x114+i*16]=static_cast<std::uint8_t>('A'+i);}
    auto write=[&] {std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));};
    write();check(read_model_player_start_defaults(path).names()==std::array<std::string,4>{"A","B","C","D"},"DOL pointer table was not decoded as four separate terminated byte strings.");
    word(0x110,0x804e7c80);write();check(read_model_player_start_defaults(path).names()[3]=="A","Shared default-name pointer was rejected or copied from the wrong address.");
    for(const auto pointer:{0u,0x80003100u,0x804e7d6fu}) {
        word(0x104,pointer);bytes.back()=255;write();
        rejects([&]{read_model_player_start_defaults(path);},"Null/text/out-of-range unterminated DOL name was accepted.");
    }
    word(0x104,0x804e7c80);std::fill(bytes.begin()+0x114,bytes.begin()+0x154,std::uint8_t{255});write();
    rejects([&]{read_model_player_start_defaults(path);},"Missing bounded name terminator was accepted.");
    bytes.resize(0x105);write();rejects([&]{read_model_player_start_defaults(path);},"Truncated DOL section was accepted.");
}

void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto database=read_igb_document(resources.read("dng101b1/separate/usa_pack.igb"));
    const auto weapon=read_igb_document(resources.read("_common/w_common05.igb"));
    const auto names=read_model_player_start_defaults(root/"sys"/"main.dol");
    ActionEventRuntime events;GameRamRuntime game(events);DatabaseRuntime databases(events);
    databases.register_source(decode_database_managers(database,"dng101b1/separate/usa_pack.igb",8));
    const auto converter=std::make_shared<GameNameConverter>(read_game_name_table(root/"sys"/"main.dol"),std::make_shared<std::vector<std::uint8_t>>());
    std::vector<ModelPlayerStartOwner> owners;
    for(std::uint32_t index=0;index<4;++index) {
        const auto binding=decode_player_runtime_binding(doc,{1386,546,index,0},"_common/p_agumon.igb",17);
        const auto id=events.register_model(binding,ModelControlState(ModelMotionPose(matrix())),true);
        events.prepare_collision_target(id,decode_model_hit_body(doc,1386,"_common/p_agumon.igb",17),false);
        owners.push_back({events.prepare_target_script_work(id),{17,1386},"_common/p_agumon.igb",
            std::make_shared<std::optional<std::string>>(binding.model_name()),std::make_shared<std::int32_t>(static_cast<std::int32_t>(index))});
    }
    std::uint64_t clock=1000;const ModelPlayerClock live_clock=[&]{return clock;};
    for(std::int32_t index=0;index<4;++index) {
        auto& messenger=events.messenger();messenger.select("GAMERAM",std::nullopt,-1);messenger.buffer().add_int(index);
        auto get=messenger.begin_send("GETPLAYER");check(events.resume(get).stop==MessengerStop::completed,"Actual native GETPLAYER failed.");
        const auto captured=std::get<MessengerDataList>(messenger.buffer().objects().at(0));
        check(captured==game.state().player(static_cast<std::size_t>(index)),"GETPLAYER did not return its actual retained row.");
        integers(captured,"UPDATA")->at(7)=17;integers(captured,"DOWNDATA")->at(6)=19;
        ModelPlayerStart start(owners[static_cast<std::size_t>(index)],captured,names);
        if(index==0) {
            check(start.resume(messenger,events,live_clock).stop==ModelPlayerStartStop::service_required &&
                start.phase()==ModelPlayerStartPhase::game_start && active(messenger).empty(),"Missing original font did not retain GAMESTART before weapon enqueue.");
            game.set_name_services({[converter]{return converter;}});clock=2000;
        }
        const auto result=start.resume(messenger,events,live_clock);
        if(result.stop!=ModelPlayerStartStop::later_ui_required)throw std::runtime_error("Original player caller: "+result.diagnostic);
        check(start.prefix_completed() && !start.faulted() && !game.faulted() && !game.pending_count(),"Original prepared prefix did not reach only its later-UI checkpoint.");
        check(start.binding().hp()==owners[static_cast<std::size_t>(index)].work->hp && start.binding().hp()->data>0 &&
            start.binding().hp()->data==captured->require<GameDataInt>("MHP")->data,"Original caller lost shared positive HP after GAMESTART/recalculation.");
        check(std::all_of(booleans(captured,"BASE_ISUSE")->begin(),booleans(captured,"BASE_ISUSE")->end(),[](auto value){return value==1;}) &&
            integers(captured,"UPDATA")->at(7)==17 && integers(captured,"DOWNDATA")->at(6)==19,
            "Original caller fill/reset boundaries differ.");
        const auto player_name=captured->require<GameDataObjectList>("PLAYERNAME")->data;
        const auto converted=std::get<std::shared_ptr<GameDataUnicode>>(player_name->at(0));
        const auto expected=make_player_name(*converter->convert(names.names()[static_cast<std::size_t>(index)]));
        check(converted && converted->buffer && *converted->buffer==*expected->buffer && converted->buffer->at(0)!=0,
            "Original DOL default name did not pass through the prepared original font.");
        const auto queued=active(messenger);
        check(queued.size()==static_cast<std::size_t>(index)+1 && queued.back().deadline==clock+16666666 &&
            queued.back().message.name=="w_common05" && queued.back().message.index==index &&
            queued.back().message.command=="GENERATE", "Original caller's queued common05 name/RAM/deadline differs.");
    }
    auto& messenger=events.messenger();
    check(game.state().common()->require<GameDataInt>("NINZU")->data==4 && active(messenger).size()==4,"Four original caller rows interfered or delivered weapons early.");
    // The real source-qualified weapon RAMs are registered as required external
    // recipients, without pretending that the unimplemented beWeapon manager ran.
    const auto rams=weapon.object_list(*weapon.field_reference(weapon.object(36),6));
    for(std::size_t i=0;i<rams.size();++i)events.register_recipient({20,*rams[i]},"PLAYERARMS","w_common05",static_cast<std::int32_t>(i));
    auto early=events.begin_deferred_update(clock+16666666-1);
    check(events.resume(early).stop==MessengerStop::completed && active(messenger).size()==4,"Original caller queue delivered before its deadline.");
    auto due=events.begin_deferred_update(clock+16666666);
    check(events.resume(due).stop==MessengerStop::service_required && !due.completed() && active(messenger).size()==4,
        "Unimplemented actual beWeapon recipient was replaced by fabricated generation success.");
    std::cout<<"Original MODELCTRL post-GETPLAYER prefixes=4, queued common05 requests=4; real beWeapon service remains required\n";
}
}
int main(int argc,char** argv) {
    try {
        ordered_retained_prefix();branch_and_nullable_values();malformed_ownership_and_faults();default_table_validation();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_model_player_start_tests [EXTRACTED_ROOT]");
        std::cout<<"Model player start checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Model player start after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
