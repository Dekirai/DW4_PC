#pragma once

#include "dw4/collision_math.hpp"
#include "dw4/igb_document.hpp"

#include <memory>
#include <utility>

namespace dw4 {
class ModelHitBodyError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Immutable named HIT graph, qualified by its explicit loaded source occurrence.
// Ordered references and duplicate local box occurrences remain distinct rows.
class ModelHitBodyDefinition {
public:
    const std::string& source() const noexcept;
    std::size_t source_occurrence() const noexcept;
    std::uint32_t model_reference() const noexcept;
    const std::string& model_name() const noexcept;
    std::uint32_t data_list_reference() const noexcept;
    std::uint32_t ram_list_reference() const noexcept;
    const std::vector<std::uint32_t>& ram_references() const noexcept;
    // Canonical physical object refs parallel to the serialized RAM occurrences.
    // A caller must share/reject aliased live owners rather than duplicate them.
    const std::vector<std::uint32_t>& ram_owners() const noexcept;
    std::uint32_t hit_reference() const noexcept;
    // Authored raw bytes; 803083CC tests !=0 when registering each category.
    std::uint8_t hit_player_bullet() const noexcept;
    std::uint8_t hit_enemy_bullet() const noexcept;
    std::uint8_t hit_player_talk() const noexcept;
    std::uint32_t box_list_reference() const noexcept;
    const std::vector<std::uint32_t>& box_references() const noexcept;
    const std::vector<CollisionBox>& local_boxes() const noexcept;
private:
    struct Data;
    explicit ModelHitBodyDefinition(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend ModelHitBodyDefinition decode_model_hit_body(const IgbDocument&,std::uint32_t,std::string,std::size_t);
};

// Exact exported MODELCTRL -> dataList -> FIRST named HIT -> AABoxList.
// Requires an authored graph; an absent/null HIT is not an empty body list.
ModelHitBodyDefinition decode_model_hit_body(const IgbDocument& document,
    std::uint32_t model_info_reference,std::string source,std::size_t source_occurrence);

// The ordinary 80307EAC body cache for one explicitly selected target RAM.
// Value copies share only immutable definitions; their mutable lists are owned.
class ModelHitBodyCache {
public:
    ModelHitBodyCache(ModelHitBodyDefinition definition,std::uint32_t instance_index);
    const ModelHitBodyDefinition& definition() const noexcept { return definition_; }
    std::uint32_t instance_index() const noexcept { return instance_index_; }
    std::uint32_t ram_reference() const noexcept;
    std::uint32_t ram_owner_reference() const noexcept;
    bool initialized() const noexcept { return initialized_; }
    std::uint64_t calc_time() const noexcept { return calc_time_; }
    const std::vector<CollisionBox>& boxes() const noexcept { return boxes_; }
    const std::vector<std::array<float,3>>& minimum_distances() const noexcept { return minimum_distances_; }
    const std::vector<std::array<float,3>>& maximum_distances() const noexcept { return maximum_distances_; }
    // Lazily creates parallel lists. Time zero initially returns exact factory
    // FLT_MAX/-FLT_MAX bounds. Only a changed system timestamp refreshes boxes.
    // Finite validation/computation precedes publication, including on cache hits.
    const std::vector<CollisionBox>& update(const AnimationMatrix& matrix,std::uint64_t system_time);
private:
    ModelHitBodyDefinition definition_;
    std::uint32_t instance_index_{};
    bool initialized_{};
    std::uint64_t calc_time_{};
    std::vector<CollisionBox> boxes_;
    std::vector<std::array<float,3>> minimum_distances_,maximum_distances_;
};
} // namespace dw4
