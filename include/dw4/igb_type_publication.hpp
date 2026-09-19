#pragma once

#include "dw4/igb_pointer_list.hpp"

namespace dw4 {
class IgbTypePublicationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbTypePublicationServices {
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    std::shared_ptr<IgbPointerListServices> pointer_lists;
    // A reached 53E6C retain is not undone by abandoning a native continuation.
    std::vector<std::shared_ptr<void>> unfinished;
};

// Actual80053998. Count and backing are captured once. The explicit canonical
// pointer-owner mapping is authoritative, as in the existing runtime lookup;
// vector length is a physical bound, never the original logical count.
std::int32_t igb_runtime_type_index(const std::shared_ptr<IgbRuntimeTypeList>&,
    const std::shared_ptr<IgbRuntimeMetaType>&);

// Actual800660AC -> 8003E450 -> 80053E6C -> native80053BC0. There is no enabled
// gate, name lookup, or implicit allocator. A nullable input reaches the index
// search before its required metadata+0C store.
class IgbTypePublicationInvocation {
public:
    explicit IgbTypePublicationInvocation(std::shared_ptr<IgbRuntimeMetaType>);
    ~IgbTypePublicationInvocation();
    IgbTypePublicationInvocation(const IgbTypePublicationInvocation&)=delete;
    IgbTypePublicationInvocation& operator=(const IgbTypePublicationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbTypePublicationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_type_publication_factory(
    std::shared_ptr<IgbTypePublicationServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
