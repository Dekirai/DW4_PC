#include "dw4/bone_select.hpp"
#include "dw4/actor_scene.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) { throw BoneSelectError(reason); }
constexpr std::size_t pool_limit=65536,nesting_limit=64;
}

struct BoneSelectTraversal::Impl {
    std::weak_ptr<BoneSelectContext::Impl> owner;
    std::uint64_t generation{},id{},parent{};
    std::shared_ptr<const ActorBoneSelector> selector;
    std::shared_ptr<BoneSelectModelView> attribute;
    BoneSelectChildren children;
    bool invoked{},executing{},done{},failed{};
    std::int32_t value{};
    std::string diagnostic;
};
struct BoneSelectContext::Impl {
    std::vector<std::shared_ptr<BoneSelectModelView>> pool,stack;
    std::shared_ptr<BoneSelectModelView> base;
    std::vector<std::shared_ptr<BoneSelectTraversal::Impl>> frames;
    std::vector<std::uint64_t> invocations;
    std::size_t cursor{};
    std::uint64_t generation{},next_id=1;
    bool failed{},releasing{};
    std::string diagnostic;
    ~Impl() { releasing=true; }
    void readable() const {
        if(releasing)fail("Bone-selector ownership cannot be reentered during release.");
    }
    void usable() const {
        readable();
        if(failed)fail("An uncertain bone-selector context requires explicit reset.");
    }
    void idle() const {
        usable();
        if(!frames.empty() || !invocations.empty())fail("The retained bone-selector invocation owns this context.");
    }
};

BoneSelectTraversal::BoneSelectTraversal(std::shared_ptr<Impl> impl):impl_(std::move(impl)) {}
BoneSelectTraversal::BoneSelectTraversal(BoneSelectTraversal&&) noexcept=default;
BoneSelectTraversal& BoneSelectTraversal::operator=(BoneSelectTraversal&&) noexcept=default;
BoneSelectTraversal::~BoneSelectTraversal()=default;
std::uint64_t BoneSelectTraversal::id() const noexcept { return impl_ ? impl_->id : 0; }
bool BoneSelectTraversal::completed() const noexcept { return impl_ && impl_->done; }
bool BoneSelectTraversal::faulted() const noexcept { return !impl_ || impl_->failed; }

BoneSelectContext::BoneSelectContext(std::optional<AnimationMatrix> initial):impl_(std::make_shared<Impl>()) {
    if(initial) {
        for(const auto value:*initial)if(!std::isfinite(value))fail("Initial prepared model-view matrix is non-finite.");
        auto attribute=std::make_shared<BoneSelectModelView>();attribute->matrix_=*initial;
        impl_->stack.push_back(attribute);impl_->base=std::move(attribute);
    }
}
BoneSelectContext::BoneSelectContext(BoneSelectContext&& other) {
    if(other.impl_) {
        other.impl_->readable();
        if(!other.impl_->invocations.empty())fail("Cannot move an executing bone-selector context.");
    }
    impl_=std::move(other.impl_);
}
BoneSelectContext& BoneSelectContext::operator=(BoneSelectContext&& other) {
    if(this==&other)return *this;
    if(impl_)impl_->idle();
    if(other.impl_) {
        other.impl_->readable();
        if(!other.impl_->invocations.empty())fail("Cannot move an executing bone-selector context.");
    }
    // Release captured callbacks under the same explicit ownership guard as reset.
    if(impl_)impl_->releasing=true;
    impl_=std::move(other.impl_);return *this;
}
BoneSelectContext::~BoneSelectContext()=default;

BoneSelectTraversal BoneSelectContext::begin(std::shared_ptr<const ActorBoneSelector> selector,
    std::optional<std::span<const AnimationMatrix>> bones) {
    if(!impl_)fail("Moved bone-selector context has no owner.");
    auto state=impl_;state->usable();
    if(!selector)fail("Common bone-selector entry requires its actual selector owner.");
    if(!state->frames.empty() && (state->invocations.empty() || state->frames.back()->id!=state->invocations.back()))
        fail("Resume the retained child through its owning bone-selector callback.");
    if(state->frames.size()>=nesting_limit)fail("Bone-selector nesting exceeds 64 invocations.");
    if(state->cursor>=pool_limit)fail("Bone-selector attribute pool exceeds 65,536 entries.");
    if(!state->next_id)fail("Bone-selector invocation identifiers exceed their host bound.");
    auto frame=std::make_shared<BoneSelectTraversal::Impl>();
    frame->owner=state;frame->generation=state->generation;frame->id=state->next_id++;
    frame->parent=state->invocations.empty() ? 0 : state->invocations.back();frame->selector=std::move(selector);
    // Preallocate host continuation bookkeeping before publishing the original prefix.
    state->stack.reserve(state->stack.size()+1);state->frames.reserve(state->frames.size()+1);
    state->invocations.reserve(nesting_limit);
    if(state->cursor==state->pool.size())state->pool.push_back(std::make_shared<BoneSelectModelView>());
    frame->attribute=state->pool[state->cursor++]; // 801DEEC8 precedes both original gates.
    try {
        const auto index=frame->selector->matrix_index();
        if(!bones || index<0) { frame->done=true;return BoneSelectTraversal(std::move(frame)); }
        if(static_cast<std::size_t>(index)>=bones->size())fail("Common bone-selector index is outside the supplied bone array.");
        // 801DEF2C: r5 is root0, r4 is the selected bone. Index zero squares root0.
        frame->attribute->matrix_=original_matrix_multiply((*bones)[0],(*bones)[static_cast<std::size_t>(index)]);
        state->stack.push_back(frame->attribute);state->frames.push_back(frame);
        return BoneSelectTraversal(std::move(frame));
    } catch(const std::exception& error) {
        state->failed=true;state->diagnostic=error.what();throw;
    } catch(...) {
        state->failed=true;state->diagnostic="Common bone-selector preparation failed after its committed pool prefix.";throw;
    }
}

BoneSelectResult BoneSelectContext::resume(BoneSelectTraversal& traversal,const BoneSelectChildren& children) {
    if(!impl_ || !traversal.impl_ || traversal.impl_->owner.lock()!=impl_)
        fail("Bone-selector traversal requires the same live context owner.");
    auto state=impl_;auto frame=traversal.impl_;state->readable();
    if(frame->generation!=state->generation) {
        frame->failed=true;frame->done=false;frame->diagnostic="Explicit reset invalidated this bone-selector traversal.";
    }
    if(frame->failed)return {BoneSelectStop::faulted,std::nullopt,frame->diagnostic};
    if(frame->done)return {BoneSelectStop::completed,frame->value,{}};
    if(state->failed)return {BoneSelectStop::faulted,std::nullopt,state->diagnostic};
    if(frame->executing)fail("A bone-selector callback cannot resume itself recursively.");
    if(state->frames.empty())fail("Bone-selector traversal lost its retained stack ownership.");
    if(state->invocations.empty()) {
        if(state->frames.front()!=frame)fail("Nested bone-selector retry must enter its retained parent callback first.");
    } else if(frame->parent!=state->invocations.back()) {
        fail("Bone-selector retry does not belong to the executing parent callback.");
    }
    struct Guard {
        Impl& state;BoneSelectTraversal::Impl& frame;
        Guard(Impl& state,BoneSelectTraversal::Impl& frame):state(state),frame(frame) {
            state.invocations.push_back(frame.id);frame.executing=true;
        }
        ~Guard() {frame.executing=false;state.invocations.pop_back();}
    } guard(*state,*frame);
    try {
        if(!frame->invoked) {
            if(!children)return {BoneSelectStop::children_required,std::nullopt,"Actual bone-selector child traversal is required."};
            frame->children=children;frame->invoked=true;
        }
        const auto result=frame->children(*this,frame->selector);
        if(state->failed) {
            frame->failed=true;frame->diagnostic=state->diagnostic;
            return {BoneSelectStop::faulted,std::nullopt,frame->diagnostic};
        }
        if(!result)return {BoneSelectStop::children_required,std::nullopt,"The retained bone-selector child traversal is incomplete."};
        if(state->frames.back()!=frame || state->stack.empty() || state->stack.back()!=frame->attribute)
            fail("A completed bone-selector callback still owns an unfinished nested traversal.");
        state->stack.pop_back();state->frames.pop_back();
        frame->value=*result==2 ? 2 : 0;frame->done=true;
        return {BoneSelectStop::completed,frame->value,{}};
    } catch(const std::exception& error) {
        frame->failed=state->failed=true;frame->diagnostic=state->diagnostic=error.what();
    } catch(...) {
        frame->failed=state->failed=true;
        frame->diagnostic=state->diagnostic="Bone-selector child traversal threw an unknown exception.";
    }
    return {BoneSelectStop::faulted,std::nullopt,frame->diagnostic};
}
std::shared_ptr<const BoneSelectModelView> BoneSelectContext::model_view() const {
    if(!impl_)fail("Moved bone-selector context has no model-view owner.");
    impl_->readable();return impl_->stack.empty() ? nullptr : impl_->stack.back();
}
std::size_t BoneSelectContext::stack_depth() const noexcept { return impl_ ? impl_->stack.size() : 0; }
std::size_t BoneSelectContext::pool_cursor() const noexcept { return impl_ ? impl_->cursor : 0; }
std::size_t BoneSelectContext::pool_size() const noexcept { return impl_ ? impl_->pool.size() : 0; }
bool BoneSelectContext::pending() const noexcept { return impl_ && !impl_->frames.empty(); }
bool BoneSelectContext::faulted() const noexcept { return !impl_ || impl_->failed; }
void BoneSelectContext::rewind_pool() {
    if(!impl_)fail("Moved bone-selector context has no pool.");
    impl_->idle();impl_->cursor=0;
}
void BoneSelectContext::reset() {
    if(!impl_)fail("Moved bone-selector context has no owner.");
    auto state=impl_;state->readable();
    if(!state->invocations.empty())fail("Cannot reset an executing bone-selector context.");
    struct Guard {bool& value;explicit Guard(bool& value):value(value){value=true;}~Guard(){value=false;}} guard(state->releasing);
    ++state->generation;state->frames.clear();state->stack.clear();state->pool.clear();
    if(state->base)state->stack.push_back(state->base);
    state->cursor=0;state->failed=false;state->diagnostic.clear();
}
} // namespace dw4
