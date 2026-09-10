#pragma once

#include "dw4/scripts.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {
struct ScriptRegistration {
    const IgbDocument* document{}; // Borrowed only while constructing the registry.
    std::string source_label;
    std::uint32_t source_occurrence{};
    std::uint32_t info_export_index{};
    std::uint32_t info_reference{};
};

struct ScriptRegistrationInfo {
    std::uint32_t registration_index{};
    std::string source_label;
    std::uint32_t source_occurrence{};
    std::uint32_t info_export_index{};
    std::uint32_t info_reference{};
};

enum class ScriptLookupState { no_match, match, name_barrier, unsupported_match };

struct ScriptLookupEntry {
    ScriptRegistrationInfo registration;
    ScriptLookupState state = ScriptLookupState::no_match;
    // Present for a match or barrier. Bytes are owned and immutable; a barrier
    // retains its exact row metadata but exposes no usable byte buffer.
    std::shared_ptr<const EmbeddedScript> script;
};

struct ScriptLookupPlan {
    std::string filename;
    std::vector<ScriptLookupEntry> entries; // One per explicit registration, including misses.
};

// Immutable prepared registry. Repeated registrations/export occurrences remain
// ordered. No document pointers survive construction and no chunks are executed.
// Lookup proves selection only: original dispatch return -1 can still require
// filesystem fallback even after an embedded chunk was selected or executed.
class ScriptRegistry {
public:
    explicit ScriptRegistry(std::span<const ScriptRegistration> registrations = {});
    std::span<const ScriptRegistrationInfo> registrations() const noexcept { return registrations_; }
    ScriptLookupPlan lookup(std::string_view filename) const;
private:
    std::vector<ScriptRegistrationInfo> registrations_;
    std::vector<std::vector<std::shared_ptr<const EmbeddedScript>>> scripts_;
};
} // namespace dw4
