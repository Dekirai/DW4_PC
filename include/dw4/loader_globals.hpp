#pragma once

#include "dw4/igb_dependencies.hpp"
#include "dw4/igb_runtime_types.hpp"

namespace dw4 {
class LoaderGlobalsError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Actual8007042C reads80562100, then captured igArkCore+40. It does not
// consult the separate direct loader global80562104 or create a loader.
class LoaderGlobalInvocation {
public:
    LoaderGlobalInvocation();
    ~LoaderGlobalInvocation();
    LoaderGlobalInvocation(const LoaderGlobalInvocation&)=delete;
    LoaderGlobalInvocation& operator=(const LoaderGlobalInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRuntimeTypesServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<SystemStageLoader> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

// Binds only IgbDependenciesServiceKind::loader to the original getter.
// The canonical services object can acquire its represented globals later;
// each selected operation retains its own getter continuation.
IgbDependenciesFactory make_native_loader_globals_factory(
    std::shared_ptr<IgbRuntimeTypesServices>,IgbDependenciesFactory remaining={});
} // namespace dw4
