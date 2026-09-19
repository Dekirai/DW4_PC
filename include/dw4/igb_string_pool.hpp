#pragma once

#include "dw4/igb_dependencies.hpp"

namespace dw4 {
class IgbStringPoolError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Header identity and physical character bytes remain distinct. Allocation does
// not initialize the header: actual80073204 writes container/refcount later.
struct IgbStringEntry {
    std::shared_ptr<IgbInternedString> owner;
    IgbRuntimeNameSource bytes;
};
struct IgbStringTable {
    using Storage=std::vector<std::shared_ptr<IgbStringEntry>>;
    std::uint32_t count{},step{},capacity{}; // +04/+08/+0C.
    std::shared_ptr<Storage> storage; // +10, live at each original read.
};
struct IgbStringPoolState {
    std::shared_ptr<IgbStringTable> table; // +00.
    std::shared_ptr<ModelSceneObject> arena; // +04, concrete80073738 still required.
    bool faulted() const noexcept {return fault_;}
private:
    friend class IgbStringInternInvocation;
    bool fault_{};
    std::vector<std::shared_ptr<void>> unreleased_;
};
struct IgbStringPoolBinding {
    std::weak_ptr<ModelSceneObject> owner;
    std::shared_ptr<IgbStringPoolState> state;
};
enum class IgbStringPoolServiceKind {
    allocate_string, //80073738(captured arena, strlen+1, container/header outputs).
    resize_table     //8005624C(captured physical storage, captured capacity*8).
};
struct IgbStringPoolCall {
    IgbStringPoolServiceKind kind{};
    std::shared_ptr<ModelSceneObject> arena;
    std::shared_ptr<IgbStringTable> table;
    std::shared_ptr<IgbStringTable::Storage> storage;
    std::uint32_t bytes{},capacity{};
};
struct IgbStringPoolOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbStringEntry> entry_result;
    std::shared_ptr<ModelSceneObject> container_result;
    std::shared_ptr<IgbStringTable::Storage> storage_result;
};
using IgbStringPoolFactory=std::function<std::shared_ptr<IgbStringPoolOperation>(const IgbStringPoolCall&)>;
struct IgbStringPoolServices {
    std::vector<IgbStringPoolBinding> pools;
    std::shared_ptr<IgbInternedStringRegistry> strings=std::make_shared<IgbInternedStringRegistry>();
    // Global805622F4. Missing storage pauses; engaged null means no locking.
    std::optional<std::shared_ptr<LoaderDirectoryMutex>> mutex;
    IgbStringPoolFactory factory;
};
void igb_bind_string_pool(const std::shared_ptr<IgbStringPoolServices>&,
    const std::shared_ptr<ModelSceneObject>&,const std::shared_ptr<IgbStringPoolState>&);

struct IgbStringSearchResult {
    std::shared_ptr<IgbStringEntry> entry;
    std::uint32_t index{};
};
// Actual80073448. A match increments the full header reference word, even when
// this helper is reached by insertion's second search. It does not lock.
IgbStringSearchResult igb_string_table_search(const std::shared_ptr<IgbStringTable>&,
    const IgbDependencyName&);

// Actual80054094, including73164/73448 and73204/73574. Unknown pools, arena
// allocation and realloc are explicit boundaries. Pool construction, container
// release and global initialization are not implied by a binding.
class IgbStringInternInvocation {
public:
    IgbStringInternInvocation(std::shared_ptr<ModelSceneObject>,IgbDependencyName);
    ~IgbStringInternInvocation();
    IgbStringInternInvocation(const IgbStringInternInvocation&)=delete;
    IgbStringInternInvocation& operator=(const IgbStringInternInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbStringPoolServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<IgbInternedString> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbDependenciesFactory make_native_igb_string_factory(std::shared_ptr<IgbStringPoolServices>,
    IgbDependenciesFactory remaining={});
} // namespace dw4
