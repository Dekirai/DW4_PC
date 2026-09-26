#pragma once

#include "dw4/igb_dependencies.hpp"
#include "dw4/igb_file_pools.hpp"
#include "dw4/igb_type_registration.hpp"

namespace dw4 {
class IgbMetadataOperationsError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Raw name-pointer publication, without guest retain/release. Displaced host
// owners remain represented, including mutations made by a release callback.
void igb_metadata_publish_name(const std::shared_ptr<IgbRuntimeMetaType>&,
    const std::shared_ptr<IgbInternedString>&);

enum class IgbMetadataOperationKind {
    pool_for_handle, //800607F4, reached only when parent+34 is null.
    create_children, //8002DE88(selected pool), not the field-registry factory.
    append_child,    //80053BC0(captured parent+34,child), no metadata retain.
    append_field     //80053BC0(captured child+28,field), after native53E6C retain.
};
struct IgbMetadataOperationCall {
    IgbMetadataOperationKind kind{};
    std::shared_ptr<IgbRuntimeMetaType> metadata,parent;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbRuntimeTypeList> children;
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::uint32_t handle{};
};
struct IgbMetadataOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> pool_result;
    std::shared_ptr<IgbRuntimeTypeList> children_result;
    // Optional native continuation. Borrow, never retain, the enclosing guard.
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbMetadataOperationsFactory=std::function<std::shared_ptr<IgbMetadataOperation>(const IgbMetadataOperationCall&)>;
struct IgbMetadataOperationsServices {
    std::shared_ptr<IgbTypeRegistrationServices> types; // Canonical805621F4.
    std::shared_ptr<IgbDependenciesServices> dependencies; // Shared80562140 and interned headers.
    IgbMetadataOperationsFactory factory;
    // A failed/suspended caller does not implicitly release original temporary
    // strings or a newly constructed list on native invocation destruction.
    std::vector<std::shared_ptr<void>> unfinished;
};

// Actual80065640, including native658E4/65820/53E6C field selection/retain.
// The child-list factory and raw append/resize implementation stay explicit.
class IgbMetadataParentInvocation {
public:
    IgbMetadataParentInvocation(std::shared_ptr<IgbRuntimeMetaType> child,std::shared_ptr<IgbRuntimeMetaType> parent);
    ~IgbMetadataParentInvocation();
    IgbMetadataParentInvocation(const IgbMetadataParentInvocation&)=delete;
    IgbMetadataParentInvocation& operator=(const IgbMetadataParentInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataOperationsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

// Actual80065788. New interning precedes the live old-pointer read and full
// reference decrement; the captured result is published after release returns.
class IgbMetadataNameInvocation {
public:
    IgbMetadataNameInvocation(std::shared_ptr<IgbRuntimeMetaType>,IgbDependencyName);
    ~IgbMetadataNameInvocation();
    IgbMetadataNameInvocation(const IgbMetadataNameInvocation&)=delete;
    IgbMetadataNameInvocation& operator=(const IgbMetadataNameInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataOperationsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

IgbMetadataOperationsFactory make_native_igb_metadata_pool_factory(
    std::shared_ptr<IgbFilePoolsServices>,IgbMetadataOperationsFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_metadata_operations_factory(
    std::shared_ptr<IgbMetadataOperationsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
