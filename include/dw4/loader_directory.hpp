#pragma once

#include "dw4/system_stage.hpp"

#include <optional>

namespace dw4 {
class LoaderDirectoryError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

struct LoaderDirectoryList {
    using Storage=std::vector<std::shared_ptr<SystemStageDirectory>>;
    std::int32_t count{},capacity{}; // +08/+0C, independent of physical storage.
    std::shared_ptr<Storage> storage=std::make_shared<Storage>(); // +10.
};
struct LoaderDirectoryMutex {
    // Original +84 with argument true and +7C. The value returned by acquire
    // is inspected only by claim acquisition; claim release ignores it.
    std::function<MessengerCallResult()> acquire,release;
};
struct LoaderDirectoryNameField {std::uint32_t offset{0x14};};
struct LoaderDirectoryRootOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<SystemStageDirectory> result;
};
struct LoaderDirectoryServices {
    // Original global80561CB0. A represented null descriptor returns no match.
    std::shared_ptr<LoaderDirectoryNameField> filename_field;
    // Original global805622D0, loaded after mutex acquisition completes.
    // Absence leaves that reached comparison pending.
    std::optional<std::uint32_t> mutex_success;
    // Actual8006F5E0, including lazy igImageDirectory creation and its final
    // 80098E7C call. An existing root alone does not complete this operation.
    std::function<std::shared_ptr<LoaderDirectoryRootOperation>(
        const std::shared_ptr<SystemStageLoader>&)> root_directory;
};

class LoaderDirectoryLookupInvocation {
public:
    LoaderDirectoryLookupInvocation(std::shared_ptr<SystemStageLoader>,SystemStageString);
    ~LoaderDirectoryLookupInvocation();
    LoaderDirectoryLookupInvocation(const LoaderDirectoryLookupInvocation&)=delete;
    LoaderDirectoryLookupInvocation& operator=(const LoaderDirectoryLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<LoaderDirectoryServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<SystemStageDirectory> result() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};

enum class LoaderDirectoryClaim {acquire,release};
class LoaderDirectoryClaimInvocation {
public:
    LoaderDirectoryClaimInvocation(std::shared_ptr<SystemStageDirectory>,LoaderDirectoryClaim);
    ~LoaderDirectoryClaimInvocation();
    LoaderDirectoryClaimInvocation(const LoaderDirectoryClaimInvocation&)=delete;
    LoaderDirectoryClaimInvocation& operator=(const LoaderDirectoryClaimInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<LoaderDirectoryServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::int32_t value() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};

// Actual80070140: lookup, capture root identity, supply a missing parent,
// release its loading claim, conditionally clear the live root, drop temporary.
class LoaderDirectoryReleaseInvocation {
public:
    LoaderDirectoryReleaseInvocation(std::shared_ptr<SystemStageLoader>,SystemStageString);
    ~LoaderDirectoryReleaseInvocation();
    LoaderDirectoryReleaseInvocation(const LoaderDirectoryReleaseInvocation&)=delete;
    LoaderDirectoryReleaseInvocation& operator=(const LoaderDirectoryReleaseInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<LoaderDirectoryServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
