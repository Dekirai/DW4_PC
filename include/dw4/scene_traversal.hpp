#pragma once

#include "dw4/actor_scene.hpp"
#include "dw4/bone_select.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4 {
class SceneTraversalError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

// A physical attribute in the immutable document retained by an actual node.
// Distinct source loads never become equal merely because their refs match.
class SceneAttributeOwner {
public:
    SceneAttributeOwner(std::shared_ptr<const ActorSceneNode>,std::uint32_t reference);
    SceneAttributeOwner(const SceneAttributeOwner&)=default;
    SceneAttributeOwner& operator=(const SceneAttributeOwner&)=delete;
    SceneAttributeOwner& operator=(SceneAttributeOwner&&)=delete;
    const auto& document_owner() const noexcept { return owner_; }
    const IgbDocument& document() const noexcept { return owner_->document(); }
    std::uint32_t reference() const noexcept { return reference_; }
    const std::string& type() const noexcept { return type_; }
    std::int16_t unit() const noexcept { return unit_; }
private:
    std::shared_ptr<const ActorSceneNode> owner_;
    std::uint32_t reference_{};
    std::string type_;
    std::int16_t unit_{};
};
struct SceneInheritedAttribute {
    std::shared_ptr<const SceneAttributeOwner> attribute;
    // An already effective outer override remains selected while normal local
    // attributes are pushed. This does not implement igOverrideAttrSet entry.
    bool override_selected{};
};
struct SceneGraphicsScope {
    std::vector<SceneInheritedAttribute> inherited_attributes;
    // These describe the supplied effective defaults when no represented
    // loaded attribute supersedes them. Missing facts never mean disabled.
    std::optional<bool> inherited_vertex_blend_enabled;
    // Original geometry checks the matrix-list STACK COUNT, not its default
    // owner or payload pointer. True means that inherited stack is nonempty.
    std::optional<bool> inherited_vertex_blend_list_present;
    bool bone_root_known{};
    std::optional<AnimationMatrix> bone_root; // null means the actual TOP/F8 pointer is null.
    bool previsit_enabled{}; // Explicit prepared context policy, not node visibility.
    // Actual prepopulated time-stack or already-cached clock value. Without
    // this supplied scope, every transform queries its retained clock service.
    std::optional<std::uint64_t> prepared_transform_clock;
};

struct SceneSubmissionRequest {
    std::shared_ptr<const ActorSceneNode> node;
    std::vector<std::shared_ptr<const ActorSceneNode>> path;
    std::vector<std::uint32_t> root_to_owner;
    AnimationMatrix model_view{};
    std::optional<std::uint32_t> attribute_list_reference;
    // Exact source-list order, independent of effective stack selection.
    std::vector<std::shared_ptr<const SceneAttributeOwner>> attributes;
    std::vector<std::shared_ptr<const SceneAttributeOwner>> effective_attributes;
    std::uint32_t geometry_flags{};
};
struct SceneTraversalServices {
    // False means the real service has retained but not yet consumed this
    // exact request. A geometry service queues the actual render work; its
    // completion does not invent a GPU completion or child return value.
    std::function<bool(const SceneSubmissionRequest&)> submit_geometry;
    std::function<bool(const SceneSubmissionRequest&)> flush_attributes;
    std::function<std::optional<std::int32_t>(const SceneSubmissionRequest&)> previsit;
    // Unimplemented registered node handlers remain a real resumable boundary.
    std::function<std::optional<std::int32_t>(const SceneSubmissionRequest&)> node_handler;
    std::function<std::optional<std::uint64_t>(const SceneSubmissionRequest&)> sample_transform_clock;
};
enum class SceneTraversalStop { completed, service_required, faulted };
struct SceneTraversalResult {
    SceneTraversalStop stop=SceneTraversalStop::completed;
    std::optional<std::int32_t> value;
    std::string diagnostic;
};
class SceneTraversalContext;
class SceneTraversal {
public:
    SceneTraversal(SceneTraversal&&) noexcept;
    SceneTraversal& operator=(SceneTraversal&&) noexcept;
    ~SceneTraversal();
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    friend class SceneTraversalContext;
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit SceneTraversal(std::shared_ptr<Impl>);
};

// Prepared normal common callbacks, directly beneath BoneSelectChildren.
// Ordered DAG occurrences remain separate visits; no mesh is world-baked.
// Contexts retain their active cursor, exact request and first invoked service
// through pauses/faults. They cannot be reused until completion or reset.
class SceneTraversalContext {
public:
    explicit SceneTraversalContext(SceneGraphicsScope);
    ~SceneTraversalContext();
    SceneTraversalContext(const SceneTraversalContext&)=delete;
    SceneTraversalContext& operator=(const SceneTraversalContext&)=delete;
    SceneTraversal begin(BoneSelectContext&,std::shared_ptr<const ActorBoneSelector>);
    SceneTraversalResult resume(SceneTraversal&,const SceneTraversalServices& = {});
    std::size_t attribute_depth() const noexcept;
    std::size_t node_depth() const noexcept;
    bool pending() const noexcept;
    bool faulted() const noexcept;
    std::optional<AnimationMatrix> model_view() const;
    void reset();
private:
    friend struct SceneTraversal::Impl;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace dw4
