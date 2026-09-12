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

// Retained actual command-loop transport. Ordinary entry additionally performs
// selection/cleanup before C3AC; its caller owes player +B8. Direct wait resume
// enters C3AC alone and owes no +B8. Both retain their exact work generation.
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
using ModelSequenceResume=ModelSequenceEntry;
} // namespace dw4
