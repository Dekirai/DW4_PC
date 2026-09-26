#pragma once

#include "dw4/igb_allocation_copy.hpp"
#include "dw4/igb_data_list_defaults.hpp"
#include "dw4/igb_thread_manager_insert.hpp"

namespace dw4 {
class IgbDataListGrowthError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbDataListGrowthCall {
    IgbObjectInstance object;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t address{},virtual_slot{}; // Captured old backing, pool+108.
};
struct IgbDataListGrowthOperation {
    std::function<MessengerCallResult()> resume;
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbDataListGrowthFactory=std::function<std::shared_ptr<IgbDataListGrowthOperation>(const IgbDataListGrowthCall&)>;
struct IgbDataListGrowthServices {
    std::shared_ptr<IgbDataListDefaultsServices> data_defaults; // Alias globals[2],80561D48.
    std::shared_ptr<IgbObjectInitServices> objects; // Captured metadata virtual+58.
    std::shared_ptr<IgbMemoryFieldConstructServices> memory; // Element, tagged pool, allocation and bytes.
    IgbDataListGrowthFactory factory; // Actual68390's selected pool+108 release.
    std::vector<std::shared_ptr<void>> retained;
};
// Actual4155C. Capacity is published only after allocation/copy/old release;
// no count is written here. Its nested62BB8 status is intentionally ignored.
class IgbDataListCapacityInvocation {
public:
    IgbDataListCapacityInvocation(IgbObjectInstance,std::uint32_t capacity,std::uint32_t element_bytes);
    ~IgbDataListCapacityInvocation();
    IgbDataListCapacityInvocation(const IgbDataListCapacityInvocation&)=delete;
    IgbDataListCapacityInvocation& operator=(const IgbDataListCapacityInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDataListGrowthServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual41660. Signed growth arithmetic precedes4155C; count is stored last.
class IgbDataListGrowthInvocation {
public:
    IgbDataListGrowthInvocation(IgbObjectInstance,std::uint32_t count,std::uint32_t element_bytes);
    ~IgbDataListGrowthInvocation();
    IgbDataListGrowthInvocation(const IgbDataListGrowthInvocation&)=delete;
    IgbDataListGrowthInvocation& operator=(const IgbDataListGrowthInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDataListGrowthServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbThreadManagerInsertFactory make_native_igb_data_list_growth_factory(
    std::shared_ptr<IgbDataListGrowthServices>,IgbThreadManagerInsertFactory remaining={});
} // namespace dw4
