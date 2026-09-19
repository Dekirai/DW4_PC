#pragma once

#include "dw4/igb_file_factory.hpp"
#include "dw4/igb_type_registration.hpp"

namespace dw4 {
class IgbRegistrationCallbacksError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbRegistrationCallbackList {
    using Storage=std::vector<std::uint32_t>;
    std::int32_t count{},capacity{}; // Original +04/+08; not igDataList layout.
    std::shared_ptr<Storage> backing; // +00, independent physical allocation.
    bool released{};
private:
    friend class IgbRegistrationCallbackAppendInvocation;
    friend void igb_initialize_registration_callbacks(const std::shared_ptr<IgbRegistrationCallbackList>&);
    std::vector<std::shared_ptr<Storage>> overwritten_backings_;
};
// Actual8003E848 raw stores. Allocating the list remains a separate operation.
void igb_initialize_registration_callbacks(const std::shared_ptr<IgbRegistrationCallbackList>&);
struct IgbRegistrationPrerequisitesState {
    //80561488/489. Absence is unrepresented storage, not a zero-valued byte.
    std::optional<std::uint8_t> started,initialized;
};
enum class IgbRegistrationCallbacksServiceKind {
    invoke_function,    // Captured original function; may return a callback list.
    allocate_backing,   //80056138(bytes,captured ArkCore+34 pool).
    reallocate_backing, //8005624C(captured backing,bytes).
    free_backing,       //80056378(captured backing).
    free_list           //800564E8(captured list).
};
struct IgbRegistrationCallbacksCall {
    IgbRegistrationCallbacksServiceKind kind{};
    std::uint32_t function{},bytes{};
    std::shared_ptr<IgbRegistrationCallbackList> callbacks;
    std::shared_ptr<IgbRegistrationCallbackList::Storage> backing;
    std::shared_ptr<ModelSceneObject> pool;
};
struct IgbRegistrationCallbacksOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbRegistrationCallbackList> callbacks_result;
    std::shared_ptr<IgbRegistrationCallbackList::Storage> backing_result;
};
using IgbRegistrationCallbacksFactory=std::function<std::shared_ptr<IgbRegistrationCallbacksOperation>(const IgbRegistrationCallbacksCall&)>;
struct IgbRegistrationCallbacksServices {
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    std::shared_ptr<IgbRegistrationPrerequisitesState> prerequisites;
    IgbRegistrationCallbacksFactory factory;
private:
    friend class IgbRegistrationCallbacksInvocation;
    friend class IgbRegistrationCallbackAppendInvocation;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> unfinished_lists_;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList::Storage>> unfinished_backings_;
};
// Actual8003E8B8, including its unusual wrapping capacity growth and the
// published count prefix before a physical indexed-store failure.
class IgbRegistrationCallbackAppendInvocation {
public:
    IgbRegistrationCallbackAppendInvocation(std::shared_ptr<IgbRegistrationCallbackList>,std::uint32_t function);
    ~IgbRegistrationCallbackAppendInvocation();
    IgbRegistrationCallbackAppendInvocation(const IgbRegistrationCallbackAppendInvocation&)=delete;
    IgbRegistrationCallbackAppendInvocation& operator=(const IgbRegistrationCallbackAppendInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRegistrationCallbacksServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual80066188, including original8003E85C(list,1) cleanup. Count is
// captured once; each indexed function and backing are read when reached.
class IgbRegistrationCallbacksInvocation {
public:
    explicit IgbRegistrationCallbacksInvocation(std::uint32_t function);
    ~IgbRegistrationCallbacksInvocation();
    IgbRegistrationCallbacksInvocation(const IgbRegistrationCallbacksInvocation&)=delete;
    IgbRegistrationCallbacksInvocation& operator=(const IgbRegistrationCallbacksInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRegistrationCallbacksServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual80021B94. The started byte is committed before any prerequisite call.
class IgbRegistrationPrerequisitesInvocation {
public:
    IgbRegistrationPrerequisitesInvocation();~IgbRegistrationPrerequisitesInvocation();
    IgbRegistrationPrerequisitesInvocation(const IgbRegistrationPrerequisitesInvocation&)=delete;
    IgbRegistrationPrerequisitesInvocation& operator=(const IgbRegistrationPrerequisitesInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRegistrationCallbacksServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual8002F1A0 arguments, using an alias of the canonical80561A04 cell.
IgbTypeRegistrationArguments igb_file_type_registration_arguments(const std::shared_ptr<IgbFileFactoryServices>&);
class IgbFileTypeRegistrationInvocation {
public:
    IgbFileTypeRegistrationInvocation();~IgbFileTypeRegistrationInvocation();
    IgbFileTypeRegistrationInvocation(const IgbFileTypeRegistrationInvocation&)=delete;
    IgbFileTypeRegistrationInvocation& operator=(const IgbFileTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRegistrationCallbacksServices>,
        std::shared_ptr<IgbFileFactoryServices>,std::shared_ptr<IgbTypeRegistrationServices>);
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_registration_function_factory(
    std::shared_ptr<IgbFileFactoryServices>,std::shared_ptr<IgbTypeRegistrationServices>,
    std::shared_ptr<IgbRegistrationCallbacksServices>,IgbRegistrationCallbacksFactory remaining={});
IgbFileFactoryFactory make_native_igb_file_registration_factory(
    std::shared_ptr<IgbRegistrationCallbacksServices>,IgbFileFactoryFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_registration_list_factory(
    std::shared_ptr<IgbRegistrationCallbacksServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
