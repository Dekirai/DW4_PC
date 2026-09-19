#pragma once

#include "dw4/igb_field_operations.hpp"
#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_object_registration.hpp"

namespace dw4 {
class IgbObjectDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbObjectDefaultsServiceKind {destroy_field}; // Actual80066E1C.
struct IgbObjectDefaultsCall {
    IgbObjectDefaultsServiceKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> field;
};
struct IgbObjectDefaultsOperation {std::function<MessengerCallResult()> resume;};
using IgbObjectDefaultsFactory=std::function<std::shared_ptr<IgbObjectDefaultsOperation>(const IgbObjectDefaultsCall&)>;
struct IgbObjectDefaultsServices {
    std::shared_ptr<IgbRegistrationMetaSlot> object_metadata; // Canonical805622A4.
    // Writable canonical global cells. Initial contents remain unrepresented
    // until the original field finalizer publishes its actual descriptor.
    std::shared_ptr<IgbRuntimeFieldSlot> ref_count=std::make_shared<IgbRuntimeFieldSlot>(); //805622A8.
    std::shared_ptr<IgbRuntimeFieldSlot> pool_index=std::make_shared<IgbRuntimeFieldSlot>(); //805622AC.
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::shared_ptr<IgbDependenciesServices> dependencies;
    std::shared_ptr<IgbMetaFieldFactoryServices> meta_fields;
    std::shared_ptr<IgbFieldOperationsServices> fields;
    IgbObjectDefaultsFactory factory;
    // Retains original temporaries and raw overwritten host bindings without
    // manufacturing guest reference changes on interruption or failure.
    std::vector<std::shared_ptr<void>> unfinished;
};

// Actual80066B7C. Both specialized fields are constructed by native80022A90;
// their names/default modes/global cells are not inferred from serialized data.
class IgbObjectDefaultsInvocation {
public:
    IgbObjectDefaultsInvocation();~IgbObjectDefaultsInvocation();
    IgbObjectDefaultsInvocation(const IgbObjectDefaultsInvocation&)=delete;
    IgbObjectDefaultsInvocation& operator=(const IgbObjectDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbObjectDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_object_defaults_factory(
    std::shared_ptr<IgbObjectDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
