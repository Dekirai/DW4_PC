#pragma once

#include "dw4/lua4.hpp"
#include "dw4/model_script_context.hpp"
#include "dw4/script_registry.hpp"

namespace dw4 {
using ModelScriptHostService = std::function<Lua4HostResult(
    ModelScriptContextRuntime&,const Lua4HostCall&)>;
// The actual filesystem owner supplies its original signed dispatch status.
// A service that starts Lua must use this same VM and retain its execution;
// nested is true only while executing an owning Lua host callback.
using ModelScriptFileService = std::function<std::optional<std::int32_t>(
    Lua4Vm&,ModelScriptContextRuntime&,const ModelScriptCall&,bool nested)>;

// Prepared target-Hit integration. Explicit ordered registrations, ONE work and
// native services are prerequisites, not substitutes for player construction.
class ModelScriptRuntime {
public:
    explicit ModelScriptRuntime(ScriptRegistry registry,
        std::shared_ptr<Lua4Vm> vm = std::make_shared<Lua4Vm>());
    ~ModelScriptRuntime();
    ModelScriptRuntime(const ModelScriptRuntime&)=delete;
    ModelScriptRuntime& operator=(const ModelScriptRuntime&)=delete;
    ModelScriptRuntime(ModelScriptRuntime&&)=delete;
    ModelScriptRuntime& operator=(ModelScriptRuntime&&)=delete;
    void register_work(std::shared_ptr<ModelScriptWork> work);
    // A partially invoked callback retains its actual service until completion.
    // Installing a newly available service does not replace that retained owner.
    void set_host_service(ModelScriptHostService service);
    void set_file_service(ModelScriptFileService service);
    Lua4RunResult initialize(const Lua4Program& program);
    std::optional<std::uint8_t> handle(const ModelTargetHitRequest& request);
    bool pending() const noexcept;
    bool faulted() const noexcept;
    const ModelScriptContextRuntime& context_runtime() const noexcept;
    const Lua4Vm& vm() const noexcept;
    const Lua4RunResult& last_lua_result() const noexcept;
    const std::string& diagnostic() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace dw4
