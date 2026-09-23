#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_field_values.hpp"
#include "dw4/igb_field_defaults.hpp"

namespace dw4 {
struct IgbObjectFieldConstructServices;
struct IgbMemoryFieldConstructServices;
class IgbFieldInitError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbFieldInitServices {
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    std::shared_ptr<IgbFieldValuesServices> values;
    std::shared_ptr<IgbObjectFieldConstructServices> object_construct;
    std::shared_ptr<IgbMemoryFieldConstructServices> memory_construct;
};

// Verified object-reference, string, integer, boolean, signed-byte and memory descriptor object virtuals,
// plus the reached reference-field passes. Integer/boolean/signed-byte/memory +2C tails are BLR;
// object-reference/string +2C still performs its actual default-buffer preparation.
// Object-reference construction composes actual6546C and assignment6A47C.
// Memory construction composes62524/62BB8. Reached services and unrecovered
// reset branches stay pending; unknown tables and slots are delegated.
// Scalar defaults compose separately.
IgbObjectInitFactory make_native_igb_field_init_factory(
    std::shared_ptr<IgbFieldInitServices>,IgbObjectInitFactory remaining={});
} // namespace dw4
