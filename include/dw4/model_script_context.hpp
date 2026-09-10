#pragma once

#include "dw4/model_collision.hpp"
#include "dw4/game_data.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace dw4 {
class ModelScriptError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// beDataObjInt: nullable inherited name and signed _data at guest+0C.
// Shared identity matters when GAMERAM and model work refer to the same object.
using ModelScriptInt=GameDataInt;

// Explicit prepared ONE work. The matrix may be an aliasing shared_ptr backed
// by the actual model owner; it is a live reference, never an implicit identity.
class ModelScriptWork {
public:
    ModelScriptWork(ControllerObjectId target,std::string source,std::string name,
        std::int32_t ram_no,std::shared_ptr<AnimationMatrix> matrix);
    ModelScriptWork(const ModelScriptWork&)=delete;
    ModelScriptWork& operator=(const ModelScriptWork&)=delete;
    ModelScriptWork(ModelScriptWork&&)=delete;
    ModelScriptWork& operator=(ModelScriptWork&&)=delete;
    ControllerObjectId target() const noexcept { return target_; }
    const std::string& source() const noexcept { return source_; }
    const std::string& name() const noexcept { return name_; }
    std::int32_t ram_no() const noexcept { return ram_no_; }
    const std::shared_ptr<AnimationMatrix>& matrix() const noexcept { return matrix_; }
    // Fresh work has two distinct zero-data objects. Actual player preparation
    // replaces these references with its existing GAMERAM HP/MHP objects.
    std::shared_ptr<ModelScriptInt> max_hp,hp;
    std::int32_t record=-1;
private:
    ControllerObjectId target_;
    std::string source_,name_;
    std::int32_t ram_no_;
    std::shared_ptr<AnimationMatrix> matrix_;
};

// One shared 80535914 context. Nested calls overwrite it and do not restore it.
// The published scalar HP values differ from the live work's shared wrappers.
struct ModelScriptContext {
    ControllerObjectId target;
    std::string target_source;
    std::int32_t target_ram{};
    std::shared_ptr<AnimationMatrix> matrix;
    std::int32_t maximum_hp{},hp{},target_record=-1;
    std::string target_name;
    std::int32_t kind{},body_ordinal{};
    std::string source_name;
    std::int32_t source_ram{},source_record=-1;
    std::uint8_t direction=1;
    std::string event;
};
struct ModelScriptCall {
    ModelTargetHitRequest request;
    std::string filename;
};

class ModelScriptContextRuntime;
class ModelScriptInvocation {
public:
    ModelScriptInvocation(ModelScriptInvocation&&) noexcept;
    ModelScriptInvocation& operator=(ModelScriptInvocation&&) noexcept;
    ~ModelScriptInvocation();
    ModelScriptInvocation(const ModelScriptInvocation&)=delete;
    ModelScriptInvocation& operator=(const ModelScriptInvocation&)=delete;
    bool completed() const noexcept;
    const ModelScriptCall& call() const;
private:
    struct State;
    explicit ModelScriptInvocation(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
    friend class ModelScriptContextRuntime;
};
using ModelScriptDispatch=std::function<bool(ModelScriptContextRuntime&,const ModelScriptCall&)>;

// 8030B6EC context publication, real script invocation and conditional HP merge.
// This neither discovers a script registry nor supplies missing Lua methods.
class ModelScriptContextRuntime {
public:
    ModelScriptContextRuntime();
    ~ModelScriptContextRuntime();
    ModelScriptContextRuntime(const ModelScriptContextRuntime&)=delete;
    ModelScriptContextRuntime& operator=(const ModelScriptContextRuntime&)=delete;
    ModelScriptContextRuntime(ModelScriptContextRuntime&&)=delete;
    ModelScriptContextRuntime& operator=(ModelScriptContextRuntime&&)=delete;
    ModelScriptInvocation begin(std::shared_ptr<ModelScriptWork> work,const ModelTargetHitRequest& request);
    // Missing/false calls retain their original publication and request. A
    // parent may re-enter its callback to resume its retained child; finishing
    // and child creation remain LIFO. Completed retries return the saved byte.
    std::optional<std::uint8_t> resume(ModelScriptInvocation& invocation,const ModelScriptDispatch& dispatch = {});
    bool has_context() const noexcept;
    bool pending() const noexcept;
    bool faulted() const noexcept;
    std::size_t depth() const noexcept;
    const ModelScriptContext& context() const;
    // Only a currently executing real script callback may mutate shared state.
    ModelScriptContext& mutable_context();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
