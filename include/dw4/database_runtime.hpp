#pragma once

#include "dw4/controllers.hpp"
#include "dw4/database.hpp"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace dw4 {
class ActionEventRuntime;
struct DatabaseRamDefinition {
    std::uint32_t reference{},owner_reference{};
    std::int32_t authored_index{};
    std::uint8_t active{}; // The original byte is preserved, including values above one.
    std::array<std::int32_t,4> authored_words{};
    std::optional<std::uint32_t> data_reference;
};
struct DatabaseInfoDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    std::uint8_t active{};
    std::optional<std::uint32_t> data_reference;
    DatabaseListIdentity ram_list;
    std::vector<DatabaseRamDefinition> rams;
    std::optional<std::uint32_t> database_reference;
    std::shared_ptr<const DatabaseDefinition> database; // Null is an authored null object.
};

// All exact beDBManagerInfo exports in one loaded document, in export order.
// Physical database aliases share one typed owner; raw info/RAM occurrences survive.
class DatabaseManagerProgram {
public:
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    const std::vector<DatabaseInfoDefinition>& infos() const noexcept;
private:
    struct Data;
    explicit DatabaseManagerProgram(std::shared_ptr<const Data> data):data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend DatabaseManagerProgram decode_database_managers(const IgbDocument&,std::string,std::uint32_t);
};
DatabaseManagerProgram decode_database_managers(const IgbDocument& document,
    std::string source,std::uint32_t source_occurrence);

struct DatabaseRamState {
    ControllerObjectId id,manager,info;
    std::int32_t index{};
    std::uint8_t active{};
    std::array<std::int32_t,4> words{}; // Generic initialization resets all four.
    std::shared_ptr<const DatabaseDefinition> database;
};

// One native beDBManager attached to the existing ActionEventRuntime messenger.
// Registration retains its owners even when this inspection facade is destroyed.
class DatabaseRuntime {
public:
    explicit DatabaseRuntime(ActionEventRuntime& runtime);
    ~DatabaseRuntime();
    DatabaseRuntime(const DatabaseRuntime&)=delete;
    DatabaseRuntime& operator=(const DatabaseRuntime&)=delete;
    DatabaseRuntime(DatabaseRuntime&&)=delete;
    DatabaseRuntime& operator=(DatabaseRuntime&&)=delete;
    void register_source(DatabaseManagerProgram program);
    ControllerObjectId manager_id() const noexcept;
    const std::vector<DatabaseManagerProgram>& sources() const noexcept;
    // Accepts an actual raw or canonical RAM reference from a registered source.
    const DatabaseRamState& ram(ControllerObjectId id) const;
    // Explicit prepared RAM state; this does not infer any original activation.
    void set_active(ControllerObjectId id,std::uint8_t active);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    ActionEventRuntime& runtime_;
};
} // namespace dw4
