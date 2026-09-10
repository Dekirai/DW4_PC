#include "dw4/model_script_context.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace dw4 {
namespace {
constexpr std::size_t max_text=4096,max_depth=32;
[[noreturn]] void fail(const char* message) { throw ModelScriptError(message); }
void text(const std::string& value,bool empty=true) {
    if((!empty && value.empty()) || value.size()>max_text || value.find('\0')!=std::string::npos)
        fail("Model script text exceeds the explicit bounded string profile.");
}
void validate_matrix(const std::shared_ptr<AnimationMatrix>& value) {
    if(!value) fail("Model script requires an owned live matrix.");
    for(const auto component:*value) if(!std::isfinite(component)) fail("Prepared model script matrix must be finite.");
}
void wrapper(const std::shared_ptr<ModelScriptInt>& value) {
    if(!value) fail("Model script requires its real HP and maximum-HP objects.");
    if(value->name) text(*value->name);
}
struct Identity { bool faulted{}; };
}

ModelScriptWork::ModelScriptWork(ControllerObjectId target,std::string source,std::string name,
    std::int32_t ram,std::shared_ptr<AnimationMatrix> live_matrix)
    :max_hp(std::make_shared<ModelScriptInt>()),hp(std::make_shared<ModelScriptInt>()),
     target_(target),source_(std::move(source)),name_(std::move(name)),ram_no_(ram),matrix_(std::move(live_matrix)) {
    if(target.is_native() || target.reference_index==std::numeric_limits<std::uint32_t>::max())
        fail("Model script target requires its actual serialized MODELCTRL RAM.");
    text(source_,false);text(name_);validate_matrix(matrix_);
}

struct ModelScriptInvocation::State {
    std::shared_ptr<Identity> owner;
    std::shared_ptr<ModelScriptWork> work;
    ModelScriptCall call;
    std::int32_t entry_hp{};
    std::optional<std::uint8_t> result;
};
ModelScriptInvocation::ModelScriptInvocation(std::shared_ptr<State> state):state_(std::move(state)) {}
ModelScriptInvocation::ModelScriptInvocation(ModelScriptInvocation&& other) noexcept:state_(std::move(other.state_)) {}
ModelScriptInvocation& ModelScriptInvocation::operator=(ModelScriptInvocation&& other) noexcept {
    if(this!=&other) {
        if(state_ && !state_->result) state_->owner->faulted=true;
        state_=std::move(other.state_);
    }
    return *this;
}
ModelScriptInvocation::~ModelScriptInvocation() { if(state_ && !state_->result) state_->owner->faulted=true; }
bool ModelScriptInvocation::completed() const noexcept { return state_ && state_->result.has_value(); }
const ModelScriptCall& ModelScriptInvocation::call() const {
    if(!state_) fail("Moved model script invocation has no call.");
    return state_->call;
}

struct ModelScriptContextRuntime::Impl {
    std::shared_ptr<Identity> identity=std::make_shared<Identity>();
    std::vector<std::shared_ptr<ModelScriptInvocation::State>> stack,active;
    std::optional<ModelScriptContext> context;
};
ModelScriptContextRuntime::ModelScriptContextRuntime():impl_(std::make_unique<Impl>()) {}
ModelScriptContextRuntime::~ModelScriptContextRuntime()=default;
bool ModelScriptContextRuntime::has_context() const noexcept { return impl_->context.has_value(); }
bool ModelScriptContextRuntime::pending() const noexcept { return !impl_->stack.empty(); }
bool ModelScriptContextRuntime::faulted() const noexcept { return impl_->identity->faulted; }
std::size_t ModelScriptContextRuntime::depth() const noexcept { return impl_->stack.size(); }
const ModelScriptContext& ModelScriptContextRuntime::context() const {
    if(!impl_->context) fail("Model script shared context has not been published.");
    return *impl_->context;
}
ModelScriptContext& ModelScriptContextRuntime::mutable_context() {
    if(impl_->identity->faulted) fail("Model script owner has an uncertain outcome; replay is forbidden.");
    if(impl_->active.empty() || !impl_->context) fail("Only an executing model script callback may mutate the shared context.");
    return *impl_->context;
}

ModelScriptInvocation ModelScriptContextRuntime::begin(std::shared_ptr<ModelScriptWork> work,const ModelTargetHitRequest& request) {
    auto& owner=*impl_;
    if(owner.identity->faulted) fail("Model script owner has an uncertain outcome; replay is forbidden.");
    if(!owner.stack.empty() && (owner.active.empty() || owner.active.back()!=owner.stack.back()))
        fail("Resume the pending model script before beginning another invocation.");
    if(owner.stack.size()>=max_depth) fail("Model script nesting exceeds 32 invocations.");
    if(!work) fail("Model script requires a retained prepared work owner.");
    if(request.target!=work->target() || request.target_source!=work->source() ||
       request.target_name!=work->name() || request.target_ram!=work->ram_no())
        fail("Model script arguments do not belong to their source-qualified target work.");
    text(request.target_source,false);text(request.target_name);text(request.script_prefix);
    text(request.event);text(request.source_name);validate_matrix(work->matrix());wrapper(work->max_hp);wrapper(work->hp);
    auto call=ModelScriptCall{request,request.script_prefix+"_"+request.target_name+".lua"};
    text(call.filename);
    // Complete all checked allocation before publishing the original fields.
    auto state=std::make_shared<ModelScriptInvocation::State>(ModelScriptInvocation::State{
        owner.identity,std::move(work),std::move(call),0,{}});
    state->entry_hp=state->work->hp->data;
    ModelScriptContext prepared;
    prepared.target_source=request.target_source;
    prepared.target_name=request.target_name;prepared.source_name=request.source_name;prepared.event=request.event;
    owner.stack.reserve(owner.stack.size()+1);owner.active.reserve(owner.stack.size()+1);
    if(!owner.context) owner.context.emplace();
    auto& live=*owner.context;
    live.target_source=std::move(prepared.target_source); // Native source qualification.
    live.target=request.target;                         // B7F4: +00.
    live.target_ram=state->work->ram_no();               // B80C: +04.
    live.matrix=state->work->matrix();                   // B81C: +08, live node matrix.
    live.maximum_hp=state->work->max_hp->data;           // B828: +0C.
    live.hp=state->entry_hp;                            // B834: +10.
    live.target_record=state->work->record;             // B83C: +14, scalar.
    live.target_name=std::move(prepared.target_name);    // B840: +18.
    live.kind=request.kind;                            // B844: +1C.
    live.body_ordinal=request.body_ordinal;             // B848: +20.
    live.source_name=std::move(prepared.source_name);    // B84C: +24.
    live.source_ram=request.source_ram;                // B850: +28.
    live.source_record=request.source_record;          // B854: +2C.
    live.direction=1;                                  // B858: +30.
    live.event=std::move(prepared.event);                // B85C: +34.
    owner.stack.push_back(state);
    return ModelScriptInvocation(std::move(state));
}

std::optional<std::uint8_t> ModelScriptContextRuntime::resume(ModelScriptInvocation& invocation,const ModelScriptDispatch& dispatch) {
    auto& owner=*impl_;
    const auto state=invocation.state_;
    if(!state || state->owner!=owner.identity) fail("Model script invocation belongs to a different owner.");
    if(state->result) return state->result;
    if(owner.identity->faulted) fail("Model script owner has an uncertain outcome; replay is forbidden.");
    const auto found=std::find(owner.stack.begin(),owner.stack.end(),state);
    if(found==owner.stack.end()) fail("Model script invocation is not pending.");
    const auto index=static_cast<std::size_t>(found-owner.stack.begin());
    // Only the outer invocation resumes at top level. Its callback can re-enter
    // exactly the retained child, which may in turn resume its own child.
    if(owner.active.size()!=index || !std::equal(owner.active.begin(),owner.active.end(),owner.stack.begin()))
        fail("Resume nested model script invocations through their owning parent callback.");
    if(!dispatch) return std::nullopt;
    owner.active.push_back(state);
    struct ActiveScope {
        std::vector<std::shared_ptr<ModelScriptInvocation::State>>& active;
        ~ActiveScope(){active.pop_back();}
    } active_scope{owner.active};
    try {
        const auto complete=dispatch(*this,state->call);
        if(owner.identity->faulted) fail("A model script invocation was abandoned with an uncertain prefix.");
        if(!complete) return std::nullopt;
        if(owner.stack.back()!=state) fail("Complete the retained child before finishing its parent model script.");
        // Reload the current HP reference. A replacement object with the same
        // entry value receives the shared context value, just as B878..B88C.
        if(!state->work->hp) fail("Completed model script removed its required live HP object.");
        if(state->work->hp->data==state->entry_hp) state->work->hp->data=owner.context->hp;
        state->result=owner.context->direction;
        owner.stack.pop_back();
        return state->result;
    } catch(...) {owner.identity->faulted=true;throw;}
}
} // namespace dw4
