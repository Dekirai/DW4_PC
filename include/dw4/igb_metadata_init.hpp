#pragma once

#include "dw4/igb_metadata_factory.hpp"
#include "dw4/igb_file_factory.hpp"
#include "dw4/igb_field_registry_factory.hpp"
#include "dw4/igb_type_list_factory.hpp"

namespace dw4 {
class IgbMetadataInitError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbMetadataInitServices {
    std::shared_ptr<IgbMetadataFactoryServices> metadata; //Canonical8056174C.
    std::shared_ptr<IgbFileFactoryServices> reflected; //Actual6546C/667F4.
    // Retain physical identities after a typed metadata result is published.
    std::vector<IgbObjectInstance> completed_allocations;
    std::shared_ptr<IgbFieldRegistryServices> field_lists; // Canonical 805619CC.
    std::shared_ptr<IgbTypeListServices> type_lists; // Children805619D4 and shared registry805619CC.
};
// Exact804761D8/80475CB0/80475C54: plain94080/94038/94030 +58 getters and verified
// BLR+0C/+24/+2C/+34. Member passes remain the actual reflected initializer.
IgbObjectInitFactory make_native_igb_metadata_init_factory(
    std::shared_ptr<IgbMetadataInitServices>,IgbObjectInitFactory remaining={});
// Connect the enabled29E64 endpoint to actual6546C and its typed initializer.
IgbMetadataFactory make_native_igb_reflected_metadata_factory(
    std::shared_ptr<IgbMetadataInitServices>,IgbMetadataFactory remaining={});
// Connect enabled2DE88/2DB4C to actual6546C with a metadata-pointer list.
// Registry metadata aliases the existing805619CC cell; it is not a new cache.
IgbTypeListFactory make_native_igb_reflected_type_list_factory(
    std::shared_ptr<IgbMetadataInitServices>,IgbTypeListFactory remaining={});
} // namespace dw4
