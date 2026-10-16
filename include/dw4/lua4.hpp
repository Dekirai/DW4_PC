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
    friend class Lua4Execution;
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
    std::uint64_t handle_{}; // Arena slot (low 32 bits) and its reuse generation (high 32 bits).
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
    std::uint64_t execution_id{}, call_id{}; // Stable across explicit incomplete retries.
};
using Lua4HostHandler = std::function<std::vector<Lua4Value>(const Lua4HostCall&)>;
struct Lua4HostResult {
    bool completed{};
    std::vector<Lua4Value> returned; // Must be empty while incomplete; imported once on completion.
    std::string diagnostic; // Incomplete diagnostics are bounded to 4096 bytes.
};
using Lua4ResumableHostHandler = std::function<Lua4HostResult(const Lua4HostCall&)>;

struct Lua4Limits {
    std::size_t max_instructions = 1000000; // Per root invocation, including nested chunks.
    // Lua frames alive at once (a port guard; every frame takes at least one
    // stack slot, so the 1024-slot stack below binds first).
    std::size_t max_call_depth = 1024;
    // The original's one Lua stack in slots: igLuaState 80272594 calls
    // lua_open(0, pool) (8027E9D4), so f_luaopen 8027E924 takes the default
    // 1024 (L->stacksize). Chunks, Lua frames and C calls share it through
    // absolute positions; luaD_checkstack (80278088) raises "stack
    // overflow" (docs/LUA4.md, Stack).
    std::size_t stack_size = 1024;
    // Live Lua data after a garbage collection (the Lua 4.0.1 nblocks model);
    // above it the VM stops with "not enough memory" like the original's
    // allocator failure (luaM_realloc 8027BD18 -> luaD_breakrun LUA_ERRMEM).
    std::size_t max_heap_bytes = 32u * 1024u * 1024u;
    std::size_t max_objects = 65536; // Live tables + closures (a collection runs before failing).
    std::size_t max_table_entries = 262144; // Live table entries (a collection runs before failing).
    std::size_t max_globals = 4096;
    std::size_t max_host_calls = 8192; // Logical calls per root; retry does not charge again.
    // Per root invocation, like max_instructions: bytes of string comparison,
    // key lookup and number conversion work (a CPU guard, not memory).
    std::size_t max_string_work = 32u * 1024u * 1024u;
};

// The collector's view of a VM (Lua 4.0.1 lgc.c accounting).
struct Lua4HeapStats {
    std::size_t live_bytes{}; // nblocks: live data at the last collection + allocations since.
    std::size_t threshold{}; // GCthreshold: 2 x live after a collection (luaC_collectgarbage).
    std::size_t collections{};
    std::size_t objects{}; // Tables + closures not yet swept (garbage included until a collection).
    std::size_t table_entries{};
    // Highest top + reserve any luaD_checkstack check of this VM asked for;
    // a check fails once this reaches stack_size - 1.
    std::size_t stack_peak{};
};

enum class Lua4RunStop { completed, host_required, faulted };
struct Lua4RunResult {
    bool completed{};
    std::string diagnostic;
    Lua4Site site;
    std::size_t instructions{};
    std::size_t host_calls{};
    std::vector<Lua4Value> returned;
    Lua4RunStop stop = Lua4RunStop::faulted;
};

// One retained invocation, owning program/frame/argument/result state. An
// incomplete handle must be resumed: abandoning it terminally faults its VM.
class Lua4Execution {
public:
    Lua4Execution(Lua4Execution&&) noexcept;
    Lua4Execution& operator=(Lua4Execution&&) noexcept;
    ~Lua4Execution();
    std::uint64_t id() const noexcept;
    bool completed() const noexcept;
private:
    friend class Lua4Vm;
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit Lua4Execution(std::shared_ptr<Impl> impl);
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
    // The first missing handler may be installed while paused. Once present,
    // its identity cannot be replaced until the root invocation completes.
    void set_resumable_host_handler(Lua4ResumableHostHandler handler);
    void set_global(std::string name, const Lua4Value& value);
    Lua4Value get_global(std::string_view name) const;
    Lua4Value table_get(const Lua4Value& table, const Lua4Value& key) const;
    // Mark & sweep like Lua 4.0.1 luaC_collectgarbage. Runs on its own at
    // instruction boundaries once the live estimate reaches the threshold;
    // this is collectgarbage() (lua_setgcthreshold 0), only between roots.
    // A table or closure value the host kept without a global or a running
    // frame referring to it is invalid after a collection.
    void collect_garbage();
    Lua4HeapStats heap_stats() const;
    // A script error (a Lua runtime error, stack overflow, out of memory or a port guard)
    // ends only its invocation, like lua_dobuffer's protected call: the
    // VM, its globals and any parent invocation go on. Only host or API
    // faults (callback exceptions, malformed host results, abandoned
    // invocations) stop the VM for good; then a fresh VM is needed.
    bool stopped() const noexcept;
    Lua4RunResult execute(const Lua4Program& program);
    // Exactly one pending root per VM. Existing execute() stays synchronous
    // and uses only the ordinary handler; this API explicitly opts into pauses.
    Lua4Execution begin_execution(const Lua4Program& program);
    // Only inside a resumable host callback. A pending child can be resumed
    // only from that same logical callback; completed children allow siblings.
    Lua4Execution begin_nested(const Lua4Program& program);
    Lua4RunResult resume(Lua4Execution& execution);
private:
    friend struct Lua4Execution::Impl;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace dw4
