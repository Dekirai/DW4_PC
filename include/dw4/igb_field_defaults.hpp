#pragma once

#include "dw4/igb_object_init.hpp"

namespace dw4 {
class IgbFieldDefaultsError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Refresh represented members after writes to a typed object's physical
// allocation. Only overlapping, fully initialized members are read. Zero
// pointers become represented null; changed nonzero pointers need an explicit
// typed binding before use. Untouched padding is never read.
void igb_synchronize_field_storage(const IgbObjectInstance&,std::uint32_t offset,std::uint32_t bytes);

// Exact uint/int/bool/ushort/short/byte scalar bindings and the shared pointer
// copy/BLR slots. Reference construction and F0 remain separate operations.
IgbObjectInitFactory make_native_igb_scalar_field_factory(IgbObjectInitFactory remaining={});
} // namespace dw4
