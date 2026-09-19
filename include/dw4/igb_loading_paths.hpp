#pragma once

#include "dw4/igb_stream.hpp"
#include "dw4/igb_type_mapping.hpp"

namespace dw4 {
class IgbLoadingPathsError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
struct IgbMemoryPoolsServices;
using IgbPoolHandleList=IgbMappingList<std::uint32_t>;
struct IgbLoadingPathsState {
    std::shared_ptr<IgbScratchBuffer> directory_path; // File+C8, mutable duplicated C string.
    std::shared_ptr<IgbPoolHandleList> pool_handles; // File+130, original igIntList.
    bool path_faulted() const noexcept {return path_fault_;}
    bool pools_faulted() const noexcept {return pools_fault_;}
private:
    friend class IgbDirectoryPathInvocation;
    friend class IgbPoolNamesInvocation;
    std::weak_ptr<void> active_path_,active_pools_;
    bool path_fault_{},pools_fault_{};
    // Raw replacement and interrupted allocation do not perform an original free.
    std::vector<std::shared_ptr<IgbScratchBuffer>> unreleased_;
};
enum class IgbLoadingPathsServiceKind {
    duplicate_path,          // Captured pool virtual+F0(stream filename).
    allocate_pool_names,     // Captured pool virtual+D8(total bytes, including header).
    free_pool_names,         // Live pool virtual+108(captured allocation).
    create_pool_handle_list, // 8003657C(captured pool), creator published directly.
    reserve_pool_handle_list // 8004155C selected by actual growth80041660, width4.
};
struct IgbLoadingPathsCall {
    IgbLoadingPathsServiceKind kind{};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbStreamState> stream;
    std::shared_ptr<IgbLoadingPathsState> state;
    std::shared_ptr<ModelSceneObject> pool;
    SystemStageString filename;
    std::shared_ptr<IgbScratchBuffer> buffer;
    std::shared_ptr<IgbPoolHandleList> list;
    std::uint32_t bytes{};
    std::int32_t capacity{};
};
struct IgbLoadingPathsOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbScratchBuffer> buffer_result;
    std::shared_ptr<IgbPoolHandleList> list_result;
};
using IgbLoadingPathsFactory=std::function<std::shared_ptr<IgbLoadingPathsOperation>(const IgbLoadingPathsCall&)>;
struct IgbLoadingPathsServices {
    std::shared_ptr<IgbMemoryPoolsServices> memory_pools;
    std::optional<std::uint32_t> default_pool_handle; // Original8055D89C, reloaded after growth.
    IgbLoadingPathsFactory factory;
};

// Native storage operations for explicitly bound scratch pools only. The list
// constructor/allocator remain explicit endpoints; unknown pool identities pause.
IgbLoadingPathsFactory make_native_igb_path_pool_factory(IgbLoadingPathsFactory remaining={});

// Original800505E0 duplicates the stream filename and truncates its final path
// separator. It does not prepare reflected types or consume any stream bytes.
class IgbDirectoryPathInvocation {
public:
    IgbDirectoryPathInvocation(std::shared_ptr<LoaderRequestFile>,std::shared_ptr<IgbStreamState>,
        std::shared_ptr<IgbLoadingPathsState>);
    ~IgbDirectoryPathInvocation();
    IgbDirectoryPathInvocation(const IgbDirectoryPathInvocation&)=delete;
    IgbDirectoryPathInvocation& operator=(const IgbDirectoryPathInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbLoadingPathsServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::uint32_t value() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Original80052554 reads pool names, resolves their handles, pads to at least46
// entries and frees the temporary allocation on each reached original path.
class IgbPoolNamesInvocation {
public:
    IgbPoolNamesInvocation(std::shared_ptr<LoaderRequestFile>,std::shared_ptr<IgbStreamState>,
        std::shared_ptr<IgbLoadingPathsState>);
    ~IgbPoolNamesInvocation();
    IgbPoolNamesInvocation(const IgbPoolNamesInvocation&)=delete;
    IgbPoolNamesInvocation& operator=(const IgbPoolNamesInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbLoadingPathsServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::uint32_t value() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
