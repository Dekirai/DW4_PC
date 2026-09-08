#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;
class SceneControlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
enum class SwitchControlCommand { all_on, all_off };
struct SwitchControlRequest {
    std::uint32_t controller_reference{};
    SwitchControlCommand command{};
};
struct SwitchControlInfo {
    std::uint32_t reference_index{};
    std::string name;
    std::uint32_t target_reference{};
    std::uint32_t child_count{};
    std::uint32_t mask_bit_count{};
    bool supported{};
    std::string diagnostic;
};
struct SwitchControlState {
    SwitchControlInfo info;
    SwitchControlCommand command{};
    std::vector<bool> selected_children;
};

// Only controllers on the explicit igInfoList are eligible. Unsupported
// descriptors remain visible with diagnostics; no runtime state is changed.
std::vector<SwitchControlInfo> list_igb_switch_controls(const IgbDocument& document);
// Evaluates the verified ALLON / ALLOF child-selection behavior of one exported
// controller. This does not execute scripts, register interfaces or mutate IGBs.
SwitchControlState evaluate_igb_switch_control(const IgbDocument& document, SwitchControlRequest request);
} // namespace dw4
