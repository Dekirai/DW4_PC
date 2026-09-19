#pragma once

#include "dw4/igb_runtime_types.hpp"

namespace dw4 {
struct IgbRegistrationCallbackList;
class IgbTypeRegistrationError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
using IgbRegistrationMetaSlot=std::optional<std::shared_ptr<IgbRuntimeMetaType>>;
struct IgbTypeRegistrationArguments {
    // Aliasing shared ownership can refer directly to an existing metadata
    // global, such as IgbFileFactoryServices::file_metadata. No duplicate cache.
    std::shared_ptr<IgbRegistrationMetaSlot> metadata_slot;
    SystemStageString name;
    std::uint32_t flag{},native_size{},parent_registration{},parent_getter{},field_callback{},layout_probe{};
    std::uint32_t defaults_callback{},trailing_callback{};
    // Null is no table; a present table must physically contain its zero
    // terminator. Read one live original function identity at each iteration.
    std::shared_ptr<std::vector<std::uint32_t>> extra_callbacks;
};
enum class IgbTypeRegistrationServiceKind {
    pool_for_handle, //800607F4; metadata handle805621F4 is read per call.
    create_metadata, //80029E64 after the reached stack-name preparation.
    register_parent, // Original callback returning a deferred callback list.
    get_parent,      // Separate original callback returning parent metadata.
    attach_parent,   //80065640.
    set_name,        //80065788, including string ownership.
    layout_probe,    // Captured layout callback, only for low-byte flag zero.
    defaults,        // Optional input callback, before flag2 publication.
    publish_type,    //800660AC.
    destroy_metadata, //80066E1C after full-word decrement reaches low23 zero.
    find_filter_name, //800744FC(captured core+68,name), -1 means absent.
    configure_filter, //80066114 with the fresh core+60 pool, result ignored.
    compute_layout, //80065A58; may publish flag4 before the next callback.
    post_layout,    // Fresh metadata+4C callback, when nonzero.
    create_callbacks, //80056138(12,pool); caller conditionally executes8003E848.
    append_callback //8003E8B8(list,function).
};
struct IgbTypeRegistrationCall {
    IgbTypeRegistrationServiceKind kind{};
    std::shared_ptr<IgbRuntimeMetaType> metadata,parent;
    std::shared_ptr<ModelSceneObject> pool,object;
    std::shared_ptr<IgbRegistrationCallbackList> callbacks;
    SystemStageString name,allocation_label;
    std::uint32_t function{},handle{},bytes{};
};
struct IgbTypeRegistrationOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    std::shared_ptr<ModelSceneObject> object_result;
    std::shared_ptr<IgbRegistrationCallbackList> callbacks_result;
};
using IgbTypeRegistrationFactory=std::function<std::shared_ptr<IgbTypeRegistrationOperation>(const IgbTypeRegistrationCall&)>;
struct IgbTypeRegistrationServices {
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    std::optional<std::uint32_t> metadata_pool_handle; //805621F4.
    IgbTypeRegistrationFactory factory;
private:
    friend class IgbTypeRegistrationInvocation;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> unfinished_callbacks_;
};

// Actual80066204. Registration publication is not inferred from a type name or
// from prepared lookup metadata; all reached construction endpoints must run.
class IgbTypeRegistrationInvocation {
public:
    explicit IgbTypeRegistrationInvocation(IgbTypeRegistrationArguments);
    ~IgbTypeRegistrationInvocation();
    IgbTypeRegistrationInvocation(const IgbTypeRegistrationInvocation&)=delete;
    IgbTypeRegistrationInvocation& operator=(const IgbTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbTypeRegistrationServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
