#pragma once

#include "dw4/igb_thread_dependencies.hpp"
#include "dw4/igb_thread_registration.hpp"
#include "dw4/igb_field_schema.hpp"

namespace dw4 {
class IgbThreadDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbThreadDefaultsServices {
    IgbThreadDefaultsServices();
    std::shared_ptr<IgbThreadDependenciesServices> dependencies;
    std::shared_ptr<IgbThreadRegistrationState> thread; // Canonical805614E8.
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldFactoryTable> factories; //8055CF98, two2879C entries.
    std::shared_ptr<IgbFieldNameTable> names; //8055CFA8.
    std::shared_ptr<IgbFieldGlobalTable> globals; //8055CFB0, cells805614EC/F0.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //8055CFB8,8/12.
    // Optional physical views of canonical metadata. Known views must agree;
    // canonical-only fixtures do not imply a fabricated original allocation.
    std::vector<IgbBootstrapPointer> physical_metadata;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual23940. Captures receiver/count after the volatile store, composes two
// original lazy metadata getters, then reloads805614E8 for redirect publication.
class IgbThreadDefaultsInvocation {
public:
    IgbThreadDefaultsInvocation();
    ~IgbThreadDefaultsInvocation();
    IgbThreadDefaultsInvocation(const IgbThreadDefaultsInvocation&)=delete;
    IgbThreadDefaultsInvocation& operator=(const IgbThreadDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_thread_defaults_factory(
    std::shared_ptr<IgbThreadDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
