#include "dw4/controllers.hpp"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace dw4 {
ControllerObjectId ControllerObjectId::allocate_native() {
    // A process-wide domain also separates independently prepared runtime
    // contexts. Exhaustion stays terminal instead of wrapping and reusing IDs.
    static std::atomic<std::uint64_t> next{1};
    auto value=next.load(std::memory_order_relaxed);
    for(;;) {
        if(value==std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Native controller identity space exhausted.");
        if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) break;
    }
    ControllerObjectId result;result.native_identity_=value;return result;
}
} // namespace dw4
