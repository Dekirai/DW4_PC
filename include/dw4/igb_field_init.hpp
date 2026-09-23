#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_field_values.hpp"
#include "dw4/igb_field_defaults.hpp"

namespace dw4 {
class IgbFieldInitError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbFieldInitServices {
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    std::shared_ptr<IgbFieldValuesServices> values;
};

// Verified object-field object virtuals and reached reference-field passes.
// Unrecovered recursive member construction/reset stays pending at its branch;
// unknown tables and slots are delegated. Scalar defaults compose separately.
IgbObjectInitFactory make_native_igb_field_init_factory(
    std::shared_ptr<IgbFieldInitServices>,IgbObjectInitFactory remaining={});
} // namespace dw4
