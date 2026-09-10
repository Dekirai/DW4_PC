#pragma once

#include "dw4/actions.hpp"
#include "dw4/actor_skin.hpp"
#include "dw4/model_sequence.hpp"

namespace dw4 {

class PlayerRuntimeBindingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PlayerRuntimeRequest {
    std::uint32_t model_reference{}, action_reference{}, instance_index{}, branch_index{};
};

struct PlayerRuntimeActor {
    std::uint32_t actor_reference{}, appearance_reference{}, skin_reference{};
    std::uint32_t selector_reference{}, geometry_reference{};
    bool operator==(const PlayerRuntimeActor&) const = default;
};

// Immutable prepared graph identity, not a generated or ready player. The
// caller identifies the loaded source occurrence explicitly; a logical path
// alone does not distinguish repeated loads. No original callback is executed.
class PlayerRuntimeBinding {
public:
    const std::string& source() const noexcept;
    std::size_t source_occurrence() const noexcept;
    const std::string& model_name() const noexcept;
    std::uint32_t model_reference() const noexcept;
    std::uint32_t model_ram_reference() const noexcept;
    std::uint32_t one_reference() const noexcept;
    std::uint32_t node_reference() const noexcept;
    std::uint32_t sequence_reference() const noexcept;
    std::uint32_t action_reference() const noexcept;
    std::uint32_t action_ram_reference() const noexcept;
    std::uint32_t combiner_reference() const noexcept;
    std::uint32_t instance_index() const noexcept;
    std::uint32_t branch_index() const noexcept;
    std::uint32_t lod_reference() const noexcept;
    // ACTION RAM order, independently matched to the model's explicit LOD.
    const std::vector<PlayerRuntimeActor>& actors() const noexcept;
    std::size_t selected_actor_index() const noexcept;
    const ModelSequenceProgram& sequence_program() const noexcept;
    const ActionProgram& action_program() const noexcept;
    const ActorSkinBinding& skin_binding() const noexcept;
private:
    struct Data;
    explicit PlayerRuntimeBinding(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend PlayerRuntimeBinding decode_player_runtime_binding(const IgbDocument&,
        PlayerRuntimeRequest,std::string,std::size_t);
};

// Both exact info exports must describe the same named instance and actual
// node. All RAMs must have distinct model/node/actor ownership. Runtime cache
// decoding and activation remain separate. Only the selected skin's vertex
// data is retained; other actor entries are metadata.
PlayerRuntimeBinding decode_player_runtime_binding(const IgbDocument& document,
    PlayerRuntimeRequest request,std::string source,std::size_t source_occurrence);

} // namespace dw4
