#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace dw4 {
// Mutable bePadData fields consumed by original MODELCTRL input and waits.
// Device sampling and repeat generation remain separate producer operations.
struct ModelPadState {
    float left_x{},left_y{};                // +0C/+10
    std::int32_t direction{};               // +24, original logical direction mask
    std::uint32_t held{},trigger{};         // +44/+48
};
struct ModelPadList {
    using Storage=std::vector<std::shared_ptr<ModelPadState>>;
    std::int32_t count{},capacity{};
    std::shared_ptr<Storage> backing;
};
struct ModelPadManager {
    std::shared_ptr<ModelPadList> data;      // +14
    std::uint8_t redirect{};                // +1D, selects physical slot 4
};

// 80312138. Read the selected physical pointer without consulting count or
// capacity. Redirected input and numbers >=4 use slot 4. A represented null
// pointer is returned unchanged; the consuming caller owns its dereference.
// Negative physical addresses and absent owners/backing are checked failures.
std::shared_ptr<ModelPadState> model_pad(const std::shared_ptr<ModelPadManager>&,
    std::int32_t number);
} // namespace dw4
