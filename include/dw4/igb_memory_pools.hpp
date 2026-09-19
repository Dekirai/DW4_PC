#pragma once

#include "dw4/igb_runtime_types.hpp"

#include <array>

namespace dw4 {
class IgbMemoryPoolsError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Original8005AF08 owner. Names occupy inline bytes +18+i*90, not a list of
// pool pointers. The represented virtual+74 returns a numeric slot handle.
struct IgbMemoryPoolRegistry {
    std::shared_ptr<IgbScratchBuffer> inline_bytes;
    bool original_lookup_bound{}; // Concrete80471BDC+74 ->8005B410.
    // Typed pointer identities at +14+i*90. Name-only construction does not
    // establish these owners. Missing and represented null remain distinct.
    std::array<std::optional<std::shared_ptr<ModelSceneObject>>,46> pools;
    bool original_pool_access_bound{}; // Concrete80471BDC+5C ->8005B060.
};
struct IgbMemoryPoolsServices {
    std::optional<std::int32_t> selector; // Global805621DC, captured once.
    std::optional<std::shared_ptr<IgbMemoryPoolRegistry>> fallback; //805621C8.
    // Only selectors1..3 select804F2294[selector-1]. Selector4 returns null.
    std::array<std::optional<std::shared_ptr<IgbMemoryPoolRegistry>>,3> active;
};

// Proven constructor name initialization8005D02C only. This neither installs
// global selection nor constructs/activates the referenced allocation pools.
std::shared_ptr<IgbMemoryPoolRegistry> make_original_igb_memory_pool_registry();

class IgbMemoryPoolLookupInvocation {
public:
    explicit IgbMemoryPoolLookupInvocation(IgbRuntimeNameSource);
    explicit IgbMemoryPoolLookupInvocation(SystemStageString);
    ~IgbMemoryPoolLookupInvocation();
    IgbMemoryPoolLookupInvocation(const IgbMemoryPoolLookupInvocation&)=delete;
    IgbMemoryPoolLookupInvocation& operator=(const IgbMemoryPoolLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryPoolsServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::uint32_t value() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
