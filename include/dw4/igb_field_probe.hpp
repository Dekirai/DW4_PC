#pragma once

#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_object_field_construct.hpp"
#include "dw4/igb_type_registration.hpp"

#include <unordered_map>

namespace dw4 {
class IgbFieldProbeError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbFieldProbeServices {
    std::shared_ptr<IgbMetaFieldFactoryServices> fields;
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    // Physical representation of the original 0x44-byte stack object.
    std::function<IgbBootstrapPointer()> local_storage;
    // Profile-specific locals: object 0x44 / scalar 0x34 / memory 0x50 /
    // string 0x3C / enumeration and pointer 0x38.
    std::function<IgbBootstrapPointer(IgbMetaFieldKind)> profile_local_storage;
    std::unordered_map<std::uint32_t,std::shared_ptr<IgbInternedString>> strings;
    std::vector<std::shared_ptr<void>> retained;
    // Explicit +48 object identities and the reached destroy_object/80066E1C
    // continuation. A numeric member alone does not manufacture an owner.
    std::shared_ptr<IgbObjectFieldConstructServices> object_cleanup;
};
// Actual80028934(object), 8002E308(int), 8002AD90(memory), 80038778(bool),
// 80025348(string), 80037C78(signed byte), 800314D4(enum), 80027648(pointer):
// reset, capture the word selected
// by live ArkCore+394, then the original profile-specific member cleanup.
class IgbFieldProbeInvocation {
public:
    explicit IgbFieldProbeInvocation(IgbBootstrapPointer,IgbMetaFieldKind=IgbMetaFieldKind::object);
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
