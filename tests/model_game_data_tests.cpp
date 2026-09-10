#include "dw4/model_game_data.hpp"

#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {
    ++checks;try {operation();}catch(const GameDataError&){return;}
    throw std::runtime_error("Invalid post-GETPLAYER binding was accepted.");
}
std::shared_ptr<ModelScriptWork> work(std::int32_t index=0) {
    return std::make_shared<ModelScriptWork>(ControllerObjectId{7,100+static_cast<std::uint32_t>(index)},
        "synthetic/player.igb","p_test",index,std::make_shared<AnimationMatrix>(AnimationMatrix{1,0,0,0,0,1,0,0,0,0,1,0,4,5,6,1}));
}
std::shared_ptr<GameDataInt> integer(const char* name,std::int32_t value) {
    return std::make_shared<GameDataInt>(GameDataInt{name,value});
}
void aliases_and_zero_refill() {
    static_assert(std::is_same_v<GameDataInt,ModelScriptInt>);
    static_assert(!std::is_default_constructible_v<ModelGameDataBinding>);
    GameDataState data;
    for(std::size_t i=0;i<4;++i) {
        auto target=work(static_cast<std::int32_t>(i));auto row=data.player(i);
        auto hp=row->require<GameDataInt>("HP"),maximum_hp=row->require<GameDataInt>("MHP");
        auto mp=row->require<GameDataInt>("MP"),maximum_mp=row->require<GameDataInt>("MMP");
        maximum_hp->data=static_cast<std::int32_t>(100+i);maximum_mp->data=static_cast<std::int32_t>(50+i);mp->data=-3;
        const auto before_matrix=target->matrix();target->record=-19;
        std::weak_ptr<ModelScriptInt> old_hp=target->hp,old_maximum=target->max_hp;
        const auto binding=bind_model_game_data(*target,row);
        check(binding.row()==row && binding.hp()==hp && binding.max_hp()==maximum_hp && binding.refilled(),
            "Binding did not retain the captured row, actual HP/MHP wrappers or zero branch.");
        check(target->hp==hp && target->max_hp==maximum_hp && old_hp.expired() && old_maximum.expired(),
            "Binding copied HP values or retained obsolete default work wrappers.");
        check(hp->data==maximum_hp->data && mp->data==maximum_mp->data,"Zero HP did not perform both original HP and MP copies.");
        check(target->matrix()==before_matrix && target->record==-19 && target->ram_no()==static_cast<std::int32_t>(i),
            "The post-GETPLAYER prefix changed unrelated work fields.");
        hp->data-=2;check(target->hp->data==98+static_cast<std::int32_t>(i),"Live row mutation did not reach the shared model HP object.");
        target->hp->data=75;check(row->require<GameDataInt>("HP")->data==75,"Live model HP mutation did not reach its data row.");
        const auto copy=binding;check(copy.row()==row && copy.hp()==target->hp,"Copied binding detached its retained ownership.");
    }
}
void exact_branch_and_signed_values() {
    for(const auto value:{std::int32_t{-1},std::int32_t{1},std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}) {
        auto target=work();auto hp=integer("HP",value),maximum=integer("MHP",33);
        // The skipped branch must not require unrelated MP/MMP fields.
        auto row=std::make_shared<GameDataList>(std::vector<GameDataObject>{hp,maximum});
        const auto binding=bind_model_game_data(*target,row);
        check(!binding.refilled() && target->hp==hp && hp->data==value && target->max_hp==maximum,
            "Nonzero/negative HP was refilled or required skipped MP fields.");
    }
    for(const auto maximum:{std::int32_t{0},std::int32_t{-10},std::numeric_limits<std::int32_t>::max()}) {
        auto target=work();auto hp=integer("HP",0),mhp=integer("MHP",maximum),mp=integer("MP",4),mmp=integer("MMP",maximum);
        const auto row=std::make_shared<GameDataList>(std::vector<GameDataObject>{hp,mhp,mp,mmp});
        const auto binding=bind_model_game_data(*target,row);
        check(binding.refilled() && hp->data==maximum && mp->data==maximum,"Refill clamped or invented a positive HP/MP value.");
    }
}
void captured_row_survives_reset() {
    GameDataState data;auto target=work();auto captured=data.player(0);
    captured->require<GameDataInt>("MHP")->data=120;captured->require<GameDataInt>("MMP")->data=80;
    const auto hp=captured->require<GameDataInt>("HP");
    // A later recipient or nested delivery may reset the manager after
    // GETPLAYER has already appended this row. Consume that retained object.
    data.reset_player(0);check(captured!=data.player(0),"Fixture did not replace the live manager row.");
    auto binding=bind_model_game_data(*target,captured);captured.reset();
    check(binding.hp()==hp && hp->data==120 && target->hp==hp && data.player(0)->require<GameDataInt>("HP")->data==0,
        "Binding rejected or retargeted a valid captured pre-reset row.");
    data.reset_player(0);
    check(binding.row()->require<GameDataInt>("HP")==target->hp && target->hp->data==120,
        "A later reset implicitly rebound existing model work.");
    const auto replacement=bind_model_game_data(*target,data.player(0));
    check(replacement.row()==data.player(0) && replacement.hp()!=binding.hp() && target->hp->data==0 && binding.hp()->data==120,
        "Explicit later preparation reused or rewrote a retained old binding.");
}
void malformed_atomic_boundary() {
    auto target=work();target->hp->data=17;target->max_hp->data=19;
    const auto before_hp=target->hp,before_mhp=target->max_hp;
    rejects([&]{bind_model_game_data(*target,{});});
    rejects([&]{bind_model_game_data(*target,std::make_shared<GameDataList>());});
    auto hp=integer("HP",0),mhp=integer("MHP",100),mp=integer("MP",3),mmp=integer("MMP",40);
    auto wrong=std::make_shared<GameDataBool>(GameDataBool{"HP",0});
    auto wrong_max=std::make_shared<GameDataBool>(GameDataBool{"MHP",0});
    auto wrong_mp=std::make_shared<GameDataBool>(GameDataBool{"MP",0});
    for(const auto& entries:std::vector<std::vector<GameDataObject>>{
        {mhp,mp,mmp},{hp,mp,mmp},{wrong,hp,mhp,mp,mmp},{hp,wrong_max,mhp,mp,mmp},
        {hp,mhp,mp},{hp,mhp,mmp},{hp,mhp,mmp,wrong_mp,mp}}) {
        auto row=std::make_shared<GameDataList>(entries);rejects([&]{bind_model_game_data(*target,row);});
        check(target->hp==before_hp && target->max_hp==before_mhp && target->hp->data==17 && target->max_hp->data==19 &&
            hp->data==0 && mhp->data==100 && mp->data==3 && mmp->data==40,
            "Malformed checked row partially published wrappers or refill effects.");
    }
    auto later=integer("HP",250);
    const auto row=std::make_shared<GameDataList>(std::vector<GameDataObject>{hp,later,mhp,mp,mmp});
    const auto binding=bind_model_game_data(*target,row);
    check(binding.hp()==hp && hp->data==100 && later->data==250 && mp->data==40,
        "Post-GETPLAYER binding did not obey first exact-name lookup.");
}
void live_script_merge() {
    GameDataState data;auto target=work();const auto row=data.player(0);
    row->require<GameDataInt>("MHP")->data=100;const auto binding=bind_model_game_data(*target,row);
    ModelScriptContextRuntime scripts;
    const ModelTargetHitRequest request{target->target(),target->source(),target->name(),0,"hit","Hit",80000,0,"source",1,-1};
    auto call=scripts.begin(target,request);
    check(scripts.context().hp==100 && scripts.context().maximum_hp==100,"Script context did not publish the bound live wrappers.");
    check(scripts.resume(call,[&](auto& owner,const auto&) {owner.mutable_context().hp=63;return true;})==std::optional<std::uint8_t>{std::uint8_t{1}} &&
        row->require<GameDataInt>("HP")->data==63 && target->hp==binding.hp(),"Script HP epilogue failed to update the actual captured GAMERAM wrapper.");
    auto second=scripts.begin(target,request);
    scripts.resume(second,[&](auto& owner,const auto&) {owner.mutable_context().hp=20;binding.hp()->data=70;return true;});
    check(row->require<GameDataInt>("HP")->data==70,"Script epilogue overwrote a live service change to bound HP.");
}
} // namespace
int main() {
    try {
        aliases_and_zero_refill();exact_branch_and_signed_values();captured_row_survives_reset();malformed_atomic_boundary();live_script_merge();
        std::cout<<"model_game_data: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"model_game_data after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
