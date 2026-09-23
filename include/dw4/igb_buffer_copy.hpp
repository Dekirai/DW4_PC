#pragma once

#include "dw4/igb_field_values.hpp"

namespace dw4 {
class IgbBufferCopyError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
enum class IgbBufferFunction { size, free, copy };
enum class IgbBufferServiceKind { allocation_pool, pool_virtual };
struct IgbBufferCall {
    IgbBufferServiceKind kind{};
    std::shared_ptr<ModelSceneObject> pool;
    // The numeric pointer remains usable before its host storage is bound.
    // Allocation +D8 receives only pool/bytes and leaves buffer/address empty.
    IgbRuntimeFieldDefaultSource buffer;
    std::uint32_t address{}, virtual_slot{}, bytes{};
};
struct IgbBufferOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> pool_result;
    IgbRuntimeFieldDefaultSource buffer_result;
    std::uint32_t buffer_address{};
};
using IgbBufferFactory = std::function<std::shared_ptr<IgbBufferOperation>(const IgbBufferCall&)>;
struct IgbBufferServices {
    std::shared_ptr<IgbFieldValuesServices> values;
    IgbBufferFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual5641C size,56508 free and5647C copy, including5645C/5609C/560C8.
// Pool lookup590A0 and fresh pool virtuals remain explicit native services.
// Copy retains three independent owner lookups and two independent sizes.
class IgbBufferCopyInvocation {
public:
    IgbBufferCopyInvocation(IgbBufferFunction, IgbRuntimeFieldDefaultSource, std::uint32_t address);
    ~IgbBufferCopyInvocation();
    IgbBufferCopyInvocation(const IgbBufferCopyInvocation&) = delete;
    IgbBufferCopyInvocation& operator=(const IgbBufferCopyInvocation&) = delete;
    MessengerCallResult resume(std::shared_ptr<IgbBufferServices> services = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    IgbRuntimeFieldDefaultSource buffer_result() const;
    std::uint32_t buffer_address() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
