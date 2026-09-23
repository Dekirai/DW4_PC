#pragma once

#include "dw4/igb_object_init.hpp"
#include "dw4/igb_file_pools.hpp"
#include "dw4/loader_dependency.hpp"

namespace dw4 {
class IgbFileFactoryError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

enum class IgbFileFactoryServiceKind {
    register_file_type, //8002F178:80066188(8002F1A0), including returned callbacks.
    redirect_type,      // Captured metadata+3C callback, before the abstract test.
    named_pool,         //80060834(captured nonempty metadata+60 name).
    current_pool,       //80058C80, including800607F4(handle2).
    allocate            // Captured pool virtual+D8(size+captured prefix).
};
struct IgbFileFactoryCall {
    IgbFileFactoryServiceKind kind{};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    SystemStageString name;
    std::uint32_t redirect{},bytes{};
};
struct IgbFileFactoryOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    std::shared_ptr<ModelSceneObject> object_result;
    std::shared_ptr<IgbObjectStorage> allocation_result;
};
using IgbFileFactoryFactory=std::function<std::shared_ptr<IgbFileFactoryOperation>(const IgbFileFactoryCall&)>;
struct IgbFileFactoryServices {
    //80561A04, reloaded only after registration returns. An absent binding
    // pauses; an engaged null is an actual null metadata dereference.
    std::optional<std::shared_ptr<IgbRuntimeMetaType>> file_metadata;
    std::shared_ptr<IgbObjectInitServices> initializer;
    IgbFileFactoryFactory factory;
private:
    friend class IgbReflectedFactoryInvocation;
    std::vector<std::shared_ptr<IgbObjectStorage>> unfinished_allocations_;
};

// Original8006546C allocation caller. Exposes typed object storage after
// the actual800667F4 initialization continuation has completed.
class IgbReflectedFactoryInvocation {
public:
    IgbReflectedFactoryInvocation(std::shared_ptr<IgbRuntimeMetaType>,std::shared_ptr<ModelSceneObject> pool);
    ~IgbReflectedFactoryInvocation();
    IgbReflectedFactoryInvocation(const IgbReflectedFactoryInvocation&)=delete;
    IgbReflectedFactoryInvocation& operator=(const IgbReflectedFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFileFactoryServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    IgbObjectInstance result_instance() const;
    // Strict file adapter; the original abstract/null result remains null.
    std::shared_ptr<LoaderRequestFile> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

// Actual8002EBEC: retain the incoming pool, perform registration, reload its
// metadata global, then enter8006546C. It does not invent a registered type.
class IgbFileFactoryInvocation {
public:
    explicit IgbFileFactoryInvocation(std::shared_ptr<ModelSceneObject> pool);
    ~IgbFileFactoryInvocation();
    IgbFileFactoryInvocation(const IgbFileFactoryInvocation&)=delete;
    IgbFileFactoryInvocation& operator=(const IgbFileFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFileFactoryServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<LoaderRequestFile> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

LoaderDependencyFactory make_native_igb_file_factory(std::shared_ptr<IgbFileFactoryServices>,LoaderDependencyFactory remaining={});
LoaderRequestFactory make_native_igb_request_file_factory(std::shared_ptr<IgbFileFactoryServices>,LoaderRequestFactory remaining={});
// Only the verified804724C4 bindings: BLR slots0C/24/2C, fresh metadata58,
// and actual post-default pools34. Unknown tables/slots delegate or stay absent.
IgbObjectInitFactory make_native_igb_file_init_factory(std::shared_ptr<IgbFileFactoryServices>,
    std::shared_ptr<IgbFilePoolsServices>,IgbObjectInitFactory remaining={});
IgbFileFactoryFactory make_native_igb_factory_pool_factory(std::shared_ptr<IgbFilePoolsServices>,
    IgbFileFactoryFactory remaining={});
} // namespace dw4
