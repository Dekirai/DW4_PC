#pragma once

// An online session's process keeps its speed in the background (round 62).
// Windows 11 may throttle a process whose window is not in front (EcoQoS:
// lower clock speed, the timer resolution it asked for ignored). A member
// whose game runs in the background, such as the second of two games on one
// PC, then executes its sealed ticks late, its pads reach the relay late, and
// the relay raises its input delay (docs/SEAMLESS.md 6.4). The process opts
// out of that throttling (SetProcessInformation ProcessPowerThrottling, its
// own scheduling hint, no system setting); elsewhere nothing happens.
namespace dw4::game::online {

// True when the process asked for it (Windows 10 1709 and later).
bool keep_full_speed_in_background() noexcept;

} // namespace dw4::game::online
