#include "dw4/model_game_data.hpp"

#include <utility>

namespace dw4 {
ModelGameDataBinding bind_model_game_data(ModelScriptWork& work,std::shared_ptr<GameDataList> returned_row) {
    return bind_model_game_data(*work.fields_owner(),std::move(returned_row));
}
ModelGameDataBinding bind_model_game_data(ModelScriptFields& fields,std::shared_ptr<GameDataList> returned_row) {
    if(!returned_row)throw GameDataError("Player data binding requires the captured GETPLAYER row object.");
    const auto hp=returned_row->require<GameDataInt>("HP");
    const auto maximum_hp=returned_row->require<GameDataInt>("MHP");
    const bool refill=hp->data==0;
    std::shared_ptr<GameDataInt> maximum_mp,mp;
    if(refill) {
        maximum_mp=returned_row->require<GameDataInt>("MMP");
        mp=returned_row->require<GameDataInt>("MP");
    }

    ModelGameDataBinding result;
    result.row_=std::move(returned_row);result.hp_=hp;result.max_hp_=maximum_hp;result.refilled_=refill;
    // 8034D9D8 / DA24: reference assignment, not copied integer payloads.
    fields.hp=hp;fields.max_hp=maximum_hp;
    if(refill) {
        // DA44 reads MHP before DA54 writes HP; DA64 reads MMP only after
        // that HP write, and DA74 then writes MP. No clamp or fallback exists.
        const auto hp_value=maximum_hp->data;hp->data=hp_value;
        const auto mp_value=maximum_mp->data;mp->data=mp_value;
    }
    return result;
}
} // namespace dw4
