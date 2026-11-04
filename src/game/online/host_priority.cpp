#include "host_priority.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace dw4::game::online {

bool keep_full_speed_in_background() noexcept {
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    // Both kinds of throttling under control, both off.
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
#else
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#endif
    state.StateMask = 0;
    return SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state) != 0;
#else
    return false;
#endif
}

} // namespace dw4::game::online
