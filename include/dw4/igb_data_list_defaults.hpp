#pragma once

#include "dw4/igb_data_list_registration.hpp"
#include "dw4/igb_list_defaults.hpp"

namespace dw4 {
class IgbDataListDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbDataListDefaultsServices {
    IgbDataListDefaultsServices();
    std::shared_ptr<IgbDataListRegistrationServices> registration;
    // Reuse the actual batch/schema, descriptor name lookup, clone dispatch
    // and6588C replacement services. Internal-list globals remain separate.
    std::shared_ptr<IgbListDefaultsServices> lists;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbRuntimeFieldSlot> unsigned_char_prototype; // Canonical805614D4.
    std::shared_ptr<IgbRuntimeFieldSlot> nonref_data; // Canonical80561734.
    std::shared_ptr<IgbRuntimeFieldSlot> ref_data; // Canonical8056170C.
    std::shared_ptr<IgbFieldFactoryTable> factories; //80467478.
    std::shared_ptr<IgbFieldNameTable> names; //80467484.
    std::shared_ptr<IgbFieldGlobalTable> globals; //80467490.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //8046749C.
    std::vector<std::shared_ptr<void>> retained;
    std::shared_ptr<IgbRuntimeFieldSlot> string_ref_data; // Canonical80561590.
    std::shared_ptr<IgbRuntimeFieldSlot> signed_int_data; // Canonical80561DC8.
};
enum class IgbDataListPrototypeKind {unsigned_char,object,string,signed_int};
// Exact23358/28BFC/256C0/2E4D8: test the distinct prototype cell,
// conditionally execute its wrapper through66188, then reload that cell.
// Registrations use the existing field-class/prototype adapters when bound.
class IgbDataListPrototypeInvocation {
public:
    explicit IgbDataListPrototypeInvocation(IgbDataListPrototypeKind);
    ~IgbDataListPrototypeInvocation();
    IgbDataListPrototypeInvocation(const IgbDataListPrototypeInvocation&)=delete;
    IgbDataListPrototypeInvocation& operator=(const IgbDataListPrototypeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDataListDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual33AA4/29724/2910C/24D24/3673C. The data-list caller appends three fields;
// specializations clone inherited _data. Only29724 clears cloned flag+42.
class IgbDataListDefaultsInvocation {
public:
    explicit IgbDataListDefaultsInvocation(IgbDataListKind);
    ~IgbDataListDefaultsInvocation();
    IgbDataListDefaultsInvocation(const IgbDataListDefaultsInvocation&)=delete;
    IgbDataListDefaultsInvocation& operator=(const IgbDataListDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDataListDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_data_list_defaults_factory(
    std::shared_ptr<IgbDataListDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
