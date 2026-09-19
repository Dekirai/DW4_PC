#pragma once

#include "dw4/igb_object_init.hpp"

namespace dw4 {
class IgbFieldDefaultsError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Exact igIntMetaField80475B60 and igBoolMetaField80473E30 bindings. Implements
// their actual scalar byte-copy defaults and verified BLR/zero-result leaves.
// Other field implementations remain explicit in the delegated factory.
IgbObjectInitFactory make_native_igb_scalar_field_factory(IgbObjectInitFactory remaining={});
} // namespace dw4
