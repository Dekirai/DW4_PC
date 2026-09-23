#pragma once

#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_type_registration.hpp"

#include <unordered_map>

namespace dw4 {
class IgbFieldProbeError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbFieldProbeServices {
    std::shared_ptr<IgbMetaFieldFactoryServices> fields;
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    // Physical representation of the original 0x44-byte stack object.
    std::function<IgbBootstrapPointer()> local_storage;
    std::unordered_map<std::uint32_t,std::shared_ptr<IgbInternedString>> strings;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual80028934: reset6ACA4, capture local word selected by live ArkCore+394,
// then destroy the +40 and +0C strings with intervening original vtable stores.
class IgbFieldProbeInvocation {
public:
    explicit IgbFieldProbeInvocation(IgbBootstrapPointer);
    ~IgbFieldProbeInvocation();
    IgbFieldProbeInvocation(const IgbFieldProbeInvocation&)=delete;
    IgbFieldProbeInvocation& operator=(const IgbFieldProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldProbeServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_field_probe_factory(
    std::shared_ptr<IgbFieldProbeServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
