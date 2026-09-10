#pragma once

#include "dw4/messenger.hpp"
#include "dw4/model_control.hpp"
#include "dw4/model_hit_body.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace dw4 {
struct ModelCollisionWork {
    // Represented live work+44 and work+0C reference presence. This does not
    // invent HP values or initialize the original HP/record/script wrappers.
    std::uint8_t control_enabled{};
    bool hp_reference_present{};
    std::optional<std::string_view> current_sequence;
};
struct ModelTargetHitRequest {
    ControllerObjectId target;
    std::string target_source,target_name;
    std::int32_t target_ram{};
    std::string script_prefix="hit",event="Hit";
    std::int32_t kind{},body_ordinal{};
    std::string source_name;
    std::int32_t source_ram{},source_record=-1;
};
struct ModelCollisionServices {
    // Current beSystem timestamp, not an ACTION clock or cached frame delta.
    std::function<std::optional<std::uint64_t>()> system_time;
    // The complete original 8030B6EC Lua/context operation, including HP wrapper
    // publication. Return its LIVE context+30 byte on completion. Nullopt keeps
    // the same request pending; the service owns any completed internal prefix.
    std::function<std::optional<std::uint8_t>(const ModelTargetHitRequest&)> hit_script;
};
enum class ModelCollisionStop { complete, system_time_required, record_required, hit_script_required };
struct ModelCollisionResult {
    ModelCollisionStop stop=ModelCollisionStop::complete;
    std::optional<std::size_t> body_ordinal;
    std::string diagnostic;
    bool complete() const noexcept { return stop==ModelCollisionStop::complete; }
};

// Original base CHKBULLET body/response path on explicitly prepared ONE work.
// Controller virtual+C4 must already have declined the command. This owns no
// original player construction, status manager, HP wrapper or global Lua VM.
// Each target owns its cache and one synchronous delivery continuation. Throws
// fault this owner: a partially committed hit/script cannot be replayed.
class ModelCollisionRuntime {
public:
    explicit ModelCollisionRuntime(ModelHitBodyCache body);
    ~ModelCollisionRuntime();
    ModelCollisionRuntime(const ModelCollisionRuntime&)=delete;
    ModelCollisionRuntime& operator=(const ModelCollisionRuntime&)=delete;
    ModelCollisionRuntime(ModelCollisionRuntime&&)=delete;
    ModelCollisionRuntime& operator=(ModelCollisionRuntime&&)=delete;
    const ModelHitBodyCache& body() const noexcept;
    bool pending() const noexcept;
    bool faulted() const noexcept;
    bool owns_delivery(const MessengerDelivery& delivery) const noexcept;
    ModelCollisionResult result() const;
    ModelCollisionResult handle(NativeMessenger& messenger,const MessengerDelivery& delivery,
        ModelControlState& model,ModelCollisionWork work,const MessengerServices& recipients,
        const ModelCollisionServices& services);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
