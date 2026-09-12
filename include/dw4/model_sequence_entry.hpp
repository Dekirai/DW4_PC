#pragma once

#include "dw4/messenger.hpp"
#include "dw4/model_subcommands.hpp"
#include "dw4/model_sequence.hpp"

#include <memory>

namespace dw4 {
class ActionEventRuntime;
class ModelPlayerWork;
struct ModelSequenceEntryServices {
    ModelSubcommandServices subcommands;
    ModelLuaDispatch lua;
};

// The ordinary base-controller entry through its actual command-loop return.
// The caller still owes player +B8 afterward, including after a WCHECK return.
// The entry retains its exact work/executor generation through host suspension.
class ModelSequenceEntry {
public:
    ~ModelSequenceEntry();
    ModelSequenceEntry(const ModelSequenceEntry&)=delete;
    ModelSequenceEntry& operator=(const ModelSequenceEntry&)=delete;
    MessengerCallResult resume(ActionEventRuntime&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelPlayerWork>& captured_work() const noexcept;
private:
    friend class ActionEventRuntime;
    struct Impl;
    explicit ModelSequenceEntry(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
