#pragma once

#include "dw4/model_script_context.hpp"

namespace dw4 {
// Retained objects consumed by the post-GETPLAYER prefix of 8034D61C. These
// identify the captured row, even if a later reset replaces the manager's row.
class ModelGameDataBinding {
public:
    const std::shared_ptr<GameDataList>& row() const noexcept { return row_; }
    const std::shared_ptr<GameDataInt>& hp() const noexcept { return hp_; }
    const std::shared_ptr<GameDataInt>& max_hp() const noexcept { return max_hp_; }
    // True means the original HP==0 branch ran; zero MHP remains zero HP.
    bool refilled() const noexcept { return refilled_; }
private:
    ModelGameDataBinding()=default;
    std::shared_ptr<GameDataList> row_;
    std::shared_ptr<GameDataInt> hp_,max_hp_;
    bool refilled_{};
    friend ModelGameDataBinding bind_model_game_data(ModelScriptWork&,std::shared_ptr<GameDataList>);
    friend ModelGameDataBinding bind_model_game_data(ModelScriptFields&,std::shared_ptr<GameDataList>);
};

// The caller supplies the actual captured GETPLAYER object0 from its retained
// completed delivery. This helper neither performs GETPLAYER nor constructs a
// player. Publish work HP/MHP aliases, then on exact zero HP copy MHP->HP and
// MMP->MP. Negative HP does not trigger either copy. The required named types
// are prevalidated before mutation; malformed host rows fail atomically.
ModelGameDataBinding bind_model_game_data(ModelScriptWork& work,std::shared_ptr<GameDataList> returned_row);
ModelGameDataBinding bind_model_game_data(ModelScriptFields& fields,std::shared_ptr<GameDataList> returned_row);
} // namespace dw4
