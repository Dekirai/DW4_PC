#pragma once

#include "dw4/igb_loading_paths.hpp"

#include <variant>

namespace dw4 {
class IgbDependenciesError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Original interned data has a container pointer at data-8 and a full 32-bit
// reference count at data-4. Host string lifetime is not that reference count.
struct IgbInternedString {
    SystemStageString value;
    // Native interning keeps the original character allocation authoritative.
    // value is its host identity/cache and must not validate bytes eagerly.
    std::optional<IgbRuntimeNameSource> native_name;
    std::shared_ptr<ModelSceneObject> container;
    std::uint32_t references{1};
    bool released{};
};
struct IgbInternedStringRegistry {std::vector<std::shared_ptr<IgbInternedString>> owners;};
void igb_bind_interned_string(const std::shared_ptr<IgbInternedStringRegistry>&,
    const std::shared_ptr<IgbInternedString>&);
// monostate is an actual null pointer; other variants retain a nonnull original
// pointer without eagerly reading its C string.
using IgbDependencyName=std::variant<std::monostate,SystemStageString,IgbRuntimeNameSource>;
SystemStageString igb_dependency_name(const IgbDependencyName&);
IgbDependencyName igb_interned_string_name(const std::shared_ptr<IgbInternedString>&);

struct IgbDependenciesState {
    std::shared_ptr<IgbScratchBuffer> body; // +E0.
    std::uint32_t body_bytes{}; // +E4.
    std::shared_ptr<IgbInternedString> saved_prefix; // +E8.
    std::shared_ptr<LoaderDirectoryList> directories; // Directory+18.
    bool faulted() const noexcept {return fault_;}
private:
    friend class IgbDependenciesInvocation;
    std::weak_ptr<void> active_;
    bool fault_{};
    std::vector<std::shared_ptr<void>> unreleased_;
    std::vector<SystemStageString> unreleased_values_;
};
enum class IgbDependenciesServiceKind {
    allocate_body,       // Selected pool virtual+DC(body bytes,alignment16).
    free_body,           // Selected pool virtual+108(body).
    loader,              // Actual ArkCore+40 getter8007042C, only for null file+20.
    create_string_pool,  // 80054140(16), result published after conditional init.
    initialize_string_pool, // 80053F28(nonnull factory result).
    intern_name,         // 80054094(captured pool,captured C-string pointer).
    release_string,      // 8007381C(container,string header), after full count reaches0.
    load_dependency,     // Actual synchronous8006FA6C, distinct from8006FD34.
    reserve_directories  // 8004155C selected by append growth80041660, width4.
};
struct IgbDependenciesCall {
    IgbDependenciesServiceKind kind{};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbStreamState> stream;
    std::shared_ptr<IgbDependenciesState> state;
    std::shared_ptr<SystemStageLoader> loader;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbScratchBuffer> buffer;
    std::shared_ptr<IgbInternedString> string;
    std::shared_ptr<LoaderDirectoryList> directories;
    IgbDependencyName name;
    std::uint32_t bytes{},alignment{};
    std::int32_t capacity{};
};
struct IgbDependenciesOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbScratchBuffer> buffer_result;
    std::shared_ptr<SystemStageLoader> loader_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    std::shared_ptr<IgbInternedString> string_result;
    std::shared_ptr<SystemStageDirectory> directory_result;
};
using IgbDependenciesFactory=std::function<std::shared_ptr<IgbDependenciesOperation>(const IgbDependenciesCall&)>;
struct IgbDependenciesServices {
    // Shares the existing actual singleton80562140 string-pool field.
    std::shared_ptr<SystemStageServices> globals;
    std::shared_ptr<IgbInternedStringRegistry> strings=std::make_shared<IgbInternedStringRegistry>();
    IgbDependenciesFactory factory;
};

// Explicitly bound native scratch allocation/free only; unknown identities
// delegate or remain pending. Other engine operations are never auto-success.
IgbDependenciesFactory make_native_igb_dependency_pool_factory(IgbDependenciesFactory remaining={});

class IgbDependenciesInvocation {
public:
    IgbDependenciesInvocation(std::shared_ptr<LoaderRequestFile>,std::shared_ptr<IgbStreamState>,
        std::shared_ptr<IgbLoadingPathsState>,std::shared_ptr<IgbDependenciesState>);
    ~IgbDependenciesInvocation();
    IgbDependenciesInvocation(const IgbDependenciesInvocation&)=delete;
    IgbDependenciesInvocation& operator=(const IgbDependenciesInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDependenciesServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::uint32_t value() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
