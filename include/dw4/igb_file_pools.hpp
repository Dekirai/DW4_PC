#pragma once

#include "dw4/igb_memory_pools.hpp"
#include "dw4/loader_request.hpp"

namespace dw4 {
class IgbFilePoolsError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
enum class IgbFilePoolsServiceKind {
    pool_virtual,   // Captured manager virtual+5C with handle2, when unbound.
    prepare_pool,   // Manager+D0=8005E7AC, includes unrecovered pool creation.
    destroy_object //80066E1C after full-word decrement reaches low23 zero.
};
struct IgbFilePoolsCall {
    IgbFilePoolsServiceKind kind{};
    std::shared_ptr<IgbMemoryPoolRegistry> manager;
    std::shared_ptr<ModelSceneObject> object;
    std::uint32_t handle{2};
};
struct IgbFilePoolsOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> object_result;
};
using IgbFilePoolsFactory=std::function<std::shared_ptr<IgbFilePoolsOperation>(const IgbFilePoolsCall&)>;
struct IgbFilePoolsServices {
    std::shared_ptr<IgbMemoryPoolsServices> memory_pools;
    IgbFilePoolsFactory factory;
};

// Actual80058C80 ->800607F4(handle2) -> selected manager+5C. Known8005B060
// reads the physical pool, lazy flag and previous active managers. It does not
// manufacture a pool, activate a registry or implement an unknown virtual.
class IgbCurrentPoolInvocation {
public:
    IgbCurrentPoolInvocation();
    ~IgbCurrentPoolInvocation();
    IgbCurrentPoolInvocation(const IgbCurrentPoolInvocation&)=delete;
    IgbCurrentPoolInvocation& operator=(const IgbCurrentPoolInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFilePoolsServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<ModelSceneObject> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

// Actual igIGBFile post-default callback80051698, selected by804724C4+34.
// Each field gets a fresh current-pool request, retain, old release and raw
// publication. The preceding reflected defaults belong to object construction.
class IgbFilePoolsInvocation {
public:
    explicit IgbFilePoolsInvocation(std::shared_ptr<LoaderRequestFile>);
    ~IgbFilePoolsInvocation();
    IgbFilePoolsInvocation(const IgbFilePoolsInvocation&)=delete;
    IgbFilePoolsInvocation& operator=(const IgbFilePoolsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFilePoolsServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
