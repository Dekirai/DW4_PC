#include "dw4/model_script_messenger.hpp"
#include "dw4/action_event_runtime.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw ModelScriptError(message); }
struct DepthScope {
    std::size_t& depth;
    explicit DepthScope(std::size_t& value):depth(value) { ++depth; }
    ~DepthScope() { --depth; }
};
}

struct ModelScriptMessenger::Impl {
    struct Call {
        ModelScriptContextRuntime* context;
        std::uint64_t execution,id;
        bool magic;
        std::array<std::uint64_t,2> arguments;
        std::array<std::uint8_t,2> flags;
        std::optional<MessengerDispatch> dispatch;
    };
    ActionEventRuntime& runtime;
    std::vector<std::unique_ptr<Call>> calls;
    std::size_t active{};
    bool fault{};
    explicit Impl(ActionEventRuntime& owner):runtime(owner) {}
};

ModelScriptMessenger::ModelScriptMessenger(ActionEventRuntime& runtime):impl_(std::make_unique<Impl>(runtime)) {}
ModelScriptMessenger::~ModelScriptMessenger()=default;
bool ModelScriptMessenger::pending() const noexcept { return !impl_->calls.empty(); }
bool ModelScriptMessenger::faulted() const noexcept { return impl_->fault; }

Lua4HostResult ModelScriptMessenger::handle(ModelScriptContextRuntime& context,const Lua4HostCall& call) {
    auto& owner=*impl_;
    if(owner.fault) fail("Model script messenger has an uncertain outcome; replay is forbidden.");
    const bool magic=call.name=="be.BLPcalcKoukaMagic";
    const bool skill=call.name=="be.BLTcalcSkillUp";
    if(!magic && !skill) return {false,{},"Native model script service is required: "+call.name};
    if(!call.execution_id || !call.call_id)
        fail("Model script messenger requires a retained Lua execution and logical call identity.");
    const auto index=owner.active;
    if(index<owner.calls.size()) {
        const auto& pending=*owner.calls[index];
        if(pending.context!=&context || pending.execution!=call.execution_id || pending.id!=call.call_id || pending.magic!=magic)
            fail("Resume the exact pending model script messenger call through its owning parent.");
    } else if(index) {
        const auto& parent=*owner.calls.back();
        if(parent.context!=&context || parent.execution==call.execution_id)
            fail("Nested model script messenger calls require the same context and a distinct owned Lua execution.");
    }
    const auto argc=magic ? 1u : 3u;
    if(call.arguments.size()!=argc || call.arguments[0].type()!=Lua4Type::HostObject || call.arguments[0].host_name()!="be")
        return {false,{},"Unverified native argument layout for "+call.name};
    std::array<std::uint64_t,2> arguments{};
    std::array<std::uint8_t,2> flags{};
    if(skill) {
        for(std::size_t i=0;i<2;++i) {
            if(call.arguments[i+1].type()!=Lua4Type::Number)
                return {false,{},"Native numeric argument diagnostic service is required for "+call.name};
            const auto number=call.arguments[i+1].as_number();
            if(!std::isfinite(number)) return {false,{},"Unverified non-finite native byte conversion for "+call.name};
            arguments[i]=std::bit_cast<std::uint64_t>(number);
            // Descriptor type9 compares binary64 with zero, independently of
            // Lua truthiness. Its prepared byte is zero-extended into the packet.
            flags[i]=number!=0 ? 1u : 0u;
        }
    }
    if(!context.has_context()) return {false,{},"Target model script context is required for "+call.name};
    // This checks that the shared context is currently owned by an executing
    // script callback. No field is changed or restored by either wrapper.
    context.mutable_context();
    if(index<owner.calls.size()) {
        if(owner.calls[index]->arguments!=arguments)
            fail("Pending model script messenger arguments changed before completion.");
    } else {
        if(owner.calls.size()>=32) fail("Model script messenger nesting exceeds 32 calls.");
        owner.calls.push_back(std::make_unique<Impl::Call>(Impl::Call{
            &context,call.execution_id,call.call_id,magic,arguments,flags,{}}));
    }
    auto& pending=*owner.calls[index];
    DepthScope active(owner.active);
    try {
        if(!pending.dispatch) {
            auto& messenger=owner.runtime.messenger();
            if(!messenger.has_category("GAMERAM"))
                return {false,{},"Original GAMERAM registration is required for "+call.name};
            messenger.select("GAMERAM",std::nullopt,-1);
            messenger.buffer().add_string(magic ? "P2PKOUKAMAGIC" : "P2ESKILLUP");
            const auto& live=context.context();
            if(magic) messenger.buffer().add_int(live.target_ram);
            messenger.buffer().add_int(live.source_ram);
            messenger.buffer().add_int(live.kind);
            if(skill) {
                messenger.buffer().add_int(pending.flags[0]);
                messenger.buffer().add_int(pending.flags[1]);
            }
            pending.dispatch.emplace(messenger.begin_send("FIGHTCALC"));
        }
        const auto result=owner.runtime.resume(*pending.dispatch);
        if(result.stop==MessengerStop::faulted) throw ModelScriptError("Original FIGHTCALC delivery failed: "+result.diagnostic);
        if(result.stop!=MessengerStop::completed)
            return {false,{},"Original FIGHTCALC delivery is pending: "+result.diagnostic};
        if(owner.calls.size()!=index+1)
            fail("Complete the retained child before finishing its parent model script messenger call.");
        Lua4HostResult returned{true,{},{}};
        if(magic) {
            // Nested manager callbacks can select another buffer or change its
            // integer list. Read the live post-send list; never the saved input.
            const auto& values=owner.runtime.messenger().buffer().integers();
            if(values.empty()) fail("Original BLPcalcKoukaMagic completed without its required signed integer result.");
            returned.returned.push_back(Lua4Value::number(values.front()));
        }
        owner.calls.pop_back();
        return returned;
    } catch(...) { owner.fault=true; throw; }
}
} // namespace dw4
