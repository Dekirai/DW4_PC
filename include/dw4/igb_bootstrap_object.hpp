#pragma once

#include "dw4/igb_object_init.hpp"
#include "dw4/igb_type_registration.hpp"

namespace dw4 {
class IgbBootstrapError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Original arithmetic identity plus explicitly represented physical storage.
// A nonzero arithmetic address alone never establishes readable host memory.
struct IgbBootstrapPointer {
    std::shared_ptr<IgbObjectStorage> storage;
    std::uint32_t offset{};
};
std::uint32_t igb_bootstrap_address(const IgbBootstrapPointer&) noexcept;
std::uint32_t igb_bootstrap_read_word(const IgbBootstrapPointer&,std::uint32_t relative);
std::uint8_t igb_bootstrap_read_byte(const IgbBootstrapPointer&,std::uint32_t relative);
void igb_bootstrap_write_word(const IgbBootstrapPointer&,std::uint32_t relative,std::uint32_t value);
void igb_bootstrap_write_halfword(const IgbBootstrapPointer&,std::uint32_t relative,std::uint16_t value);
void igb_bootstrap_write_byte(const IgbBootstrapPointer&,std::uint32_t relative,std::uint8_t value);
std::shared_ptr<IgbRuntimeMetaType> igb_bootstrap_metadata(const IgbBootstrapPointer&);
std::shared_ptr<IgbDecodeFieldRegistry> igb_bootstrap_fields(const IgbBootstrapPointer&);
std::shared_ptr<IgbRuntimeTypeList> igb_bootstrap_types(const IgbBootstrapPointer&);
std::shared_ptr<IgbDecodeRuntimeField> igb_bootstrap_field(const IgbBootstrapPointer&);
std::shared_ptr<IgbRuntimeObject> igb_bootstrap_runtime_object(const IgbBootstrapPointer&);

enum class IgbBootstrapServiceKind {
    pool_for_handle, // Actual800607F4, default handle805621E0.
    pool_allocate,  // Captured pool virtual+D8(total bytes).
    heap_allocate,  // Actual8009281C(total bytes), no prefix adjustment.
    allocation_pool, // Actual800590A0(object address - live base metadata+20).
    pool_index,      // Captured allocation pool virtual+16C, low byte returned.
    base_metadata   // Unknown live object vtable+58; known804717F0 uses805622A4.
};
struct IgbBootstrapCall {
    IgbBootstrapServiceKind kind{};
    IgbBootstrapPointer object;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t handle{},bytes{},address{},vtable_word{};
};
struct IgbBootstrapOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer allocation_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
};
using IgbBootstrapFactory=std::function<std::shared_ptr<IgbBootstrapOperation>(const IgbBootstrapCall&)>;
struct IgbBootstrapServices {
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    std::optional<std::uint32_t> default_pool_handle; //805621E0, no implicit default.
    std::shared_ptr<IgbRegistrationMetaSlot> object_metadata; // Alias of805622A4.
    IgbBootstrapFactory factory;
};

// Actual80066D60. The pooled branch adds prefix even to a null allocation;
// the heap fallback returns its captured allocation without adding prefix.
class IgbRawAllocationInvocation {
public:
    IgbRawAllocationInvocation(std::uint32_t bytes,std::uint32_t prefix,std::shared_ptr<ModelSceneObject> pool);
    ~IgbRawAllocationInvocation();
    IgbRawAllocationInvocation(const IgbRawAllocationInvocation&)=delete;
    IgbRawAllocationInvocation& operator=(const IgbRawAllocationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbBootstrapServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    IgbBootstrapPointer result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

// with_pool_tag=true selects80066710; false selects reset8006665C.
// These retain the actual header read/modify/write order, including failure on
// an unknown old reference word. They do not manufacture initialized malloc.
class IgbBootstrapBaseInvocation {
public:
    IgbBootstrapBaseInvocation(IgbBootstrapPointer,bool with_pool_tag);
    ~IgbBootstrapBaseInvocation();
    IgbBootstrapBaseInvocation(const IgbBootstrapBaseInvocation&)=delete;
    IgbBootstrapBaseInvocation& operator=(const IgbBootstrapBaseInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbBootstrapServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
