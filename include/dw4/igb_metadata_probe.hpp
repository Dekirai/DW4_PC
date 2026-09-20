#pragma once

#include "dw4/igb_metadata_factory.hpp"

#include <unordered_map>

namespace dw4 {
class IgbMetadataProbeError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbMetadataProbeServices {
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    // Host representation of the original 0x64-byte stack local. This is not
    // an original heap allocation or a replaceable constructor endpoint.
    std::function<IgbBootstrapPointer()> local_storage;
    // Optional address bindings for captured pointers no longer in a live slot.
    std::unordered_map<std::uint32_t,std::shared_ptr<IgbInternedString>> strings;
    // Raw local overwrites must not silently destroy surviving guest owners.
    std::vector<std::shared_ptr<void>> retained;
};
// Actual80029FFC, including actual reset constructor80065218, live ArkCore
// selection, captured local word and ordered +60/+1C string destruction.
class IgbMetadataProbeInvocation {
public:
    explicit IgbMetadataProbeInvocation(IgbBootstrapPointer);
    ~IgbMetadataProbeInvocation();
    IgbMetadataProbeInvocation(const IgbMetadataProbeInvocation&)=delete;
    IgbMetadataProbeInvocation& operator=(const IgbMetadataProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataProbeServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_metadata_probe_factory(
    std::shared_ptr<IgbMetadataProbeServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
