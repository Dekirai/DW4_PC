#pragma once

#include "dw4/igb_gamecube_thread_init.hpp"
#include "dw4/igb_thread_startup.hpp"

namespace dw4 {
class IgbGamecubeThreadAdoptError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbOsThreadState {
    // Canonical absolute800000E4. Missing means unknown, engaged zero is null.
    // Actual80260FB4 initialization remains a separate required operation.
    std::optional<std::uint32_t> current_thread_address;
    // Explicit raw original OS-context spans, including possible aliases.
    // Native igThread reflected ownership is not required for these sources.
    std::vector<IgbBootstrapPointer> mappings;
};
struct IgbGamecubeThreadAdoptCall {
    IgbBootstrapPointer object;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t address{},virtual_slot{}; // Captured stack pointer, pool+108.
};
struct IgbGamecubeThreadAdoptOperation {
    std::function<MessengerCallResult()> resume;
    // The enclosing guard is borrowed for this resume only, never retained.
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbGamecubeThreadAdoptFactory=std::function<std::shared_ptr<IgbGamecubeThreadAdoptOperation>(const IgbGamecubeThreadAdoptCall&)>;
struct IgbGamecubeThreadAdoptServices {
    std::shared_ptr<IgbOsThreadState> os;
    // Reuses the actual68430 byte-tag/5929C request; not allocation-owner590A0.
    std::shared_ptr<IgbMemoryFieldConstructServices> memory;
    IgbGamecubeThreadAdoptFactory factory;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual77EFC only. Two fresh current-thread loads, first pointer publication,
// exact grouped0x318 copy, captured nonnull68390 free, then stack+20=0.
// It does not initialize OS state or schedule a host thread. The original
// caller treats this method as void; the Messenger value is unused.
class IgbGamecubeThreadAdoptInvocation {
public:
    explicit IgbGamecubeThreadAdoptInvocation(IgbBootstrapPointer);
    ~IgbGamecubeThreadAdoptInvocation();
    IgbGamecubeThreadAdoptInvocation(const IgbGamecubeThreadAdoptInvocation&)=delete;
    IgbGamecubeThreadAdoptInvocation& operator=(const IgbGamecubeThreadAdoptInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbGamecubeThreadAdoptServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Exact concrete80470110+DC, reached through actual755F8. Other calls delegate.
IgbThreadStartupFactory make_native_igb_gamecube_thread_adopt_factory(
    std::shared_ptr<IgbGamecubeThreadAdoptServices>,IgbThreadStartupFactory remaining={});
} // namespace dw4
