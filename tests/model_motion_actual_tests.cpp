#include "dw4/model_motion_actual.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/model_timer.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
constexpr AnimationMatrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
constexpr float nan=std::numeric_limits<float>::quiet_NaN();
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
void near(float value,float expected,const char* reason) {check(std::abs(value-expected)<0.000003f,reason);}
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
IgbDocument document(pf::RuntimeFixture& authored,bool transform) {
    const auto null=authored.memory(pf::words({pf::null_ref}));auto doc=authored.document();
    const auto offset=doc.memories.at(doc.references.at(null).value_index).offset;
    for(const auto ref:authored.nodes)doc.objects.at(doc.references.at(ref).value_index).fields.push_back({3,2,offset,4});
    if(transform)doc.metadata.object_types.at(pf::model_node).name="igTransform";
    return doc;
}
struct Fixture {
    pf::RuntimeFixture authored;
    ActorSceneResource scene;
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSceneOne> one=std::make_shared<ModelSceneOne>();
    std::shared_ptr<ModelSceneTimer> record=std::make_shared<ModelSceneTimer>();
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    unsigned clocks{};std::uint64_t world=42;std::function<void()> hook;
    std::shared_ptr<ModelRamTimerAccess> timer=std::make_shared<ModelRamTimerAccess>(ram,[this](const ModelSceneRam& actual) {
        check(&actual==ram.get(),"Motion clock consumed a different RAM.");++clocks;if(hook)hook();return world;
    });
    explicit Fixture(bool transform=false):scene(document(authored,transform),"synthetic/actual-motion.igb",1) {
        one->name="ONE";one->work=work;one->node=scene.mutable_node(authored.nodes[0]);*one->node->matrix_owner()=identity;
        record->name="KEIKATIMER";record->timer.saved_world_time=42;record->timer.cached_frames=1;
        ram->data=std::make_shared<ModelSceneList>();ram->data->append(one);ram->data->append(record);
    }
    void channels() {
        for(unsigned i=0;i<4;++i)work->control.movement->mutable_entries().push_back(std::make_shared<MotionState>());
    }
    void active(std::size_t index,float speed=1) {
        auto& channel=*work->control.movement->entry_owner(index);channel.active=true;channel.mode=MotionMode::impulse_positive;
        channel.speed=speed;channel.acceleration=0;channel.end_time=10;
    }
    std::shared_ptr<ModelSceneOne> replacement() {
        auto next=std::make_shared<ModelSceneOne>();next->name="ONE";next->work=make_model_player_work();
        next->node=scene.mutable_node(authored.nodes[1]);*next->node->matrix_owner()=identity;return next;
    }
};
void entry_and_unused_field_gates() {
    auto work=make_model_player_work();work->motion.translated=255;work->motion.turn.desired={1,2,3};
    ModelMotionInvocation empty({}, {},work);
    check(empty.resume().completed && !work->motion.translated && !empty.result().turned,
        "Empty movement read RAM/node/timer or turned pending angles before its count-zero return.");
    work->motion.translated=255;work->control.movement.reset();ModelMotionInvocation null_list({}, {},work);
    rejects([&]{null_list.resume();},"Null movement list was fabricated.");
    check(!work->motion.translated && null_list.faulted(),"Null-list failure lost the translated-clear prefix.");
    Fixture inactive;inactive.channels();inactive.one->node.reset();
    for(const auto& entry:inactive.work->control.movement->mutable_entries()) {entry->mode=static_cast<MotionMode>(-19);entry->value=entry->stop_value=entry->speed=nan;}
    inactive.work->motion.turn.rate.fill(nan);ModelMotionInvocation skipped(inactive.ram,{},inactive.work,inactive.timer);
    check(skipped.resume().completed && inactive.clocks==1 && !skipped.result().turned && !inactive.work->motion.translated,
        "Inactive channels/equal turn angles consumed unused payloads or a missing node.");
    Fixture wrong;wrong.work->control.movement->mutable_entries().push_back(std::make_shared<MotionState>());wrong.world=1ull<<24;
    auto expected=wrong.record->timer;motion_timer_frames(expected,wrong.world);wrong.work->motion.translated=1;
    ModelMotionInvocation short_list(wrong.ram,{},wrong.work,wrong.timer);
    rejects([&]{short_list.resume();},"Missing physical rotation slot was accepted.");
    check(wrong.record->timer==expected && wrong.clocks==2 && !wrong.work->motion.translated,
        "Missing rotation slot failed before its real timer publication.");
}
void captured_list_and_live_scalar_reads() {
    Fixture f;f.channels();f.active(3,90);const auto original=f.work->control.movement;
    auto fresh=std::make_shared<MotionChannelList>();
    for(unsigned i=0;i<4;++i)fresh->mutable_entries().push_back(std::make_shared<MotionState>());
    fresh->entry_owner(0)->active=true;fresh->entry_owner(0)->speed=12;
    fresh->entry_owner(3)->mode=static_cast<MotionMode>(-1);fresh->entry_owner(3)->value=nan;
    f.one->node.reset();f.hook=[&]{f.work->control.movement=fresh;};
    ModelMotionInvocation wait(f.ram,{},f.work,f.timer);
    check(wait.resume().completed && f.clocks==1 && original->entry_owner(3)->speed==90 &&
        original->entry_owner(3)->end_time==10 && fresh->entry_owner(0)->speed==12 && !wait.result().channels.translation_updated,
        "Captured activity gates or fresh work+C8 scalar reads were replaced by one list snapshot.");
    check(wait.result().channels.rotation==0 && f.work->motion.turn.desired[1]==0,
        "Rotation advanced the captured list instead of the fresh inactive channel.");

    Fixture translated;translated.channels();translated.active(0);const auto old=translated.work->control.movement;
    auto replacement=std::make_shared<MotionChannelList>();
    for(unsigned i=0;i<4;++i)replacement->mutable_entries().push_back(std::make_shared<MotionState>());
    translated.hook=[&]{translated.work->control.movement=replacement;};
    ModelMotionInvocation zero(translated.ram,{},translated.work,translated.timer);
    check(zero.resume().completed && zero.result().channels.translation_updated && translated.work->motion.translated==1 &&
        old->entry_owner(0)->end_time==10 && *translated.one->node->matrix_owner()==identity,
        "Captured active XYZ did not perform translation when freshly read scalar channels were inactive.");
}
void fresh_turn_and_translation_node() {
    Fixture f;f.channels();f.active(0);f.work->motion.turn.desired[1]=1;f.work->motion.turn.rate[1]=0.25f;
    auto next=f.replacement();(*next->node->matrix_owner())[12]=10;(*next->node->matrix_owner())[0]=2;
    const auto original=*f.one->node->matrix_owner();const auto initial_next=*next->node->matrix_owner();
    const auto later=std::make_shared<ModelSceneTimer>();later->name="KEIKATIMER";later->timer.saved_world_time=42;later->timer.cached_frames=2;
    f.hook=[&] {if(f.clocks==2) {auto list=std::make_shared<ModelSceneList>();list->append(next);list->append(later);f.ram->data=list;}};
    ModelMotionInvocation invocation(f.ram,{},f.work,f.timer);
    check(invocation.resume().completed && f.clocks==2 && invocation.result().turned && f.work->motion.turn.applied[1]==0.25f &&
        next->work->motion.turn.applied==std::array<float,3>{},"Turning failed to retain the outer work through fresh ONE replacement.");
    check(*f.one->node->matrix_owner()==original,"Turning or translation wrote the entry's obsolete ONE node.");
    auto expected=initial_next;original_model_turn_basis(expected,{0,0.25f,0});translate_model_local(expected,{1,0,0});
    check(*next->node->matrix_owner()==expected && f.work->motion.translated==1,
        "Translation did not copy the freshly turned current node matrix or used a replacement-work scalar channel.");
    check(f.record->timer.cached_frames==1 && later->timer.cached_frames==2,"Clock-driven RAM list replacement merged timer owners.");
    const auto after=*next->node->matrix_owner();check(invocation.resume().completed && f.clocks==2 && *next->node->matrix_owner()==after,
        "Completed actual movement replayed its clock or displacement.");
}
void per_axis_queries_and_matrix_free_gates() {
    Fixture f;f.channels();f.work->motion.turn.desired={1,1,1};f.work->motion.turn.rate={0.1f,0.1f,0.1f};
    f.hook=[&]{f.record->timer.cached_frames=static_cast<float>(f.clocks);};
    ModelMotionInvocation invocation(f.ram,{},f.work,f.timer);
    check(invocation.resume().completed && f.clocks==4,"Actual movement reused one frame result across turn axes.");
    near(f.work->motion.turn.applied[0],0.2f,"X did not read its own actual timer.");
    near(f.work->motion.turn.applied[1],0.3f,"Y did not read its own actual timer.");
    near(f.work->motion.turn.applied[2],0.4f,"Z did not read its own actual timer.");
    Fixture rates;rates.channels();rates.one->node.reset();rates.work->motion.turn.desired={1,2,3};
    ModelMotionInvocation zero(rates.ram,{},rates.work,rates.timer);
    check(zero.resume().completed && rates.clocks==1,"Zero-rate turn read an absent node or additional timer.");
    Fixture direct(true);direct.channels();direct.work->motion.turn.desired[1]=1;direct.work->motion.turn.rate[1]=1;
    ModelMotionInvocation supported(direct.ram,{},direct.work,direct.timer);
    check(supported.resume().completed && supported.result().turned,"C85C incorrectly required the Node2 virtual placement setter.");
}
void scalar_aliases_and_committed_prefixes() {
    Fixture alias;alias.channels();alias.active(0);const auto same=alias.work->control.movement->entry_owner(0);same->end_time=3;
    alias.work->control.movement->mutable_entries()[1]=same;alias.work->control.movement->mutable_entries()[2]=same;
    same->value=same->stop_value=same->stop_frames=nan;
    ModelMotionInvocation shared(alias.ram,{},alias.work,alias.timer);
    check(shared.resume().completed && same->end_time==0 && shared.result().channels.translation==std::array<float,3>{1,1,1},
        "Actual XYZ rejected aliases or eagerly validated unused scalar parameters.");
    Fixture missing;missing.channels();missing.active(0);missing.work->control.movement->mutable_entries()[1].reset();
    const auto initial=*missing.one->node->matrix_owner();ModelMotionInvocation failed(missing.ram,{},missing.work,missing.timer);
    rejects([&]{failed.resume();},"Missing reached Y scalar was accepted.");
    check(missing.work->control.movement->entry_owner(0)->end_time==9 && !missing.work->motion.translated &&
        *missing.one->node->matrix_owner()==initial,"A later scalar fault rolled back X or published the temporary matrix.");
    Fixture copy;copy.channels();copy.active(0);(*copy.one->node->matrix_owner())[0]=nan;
    ModelMotionInvocation before(copy.ram,{},copy.work,copy.timer);
    rejects([&]{before.resume();},"Nonfinite source matrix copy was accepted.");
    check(copy.work->control.movement->entry_owner(0)->end_time==10 && copy.clocks==1,
        "Scalar integration preceded the original source matrix copy.");
    Fixture stop;stop.channels();stop.active(0);auto scalar=stop.work->control.movement->entry_owner(0);
    scalar->end_time=0.5f;scalar->mode=MotionMode::accelerate_positive;scalar->stop_value=nan;
    ModelMotionInvocation stopping(stop.ram,{},stop.work,stop.timer);
    rejects([&]{stopping.resume();},"Unsupported used stop parameter was accepted.");
    check(scalar->end_time==0 && scalar->active && scalar->mode==MotionMode::accelerate_positive,
        "A checked scalar stop failure discarded its original expiry-clear prefix.");
    Fixture setter(true);setter.channels();setter.active(0);ModelMotionInvocation unsupported(setter.ram,{},setter.work,setter.timer);
    rejects([&]{unsupported.resume();},"An unknown concrete translation setter was fabricated.");
    check(setter.work->control.movement->entry_owner(0)->end_time==9 && !setter.work->motion.translated &&
        *setter.one->node->matrix_owner()==identity,"Unsupported setter failure lost scalar prefix or directly published into the node.");
}
void boxes_clock_defaults_and_faults() {
    Fixture f;f.channels();f.active(0,2);f.work->base_box=std::make_shared<CollisionBox>(CollisionBox{{-1,-2,-3},{1,2,3}});
    ModelMotionInvocation invocation(f.ram,{},f.work,f.timer);
    check(invocation.resume().completed && f.one->node->box_owner() && f.work->motion.translated==1,
        "Movement omitted its nullable saved-box transform tail.");
    check(*f.one->node->box_owner()==CollisionBox{{1,-2,-3},{3,2,3}} &&
        *f.work->base_box==CollisionBox{{-1,-2,-3},{1,2,3}},"Movement transformed the saved source or used the pre-translation node matrix.");
    Fixture shared;shared.channels();shared.active(0);shared.work->base_box=std::make_shared<CollisionBox>(CollisionBox{{0,0,0},{1,1,1}});
    shared.one->node->set_box_owner(shared.work->base_box);ModelMotionInvocation same(shared.ram,{},shared.work,shared.timer);
    check(same.resume().completed && shared.work->base_box->minimum[0]==1 && shared.work->base_box->maximum[0]==2,
        "Aliased saved/destination box owners were silently detached.");
    Fixture default_clock;default_clock.channels();default_clock.active(0);
    auto manager=std::make_shared<ModelSceneManager>();manager->context=std::make_shared<ModelSceneContext>(ModelSceneContext{42});
    default_clock.ram->manager=manager;ModelMotionInvocation actual(default_clock.ram,{},default_clock.work);
    check(actual.resume().completed && (*default_clock.one->node->matrix_owner())[12]==1 && default_clock.clocks==0,
        "Default actual motion substituted a prepared/OS clock or required a custom provider.");
    Fixture recursive;recursive.channels();ModelMotionInvocation loop(recursive.ram,{},recursive.work,recursive.timer);
    recursive.hook=[&]{rejects([&]{loop.resume();},"Recursive movement was accepted.");};
    rejects([&]{loop.resume();},"Caught recursive failure did not poison its outer invocation.");
    check(loop.faulted() && recursive.clocks==1 && !recursive.work->motion.translated,"Recursive movement published after a caught inner failure.");
    rejects([&]{loop.resume();},"Faulted movement was replayed.");
}
}
int main() {
    try {entry_and_unused_field_gates();captured_list_and_live_scalar_reads();fresh_turn_and_translation_node();
        per_axis_queries_and_matrix_free_gates();scalar_aliases_and_committed_prefixes();boxes_clock_defaults_and_faults();
        std::cout<<"Actual motion checks: "<<checks<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<"Actual motion after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
