#pragma once

#include "dw4/animation_math.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace dw4 {
class ActorBoneSelector;
class BoneSelectContext;
class BoneSelectError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

// A stable reusable original model-view attribute owner. Observations remain
// live when the explicit pool rewind later reuses this same attribute.
class BoneSelectModelView {
public:
    const AnimationMatrix& matrix() const noexcept { return matrix_; }
private:
    friend class BoneSelectContext;
    AnimationMatrix matrix_{};
};

enum class BoneSelectStop { completed, children_required, faulted };
struct BoneSelectResult {
    BoneSelectStop stop=BoneSelectStop::completed;
    std::optional<std::int32_t> value; // Present only after original return.
    std::string diagnostic;
};
// The callback owns the actual selector child-list traversal and its retained
// cursor. nullopt pauses; its first invoked callable is retained on retries.
// Nested selectors must begin/resume through this same owning callback/context.
using BoneSelectChildren=std::function<std::optional<std::int32_t>(
    BoneSelectContext&,const std::shared_ptr<const ActorBoneSelector>&)>;

class BoneSelectTraversal {
public:
    BoneSelectTraversal(BoneSelectTraversal&&) noexcept;
    BoneSelectTraversal& operator=(BoneSelectTraversal&&) noexcept;
    ~BoneSelectTraversal();
    std::uint64_t id() const noexcept;
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    friend class BoneSelectContext;
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit BoneSelectTraversal(std::shared_ptr<Impl>);
};

// Prepared common visitor 801DEE78 only. This is not the compiled selector
// visitor, a scene scheduler, or a successful geometry/render implementation.
class BoneSelectContext {
public:
    explicit BoneSelectContext(std::optional<AnimationMatrix> initial_model_view=std::nullopt);
    BoneSelectContext(const BoneSelectContext&)=delete;
    BoneSelectContext& operator=(const BoneSelectContext&)=delete;
    BoneSelectContext(BoneSelectContext&&);
    BoneSelectContext& operator=(BoneSelectContext&&);
    ~BoneSelectContext();

    // Supply the already-published TOP/F8 array selected by the actor's
    // cache_used flag. nullopt is a missing pointer; an engaged empty span is
    // malformed for a nonnegative index. No borrowed matrix storage survives
    // begin. It consumes a pool slot BEFORE null/negative gates or checked math.
    BoneSelectTraversal begin(std::shared_ptr<const ActorBoneSelector>,
        std::optional<std::span<const AnimationMatrix>> bone_array);
    BoneSelectResult resume(BoneSelectTraversal&,const BoneSelectChildren& = {});
    std::shared_ptr<const BoneSelectModelView> model_view() const;
    std::size_t stack_depth() const noexcept;
    std::size_t pool_cursor() const noexcept;
    std::size_t pool_size() const noexcept;
    bool pending() const noexcept;
    bool faulted() const noexcept;

    // Explicit prepared frame boundary, only after all traversals completed.
    // Keeps pooled attribute identities; subsequent begin overwrites reused ones.
    void rewind_pool();
    // Invalidate paused/faulted/abandoned work without inventing an original
    // callback return or pop. Restore only this context's supplied base state.
    // Forbidden while any child callback or owner-release operation executes.
    void reset();
private:
    friend struct BoneSelectTraversal::Impl;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
// Checked host bounds: 65,536 pooled attributes and 64 nested selector calls.
// Nonnegative bone indices and finite matrix arithmetic are checked. Original
// common index zero computes bone[0]*bone[0]; negative indices skip children.
} // namespace dw4
