#pragma once

#include "dw4/igb_metadata_factory.hpp"
#include "dw4/igb_thread_registration.hpp"

namespace dw4 {
class IgbThreadDependenciesError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbThreadDependencyEntry {thread_list,semaphore};
struct IgbThreadDependenciesServices {
    std::shared_ptr<IgbRegistrationMetaSlot> thread_list; // Canonical 80561530.
    std::shared_ptr<IgbRegistrationMetaSlot> semaphore; // Canonical 805615EC.
    std::shared_ptr<IgbTypeRegistrationServices> types; // Metadata handle 805621F4.
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    // Actual completed metadata allocations, separate from class readiness.
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual23E40/26020. Allocate only when the canonical pointer is null;
// neither getter tests flag4 nor registers the returned dependency class.
class IgbThreadDependencyLookupInvocation {
public:
    explicit IgbThreadDependencyLookupInvocation(IgbThreadDependencyEntry);
    ~IgbThreadDependencyLookupInvocation();
    IgbThreadDependencyLookupInvocation(const IgbThreadDependencyLookupInvocation&)=delete;
    IgbThreadDependencyLookupInvocation& operator=(const IgbThreadDependencyLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadDependenciesServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
    // Empty when the ready pointer has no represented physical allocation.
    IgbBootstrapPointer allocation()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual65D94 ->3E4D8 ->3E8B8. Capture the reached ArkCore then its+28
// callback list; allocation inside append reads its own fresh ArkCore+34.
class IgbBootstrapCallbackAppendInvocation {
public:
    explicit IgbBootstrapCallbackAppendInvocation(std::uint32_t function);
    ~IgbBootstrapCallbackAppendInvocation();
    IgbBootstrapCallbackAppendInvocation(const IgbBootstrapCallbackAppendInvocation&)=delete;
    IgbBootstrapCallbackAppendInvocation& operator=(const IgbBootstrapCallbackAppendInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRegistrationCallbacksServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
