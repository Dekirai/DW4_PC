#pragma once

#include "dw4/igb_file_factory.hpp"
#include "dw4/igb_field_values.hpp"

namespace dw4 {
class IgbObjectFieldConstructError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbObjectFieldConstructServiceKind {
    named_pool,     // Actual60834(captured nonempty descriptor+40 name).
    tagged_pool,    // Actual68430 ->5929C(captured target reference high byte).
    destroy_object // Actual66E1C after decrement reaches low23 zero.
};
struct IgbObjectFieldConstructCall {
    IgbObjectFieldConstructServiceKind kind{};
    IgbObjectInstance object;
    IgbDependencyName name;
    std::uint32_t handle{};
};
struct IgbObjectFieldConstructOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> pool_result;
};
using IgbObjectFieldConstructFactory=std::function<std::shared_ptr<IgbObjectFieldConstructOperation>(const IgbObjectFieldConstructCall&)>;
struct IgbObjectFieldConstructServices {
    std::shared_ptr<IgbFileFactoryServices> reflected;
    std::shared_ptr<IgbFieldValuesServices> values;
    IgbObjectFieldConstructFactory factory;
    // Explicit arithmetic/typed identities, including completed allocations.
    // These preserve host lifetime without changing original packed counts.
    std::vector<IgbObjectInstance> objects;
    std::vector<std::shared_ptr<void>> unfinished;
};
void igb_bind_object_field_instance(const std::shared_ptr<IgbObjectFieldConstructServices>&,const IgbObjectInstance&);

// Actual6A47C: read the old member, conditionally retain/release, then reload
// descriptor+08 for the store. Composes inside the outer construction lease.
class IgbObjectFieldAssignInvocation {
public:
    IgbObjectFieldAssignInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance target,IgbObjectInstance incoming);
    ~IgbObjectFieldAssignInvocation();
    IgbObjectFieldAssignInvocation(const IgbObjectFieldAssignInvocation&)=delete;
    IgbObjectFieldAssignInvocation& operator=(const IgbObjectFieldAssignInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbObjectFieldConstructServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual6A618: construct gate, named/tagged pool, live object metadata,
// actual6546C reflected factory, assignment, and live-flag temporary release.
class IgbObjectFieldConstructInvocation {
public:
    IgbObjectFieldConstructInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance target);
    ~IgbObjectFieldConstructInvocation();
    IgbObjectFieldConstructInvocation(const IgbObjectFieldConstructInvocation&)=delete;
    IgbObjectFieldConstructInvocation& operator=(const IgbObjectFieldConstructInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbObjectFieldConstructServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
