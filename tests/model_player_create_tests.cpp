#include "dw4/model_player_create.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/action_dispatch.hpp"
#include "dw4/model_frame.hpp"
#include "dw4/model_subcommand_update.hpp"
#include "dw4/model_movement_collision.hpp"
#include "dw4/model_motion_actual.hpp"
#include "dw4/model_timer.hpp"
#include "dw4/database_runtime.hpp"
#include "dw4/game_name.hpp"
#include "dw4/game_ram.hpp"
#include "dw4/meter_control.hpp"
#include "dw4/number_control.hpp"
#include "dw4/player_point.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
AnimationMatrix translated(float value) {auto matrix=identity();matrix[12]=value;return matrix;}
ModelPlayerStartDefaults defaults() {return ModelPlayerStartDefaults({"A","B","C","D"});}
ModelPlayerMapRules maps() {return {{"metal0","metal1","metal2","metal3","metal4"},{"lobby0","lobby1","lobby2","lobby3"}};}
const ModelPlayerClock clock=[] {return std::uint64_t{100};};
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,std::string category,std::optional<std::string> name,int index,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [hook=std::move(hook)]{return MessengerManager{hook,{}};}},std::move(category),std::move(name),index);
}
struct Fixture {
    player_runtime_fixture::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> resource=std::make_shared<ActorSceneResource>(source.document(),"synthetic/create.igb",3);
    std::shared_ptr<ModelSceneInfo> info=std::make_shared<ModelSceneInfo>();
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSceneOne> one=std::make_shared<ModelSceneOne>();
    std::shared_ptr<const ModelSceneManager> manager=std::make_shared<ModelSceneManager>();
    GameDataState data;
    ActionEventRuntime events;
    std::shared_ptr<GameDataList> row=data.player(0);
    std::shared_ptr<const std::string> map=std::make_shared<const std::string>("ordinary");
    unsigned queries{},gets{},starts{},recalculations{},ui_calls{};
    Fixture() {
        info->name="original-parent";info->kind="KIND_PLAYER";
        one->name="ONE";one->node=resource->mutable_node(source.nodes[0]);
        ram->data=std::make_shared<ModelSceneList>();ram->data->append(one);
        register_model_ram(info,ram,0,manager);
        row->require<GameDataInt>("MHP")->data=91;row->require<GameDataInt>("MMP")->data=37;
    }
    ModelPlayerMapReader reader() {return [this]{return map;};}
    void game() {
        provider(events,"GAMERAM",{},0,[this](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            if(delivery.message.command=="GETPLAYER") {++gets;messenger.buffer().add_data_list(row);}
            else if(delivery.message.command=="GAMESTART")++starts;
            else if(delivery.message.command=="RECALCPLAYER")++recalculations;
            else throw std::runtime_error("Unexpected player creation game-data command.");
            return MessengerCallResult{true,1,{}};
        });
    }
    void ui(int index=0) {
        for(const auto* name:{"lvnm","hpnm","mpnm"})provider(events,"NUMBERCTRL",name,index,
            [this](NativeMessenger&,const MessengerDelivery&){++ui_calls;return MessengerCallResult{true,1,{}};});
        for(const auto* name:{"hpmt","mpmt"})provider(events,"METERCTRL",name,index,
            [this](NativeMessenger&,const MessengerDelivery&){++ui_calls;return MessengerCallResult{true,1,{}};});
    }
};

void retained_actual_owner_scopes() {
    Fixture f;f.ui(1);f.map=std::make_shared<const std::string>("lobby1");
    const auto old=make_model_player_work();old->script.record=88;f.one->work=old;
    const auto old_hp=old->script.hp;old_hp->data=17;
    const auto timer=start_model_timer(*f.ram,"KEIKATIMER",123);
    const auto timer_before=timer->timer;
    const auto old_matrix=f.one->node->matrix_owner();const auto authored=*old_matrix;
    const auto replacement_one=std::make_shared<ModelSceneOne>();replacement_one->name="ONE";
    replacement_one->node=f.resource->mutable_node(f.source.nodes[1]);
    const auto replacement_list=std::make_shared<ModelSceneList>();replacement_list->append(replacement_one);
    const auto other_parent=std::make_shared<ModelSceneInfo>();other_parent->name="replacement-parent";
    const auto foreign_work=make_model_player_work();foreign_work->script.hp->data=222;
    auto& messenger=f.events.messenger();messenger.select("MODELCTRL",f.info->name,0);
    messenger.buffer().add_string("outer argument");messenger.buffer().add_int(73);
    const auto parent_buffer=messenger.buffer_owner();std::shared_ptr<const MessengerBuffer> query_buffer;
    ModelPlayerCreate create(f.ram,defaults(),maps());bool point_ready{},get_ready{},start_ready{};
    provider(f.events,"PLAYERPOINT",{},0,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++f.queries;
        check(delivery.message.command=="GETMATRIX" && !delivery.message.name && delivery.message.index==-1 &&
            live.buffer().integers()==std::vector<std::int32_t>{0,0},"Player point packet differs from original construction.");
        const auto work=create.created_work();
        check(work && work!=old && f.one->work==work && work->status.is_player==1 &&
            work->motion.turn.rate==std::array<float,3>{0,7,0} && work->turn_attack_timers &&
            *work->turn_attack_timers==std::vector<std::int32_t>(8,0),"First real service did not observe the installed player fields.");
        check(work->script.hp!=old_hp && old_hp->data==17 && old->script.record==88 &&
            f.one->node->matrix_owner()==old_matrix && *old_matrix==authored && timer->timer==timer_before,
            "Factory replacement mutated retained old work, node storage or RAM timer.");
        if(f.queries==1) {
            query_buffer=live.buffer_owner();live.buffer().add_matrix(translated(10));live.buffer().add_matrix(translated(20));
            f.ram->data=replacement_list;
        } else check(live.buffer_owner()==query_buffer && live.buffer().matrices().size()==2,
            "Point retry replaced its current packet or appended results twice.");
        return MessengerCallResult{point_ready,1,"point paused"};
    });
    provider(f.events,"GAMERAM",{},0,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        if(delivery.message.command=="GETPLAYER") {
            ++f.gets;
            check(live.buffer().integers()==std::vector<std::int32_t>{0} && live.buffer_owner()==parent_buffer &&
                live.buffer_depth()==0 && *replacement_one->node->matrix_owner()==translated(10) && *old_matrix==authored,
                "GETPLAYER lost fresh placement ONE lookup, first point matrix or parent buffer identity.");
            if(f.gets==1) {
                f.ram->number=1;f.ram->parent=other_parent;f.info->name="captured-parent-live";f.one->work=foreign_work;
                live.buffer().add_data_list(f.row);
            } else check(live.buffer().objects().size()==1,"GETPLAYER retry rebuilt its returned row packet.");
            return MessengerCallResult{get_ready,1,"GETPLAYER paused"};
        }
        check(live.buffer().integers()==std::vector<std::int32_t>{1},"Startup did not reread the actual live RAM number.");
        if(delivery.message.command=="GAMESTART") {
            ++f.starts;
            check(live.buffer().strings()==std::vector<std::optional<std::string>>{"captured-parent-live","B"} &&
                create.created_work()->script.hp==f.row->require<GameDataInt>("HP") && f.one->work==foreign_work &&
                foreign_work->script.hp->data==222,"Startup redirected retained work or parent after GETPLAYER.");
            if(f.starts==1)f.data.reset_player(0);
            return MessengerCallResult{start_ready,1,"GAMESTART paused"};
        }
        check(delivery.message.command=="RECALCPLAYER","Unexpected startup command.");++f.recalculations;
        return MessengerCallResult{true,1,{}};
    });
    check(create.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::service_required &&
        create.phase()==ModelPlayerCreatePhase::point_query && messenger.buffer_depth()==1,"Point boundary was not retained.");
    const auto created=create.created_work();point_ready=true;
    check(create.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::service_required &&
        create.phase()==ModelPlayerCreatePhase::get_player && create.created_work()==created &&
        create.placement_one()==replacement_one,"GETPLAYER did not retain the existing creation/placement prefix.");
    get_ready=true;
    check(create.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::service_required &&
        create.phase()==ModelPlayerCreatePhase::startup && create.startup()->prefix().row()==f.row,
        "Post-GETPLAYER continuation rejected live RAM changes or lost the returned row.");
    start_ready=true;
    check(create.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::outer_generate_required &&
        create.returned() && !create.faulted() && create.captured_parent()==f.info && create.captured_one()==f.one &&
        created->script.original_is_main_lobby==1 && foreign_work->script.original_is_main_lobby==0 && f.ui_calls==5 &&
        f.recalculations==1 && f.queries==2 && f.gets==2 && f.starts==2,"Player +98 did not return with its precise captured state.");
    check(create.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::outer_generate_required &&
        f.recalculations==1,"Completed player callback replayed its effects.");
}

void placement_inputs_and_repeated_factory() {
    for(unsigned shape=0;shape<6;++shape) {
        Fixture f;f.game();f.ui();auto& messenger=f.events.messenger();
        messenger.select("MODELCTRL",{},0);
        if(shape==1 || shape==2 || shape==3 || shape==5)messenger.buffer().add_vector({4,5,6});
        if(shape==2 || shape==3)messenger.buffer().add_vector({0,0,0});
        if(shape==3)messenger.buffer().add_vector({100,100,100});
        if(shape==4 || shape==5)messenger.buffer().add_matrix(translated(81));
        provider(f.events,"PLAYERPOINT",{},0,[&](NativeMessenger&,const MessengerDelivery&) {
            ++f.queries;return MessengerCallResult{true,1,{}}; // No matrix really leaves identity.
        });
        ModelPlayerCreate first(f.ram,defaults(),maps());
        check(first.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::outer_generate_required,
            "Original player creation did not return for a supported placement packet.");
        const auto matrix=*f.one->node->matrix_owner();
        check(f.queries==unsigned(shape==0) && matrix[12]==(shape==4 ? 81:shape==0 ? 0:4) &&
            matrix[13]==(shape==0 || shape==4 ? 0:5),"Vector precedence, query bypass or missing-result identity differs.");
        const auto old=first.created_work();const auto old_attack=old->turn_attack_timers;old->script.record=43;
        messenger.select("MODELCTRL",{},0);messenger.buffer().add_matrix(translated(92));
        ModelPlayerCreate second(f.ram,defaults(),maps());
        check(second.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::outer_generate_required &&
            f.one->work==second.created_work() && second.created_work()!=old &&
            second.created_work()->turn_attack_timers!=old_attack && old->script.record==43,
            "Repeated player creation reset the retained old generation or reused its new list.");
    }
}

void checked_failure_prefixes() {
    for(bool allocated:{false,true}) {
        Fixture f;if(allocated)f.ram->data=std::make_shared<ModelSceneList>();else f.ram->data.reset();
        const auto existing=f.ram->data;ModelPlayerCreate create(f.ram,defaults(),maps());
        rejects([&]{create.resume(f.events.messenger(),f.events,clock,f.reader());},"Missing ONE was implicitly constructed.");
        check(create.faulted() && !create.created_work() && f.ram->data && f.ram->data->values().empty() &&
            (!allocated || f.ram->data==existing),"Missing ONE fault rolled back ensure-data or replaced an existing empty list.");
    }
    for(unsigned wrong=0;wrong<3;++wrong) {
        Fixture f;auto& messenger=f.events.messenger();messenger.select("MODELCTRL",{},0);messenger.buffer().add_matrix(translated(71));
        provider(f.events,"GAMERAM",{},0,[&](NativeMessenger& live,const MessengerDelivery&) {
            if(wrong==1)live.buffer().add_data_list({});
            if(wrong==2)live.buffer().add_game_object(std::make_shared<GameDataInt>());
            if(wrong)live.buffer().add_data_list(f.row);
            return MessengerCallResult{true,1,{}};
        });
        ModelPlayerCreate create(f.ram,defaults(),maps());
        rejects([&]{create.resume(messenger,f.events,clock,f.reader());},"Missing/wrong object0 was replaced by a later valid row.");
        check(create.faulted() && f.one->work==create.created_work() && f.one->work->status.is_player==1 &&
            *f.one->node->matrix_owner()==translated(71) && !create.startup(),"GETPLAYER failure lost committed work/placement effects.");
        rejects([&]{create.resume(messenger,f.events,clock,f.reader());},"Faulted construction replayed its installed prefix.");
    }
}

void nested_delivery_and_recursive_fault() {
    Fixture f;f.game();f.ui();ModelPlayerCreate create(f.ram,defaults(),maps());bool ready{};
    provider(f.events,"PLAYERPOINT",{},0,[&](NativeMessenger& messenger,const MessengerDelivery&) {
        if(++f.queries==1)messenger.buffer().add_matrix(translated(35));
        return MessengerCallResult{ready,1,"nested point pause"};
    });
    provider(f.events,"TESTCREATE",{},0,[&](NativeMessenger& messenger,const MessengerDelivery&) {
        const auto result=create.resume(messenger,f.events,clock,f.reader());
        return MessengerCallResult{result.stop==ModelPlayerCreateStop::outer_generate_required,1,result.diagnostic};
    });
    auto& messenger=f.events.messenger();messenger.select("TESTCREATE",{},0);auto outer=messenger.begin_send("CALL_PLAYER_98");
    check(f.events.resume(outer).stop==MessengerStop::service_required && messenger.buffer_depth()==1,
        "Nested player point pause did not retain its owning outer delivery.");
    const auto work=create.created_work();ready=true;
    check(f.events.resume(outer).stop==MessengerStop::completed && create.returned() && create.created_work()==work &&
        f.queries==2 && f.gets==1 && messenger.buffer_depth()==0,"Nested delivery retry replayed the player factory or packet prefix.");

    Fixture recursive;ModelPlayerCreate uncertain(recursive.ram,defaults(),maps());
    provider(recursive.events,"PLAYERPOINT",{},0,[&](NativeMessenger& live,const MessengerDelivery&) {
        rejects([&]{uncertain.resume(live,recursive.events,clock,recursive.reader());},"Recursive construction was accepted.");
        return MessengerCallResult{true,1,{}};
    });
    auto& live=recursive.events.messenger();live.select("MODELCTRL",{},0);
    rejects([&]{uncertain.resume(live,recursive.events,clock,recursive.reader());},"A caught recursive call was converted into successful construction.");
    check(uncertain.faulted() && recursive.one->work==uncertain.created_work() && live.buffer_depth()==1,
        "Recursive fault restored the original packet or erased the installed work prefix.");
}

void nullable_parent_original_read_site() {
    for(const bool used:{false,true}) {
        Fixture f;f.game();f.ui();f.ram->parent.reset();
        f.row->require<GameDataBool>("ISUSE")->data=used ? 0:1;
        auto& messenger=f.events.messenger();messenger.select("MODELCTRL",{},0);messenger.buffer().add_matrix(translated(46));
        ModelPlayerCreate create(f.ram,defaults(),maps());
        if(used) {
            rejects([&]{create.resume(messenger,f.events,clock,f.reader());},"GAMESTART accepted its captured null parent.");
            check(create.faulted() && create.startup() && create.startup()->prefix().phase()==ModelPlayerStartPhase::game_start &&
                messenger.buffer().integers()==std::vector<std::int32_t>{0} && messenger.buffer().strings().empty() &&
                f.gets==1 && f.starts==0,"Null parent fault occurred before the original GAMESTART name read and integer prefix.");
        } else {
            check(create.resume(messenger,f.events,clock,f.reader()).stop==ModelPlayerCreateStop::outer_generate_required &&
                !create.faulted() && f.recalculations==1 && f.ui_calls==5,
                "Unused captured null parent was eagerly rejected before player +98 could return.");
        }
        check(!create.captured_parent() && f.one->work==create.created_work() &&
            create.created_work()->script.hp==f.row->require<GameDataInt>("HP") &&
            *f.one->node->matrix_owner()==translated(46),"Null parent handling erased earlier factory/placement/HP binding effects.");
    }
}

void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    auto stage=std::make_shared<ActorSceneResource>(read_igb_document(resources.archives().at(0).read(179)),"ndmw.afs/179",30);
    auto player=std::make_shared<ActorSceneResource>(read_igb_document(resources.read("_common/p_agumon.igb")),"_common/p_agumon.igb",17);
    const auto info=player->models().info(1386);
    const auto names=read_model_player_start_defaults(root/"sys"/"main.dol");
    const auto rules=read_model_player_map_rules(root/"sys"/"main.dol");
    ActionEventRuntime events;GameRamRuntime game(events);DatabaseRuntime databases(events);
    NumberControlRuntime numbers(events);MeterRuntime meters(events);PlayerPointRuntime points(events);
    points.register_source(decode_player_points(stage));
    numbers.register_source(decode_number_controls(stage->document(),stage->source(),stage->source_occurrence()));
    meters.register_source(decode_meter_controls(stage));
    databases.register_source(decode_database_managers(read_igb_document(resources.read("dng101b1/separate/usa_pack.igb")),
        "dng101b1/separate/usa_pack.igb",8));
    const auto converter=std::make_shared<GameNameConverter>(read_game_name_table(root/"sys"/"main.dol"),
        std::make_shared<std::vector<std::uint8_t>>());
    game.set_name_services({[converter]{return converter;}});
    const auto map=std::make_shared<const std::string>("dng101b1");
    const ModelPlayerMapReader reader=[map]{return map;};
    constexpr std::array<std::uint32_t,4> references{1725,6417,6420,6622},node_references{6390,6457,6524,6590};
    std::array<std::shared_ptr<ModelSceneRam>,4> rams;
    std::array<ControllerObjectId,4> ids;
    std::array<std::shared_ptr<ModelPlayerWork>,4> retained;
    for(std::size_t index=0;index<4;++index) {
        ids[index]=events.register_model_scene_ram(player,1386,references[index],static_cast<std::int32_t>(index));
        rams[index]=events.model_ram(ids[index]);
        const auto one=find_model_one(*rams[index]);
        check(one && !one->work && one->node==player->mutable_node(node_references[index]),
            "Original player creation did not receive the actual serialized-null work and canonical node owner.");
    }
    const auto land_info=player->models().hit_land_info(1712);
    const auto land_rams=land_info->rams->values();
    check(land_rams.size()==4,"Original HITLANDDELIVER must retain its authored physical four-RAM list.");
    for(std::size_t index=0;index<land_rams.size();++index) {
        const auto original=std::dynamic_pointer_cast<ModelHitLandRam>(land_rams[index]);
        if(!original || !original->reference())throw std::runtime_error("Original HITLANDDELIVER list contains no concrete source RAM.");
        const auto id=events.register_hit_land_scene_ram(player,1712,*original->reference(),static_cast<std::int32_t>(index));
        const auto actual=events.hit_land_ram(id);
        check(actual==original && actual->node==find_model_one(*rams[index])->node &&
            actual->parent.lock()==land_info && actual->manager.lock()!=events.model_manager() &&
            actual->previous_position==std::array<float,3>{},
            "Original HITLANDDELIVER registration detached its shared node or invented previous position/manager identity.");
    }
    auto& messenger=events.messenger();
    for(std::int32_t group=0;group<2;++group) {
        messenger.select("PLAYERPOINT",{},-1);messenger.buffer().add_int(group);
        auto select=messenger.begin_send("SETGENERATEPOS");
        check(events.resume(select).stop==MessengerStop::completed,"Original point-group selection did not complete.");
        for(std::size_t index=0;index<4;++index) {
            messenger.select("MODELCTRL",info->name,static_cast<std::int32_t>(index));
            ModelPlayerCreate create(rams[index],names,rules);
            const auto result=create.resume(messenger,events,clock,reader);
            if(result.stop!=ModelPlayerCreateStop::outer_generate_required)
                throw std::runtime_error("Original player +98 continuation: "+result.diagnostic);
            const auto work=create.created_work();const auto one=find_model_one(*rams[index]);
            const auto point=decode_igb_player_placement(stage->document(),115,static_cast<std::uint32_t>(group),static_cast<std::uint32_t>(index));
            const auto row=game.state().player(index);
            check(create.returned() && one->work==work && create.captured_one()==one && create.captured_parent()==info &&
                create.placement_one()==one && work!=retained[index],
                "Original player +98 lost factory replacement or actual owner captures.");
            const auto expected=points.point({30,point.transform_reference}).matrix;
            check(expected==point.matrix,"Original point provider matrix differs from the independently decoded group/slot transform.");
            if(*one->node->matrix_owner()!=expected) {
                for(std::size_t element=0;element<16;++element)if(one->node->matrix_owner()->at(element)!=expected[element])
                    std::cerr<<"Placement difference group="<<group<<" player="<<index<<" element="<<element<<" actual="<<
                        one->node->matrix_owner()->at(element)<<" expected="<<expected[element]<<'\n';
            }
            check(*one->node->matrix_owner()==expected,"Original player +98 lost the current point provider group/slot matrix.");
            check(work->script.hp==row->require<GameDataInt>("HP") && work->script.max_hp==row->require<GameDataInt>("MHP") &&
                create.startup()->prefix().row()==row && work->turn_attack_timers && work->turn_attack_timers->size()==8 &&
                work->script.original_is_main_lobby==0 && !game.pending_count() && !game.faulted(),
                "Original GETPLAYER/startup failed to retain native data rows or return through the ordinary-map tail.");
            for(const auto& number:numbers.sources()[0].infos()) {
                const char* field=number.name=="lvnm" ? "LEVEL":number.name=="hpnm" ? "HP":number.name=="mpnm" ? "MP":nullptr;
                if(!field)continue;
                const auto& shown=numbers.ram({30,number.rams.at(index).reference});
                check(shown.work && shown.work->current==row->require<GameDataInt>(field)->data && shown.uv && shown.uv->revision>0,
                    "Original constructed player startup did not update the actual number-controller work/UV owner.");
            }
            retained[index]=work;
        }
    }
    check(points.saved_group()==1,"Original player creation changed the retained point manager group.");
    auto definitions=std::make_shared<ModelPlayerCreateDefinitions>(ModelPlayerCreateDefinitions{names,rules});
    events.set_player_creation_services({std::move(definitions),clock,reader});
    events.set_model_context(std::make_shared<ModelSceneContext>(ModelSceneContext{1000000000}));
    std::size_t table_count{};
    for(const auto& source:{player,stage}) {
        for(const auto reference:source->document().object_list(*source->document().info_list_reference)) {
            if(!reference)continue;
            const auto& object=source->document().object(*reference);
            const auto& type=source->document().metadata.object_types.at(object.type_index).name;
            if(type!="beSeInfo" && type!="beActionStarterInfo")continue;
            events.register_consumer(decode_action_event_tables(source->document(),*reference,source->source(),source->source_occurrence()),true);
            ++table_count;
        }
    }
    const auto old_work=find_model_one(*rams[0])->work;
    messenger.select("MODELCTRL",info->name,0);auto generate=messenger.begin_send("GENERATE");
    const auto boundary=events.resume(generate);
    check(boundary.stop==MessengerStop::service_required && boundary.diagnostic.find("ACTION")!=std::string::npos,
        "Registered original GENERATE did not retain its reached native ACTION service boundary.");
    const auto created=events.model_work(ids[0]);const auto generated_one=find_model_one(*rams[0]);
    check(created && created!=old_work && rams[0]->active==1 && rams[0]->words==std::array<std::uint32_t,4>{} &&
        created->status.is_player==1 && created->script.hp==game.state().player(0)->require<GameDataInt>("HP") &&
        !generated_one->node->box_owner(),"Registered GENERATE did not commit player startup and outer root-box clearing.");
    ModelMotionPose expected_heading(generated_one->node->matrix_owner());synchronize_model_heading(expected_heading);
    check(created->motion.turn.desired[1]==expected_heading.turn().desired[1] &&
        created->motion.turn.applied[1]==expected_heading.turn().applied[1],
        "Registered GENERATE did not synchronize heading before its actual direct SQ_WAIT entry.");
    const auto retry=events.resume(generate);
    check(retry.stop==MessengerStop::service_required && events.model_work(ids[0])==created &&
        retry.diagnostic==boundary.diagnostic,"Retrying unfinished original GENERATE repeated its factory or lost the direct-entry boundary.");

    // Install actual actor-aware ACTION execution and the original exported
    // broadcast tables, then resume the already-entered original GENERATE.
    const auto skin=decode_igb_skinning(player->document(),5665);
    std::vector<AnimationClip> clips;
    for(const auto& animation:list_igb_animations(player->document(),skin))
        clips.push_back(decode_igb_animation(player->document(),skin,animation.reference_index));
    auto sources=std::make_shared<AnimationSourceRuntime>(player->source(),std::move(clips));sources->initialize_action_offset();
    std::vector<std::shared_ptr<ActionRuntime>> actions;
    auto action_manager=std::make_shared<ModelSceneManager>();action_manager->context=events.model_context();
    for(std::uint32_t slot=0;slot<4;++slot) {
        const auto binding=decode_player_runtime_binding(player->document(),{1386,546,slot,1},player->source(),player->source_occurrence());
        auto action=std::make_shared<ActionRuntime>(decode_action_actor_runtime(player->document(),binding.action_program(),*sources,static_cast<std::int32_t>(slot)));
        auto services=std::make_shared<ActionServices>(events.action_services(binding));
        auto scene_queries=std::make_shared<ActionSceneQueryOwner>();scene_queries->prepare(binding,player->action(binding.action_reference(),binding.action_ram_reference()));
        auto manager=make_action_dispatch_manager(binding,action,sources,services,action_manager,scene_queries);
        const ControllerObjectId recipient{player->source_occurrence(),binding.action_ram_reference()};
        const PreparedControllerRegistration controller{recipient,
            {[action]{return std::optional<std::uint8_t>{action->active()?std::uint8_t{1}:std::uint8_t{0}};},[manager]{return manager;}},player->source()};
        const MessengerRegistration row{recipient,"ACTION",binding.model_name(),static_cast<std::int32_t>(slot)};
        events.register_controllers(std::span(&controller,1),std::span(&row,1));
        actions.push_back(std::move(action));
    }
    const auto finished=events.resume(generate);
    if(finished.stop!=MessengerStop::completed)throw std::runtime_error("Original GENERATE after ACTION connection: "+finished.diagnostic);
    const auto check_generated=[&](std::size_t slot,const std::shared_ptr<ModelPlayerWork>& work) {
        const auto one=find_model_one(*rams[slot]);
        // Original SQ_WAIT's dual branch is LABEL, CTRL, MOVE, ACTION WAITD1.
        // C3AC returns at the next LABEL (index 4), without installing a wait.
        check(work->sequence.name=="SQ_WAIT" && work->sequence.command_data.actual_owner()==player->models().object(1534) &&
            work->sequence.command_index==4 && work->sequence.label_count==1 && !work->sequence.wait.name &&
            actions[slot]->current_action()=="WAITD1" && find_model_timer(*rams[slot],"KEIKATIMER"),
            "Connected original GENERATE did not return at SQ_WAIT's next LABEL after actual ACTION execution.");
        check(!one->node->child_list_owner()->first_backing_node()->box_owner() && !work->base_box && !one->node->box_owner(),
            "Original GENERATE invented a box despite the physical first child's null source box.");
        std::size_t number_effects{};
        for(const auto& entry:*work->subcommands->list_owner()) {
            const auto effect=std::get_if<ModelEffectSubcommandState>(&entry.value);
            if(!effect || effect->command.model_name!="i_pcnumic")continue;
            ++number_effects;
            check(!entry.started && entry.delay==480 && effect->command.sequence_name=="SQ_OPENP"+std::to_string(slot) &&
                effect->command.offset==std::array<float,3>{0,10,0} && effect->command.sequence_dead && effect->command.position_lock,
                "Original GENERATE lost the real player +B8 number-effect payload or started it without the frame scheduler.");
        }
        check(number_effects==1,"Original GENERATE skipped or repeated its player +B8 number effect.");
    };
    check(events.model_work(ids[0])==created,"Resuming original GENERATE replaced its already-created player work.");
    check_generated(0,created);
    for(std::size_t slot=1;slot<4;++slot) {
        messenger.select("MODELCTRL",info->name,static_cast<std::int32_t>(slot));auto next=messenger.begin_send("GENERATE");
        const auto result=events.resume(next);
        if(result.stop!=MessengerStop::completed)throw std::runtime_error("Original GENERATE player "+std::to_string(slot)+": "+result.diagnostic);
        const auto work=events.model_work(ids[slot]);
        check(work && work!=retained[slot],"An original player slot failed its fresh-work GENERATE return.");
        check_generated(slot,work);
    }
    // Explicit neutral original pad data for the first per-RAM update. Device
    // polling and manager scheduling are not supplied by this integration test.
    const auto pads=std::make_shared<ModelPadManager>();pads->data=std::make_shared<ModelPadList>();
    pads->data->backing=std::make_shared<ModelPadList::Storage>(5);
    for(auto& pad:*pads->data->backing)pad=std::make_shared<ModelPadState>();
    events.model_manager()->pads=pads;
    events.model_manager()->system=std::make_shared<ModelSceneSystem>(ModelSceneSystem{1});
    auto frame=events.begin_model_frame(ids[0]);const auto frame_result=events.resume(*frame);
    if(frame_result.completed || frame_result.diagnostic.find("HITLANDMODEL")==std::string::npos)
        throw std::runtime_error("Original first model frame boundary: "+frame_result.diagnostic);
    check(frame->captured().work==created && frame->input() && frame->input()->completed() &&
        frame->status() && frame->status()->completed() && frame->motion() && frame->motion()->completed() &&
        frame->subcommands() && frame->subcommands()->completed() &&
        frame->collision() && !frame->collision()->completed() &&
        created->key_input==0 && created->key_event_name=="SQ_NONE" && created->sequence.name=="SQ_WAIT",
        "Original first frame did not preserve generated work through input, control, status, motion and subcommands.");
    check(created->subcommands->list_owner()->size()==1 &&
        created->subcommands->list_owner()->front().elapsed==1 &&
        !created->subcommands->list_owner()->front().started,
        "Original delayed player number effect did not consume the actual system delta exactly once.");
    std::cout<<"Original player +98 returns=8 across four actual RAM/ONE/node owners and two point groups; "
        "registered GENERATE returns=4 with real actor-aware ACTION, "<<table_count<<" exported broadcast tables, player +B8 and nullable box tails; "
        "first neutral actual frame enters its shared-node HITLANDDELIVER and reaches the actual HITLANDMODEL query boundary; "
        "original query geometry, scheduling and full gameplay remain required\n";
}
}
int main(int argc,char** argv) {
    try {
        retained_actual_owner_scopes();placement_inputs_and_repeated_factory();checked_failure_prefixes();nested_delivery_and_recursive_fault();
        nullable_parent_original_read_site();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_model_player_create_tests [EXTRACTED_ROOT]");
        std::cout<<"Model player creation checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Model player creation after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
