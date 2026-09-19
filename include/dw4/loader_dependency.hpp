#pragma once

#include "dw4/igb_dependencies.hpp"

namespace dw4 {
class LoaderDependencyError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

enum class LoaderDependencyServiceKind {
    object_pool,          //80068430 reads owner+04 high byte, then8005929C(handle).
    create_igb_file,      //8002EBEC(captured pool), including actual defaults.
    destroy_object,      //80066E1C, after the packed low23 count reaches zero.
    create_string_pool,  //80054140(16).
    initialize_string_pool, //80053F28(nonnull allocation).
    intern_name,          //80054094(captured pool, original request pointer).
    release_string,       //8007381C(container, header), full32 count zero.
    reserve_directories   //8004155C(list, capacity, width4).
};
struct LoaderDependencyCall {
    LoaderDependencyServiceKind kind{};
    std::shared_ptr<SystemStageLoader> loader;
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<ModelSceneObject> object; // Pool or ordinary destructor owner.
    std::shared_ptr<SystemStageDirectory> directory; // Directory destructor owner.
    std::shared_ptr<LoaderDirectoryList> list;
    std::shared_ptr<IgbInternedString> string;
    IgbDependencyName name;
    std::uint32_t pool_handle{}; // Captured big-endian byte at loader+04.
    std::int32_t capacity{};
};
struct LoaderDependencyOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> object_result;
    std::shared_ptr<LoaderRequestFile> file_result;
    std::shared_ptr<IgbInternedString> string_result;
};
using LoaderDependencyFactory=std::function<std::shared_ptr<LoaderDependencyOperation>(const LoaderDependencyCall&)>;
struct LoaderDependencyServices {
    std::shared_ptr<LoaderDirectoryServices> directory;
    std::shared_ptr<LoaderRequestServices> request; // Actual80070018 decoder services.
    std::shared_ptr<SystemStageServices> globals;
    std::shared_ptr<IgbInternedStringRegistry> strings=std::make_shared<IgbInternedStringRegistry>();
    LoaderDependencyFactory factory;
};

// Adapts the same canonical string operations used by800521EC. It does not
// supply factories, initializers, allocation or container destruction itself.
LoaderDependencyFactory make_loader_dependency_string_factory(IgbDependenciesFactory,
    LoaderDependencyFactory remaining={});
IgbDependenciesFactory make_native_loader_dependency_factory(std::shared_ptr<LoaderDependencyServices>,
    IgbDependenciesFactory remaining={});

// Actual8006FA6C. Cache hits drop their lookup temporary before acquiring one
// loading claim. Misses always call80070018 synchronously, regardless of +1C.
// Different invocations may nest on one loader during synchronous decoding.
class LoaderDependencyInvocation {
public:
    LoaderDependencyInvocation(std::shared_ptr<SystemStageLoader>,IgbDependencyName);
    ~LoaderDependencyInvocation();
    LoaderDependencyInvocation(const LoaderDependencyInvocation&)=delete;
    LoaderDependencyInvocation& operator=(const LoaderDependencyInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<LoaderDependencyServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<SystemStageDirectory> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
