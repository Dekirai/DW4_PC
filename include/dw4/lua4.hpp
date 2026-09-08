#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {
class Lua4Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Lua4ReadLimits {
    std::size_t max_bytes = 4u * 1024u * 1024u;
    std::size_t max_instructions = 262144;
    std::size_t max_functions = 4096;
    std::size_t max_string_bytes = 2u * 1024u * 1024u;
    std::size_t max_depth = 32;
};

struct Lua4ProgramInfo {
    std::string label;
    std::size_t functions{};
    std::size_t instructions{};
    std::size_t string_bytes{};
};

class Lua4Program {
public:
    Lua4Program() = default;
    const Lua4ProgramInfo& info() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend Lua4Program read_lua4_chunk(std::span<const std::uint8_t>, std::string_view, Lua4ReadLimits);
    friend class Lua4Vm;
};

// Reads only the verified Lua 4.0 32-bit-instruction/64-bit-number chunk profile.
// Both byte orders are decoded explicitly. No source compiler or file loader.
Lua4Program read_lua4_chunk(std::span<const std::uint8_t> bytes,
    std::string_view label = "chunk", Lua4ReadLimits limits = {});

enum class Lua4Type { Nil, Number, String, Table, Function, HostObject, HostFunction };

class Lua4Value {
public:
    Lua4Value() = default;
    static Lua4Value number(double value);
    static Lua4Value string(std::string value);
    static Lua4Value host_object(std::string name);
    static Lua4Value host_function(std::string name);
    Lua4Type type() const noexcept { return type_; }
    bool truthy() const noexcept { return type_ != Lua4Type::Nil; }
    double as_number() const;
    std::string_view as_string() const;
    std::string_view host_name() const;
    std::string describe() const;
private:
    Lua4Type type_ = Lua4Type::Nil;
    double number_{};
    std::shared_ptr<const std::string> text_;
    std::uint64_t owner_{};
    std::size_t handle_{};
    friend class Lua4Vm;
};

struct Lua4Site {
    std::string source; // Caller label; the embedded source string is untrusted debug metadata.
    std::string prototype; // "main", "main/0", etc.
    std::size_t pc{}; // Zero-based instruction index.
};

struct Lua4HostCall {
    std::string name; // Exact symbolic name, e.g. "be.CFLclear".
    std::span<const Lua4Value> arguments; // A colon call includes the receiver at index zero.
    Lua4Site site;
};
using Lua4HostHandler = std::function<std::vector<Lua4Value>(const Lua4HostCall&)>;

struct Lua4Limits {
    std::size_t max_instructions = 1000000; // Per execute call.
    std::size_t max_call_depth = 64;
    std::size_t max_stack_values = 16384;
    std::size_t max_heap_bytes = 32u * 1024u * 1024u; // Cumulative allocation/string-work allowance.
    std::size_t max_objects = 65536;
    std::size_t max_table_entries = 262144;
    std::size_t max_globals = 4096;
    std::size_t max_host_calls = 8192; // Per execute call, including the stopped call.
};

struct Lua4RunResult {
    bool completed{};
    std::string diagnostic;
    Lua4Site site;
    std::size_t instructions{};
    std::size_t host_calls{};
    std::vector<Lua4Value> returned;
};

class Lua4Vm {
public:
    explicit Lua4Vm(Lua4Limits limits = {});
    ~Lua4Vm();
    Lua4Vm(const Lua4Vm&) = delete;
    Lua4Vm& operator=(const Lua4Vm&) = delete;
    Lua4Vm(Lua4Vm&&) noexcept;
    Lua4Vm& operator=(Lua4Vm&&) noexcept;
    void set_host_handler(Lua4HostHandler handler);
    void set_global(std::string name, const Lua4Value& value);
    Lua4Value get_global(std::string_view name) const;
    Lua4Value table_get(const Lua4Value& table, const Lua4Value& key) const;
    Lua4RunResult execute(const Lua4Program& program);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
