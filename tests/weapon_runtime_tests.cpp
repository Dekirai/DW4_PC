#include "dw4/action_event_runtime.hpp"
#include "dw4/weapon_runtime.hpp"
#include "player_runtime_binding_fixture.hpp"
#include "weapon_fixture.hpp"

#include <iostream>

namespace {
using namespace dw4;
using weapon_fixture::Fixture;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid weapon runtime operation was accepted.");
}
WeaponProgram program(Fixture fixture={},std::uint32_t occurrence=9) {
    return decode_weapons(std::make_shared<ActorSceneResource>(fixture.document(),"synthetic/weapon.igb",occurrence));
}
struct PlayerScene {
    std::shared_ptr<ActorSceneResource> resource;
    std::array<std::shared_ptr<ActorSceneAppearanceList>,2> lists;
    std::shared_ptr<const ActorSceneSkeleton> skeleton;
    explicit PlayerScene(std::uint32_t occurrence=30) {
        player_runtime_fixture::RuntimeFixture f;
        f.text(f.bones[2],2,"RArm04");f.text(f.bones[3],2,"LArm04");
        for(const auto& actors:f.ordered_actors)for(const auto actor:actors) {
            const auto& b=f.records[actor].values[11];
            const auto appearance=std::uint32_t(b[0]) | std::uint32_t(b[1])<<8 | std::uint32_t(b[2])<<16 | std::uint32_t(b[3])<<24;
            f.scalar(appearance,5,f.list(player_runtime_fixture::matrix_bone_list,{}));
        }
        resource=std::make_shared<ActorSceneResource>(f.document(),"synthetic/player.igb",occurrence);
        for(std::size_t i=0;i<2;++i)lists[i]=resource->appearance_list(f.appearance_lists[i]);
        skeleton=resource->skeleton(f.skeleton);
    }
};
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void endpoint(ActionEventRuntime& events,std::string category,std::optional<std::string> name,std::int32_t index,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []{return std::optional<std::uint8_t>{std::uint8_t{1}};},[hook=std::move(hook)] {return MessengerManager{hook,{}};}},
        std::move(category),std::move(name),index);
}
MessengerDispatch send(ActionEventRuntime& events,std::string command="GENERATE",std::string name="weapon",std::int32_t index=0) {
    return events.messenger().begin_send_to("PLAYERARMS",std::move(name),index,std::move(command));
}
void complete(ActionEventRuntime& events,MessengerDispatch& dispatch) {
    check(events.resume(dispatch).stop==MessengerStop::completed,"Weapon message did not complete.");
}
struct Providers {
    PlayerScene scene;
    std::shared_ptr<ActorSceneAppearanceList> appearances=scene.lists[0];
    std::shared_ptr<const ActorSceneSkeleton> skeleton=scene.skeleton;
    unsigned names=1,player_calls{},action_calls{};
    std::vector<std::array<std::optional<std::string>,2>> labels;
    std::function<void()> player_effect,label_effect;
    explicit Providers(ActionEventRuntime& events) {
        for(std::int32_t index=0;index<2;++index) {
            endpoint(events,"PLAYER",{},index,[this](NativeMessenger& messenger,const MessengerDelivery& delivery) {
                check(delivery.message.command=="GETINFONAME_RUN","Weapon sent another PLAYER query.");
                ++player_calls;for(unsigned i=0;i<names;++i)messenger.buffer().add_string("player");
                if(player_effect)player_effect();
                return MessengerCallResult{true,1,{}};
            });
            endpoint(events,"ACTION","player",index,[this](NativeMessenger& messenger,const MessengerDelivery& delivery) {
                check(delivery.message.command=="GETAPPEARANCELIST","Weapon sent another ACTION query.");
                ++action_calls;messenger.buffer().add_game_object(appearances);messenger.buffer().add_game_object(skeleton);
                return MessengerCallResult{true,1,{}};
            });
            endpoint(events,"MODELCTRL","player",index,[this](NativeMessenger& messenger,const MessengerDelivery& delivery) {
                check(delivery.message.command=="SETLABEL" && messenger.buffer().strings().size()==2,
                    "Weapon SETLABEL argument shape differs.");
                labels.push_back({messenger.buffer().strings()[0],messenger.buffer().strings()[1]});
                if(label_effect)label_effect();
                return MessengerCallResult{true,1,{}};
            });
        }
    }
};
void aliases_registration_and_lifetime() {
    Fixture f;f.contents(f.ram_list,{f.rams[0],f.rams[1],f.rams[0]});f.contents(f.exported_info_list,{f.info,f.other_info});
    auto doc=f.document();doc.references[f.rams[1]].value_index=doc.references[f.rams[0]].value_index;
    auto p=decode_weapons(std::make_shared<ActorSceneResource>(std::move(doc),"synthetic/weapon.igb",9));
    ActionEventRuntime events;
    {
        WeaponRuntime runtime(events);runtime.register_source(p);
        const auto& ram=runtime.ram({9,f.rams[1]});
        check(&ram==&runtime.ram({9,f.rams[0]}) && ram.index==2 && ram.info==ControllerObjectId{9,f.other_info} &&
            ram.words==std::array<std::int32_t,4>{} && ram.active==255,"Weapon registration lost final shared RAM writes.");
        check(runtime.manager_id().is_native() && events.messenger().registration_count()==6,
            "Weapon registrations were deduplicated or manager identity was serialized.");
        auto name=send(events,"GETINFONAME","weapon",0);complete(events,name);
        check(events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"other"},
            "Weapon GETINFONAME used registration name instead of live final parent.");
        complete(events,name);check(events.messenger().buffer().strings().size()==1,"Completed weapon send repeated an append.");
        runtime.set_active({9,f.rams[1]},0);runtime.set_index({9,f.rams[0]},1);
        check(runtime.ram({9,f.rams[0]}).active==0 && runtime.ram({9,f.rams[1]}).index==1,"Weapon live setters detached alias owners.");
        rejects([&]{runtime.set_index({9,f.rams[0]},4);});rejects([&]{runtime.ram({10,f.rams[0]});});
        rejects([&]{runtime.register_source(p);});
        check(events.messenger().registration_count()==6 && runtime.sources().size()==1,"Weapon duplicate registration partially committed.");
    }
    auto alive=send(events,"GETINFONAME","other",1);complete(events,alive);
    check(events.messenger().buffer().strings()[0]=="other","Destroyed weapon facade released the registered provider.");
    for(const auto* command:{"getinfoname","UNKNOWN",""}) {
        auto unknown=send(events,command,"other",1);complete(events,unknown);
        check(events.messenger().buffer().strings().empty(),"Unknown weapon command fabricated a result.");
    }
    ActionEventRuntime invalid;WeaponRuntime runtime(invalid);Fixture large;
    large.contents(large.ram_list,std::vector<std::uint32_t>(5,large.rams[0]));
    rejects([&]{runtime.register_source(program(large));});
    check(runtime.sources().empty() && invalid.messenger().registration_count()==0,"Unsupported ordinal profile partially registered.");
    Fixture unnamed;unnamed.records[unnamed.info].values[2]=dw4::test::words({0});runtime.register_source(program(unnamed,12));
    auto empty_name=send(invalid,"GETINFONAME","");complete(invalid,empty_name);
    check(invalid.messenger().buffer().strings()==std::vector<std::optional<std::string>>{""},
        "Null weapon info name did not register and return the original empty string.");
}
void generation_removal_and_current_list() {
    ActionEventRuntime events;WeaponRuntime runtime(events);Fixture f;f.text(f.bones[0],2,"rArM04");
    const auto p=program(f);runtime.register_source(p);Fixture replacement;replacement.text(replacement.info,2,"replacement");
    runtime.register_source(program(replacement,10));Providers providers(events);
    runtime.set_active({9,f.rams[0]},0);
    auto generate=send(events);complete(events,generate);
    check(runtime.attachments(0).size()==2 && providers.labels.size()==2 && providers.labels[0][1]=="" && !providers.labels[1][1],
        "Weapon lost attachment count or nullable SETLABEL payloads.");
    const auto old=runtime.attachments(0);
    check(old[0].selector->matrix_index()==2 && old[0].selector->children()[0]==p.infos()[0].nodes[0].node &&
        providers.scene.skeleton->hierarchy().bones()[2].joint_index==-1,"Weapon used a joint palette instead of the actual helper bone/node owner.");
    for(const auto& appearance:providers.appearances->values())
        check(appearance->bone_selectors()->values()==std::vector<std::shared_ptr<const ActorBoneSelector>>{old[0].selector,old[1].selector},
            "One generated selector was not shared across all appearance owners.");
    auto unrelated=std::make_shared<const ActorBoneSelector>(1,std::vector<std::shared_ptr<const ActorSceneNode>>{p.infos()[0].nodes[0].node});
    for(const auto& appearance:providers.appearances->values()) {
        appearance->bone_selectors()->append(unrelated);appearance->bone_selectors()->append(old[0].selector);
    }
    auto replace=send(events,"GENERATE","replacement");complete(events,replace);
    check(runtime.attachments(0).size()==2 && runtime.attachments(0)[0].selector!=old[0].selector,
        "Another weapon info failed to remove and replace the same manager/player tracking.");
    for(const auto& appearance:providers.appearances->values())
        check(appearance->bone_selectors()->values().size()==3 && appearance->bone_selectors()->values()[0]==unrelated,
            "Weapon REMOVE failed all-occurrence equality or other-selector order.");
    const auto tracked=providers.appearances;providers.appearances=providers.scene.lists[1];
    auto remove=send(events,"REMOVE","weapon");complete(events,remove);
    for(const auto& appearance:tracked->values())check(appearance->bone_selectors()->values().size()==1,
        "REMOVE retargeted a newly returned appearance list instead of its captured owner.");
    check(providers.appearances->values()[0]->bone_selectors()->values().empty() && runtime.attachments(0).empty(),
        "REMOVE modified an unrelated live appearance list or retained tracking.");
    auto second=send(events);complete(events,second);providers.names=2;const auto labels=providers.labels.size();
    auto none=send(events);complete(events,none);
    check(runtime.attachments(0).empty() && providers.labels.size()==labels && providers.action_calls==3,
        "PLAYER cardinality exit skipped initial REMOVE or ran another appearance/label phase.");
    providers.names=0;auto absent=send(events);complete(events,absent);
    check(runtime.attachments(0).empty() && !runtime.faulted(),"Empty PLAYER result was faulted instead of handled.");
    providers.names=1;
    WeaponRuntime independent(events);Fixture separate;separate.text(separate.info,2,"separate");
    independent.register_source(program(separate,11));
    auto original_again=send(events);complete(events,original_again);
    auto another_manager=send(events,"GENERATE","separate");complete(events,another_manager);
    const auto independent_selector=independent.attachments(0)[0].selector;
    auto remove_original=send(events,"REMOVE");complete(events,remove_original);
    check(independent.attachments(0).size()==2 && providers.appearances->values()[0]->bone_selectors()->values().size()==2 &&
        providers.appearances->values()[0]->bone_selectors()->values()[0]==independent_selector,
        "One weapon manager removed another manager's selectors on the same appearances.");
    auto remove_other=send(events,"REMOVE","separate");complete(events,remove_other);
    Fixture missing_bone;missing_bone.text(missing_bone.info,2,"unmapped");
    missing_bone.records[missing_bone.bones[0]].values[2]=dw4::test::words({0});
    missing_bone.records[missing_bone.sequences[0]].values[2]=dw4::test::words({0});
    runtime.register_source(program(missing_bone,12));const auto before_labels=providers.labels.size();
    auto skipped=send(events,"GENERATE","unmapped");complete(events,skipped);
    check(runtime.attachments(0).size()==1 && runtime.attachments(0)[0].selector->matrix_index()==3 &&
        providers.labels[before_labels][0]=="","Null bone/name handling did not skip the absent bone and normalize the sequence name.");
}
void suspended_prefix_and_live_index() {
    ActionEventRuntime events;WeaponRuntime runtime(events);Fixture f;runtime.register_source(program(f));
    PlayerScene scene;bool ready=false;unsigned player_calls{},action_calls{},label_entries{},external_calls{};
    std::optional<MessengerDispatch> nested;
    endpoint(events,"EXTERNAL","work",0,[&](NativeMessenger&,const MessengerDelivery&) {
        ++external_calls;return MessengerCallResult{ready,1,{}};
    });
    endpoint(events,"PLAYER",{},0,[&](NativeMessenger& messenger,const MessengerDelivery&) {
        ++player_calls;messenger.buffer().add_string("player");runtime.set_index({9,f.rams[0]},1);return MessengerCallResult{true,1,{}};
    });
    endpoint(events,"ACTION","player",1,[&](NativeMessenger& messenger,const MessengerDelivery&) {
        ++action_calls;messenger.buffer().add_game_object(scene.lists[1]);messenger.buffer().add_game_object(scene.skeleton);
        return MessengerCallResult{true,1,{}};
    });
    endpoint(events,"MODELCTRL","player",1,[&](NativeMessenger& messenger,const MessengerDelivery&) {
        if(!nested) {
            ++label_entries;
            if(label_entries==1)return MessengerCallResult{true,1,{}};
            nested.emplace(messenger.begin_send_to("EXTERNAL","work",0,"WORK"));
        }
        const auto result=events.resume(*nested);
        return MessengerCallResult{result.stop==MessengerStop::completed,1,result.diagnostic};
    });
    auto call=send(events);
    check(events.resume(call).stop==MessengerStop::service_required && label_entries==2 && runtime.attachments(0).empty() &&
        runtime.attachments(1).empty(),"Paused SETLABEL lost its exact committed prefix.");
    rejects([&]{send(events,"REMOVE");});
    ready=true;complete(events,call);
    check(player_calls==1 && action_calls==1 && label_entries==2 && external_calls==2 && runtime.attachments(1).size()==2,
        "Weapon retry replayed a query/label or missed live ordinal reload.");
    complete(events,call);check(label_entries==2 && runtime.attachments(1).size()==2,"Completed generation replayed.");
}
void nested_same_ram_and_faults() {
    ActionEventRuntime events;WeaponRuntime runtime(events);runtime.register_source(program());Providers providers(events);
    bool entered{};std::optional<MessengerDispatch> nested;
    providers.label_effect=[&] {
        if(entered)return;
        entered=true;nested.emplace(send(events));complete(events,*nested);
    };
    auto call=send(events);complete(events,call);
    check(runtime.attachments(0).size()==4 && providers.labels.size()==4,"Nested generation on one RAM replaced its parent's invocation state.");
    auto remove=send(events,"REMOVE");complete(events,remove);check(runtime.attachments(0).empty(),"Nested tracking was not removable.");

    ActionEventRuntime bad;WeaponRuntime faulted(bad);faulted.register_source(program());Providers supplied(bad);unsigned invoked{};
    supplied.label_effect=[&]{++invoked;throw std::runtime_error("Explicit uncertain test service.");};
    auto uncertain=send(bad);check(bad.resume(uncertain).stop==MessengerStop::faulted && invoked==1 && faulted.faulted(),
        "Throwing nested service did not retain an uncertain weapon fault.");
    check(bad.resume(uncertain).stop==MessengerStop::faulted && invoked==1,"Uncertain weapon callback was replayed.");
    rejects([&]{faulted.set_index({9,Fixture{}.rams[0]},1);});

    ActionEventRuntime mixed;WeaponRuntime guarded(mixed);guarded.register_source(program());Providers mismatch(mixed);
    PlayerScene separate_same_labels;mismatch.skeleton=separate_same_labels.skeleton;
    auto alien=send(mixed);check(mixed.resume(alien).stop==MessengerStop::faulted && guarded.attachments(0).empty() && mismatch.labels.empty(),
        "Same-labelled separately loaded appearance/skeleton pair was accepted.");
}
int run() {
    try {
        aliases_registration_and_lifetime();generation_removal_and_current_list();suspended_prefix_and_live_index();nested_same_ram_and_faults();
        std::cout<<"weapon_runtime: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"weapon_runtime after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
} // namespace
int main() {return run();}
