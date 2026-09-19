#pragma once

#include "dw4/loader_directory.hpp"

namespace dw4 {
class LoaderRequestError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
struct IgbDecodeState;

// Reached igIGBFile fields. This is an owner representation, not an original
// factory: original reflected defaults and post-default pool initialization
// must be supplied by the concrete create_igb_file operation.
struct LoaderRequestFile : SystemStageDirectory {
    std::uint8_t format_24{},option_41{},succeeded_31{},initialize_after_load_115{};
    std::uint32_t option_a4{};
    std::shared_ptr<ModelSceneObject> pool_118,pool_11c,pool_120;
    // Concrete decoder fields, supplied by reflected construction or explicit
    // preparation. Absence does not imply an initialized empty igIGBFile.
    std::shared_ptr<IgbDecodeState> igb_decode;
private:
    friend class LoaderFileLoadInvocation;
    friend class IgbDecodeInvocation;
    friend class IgbFilePoolsInvocation;
    std::weak_ptr<void> active_load_;
    std::weak_ptr<void> active_decode_;
    bool load_fault_{},decode_fault_{};
    bool pool_initialization_fault_{};
    std::vector<std::shared_ptr<ModelSceneObject>> unpublished_pools_;
};

enum class LoaderRequestServiceKind {
    create_igb_file,       // 8002EBEC(pool38): reflected factory and post-defaults.
    create_mutex,         // 80025FE8(null), before publication at file+44.
    initialize_mutex,     // Selected mutex virtual+5C, after publication.
    create_string_pool,   // 80054140(16), conditional80053F28.
    intern_filename,      // 80054094(captured pool, captured request filename).
    resize_directory_list,// 8004155C(list, requested capacity, element size4).
    enqueue_directory,    // Queue virtual+68(queue, file, true).
    decode_igb            // Concrete igIGBFile virtual+74=800528DC, not just parsing.
};
struct LoaderRequestCall {
    LoaderRequestServiceKind kind{};
    std::shared_ptr<SystemStageLoader> loader;
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<ModelSceneObject> object; // Factory pool, string pool or queue.
    std::shared_ptr<LoaderDirectoryMutex> mutex;
    std::shared_ptr<LoaderDirectoryList> list;
    SystemStageString filename;
    std::int32_t capacity{}; // resize_directory_list; element size is always4.
    bool queue_wait{}; // enqueue_directory always passes true.
};
struct LoaderRequestOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<LoaderRequestFile> file_result;
    std::shared_ptr<LoaderDirectoryMutex> mutex_result;
    std::shared_ptr<ModelSceneObject> object_result;
    SystemStageString string_result;
    std::int32_t load_result{}; // Signed original virtual+74 result.
};
using LoaderRequestFactory=std::function<std::shared_ptr<LoaderRequestOperation>(const LoaderRequestCall&)>;
struct LoaderRequestServices {
    std::shared_ptr<LoaderDirectoryServices> directory;
    // Canonical singleton state shared with SystemStageInvocation, including
    // string_pool. A missing owner pauses; a represented null pool is distinct.
    std::shared_ptr<SystemStageServices> globals;
    LoaderRequestFactory factory;
};

// Actual80070018: primary prefix, secondary prefix, bare filename. A positive
// signed decoder result suppresses retries; the final result is ignored.
class LoaderFileLoadInvocation {
public:
    LoaderFileLoadInvocation(std::shared_ptr<SystemStageLoader>,std::shared_ptr<SystemStageDirectory>);
    ~LoaderFileLoadInvocation();
    LoaderFileLoadInvocation(const LoaderFileLoadInvocation&)=delete;
    LoaderFileLoadInvocation& operator=(const LoaderFileLoadInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<LoaderRequestServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<SystemStageDirectory> result() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};

// Actual8006FD34. Cache hits acquire one loading claim. New files are initialized
// and published before synchronous load or the async second claim/queue call.
// Missing factories, queue execution and the concrete decoder stay pending.
class LoaderDirectoryRequestInvocation {
public:
    LoaderDirectoryRequestInvocation(std::shared_ptr<SystemStageLoader>,SystemStageString);
    ~LoaderDirectoryRequestInvocation();
    LoaderDirectoryRequestInvocation(const LoaderDirectoryRequestInvocation&)=delete;
    LoaderDirectoryRequestInvocation& operator=(const LoaderDirectoryRequestInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<LoaderRequestServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<SystemStageDirectory> result() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
