#include "dw4/model_script_runtime.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const std::string& message) { throw ModelScriptError(message); }
std::uint64_t key(ControllerObjectId id) {
    return (static_cast<std::uint64_t>(id.file_index)<<32)|id.reference_index;
}
bool same_request(const ModelTargetHitRequest& a,const ModelTargetHitRequest& b) {
    return a.target==b.target && a.target_source==b.target_source &&
        a.target_name==b.target_name && a.target_ram==b.target_ram &&
        a.script_prefix==b.script_prefix && a.event==b.event && a.kind==b.kind &&
        a.body_ordinal==b.body_ordinal && a.source_name==b.source_name &&
        a.source_ram==b.source_ram && a.source_record==b.source_record;
}
struct DepthScope {
    std::size_t& value;
    explicit DepthScope(std::size_t& depth):value(depth) { ++value; }
    ~DepthScope() { --value; }
};
}
struct ModelScriptRuntime::Impl {
    struct Invocation {
        ModelScriptInvocation context;
        ScriptLookupPlan plan;
        std::size_t cursor{};
        std::optional<Lua4Execution> execution;
        std::int32_t status=-1;
        ModelScriptFileService file_service;
        Invocation(ModelScriptInvocation call,ScriptLookupPlan lookup):
            context(std::move(call)),plan(std::move(lookup)) {}
    };
    ScriptRegistry registry;
    std::shared_ptr<Lua4Vm> vm;
    ModelScriptContextRuntime context;
    std::map<std::uint64_t,std::shared_ptr<ModelScriptWork>> works;
    std::map<std::pair<std::uint32_t,std::uint32_t>,Lua4Program> programs;
    std::vector<std::unique_ptr<Invocation>> invocations;
    ModelScriptHostService host_service;
    ModelScriptFileService file_service;
    std::map<std::pair<std::uint64_t,std::uint64_t>,ModelScriptHostService> hosts;
    std::size_t active_depth{},host_depth{},file_depth{};
    bool fault{};
    Lua4RunResult last_lua;
    std::string diagnostic;

    Impl(ScriptRegistry selected,std::shared_ptr<Lua4Vm> shared_vm):
        registry(std::move(selected)),vm(std::move(shared_vm)) {}
    void available() const {
        if(fault || context.faulted()) fail("Model script runtime has an uncertain outcome; replay is forbidden.");
    }
    void outside_callback() const {
        available();
        if(active_depth || host_depth || file_depth) fail("Cannot replace model script services inside a callback.");
    }
    Lua4HostResult host(const Lua4HostCall& call) {
        DepthScope scope(host_depth);
        const bool ram=call.name=="be.MDLgetRamNo";
        const bool record=call.name=="be.MDLgetRecNo";
        const bool kind=call.name=="be.MDLgetBulletKind";
        const bool box=call.name=="be.MDLgetBulletBoxNo";
        const bool direction=call.name=="be.MDLsetDMGTurn";
        if(ram || record || kind || box || direction) {
            if(!context.has_context()) return {false,{},"Target model script context is required for "+call.name};
            const auto argc=direction ? 2u : 1u;
            if(call.arguments.size()!=argc || call.arguments[0].type()!=Lua4Type::HostObject ||
                call.arguments[0].host_name()!="be")
                return {false,{},"Unverified native argument layout for "+call.name};
            if(direction) {
                // Original descriptor type 5 accepts Number, then fctiwz. Keep
                // unverified diagnostic/out-of-range behavior at a boundary.
                if(call.arguments[1].type()!=Lua4Type::Number)
                    return {false,{},"Native numeric argument diagnostic service is required for "+call.name};
                const auto value=std::trunc(call.arguments[1].as_number());
                if(!std::isfinite(value) || value<std::numeric_limits<std::int32_t>::min() ||
                    value>std::numeric_limits<std::int32_t>::max())
                    return {false,{},"Unverified native integer conversion for "+call.name};
                context.mutable_context().direction=static_cast<std::int32_t>(value)==1 ? 1u : 0u;
                return {true,{},{}};
            }
            const auto& live=context.context();
            const auto value=ram ? live.target_ram : record ? live.target_record : kind ? live.kind : live.body_ordinal;
            return {true,{Lua4Value::number(value)},{}};
        }
        const auto identity=std::pair{call.execution_id,call.call_id};
        auto found=hosts.find(identity);
        if(found==hosts.end()) {
            if(!host_service) return {false,{},"Native model script service is required: "+call.name};
            found=hosts.emplace(identity,host_service).first;
        }
        // std::map keeps this owning service stable through nested host calls.
        auto result=found->second(context,call);
        if(result.completed) hosts.erase(identity);
        return result;
    }
    bool dispatch(Invocation& invocation,const ModelScriptCall& call) {
        while(invocation.cursor<invocation.plan.entries.size()) {
            const auto& entry=invocation.plan.entries[invocation.cursor];
            if(entry.state==ScriptLookupState::no_match) { ++invocation.cursor; continue; }
            if(entry.state!=ScriptLookupState::match) {
                diagnostic="Original script selection is unresolved at registration "+
                    std::to_string(entry.registration.registration_index)+": "+
                    (entry.script ? entry.script->info.diagnostic : "missing selected row");
                return false;
            }
            if(!invocation.execution) {
                if(!entry.script) fail("Matched script registration has no owned bytes.");
                const auto label=entry.registration.source_label+" [occurrence "+
                    std::to_string(entry.registration.source_occurrence)+", registration "+
                    std::to_string(entry.registration.registration_index)+", info "+
                    std::to_string(entry.registration.info_reference)+", data "+
                    std::to_string(entry.script->info.data_index)+"] "+call.filename;
                const auto identity=std::pair{entry.registration.registration_index,entry.script->info.data_index};
                auto program=programs.find(identity);
                if(program==programs.end()) program=programs.emplace(identity,
                    read_lua4_chunk(entry.script->bytes,label)).first;
                invocation.execution.emplace(host_depth ? vm->begin_nested(program->second) : vm->begin_execution(program->second));
            }
            last_lua=vm->resume(*invocation.execution);
            if(last_lua.stop==Lua4RunStop::faulted) fail("Target Lua execution failed: "+last_lua.diagnostic);
            if(!last_lua.completed) { diagnostic=last_lua.diagnostic; return false; }
            // 80302AE4 -> loader/protected runner: supported success is status 0.
            // Lua return values (including -1) are unrelated to this status.
            invocation.status=0;
            invocation.execution.reset();
            ++invocation.cursor;
        }
        if(invocation.status==-1) {
            if(!invocation.file_service) invocation.file_service=file_service;
            if(!invocation.file_service) {
                diagnostic="Original filesystem script service is required: "+call.filename;
                return false;
            }
            DepthScope scope(file_depth);
            const auto status=invocation.file_service(*vm,context,call,host_depth!=0);
            if(!status) { diagnostic="Filesystem script invocation is pending: "+call.filename; return false; }
            invocation.status=*status;
        }
        diagnostic.clear();
        return true;
    }
};

ModelScriptRuntime::ModelScriptRuntime(ScriptRegistry registry,std::shared_ptr<Lua4Vm> vm):
    impl_(std::make_shared<Impl>(std::move(registry),std::move(vm))) {
    if(!impl_->vm) fail("Model script runtime requires one owned Lua VM.");
    const auto be=impl_->vm->get_global("be");
    if(be.type()==Lua4Type::Nil) impl_->vm->set_global("be",Lua4Value::host_object("be"));
    else if(be.type()!=Lua4Type::HostObject || be.host_name()!="be")
        fail("Existing Lua be global is not the original host interface.");
    const std::weak_ptr<Impl> weak=impl_;
    impl_->vm->set_host_handler([weak](const Lua4HostCall& call) {
        const auto owner=weak.lock();
        if(!owner) fail("Model script host owner is unavailable.");
        auto result=owner->host(call);
        if(!result.completed) fail(result.diagnostic);
        return result.returned;
    });
    impl_->vm->set_resumable_host_handler([weak](const Lua4HostCall& call) {
        const auto owner=weak.lock();
        if(!owner) fail("Model script host owner is unavailable.");
        return owner->host(call);
    });
}
ModelScriptRuntime::~ModelScriptRuntime()=default;
void ModelScriptRuntime::register_work(std::shared_ptr<ModelScriptWork> work) {
    impl_->outside_callback();
    if(pending()) fail("Cannot register target work during a pending script.");
    if(!work) fail("Cannot register missing target script work.");
    if(impl_->works.size()>=4096) fail("Target script work exceeds 4096 owners.");
    const auto identity=key(work->target());
    if(!impl_->works.emplace(identity,std::move(work)).second)
        fail("Target script work is already registered.");
}
void ModelScriptRuntime::set_host_service(ModelScriptHostService service) {
    impl_->outside_callback(); impl_->host_service=std::move(service);
}
void ModelScriptRuntime::set_file_service(ModelScriptFileService service) {
    impl_->outside_callback(); impl_->file_service=std::move(service);
}
Lua4RunResult ModelScriptRuntime::initialize(const Lua4Program& program) {
    impl_->outside_callback();
    if(pending()) fail("Cannot initialize Lua during a pending target script.");
    impl_->last_lua=impl_->vm->execute(program);
    impl_->diagnostic=impl_->last_lua.diagnostic;
    if(!impl_->last_lua.completed) impl_->fault=true;
    return impl_->last_lua;
}
std::optional<std::uint8_t> ModelScriptRuntime::handle(const ModelTargetHitRequest& request) {
    auto& owner=*impl_;
    owner.available();
    const auto index=owner.active_depth;
    if(index==owner.invocations.size()) {
        const auto found=owner.works.find(key(request.target));
        if(found==owner.works.end()) {
            owner.diagnostic="Source-qualified target script work is required.";
            return std::nullopt;
        }
        // Build the lookup before publishing context, so a lookup failure cannot
        // strand an invocation whose original effects have already begun.
        auto plan=owner.registry.lookup(request.script_prefix+"_"+request.target_name+".lua");
        owner.invocations.reserve(index+1);
        auto invocation=std::make_unique<Impl::Invocation>(owner.context.begin(found->second,request),std::move(plan));
        owner.invocations.push_back(std::move(invocation));
    }
    auto& invocation=*owner.invocations.at(index);
    if(!same_request(invocation.context.call().request,request))
        fail("Resume the exact pending model script request before another target invocation.");
    DepthScope scope(owner.active_depth);
    try {
        const auto result=owner.context.resume(invocation.context,[&](auto&,const auto& call) {
            return owner.dispatch(invocation,call);
        });
        if(result) owner.invocations.pop_back();
        return result;
    } catch(const std::exception& error) {
        owner.fault=true; owner.diagnostic=error.what(); throw;
    } catch(...) {
        owner.fault=true; owner.diagnostic="Target script callback failed with an uncertain outcome."; throw;
    }
}
bool ModelScriptRuntime::pending() const noexcept { return !impl_->invocations.empty(); }
bool ModelScriptRuntime::faulted() const noexcept { return impl_->fault || impl_->context.faulted(); }
const ModelScriptContextRuntime& ModelScriptRuntime::context_runtime() const noexcept { return impl_->context; }
const Lua4Vm& ModelScriptRuntime::vm() const noexcept { return *impl_->vm; }
const Lua4RunResult& ModelScriptRuntime::last_lua_result() const noexcept { return impl_->last_lua; }
const std::string& ModelScriptRuntime::diagnostic() const noexcept { return impl_->diagnostic; }
} // namespace dw4
