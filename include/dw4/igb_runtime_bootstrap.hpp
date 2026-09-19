#pragma once

#include "dw4/igb_runtime_types.hpp"

namespace dw4 {

// A deliberately bounded set of recovered USA runtime registrations. It is
// independent of input files and is not a complete ArkCore initialization.
class IgbRuntimeBootstrap {
public:
    std::shared_ptr<IgbRuntimeTypesServices> type_services() const noexcept;
    std::shared_ptr<IgbDecodeFieldRegistry> field_registry() const noexcept;
    // Actual metadata identity for a represented field owner's virtual+58.
    // An unrelated instance or replaced owner remains an explicit boundary.
    std::shared_ptr<IgbRuntimeMetaType> field_metadata(
        const std::shared_ptr<IgbDecodeRuntimeField>& field) const;
private:
    friend IgbRuntimeBootstrap make_original_ndmw_runtime_bootstrap();
    struct Impl;
    explicit IgbRuntimeBootstrap(std::shared_ptr<Impl> impl):impl_(std::move(impl)) {}
    std::shared_ptr<Impl> impl_;
};

IgbRuntimeBootstrap make_original_ndmw_runtime_bootstrap();

} // namespace dw4
