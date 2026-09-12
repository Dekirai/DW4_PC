#include "dw4/action_event_runtime.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F call,const char* message) {
    ++checks;try{call();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
IgbDocument document(const pf::RuntimeFixture& fixture) {
    auto doc=fixture.document();
    const auto zero=*doc.field(doc.object(fixture.ones[0]),3);
    auto active=*doc.field(doc.object(fixture.state),4);
    active.slot=4;doc.objects.at(doc.references.at(fixture.model).value_index).fields.push_back(active);
    for(const auto ref:fixture.model_rams) {
        auto& ram=doc.objects.at(doc.references.at(ref).value_index);
        for(auto slot:{2u,6u,7u,8u,9u}) {auto value=zero;value.slot=static_cast<std::uint16_t>(slot);ram.fields.push_back(value);}
        active.slot=5;ram.fields.push_back(active);
    }
    auto bound=*doc.field(doc.object(fixture.ones[0]),5);bound.slot=3;
    for(const auto ref:fixture.nodes)doc.objects.at(doc.references.at(ref).value_index).fields.push_back(bound);
    for(const auto ref:fixture.lods)doc.objects.at(doc.references.at(ref).value_index).fields.push_back(bound);
    return doc;
}
struct Fixture {
    pf::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> scene=std::make_shared<ActorSceneResource>(document(source),"synthetic/dispatch.igb",12);
    ActionEventRuntime events;
    std::shared_ptr<ModelSceneInfo> info=scene->models().info(source.model);
    std::shared_ptr<ModelSceneRam> ram=scene->models().ram(source.model_rams[0]);
    std::shared_ptr<ModelSceneOne> one=find_model_one(*ram);
    ControllerObjectId id=events.register_model_scene_ram(scene,source.model,source.model_rams[0],0);
};
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,const char* category,std::optional<std::string> name,int number,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return 1;},[hook=std::move(hook)]{return MessengerManager{hook,{}};}},category,std::move(name),number);
}
ModelPlayerCreateServices creation_services() {
    auto definitions=std::make_shared<ModelPlayerCreateDefinitions>(ModelPlayerCreateDefinitions{
        ModelPlayerStartDefaults({"A","B","C","D"}),
        ModelPlayerMapRules({"m0","m1","m2","m3","m4"},{"l0","l1","l2","l3"})});
    return {definitions,[]{return std::uint64_t{100};},[]{return std::make_shared<const std::string>("ordinary");}};
}
void independent_captures() {
    Fixture f;auto& messenger=f.events.messenger();messenger.select("MODELCTRL",f.info->name,0);
    const auto old=make_model_player_work();old->script.hp->data=17;old->script.record=9;f.one->work=old;
    ModelBaseInvocation hp(f.ram,"GETHP");
    const auto replacement=make_model_player_work();replacement->script.hp->data=99;
    f.one->work=replacement;
    messenger.buffer().add_int(71);
    check(hp.resume(messenger,f.events).completed,"Captured GETHP did not complete.");
    check(hp.captured_work()==old && hp.status_work()==replacement &&
        messenger.buffer().integers()==std::vector<std::int32_t>{71,17},"Base and status capture scopes were collapsed.");
    check(messenger.buffer().objects().size()==2 && std::get<std::shared_ptr<GameDataInt>>(messenger.buffer().objects()[0])==old->script.hp,
        "GETHP did not retain the captured generation's actual HP wrapper.");
    messenger.buffer().clear();f.one->work.reset();
    ModelBaseInvocation null_hp(f.ram,"GETHP");f.one->work=replacement;
    check(null_hp.resume(messenger,f.events).completed && messenger.buffer().integers()==std::vector<std::int32_t>{0} &&
        messenger.buffer().objects().empty(),"Captured null work was replaced by the status scope's new work.");
    messenger.buffer().clear();ModelBaseInvocation inactive(f.ram,"GETRECNO");f.ram->active=0;
    check(inactive.resume(messenger,f.events).completed && messenger.buffer().integers().empty(),"Inactive GETRECNO appended a result.");
    f.ram->active=1;ModelBaseInvocation invalid(f.ram,"GETRECNO");f.ram->data=std::make_shared<ModelSceneList>();
    rejects([&]{invalid.resume(messenger,f.events);},"Unknown status command skipped its fresh ONE lookup.");
    check(invalid.faulted(),"Failed fresh status lookup remained replayable.");
}
void registration_and_live_fields() {
    Fixture f;const auto original_name=f.info->name;const auto initial_rows=f.events.messenger().registration_count();
    check(f.events.model_ram(f.id)==f.ram && !f.events.model_work(f.id) && f.ram->parent.lock()==f.info && !f.ram->manager.expired(),
        "Registration invented work or lost the actual RAM/manager/parent owners.");
    f.ram->active=255;f.ram->words={1,2,3,4};f.info->name="Changed parent";
    check(f.events.register_model_scene_ram(f.scene,f.source.model,f.source.model_rams[0],2)==f.id &&
        f.ram->number==2 && f.ram->active==255 && f.ram->words==std::array<std::uint32_t,4>{} &&
        f.events.messenger().registration_count()==initial_rows+2,"Aliased registration deduplicated rows or reset the active byte.");
    auto& messenger=f.events.messenger();messenger.select("MODELCTRL",original_name,0);
    auto name=messenger.begin_send("GETINFONAME");
    check(f.events.resume(name).stop==MessengerStop::completed && messenger.buffer().strings()==std::vector<std::optional<std::string>>{"Changed parent"},
        "Registration snapshots or live captured-parent name reads changed.");
    auto other=std::make_shared<ActorSceneResource>(document(f.source),"synthetic/dispatch.igb",12);
    rejects([&]{f.events.register_model_scene_ram(other,f.source.model,f.source.model_rams[0],0);},"Equal-looking loads merged live owners.");
    std::weak_ptr<const void> manager;
    {ActionEventRuntime runtime;auto source=std::make_shared<ActorSceneResource>(document(f.source),"synthetic/other.igb",13);
        const auto id=runtime.register_model_scene_ram(source,f.source.model,f.source.model_rams[0],0);manager=runtime.model_ram(id)->manager;
        check(!manager.expired(),"Actual native manager owner was unavailable while registered.");}
    check(manager.expired(),"A dummy persistent manager token or strong graph cycle survived runtime destruction.");
}
void status_retry_retains_work() {
    Fixture f;auto old=make_model_player_work();old->status.is_player=1;f.one->work=old;
    GameDataState data;auto down=data.player(0)->require<GameDataIntList>("DOWNDATA")->data;
    unsigned calls{};bool ready{};
    provider(f.events,"GAMERAM",{},0,[&](NativeMessenger& messenger,const MessengerDelivery&) {
        ++calls;if(!ready)return MessengerCallResult{false,0,"status query paused"};
        messenger.buffer().add_data_list(data.player(0));return MessengerCallResult{true,1,{}};
    });
    auto& messenger=f.events.messenger();messenger.select("MODELCTRL",f.info->name,0);
    auto dispatch=messenger.begin_send("SETMETAL");
    check(f.events.resume(dispatch).stop==MessengerStop::service_required && calls==1 && old->status.first_flags &&
        (*old->status.first_flags)[7]==1,"Status prefix was not committed before GETPLAYER pause.");
    const auto new_work=make_model_player_work();f.one->work=new_work;ready=true;
    const auto manager=f.ram->manager;f.ram->manager.reset();
    check(f.events.model_work(f.id)==new_work,"Current work lookup did not observe replacement during a retained call.");
    check(f.events.resume(dispatch).stop==MessengerStop::completed && calls==2 && old->status.down_data==down &&
        !new_work->status.down_data && !new_work->status.first_flags,"Status retry re-resolved the replacement ONE.work.");
    messenger.select("MODELCTRL",f.info->name,0);auto hp=messenger.begin_send("GETHP");
    check(f.events.resume(hp).stop==MessengerStop::service_required,"Fresh manager resolution ignored a missing actual RAM backlink.");
    f.ram->manager=manager;
    check(f.events.resume(hp).stop==MessengerStop::completed,"Restoring the real manager could not resume unstarted delivery.");
}
void captured_queue_and_recursive_fault() {
    Fixture f;auto& messenger=f.events.messenger();messenger.select("MODELCTRL",f.info->name,0);
    const auto old=make_model_player_work();f.one->work=old;
    ModelBaseInvocation sound(f.ram,"SETSOUND");f.one->work=make_model_player_work();
    messenger.buffer().add_float(3);messenger.buffer().add_string("sound");
    check(sound.resume(messenger,f.events).completed,"Captured sound append did not complete.");
    ModelSubcommandQueue old_queue(std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>>(old,&old->subcommands));
    ModelSubcommandQueue new_queue(std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>>(f.one->work,&f.one->work->subcommands));
    check(old_queue.entries().size()==1 && new_queue.entries().size()==0,"Subcommand append targeted the replacement generation.");
    messenger.select("MODELCTRL",f.info->name,0);f.one->work->status.is_player=1;
    ModelBaseInvocation status(f.ram,"SETMETAL");GameDataState data;bool recursion_rejected{};
    provider(f.events,"GAMERAM",{},0,[&](NativeMessenger& packet,const MessengerDelivery&) {
        try {status.resume(packet,f.events);}catch(const ModelSceneError&){recursion_rejected=true;}
        packet.buffer().add_data_list(data.player(0));return MessengerCallResult{true,1,{}};
    });
    rejects([&]{status.resume(messenger,f.events);},"Catching a recursive callback let the outer base invocation succeed.");
    check(recursion_rejected && status.faulted() && !status.completed(),"Recursive fault did not remain visible to its owner.");
}
void generate_prefix_and_outer_tail() {
    Fixture f;auto& messenger=f.events.messenger();f.ram->active=0;f.ram->words={4,3,2,1};
    const auto old=make_model_player_work();f.one->work=old;
    messenger.select("MODELCTRL",f.info->name,0);messenger.buffer().add_vector({3,4,5});
    messenger.buffer().add_string("first");messenger.buffer().add_string("SQ_CUSTOM");
    ModelBaseInvocation generate(f.ram,"GENERATE");
    check(!generate.resume(messenger,f.events).completed && f.ram->active==1 && f.ram->words==std::array<std::uint32_t,4>{} &&
        f.one->work==old,"Missing definitions skipped GENERATE's committed activation/reset prefix.");
    GameDataState data;unsigned gets{},starts{};
    data.player(0)->require<GameDataInt>("MHP")->data=91;
    data.player(0)->require<GameDataInt>("MMP")->data=37;
    provider(f.events,"GAMERAM",{},0,[&](NativeMessenger& packet,const MessengerDelivery& delivery) {
        if(delivery.message.command=="GETPLAYER") {++gets;packet.buffer().add_data_list(data.player(0));}
        else if(delivery.message.command=="GAMESTART")++starts;
        return MessengerCallResult{true,1,{}};
    });
    for(const auto* name:{"lvnm","hpnm","mpnm"})provider(f.events,"NUMBERCTRL",name,0,
        [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    for(const auto* name:{"hpmt","mpmt"})provider(f.events,"METERCTRL",name,0,
        [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    f.ram->words[0]=29;
    // Base, player +98 and outer return must observe three distinct work reads.
    const auto outer=make_model_player_work();f.one->work=outer;
    f.one->node->child_list_owner()->clear(); // Original first-slot read ignores this zero count.
    const auto player_one=std::make_shared<ModelSceneOne>();player_one->name="ONE";
    player_one->node=f.scene->mutable_node(f.source.nodes[1]);
    f.ram->data=std::make_shared<ModelSceneList>();f.ram->data->append(player_one);
    const auto services=creation_services();
    const auto result=generate.resume(messenger,f.events,services);
    check(!result.completed && generate.player_create() && generate.player_create()->returned() && gets==1 && starts==1,
        "Connected player construction did not return before the real outer sequence boundary.");
    check(generate.captured_work()==old && generate.outer_work()==outer && player_one->work && player_one->work!=outer &&
        generate.player_create()->captured_one()==player_one && outer->script.hp->data==100 &&
        outer->script.max_hp->data==100 && player_one->work->script.max_hp->data==91 &&
        f.ram->words[0]==29 && !f.one->node->box_owner(),
        "Outer GENERATE lost its work reload, zero-HP fallback, root box clear or retained activation prefix.");
    check(generate.direct_entry() && generate.direct_entry()->captured_work()==player_one->work &&
        generate.direct_entry()->status_work()==player_one->work,"Outer direct controller entry skipped its new base/status lookup.");
    const auto work=player_one->work;
    check(!generate.resume(messenger,f.events,services).completed && player_one->work==work && gets==1 && starts==1,
        "Retry at the ordinary sequence boundary repeated player allocation or startup.");
}
void completed_direct_query_box_tail() {
    Fixture f;GameDataState data;
    data.player(0)->require<GameDataBool>("ISUSE")->data=1;
    data.player(0)->require<GameDataInt>("MHP")->data=91;
    data.player(0)->require<GameDataInt>("HP")->data=43;
    data.player(0)->require<GameDataInt>("MMP")->data=37;
    const auto source=std::make_shared<CollisionBox>(CollisionBox{{-1,-2,-3},{1,2,3}});
    f.scene->mutable_node(f.source.lods[0])->set_box_owner(source);
    const auto previous=std::make_shared<CollisionBox>(CollisionBox{{7,7,7},{8,8,8}});
    f.one->node->set_box_owner(previous);
    provider(f.events,"GAMERAM",{},0,[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
        if(delivery.message.command=="GETPLAYER")messenger.buffer().add_data_list(data.player(0));
        return MessengerCallResult{true,1,{}};
    });
    for(const auto* name:{"lvnm","hpnm","mpnm"})provider(f.events,"NUMBERCTRL",name,0,
        [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    for(const auto* name:{"hpmt","mpmt"})provider(f.events,"METERCTRL",name,0,
        [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    f.events.set_player_creation_services(creation_services());
    auto& messenger=f.events.messenger();messenger.select("MODELCTRL",f.info->name,0);
    messenger.buffer().add_vector({3,4,5});messenger.buffer().add_string("GETHP");
    auto dispatch=messenger.begin_send("GENERATE");
    const auto result=f.events.resume(dispatch);
    check(result.stop==MessengerStop::completed,"A completed direct base query did not allow the original final box tail.");
    const auto destination=f.one->node->box_owner();
    check(destination && destination!=source && destination!=previous && f.one->work->base_box==source &&
        *destination==original_transform_box(*f.one->node->matrix_owner(),*source),"Final GENERATE box lost original source/destination ownership or transform.");
    check(*source==CollisionBox{{-1,-2,-3},{1,2,3}} && *previous==CollisionBox{{7,7,7},{8,8,8}},
        "Root box clearing or final publication mutated retained previous/source boxes.");
}
}
int main() {
    try {independent_captures();registration_and_live_fields();status_retry_retains_work();captured_queue_and_recursive_fault();generate_prefix_and_outer_tail();completed_direct_query_box_tail();
        std::cout<<"Model dispatch checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Model dispatch after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
