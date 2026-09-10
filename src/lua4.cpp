#include "dw4/lua4.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <map>
#include <utility>

namespace dw4 {
namespace {
// Independently implemented from the Lua 4.0.1 format and VM specifications.
// Primary references and intentional unsupported behavior are in docs/LUA4.md.
constexpr std::size_t max_value_string = 1024u * 1024u;
constexpr std::size_t max_prototype_stack = 250;
constexpr std::size_t fields_per_flush = 62; // Lua 4.0.1: min(64, MAXSTACK / 4).
constexpr std::uint32_t signed_bias = 33554431;
constexpr std::array<std::string_view, 49> op_names{
    "END", "RETURN", "CALL", "TAILCALL", "PUSHNIL", "POP", "PUSHINT", "PUSHSTRING",
    "PUSHNUM", "PUSHNEGNUM", "PUSHUPVALUE", "GETLOCAL", "GETGLOBAL", "GETTABLE",
    "GETDOTTED", "GETINDEXED", "PUSHSELF", "CREATETABLE", "SETLOCAL", "SETGLOBAL",
    "SETTABLE", "SETLIST", "SETMAP", "ADD", "ADDI", "SUB", "MULT", "DIV", "POW",
    "CONCAT", "MINUS", "NOT", "JMPNE", "JMPEQ", "JMPLT", "JMPLE", "JMPGT", "JMPGE",
    "JMPT", "JMPF", "JMPONT", "JMPONF", "JMP", "PUSHNILJMP", "FORPREP", "FORLOOP",
    "LFORPREP", "LFORLOOP", "CLOSURE"};
[[noreturn]] void fail(const std::string& reason) { throw Lua4Error("Lua 4: " + reason); }
void limit(std::size_t value, std::size_t ceiling, std::string_view name) {
    if (!value || value > ceiling) fail(std::string(name) + " limit is outside the supported range");
}
void charge(std::size_t& remaining, std::size_t amount, std::string_view name) {
    if (amount > remaining) fail(std::string(name) + " budget exhausted");
    remaining -= amount;
}
std::string safe_label(std::string_view value) {
    if (value.size() > 4096) fail("source label exceeds 4096 bytes");
    std::string result;
    for (const unsigned char c : value) result += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
    return result;
}
struct Proto {
    std::string path;
    std::size_t parameters{}, stack{};
    bool vararg{};
    std::vector<std::shared_ptr<const std::string>> strings;
    std::vector<double> numbers;
    std::vector<std::shared_ptr<const Proto>> children;
    std::vector<std::uint32_t> code;
    std::size_t upvalues{};
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    Lua4ReadLimits limits;
    Lua4ProgramInfo info;
    std::size_t at{}, strings_remaining{}, code_remaining{}, functions_remaining{};
    std::size_t records_remaining = 1048576;
    std::size_t retained_bytes{};
    std::size_t allocation_remaining = 64u * 1024u * 1024u;
    bool little{};
    std::span<const std::uint8_t> take(std::size_t count) {
        if (count > bytes.size() - at) fail("truncated chunk at byte " + std::to_string(at));
        const auto result = bytes.subspan(at, count); at += count; return result;
    }
    std::uint8_t byte() { return take(1)[0]; }
    std::uint32_t word() {
        const auto data = take(4); std::uint32_t value{};
        for (std::size_t i = 0; i < 4; ++i) value |= std::uint32_t(data[i]) << (8 * (little ? i : 3 - i));
        return value;
    }
    double number() {
        const auto data = take(8); std::uint64_t value{};
        for (std::size_t i = 0; i < 8; ++i) value |= std::uint64_t(data[i]) << (8 * (little ? i : 7 - i));
        const auto result = std::bit_cast<double>(value);
        if (!std::isfinite(result)) fail("non-finite binary number");
        return result;
    }
    std::size_t count(std::size_t maximum, std::string_view name) {
        const auto value = word();
        if (value > maximum) fail(std::string(name) + " count exceeds its limit");
        charge(records_remaining, value, "chunk record work"); return value;
    }
    std::shared_ptr<const std::string> string(bool required = false) {
        const auto length = word();
        if (!length) {
            if (required) fail("null string constant");
            return {};
        }
        if (length > max_value_string + 1) fail("chunk string exceeds 1 MiB");
        charge(strings_remaining, length, "serialized chunk strings");
        charge(allocation_remaining, length + 128, "chunk allocation");
        const auto data = take(length);
        if (data.back() != 0) fail("chunk string has no final NUL terminator");
        info.string_bytes += length;
        return std::make_shared<const std::string>(data.begin(), data.end() - 1);
    }
    std::shared_ptr<const Proto> function(std::size_t depth, std::string path) {
        if (depth >= limits.max_depth) fail("prototype nesting depth exceeded");
        charge(functions_remaining, 1, "prototype count"); ++info.functions;
        charge(allocation_remaining, sizeof(Proto) + path.size() + 128, "chunk allocation");
        auto result = std::make_shared<Proto>(); result->path = std::move(path);
        retained_bytes += sizeof(Proto) + result->path.size() + 64;
        static_cast<void>(string()); // Embedded source is debug metadata only.
        static_cast<void>(word()); // Definition line.
        result->parameters = word();
        const auto vararg = byte(); result->stack = word();
        if (vararg > 1 || result->parameters > 100 || result->stack > max_prototype_stack ||
            result->parameters + vararg > result->stack) fail("invalid prototype parameter/vararg/stack layout");
        result->vararg = vararg != 0;
        const auto locals = count(262144, "debug local");
        charge(allocation_remaining, locals * sizeof(std::pair<std::uint32_t, std::uint32_t>), "chunk allocation");
        std::vector<std::pair<std::uint32_t, std::uint32_t>> local_ranges;
        local_ranges.reserve(locals);
        for (std::size_t i = 0; i < locals; ++i) {
            static_cast<void>(string(true));
            const auto start = word(), end = word();
            if (start > end) fail("inverted debug local range");
            local_ranges.emplace_back(start, end);
        }
        const auto lines = count(262144, "debug line");
        static_cast<void>(take(lines * 4));
        const auto strings = count(65536, "constant string");
        charge(allocation_remaining, strings * sizeof(std::shared_ptr<const std::string>), "chunk allocation");
        result->strings.reserve(strings);
        for (std::size_t i = 0; i < strings; ++i) {
            result->strings.push_back(string(true));
            retained_bytes += result->strings.back()->size() + sizeof(std::shared_ptr<const std::string>) + 96;
        }
        const auto numbers = count(262144, "constant number");
        charge(allocation_remaining, numbers * sizeof(double), "chunk allocation"); result->numbers.reserve(numbers);
        retained_bytes += numbers * sizeof(double);
        for (std::size_t i = 0; i < numbers; ++i) result->numbers.push_back(number());
        const auto children = count(functions_remaining, "child prototype");
        charge(allocation_remaining, children * sizeof(std::shared_ptr<const Proto>), "chunk allocation"); result->children.reserve(children);
        retained_bytes += children * sizeof(std::shared_ptr<const Proto>);
        for (std::size_t i = 0; i < children; ++i)
            result->children.push_back(function(depth + 1, result->path + "/" + std::to_string(i)));
        const auto instructions = count(code_remaining, "instruction");
        charge(code_remaining, instructions, "instruction storage"); info.instructions += instructions;
        charge(allocation_remaining, instructions * sizeof(std::uint32_t), "chunk allocation");
        retained_bytes += instructions * sizeof(std::uint32_t);
        result->code.reserve(instructions);
        for (std::size_t i = 0; i < instructions; ++i) result->code.push_back(word());
        if (result->code.empty() || result->code.back() != 0) fail("prototype must end with exact END instruction");
        for (const auto& range : local_ranges)
            if (range.second > instructions) fail("debug local range exceeds code");
        for (std::size_t pc = 0; pc < instructions; ++pc) {
            const auto raw = result->code[pc], op = raw & 63, u = raw >> 6, a = raw >> 15, b = (raw >> 6) & 511;
            if (op >= op_names.size()) fail("unknown opcode " + std::to_string(op));
            if ((op == 7 || op == 12 || op == 14 || op == 16 || op == 19) && u >= strings)
                fail("instruction string index is outside constants");
            if ((op == 8 || op == 9) && u >= numbers) fail("instruction number index is outside constants");
            if ((op == 2 || op == 3) && (a >= result->stack ||
                (op == 2 && b != 255 && b > result->stack - a)))
                fail("call target or fixed results exceed declared stack");
            if ((op == 11 || op == 15 || op == 18) && u >= result->stack) fail("local index exceeds declared stack");
            if (op == 10) {
                if (u >= 32) fail("upvalue index exceeds the supported profile");
                result->upvalues = std::max(result->upvalues, std::size_t(u + 1));
            }
            if (op == 48 && (a >= children || b > 32 || b < result->children[a]->upvalues))
                fail("closure prototype or capture count is invalid");
            if ((op >= 32 && op <= 42) || (op >= 44 && op <= 47)) {
                const auto target = static_cast<std::int64_t>(pc) + 1 + static_cast<std::int64_t>(u) - signed_bias;
                if (target < 0 || target >= static_cast<std::int64_t>(instructions)) fail("jump target is outside prototype");
            }
            if (op == 43 && pc + 2 >= instructions) fail("PUSHNILJMP target is outside prototype");
            if ((op == 4 && !u) || (op == 29 && u < 2)) fail("invalid zero/short stack operand");
        }
        return result;
    }
};
} // namespace

struct Lua4Program::Impl {
    Lua4ProgramInfo info;
    std::shared_ptr<const Proto> root;
    std::size_t retained_bytes{};
};
const Lua4ProgramInfo& Lua4Program::info() const {
    if (!impl_) fail("empty program"); return impl_->info;
}
Lua4Program read_lua4_chunk(std::span<const std::uint8_t> bytes, std::string_view label, Lua4ReadLimits limits) {
    limit(limits.max_bytes, 16u * 1024u * 1024u, "chunk byte");
    limit(limits.max_instructions, 1048576, "instruction storage");
    limit(limits.max_functions, 4096, "prototype count");
    limit(limits.max_string_bytes, 8u * 1024u * 1024u, "serialized string");
    limit(limits.max_depth, 64, "prototype depth");
    if (bytes.size() > limits.max_bytes) fail("chunk exceeds caller byte limit");
    Reader reader{bytes, limits, {safe_label(label)}, 0, limits.max_string_bytes,
        limits.max_instructions, limits.max_functions};
    const auto header = reader.take(13);
    constexpr std::array<std::uint8_t, 7> sizes{4, 4, 4, 32, 6, 9, 8};
    if (header[0] != 0x1b || header[1] != 'L' || header[2] != 'u' || header[3] != 'a' ||
        header[4] != 0x40 || header[5] > 1 || !std::equal(sizes.begin(), sizes.end(), header.begin() + 6))
        fail("unsupported binary chunk header/profile");
    reader.little = header[5] != 0;
    if (std::abs(reader.number() - 314159265.3589793) > 0.000001) fail("binary number format check failed");
    auto impl = std::make_shared<Lua4Program::Impl>();
    impl->root = reader.function(0, "main");
    if (impl->root->upvalues) fail("top-level prototype cannot require captured upvalues");
    if (reader.at != bytes.size()) fail("trailing bytes after the binary chunk");
    impl->info = std::move(reader.info);
    // Includes conservative metadata/vector overhead, not just serialized bytes.
    impl->retained_bytes = reader.retained_bytes + impl->info.label.size() + 128;
    Lua4Program result; result.impl_ = std::move(impl); return result;
}

Lua4Value Lua4Value::number(double value) {
    if (!std::isfinite(value)) fail("non-finite value");
    Lua4Value result; result.type_ = Lua4Type::Number; result.number_ = value; return result;
}
Lua4Value Lua4Value::string(std::string value) {
    if (value.size() > max_value_string) fail("value string exceeds 1 MiB");
    Lua4Value result; result.type_ = Lua4Type::String;
    result.text_ = std::make_shared<const std::string>(std::move(value)); return result;
}
Lua4Value Lua4Value::host_object(std::string name) {
    auto result = string(std::move(name)); result.type_ = Lua4Type::HostObject; return result;
}
Lua4Value Lua4Value::host_function(std::string name) {
    auto result = string(std::move(name)); result.type_ = Lua4Type::HostFunction; return result;
}
double Lua4Value::as_number() const {
    if (type_ != Lua4Type::Number) fail("value is not a number"); return number_;
}
std::string_view Lua4Value::as_string() const {
    if (type_ != Lua4Type::String) fail("value is not a string"); return *text_;
}
std::string_view Lua4Value::host_name() const {
    if (type_ != Lua4Type::HostObject && type_ != Lua4Type::HostFunction) fail("value is not a host symbol");
    return *text_;
}
std::string Lua4Value::describe() const {
    switch (type_) {
    case Lua4Type::Nil: return "nil";
    case Lua4Type::Number: {
        std::array<char, 64> buffer{};
        const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number_, std::chars_format::general, 16);
        return std::string(buffer.data(), converted.ptr);
    }
    case Lua4Type::String: return "string[" + std::to_string(text_->size()) + "]";
    case Lua4Type::Table: return "table";
    case Lua4Type::Function: return "Lua closure";
    case Lua4Type::HostObject: return "host object " + safe_label(text_->substr(0, 4096));
    case Lua4Type::HostFunction: return "host function " + safe_label(text_->substr(0, 4096));
    }
    return "invalid value";
}

struct Lua4Execution::Impl : std::enable_shared_from_this<Lua4Execution::Impl> {
    struct Call {
        Lua4Value function;
        std::vector<Lua4Value> arguments;
        Lua4Site site;
        std::uint32_t base{}, results{};
        std::uint64_t id{};
        bool tail{}, entered{};
        Lua4ResumableHostHandler handler;
        std::weak_ptr<Impl> child;
    };
    struct Frame {
        std::shared_ptr<const Lua4Program::Impl> program;
        std::shared_ptr<const Proto> proto;
        std::vector<Lua4Value> captures, stack;
        std::size_t pc{};
        std::optional<Call> call;
    };
    std::weak_ptr<Lua4Vm::Impl> owner;
    std::shared_ptr<const Lua4Program::Impl> program;
    std::weak_ptr<Impl> parent;
    std::uint64_t id{}, parent_call{};
    std::vector<std::unique_ptr<Frame>> frames;
    Lua4RunResult result;
    bool resumable=true, initialized{}, executing{}, finished{};
    void abandon() noexcept;
    ~Impl();
};

struct Lua4Vm::Impl {
    using Execution=Lua4Execution::Impl;
    struct Key {
        Lua4Type type{};
        double number{};
        std::string text;
        std::size_t handle{};
        bool operator<(const Key& other) const {
            if (type != other.type) return type < other.type;
            if (type == Lua4Type::Number) return number < other.number;
            if (type == Lua4Type::String || type == Lua4Type::HostObject || type == Lua4Type::HostFunction)
                return text < other.text;
            return handle < other.handle;
        }
    };
    struct Table { std::map<Key, Lua4Value> values; };
    struct Closure {
        std::shared_ptr<const Lua4Program::Impl> program;
        std::shared_ptr<const Proto> proto;
        std::vector<Lua4Value> upvalues;
    };
    Lua4Limits limits;
    std::uint64_t identity{};
    Lua4HostHandler host;
    Lua4ResumableHostHandler resumable_host;
    std::weak_ptr<Execution> active_execution;
    Execution* callback_execution{};
    Execution::Call* callback{};
    std::uint64_t next_execution=1,next_call=1;
    std::map<std::string, Lua4Value, std::less<>> globals;
    std::vector<std::unique_ptr<Table>> tables;
    std::vector<std::unique_ptr<Closure>> closures;
    std::vector<std::shared_ptr<const Lua4Program::Impl>> programs;
    std::size_t heap_remaining{}, objects_remaining{}, entries_remaining{};
    std::size_t instructions{}, host_calls{}, active_stack{}, depth{};
    bool stopped{}, running{};
    Lua4Site site;

    explicit Impl(Lua4Limits requested) : limits(requested), heap_remaining(requested.max_heap_bytes),
        objects_remaining(requested.max_objects), entries_remaining(requested.max_table_entries) {
        limit(limits.max_instructions, 10000000, "execution instruction");
        limit(limits.max_call_depth, 128, "call depth");
        limit(limits.max_stack_values, 65536, "stack value");
        limit(limits.max_heap_bytes, 128u * 1024u * 1024u, "heap byte");
        limit(limits.max_objects, 262144, "object");
        limit(limits.max_table_entries, 1048576, "table entry");
        limit(limits.max_globals, 65536, "global");
        limit(limits.max_host_calls, 65536, "host call");
        static std::atomic<std::uint64_t> next{1}; identity = next.fetch_add(1);
        if (!identity) fail("VM identity space exhausted");
    }
    void allocate(std::size_t bytes) { charge(heap_remaining, bytes, "VM heap allocation"); }
    void validate(const Lua4Value& value) const {
        if (value.type_ == Lua4Type::Table || value.type_ == Lua4Type::Function) {
            if (value.owner_ != identity) fail("value belongs to another VM");
            if ((value.type_ == Lua4Type::Table && value.handle_ >= tables.size()) ||
                (value.type_ == Lua4Type::Function && value.handle_ >= closures.size())) fail("invalid VM-owned handle");
        }
        if (value.text_ && value.text_->size() > max_value_string) fail("oversized imported string");
        if (value.type_ == Lua4Type::Number && !std::isfinite(value.number_)) fail("non-finite imported number");
    }
    void import(const Lua4Value& value) {
        validate(value);
        if (value.text_) allocate(value.text_->size() + 64);
    }
    Key key(const Lua4Value& value) {
        validate(value);
        if (value.type_ == Lua4Type::Nil) fail("nil table key");
        if (value.text_) allocate(value.text_->size());
        return {value.type_, value.number_, value.text_ ? *value.text_ : std::string{}, value.handle_};
    }
    Lua4Value string(std::string value) {
        if (value.size() > max_value_string) fail("computed string exceeds 1 MiB");
        allocate(value.size() + 64); return Lua4Value::string(std::move(value));
    }
    Lua4Value table() {
        charge(objects_remaining, 1, "VM object count"); allocate(sizeof(Table) + 64);
        Lua4Value value; value.type_ = Lua4Type::Table; value.owner_ = identity; value.handle_ = tables.size();
        tables.push_back(std::make_unique<Table>()); return value;
    }
    Lua4Value closure(std::shared_ptr<const Lua4Program::Impl> program, std::shared_ptr<const Proto> proto,
        std::vector<Lua4Value> upvalues) {
        charge(objects_remaining, 1, "VM object count"); allocate(sizeof(Closure) + 64 + upvalues.size() * sizeof(Lua4Value));
        Lua4Value value; value.type_ = Lua4Type::Function; value.owner_ = identity; value.handle_ = closures.size();
        closures.push_back(std::make_unique<Closure>(Closure{std::move(program), std::move(proto), std::move(upvalues)}));
        return value;
    }
    Lua4Value table_get(const Lua4Value& value, const Lua4Value& index) {
        validate(value);
        if (value.type_ != Lua4Type::Table) fail("table access needs a table, got " + value.describe());
        if (index.type_ == Lua4Type::Nil) return {};
        const auto& entries = tables[value.handle_]->values;
        const auto found = entries.find(key(index)); return found == entries.end() ? Lua4Value{} : found->second;
    }
    Lua4Value get(const Lua4Value& value, const Lua4Value& index) {
        if (value.type_ == Lua4Type::HostObject) {
            if (index.type_ != Lua4Type::String) fail("host object method key must be a string");
            if (value.text_->size() + index.text_->size() + 1 > 4096) fail("host function name exceeds 4096 bytes");
            auto name = *value.text_ + "." + *index.text_;
            allocate(name.size() + 64); return Lua4Value::host_function(std::move(name));
        }
        return table_get(value, index);
    }
    void set(const Lua4Value& value, const Lua4Value& index, const Lua4Value& assigned) {
        validate(value); validate(assigned);
        if (value.type_ != Lua4Type::Table) fail("table assignment needs a table");
        auto& entries = tables[value.handle_]->values;
        const auto converted = key(index); const auto found = entries.find(converted);
        if (found == entries.end()) {
            if (assigned.type_ == Lua4Type::Nil) return;
            charge(entries_remaining, 1, "table entry"); allocate(sizeof(Key) + sizeof(Lua4Value) + converted.text.size() + 64);
            entries.emplace(converted, assigned);
        } else {
            // Keep deleted slots: this avoids reclaim/reallocate cycles evading budgets.
            found->second = assigned;
        }
    }
    void set_global(std::string name, const Lua4Value& value) {
        if (name.size() > max_value_string) fail("global name exceeds 1 MiB");
        validate(value);
        const auto found = globals.find(name);
        if (found == globals.end()) {
            if (globals.size() >= limits.max_globals) fail("global count limit exceeded");
            allocate(name.size() + sizeof(Lua4Value) + 96); globals.emplace(std::move(name), value);
        } else found->second = value;
    }
    Lua4Value get_global(std::string_view name) {
        if (name.size() > max_value_string) fail("global name exceeds 1 MiB");
        allocate(name.size());
        const auto found = globals.find(name); return found == globals.end() ? Lua4Value{} : found->second;
    }
    double numeric(const Lua4Value& value) {
        if (value.type_ == Lua4Type::Number) return value.number_;
        if (value.type_ != Lua4Type::String) fail("arithmetic/tag method unsupported for " + value.describe());
        allocate(value.text_->size());
        std::string_view input = *value.text_;
        auto whitespace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
        while (!input.empty() && whitespace(input.front())) input.remove_prefix(1);
        while (!input.empty() && whitespace(input.back())) input.remove_suffix(1);
        if (!input.empty() && input.front() == '+') {
            input.remove_prefix(1);
            if (!input.empty() && (input.front() == '+' || input.front() == '-')) fail("invalid numeric string sign");
        }
        double result{};
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), result, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || !std::isfinite(result))
            fail("unsupported numeric string or arithmetic tag method");
        return result;
    }
    std::string text(const Lua4Value& value) {
        if (value.type_ == Lua4Type::String) { allocate(value.text_->size()); return *value.text_; }
        if (value.type_ == Lua4Type::Number) return value.describe();
        fail("concatenation tag method is unsupported for " + value.describe());
    }
    bool equal(const Lua4Value& a, const Lua4Value& b) {
        if (a.type_ != b.type_) return false;
        if (a.type_ == Lua4Type::Nil) return true;
        if (a.type_ == Lua4Type::Number) return a.number_ == b.number_;
        if (a.text_) { allocate(a.text_->size() + b.text_->size()); return *a.text_ == *b.text_; }
        return a.owner_ == b.owner_ && a.handle_ == b.handle_;
    }
    bool less(const Lua4Value& a, const Lua4Value& b) {
        if (a.type_ == Lua4Type::Number && b.type_ == Lua4Type::Number) return a.number_ < b.number_;
        if (a.type_ == Lua4Type::String && b.type_ == Lua4Type::String) {
            allocate(a.text_->size() + b.text_->size());
            return std::lexicographical_compare(a.text_->begin(), a.text_->end(), b.text_->begin(), b.text_->end(),
                [](unsigned char l, unsigned char r) { return l < r; });
        }
        fail("comparison tag method is unsupported");
    }
    bool pending_execution() const {
        const auto active=active_execution.lock();return active && !active->finished;
    }
    void release_frames(Execution& execution) noexcept {
        for(const auto& frame:execution.frames) {active_stack-=frame->proto->stack;--depth;}
        execution.frames.clear();
    }
    void enter_frame(Execution& execution,const Lua4Value& function,std::span<const Lua4Value> arguments) {
        validate(function);
        if(function.type_!=Lua4Type::Function) fail("cannot call " + function.describe());
        if(depth>=limits.max_call_depth) fail("call depth budget exhausted");
        const auto& value=*closures[function.handle_];
        if(value.proto->stack>limits.max_stack_values-active_stack) fail("aggregate stack budget exhausted");
        if(execution.resumable) allocate(sizeof(Execution::Frame)+(value.proto->stack+value.upvalues.size())*sizeof(Lua4Value));
        auto frame=std::make_unique<Execution::Frame>();
        frame->program=value.program;frame->proto=value.proto;frame->captures=value.upvalues;frame->stack.reserve(value.proto->stack);
        const auto& proto=*frame->proto;
        for(std::size_t i=0;i<proto.parameters;++i) frame->stack.push_back(i<arguments.size() ? arguments[i] : Lua4Value{});
        if(proto.vararg) {
            auto packed=table();const auto count=arguments.size()>proto.parameters ? arguments.size()-proto.parameters : 0;
            for(std::size_t i=0;i<count;++i) set(packed,Lua4Value::number(static_cast<double>(i+1)),arguments[proto.parameters+i]);
            set(packed,string("n"),Lua4Value::number(static_cast<double>(count)));frame->stack.push_back(packed);
        }
        const auto size=proto.stack;
        execution.frames.push_back(std::move(frame));active_stack+=size;++depth;
    }
    void finish_frame(Execution& execution,std::vector<Lua4Value> returned) {
        for(;;) {
            active_stack-=execution.frames.back()->proto->stack;--depth;execution.frames.pop_back();
            if(execution.frames.empty()) {
                execution.result.returned=std::move(returned);execution.result.completed=true;
                execution.result.stop=Lua4RunStop::completed;execution.finished=true;return;
            }
            auto& frame=*execution.frames.back();
            if(!frame.call) fail("missing retained caller result boundary");
            const auto base=frame.call->base,count=frame.call->results;const bool tail=frame.call->tail;
            site=frame.call->site;frame.stack.resize(base);
            const auto desired=tail || count==255 ? returned.size() : count;
            if(desired>frame.proto->stack-frame.stack.size()) fail("call results exceed declared stack");
            for(std::size_t i=0;i<desired;++i) frame.stack.push_back(i<returned.size() ? returned[i] : Lua4Value{});
            frame.call.reset();
            if(!tail)return;
            if(count>frame.stack.size()) fail("tail return begins beyond active stack");
            returned.assign(frame.stack.begin()+count,frame.stack.end());
        }
    }
    void host_results(Execution& execution,Execution::Frame& frame,std::vector<Lua4Value> returned) {
        if(returned.size()>max_prototype_stack) fail("host returned too many values");
        for(const auto& value:returned) import(value);
        const auto base=frame.call->base,count=frame.call->results;const bool tail=frame.call->tail;
        frame.stack.resize(base);
        const auto desired=tail || count==255 ? returned.size() : count;
        if(desired>frame.proto->stack-frame.stack.size()) fail("call results exceed declared stack");
        for(std::size_t i=0;i<desired;++i) frame.stack.push_back(i<returned.size() ? returned[i] : Lua4Value{});
        frame.call.reset();
        if(tail) {
            if(count>frame.stack.size()) fail("tail return begins beyond active stack");
            finish_frame(execution,{frame.stack.begin()+count,frame.stack.end()});
        }
    }
    bool pending_call(Execution& execution,Execution::Frame& frame) {
        auto& call=*frame.call;site=call.site;validate(call.function);
        if(call.function.type_!=Lua4Type::HostFunction) {
            if(call.entered) fail("retained Lua call has no child frame");
            call.entered=true;enter_frame(execution,call.function,call.arguments);return true;
        }
        if(!call.entered) {
            if(host_calls>=limits.max_host_calls) fail("host call budget exhausted");
            if(!next_call) fail("host call identity space exhausted");
            ++host_calls;call.id=next_call++;call.entered=true;
        }
        const Lua4HostCall request{*call.function.text_,call.arguments,call.site,execution.id,call.id};
        if(!execution.resumable) {
            if(!host) fail("unbound native host function " + safe_label(*call.function.text_));
            auto returned=host(request);site=call.site;host_results(execution,frame,std::move(returned));return true;
        }
        if(!call.handler)call.handler=resumable_host;
        if(!call.handler) {
            execution.result.diagnostic="Lua 4: unbound native host function " + safe_label(*call.function.text_);
            return false;
        }
        struct CallbackScope {
            Impl& vm;Execution* previous_execution;Execution::Call* previous_call;
            CallbackScope(Impl& value,Execution& execution,Execution::Call& call)
                :vm(value),previous_execution(value.callback_execution),previous_call(value.callback) {
                vm.callback_execution=&execution;vm.callback=&call;
            }
            ~CallbackScope(){vm.callback_execution=previous_execution;vm.callback=previous_call;}
        };
        Lua4HostResult returned;
        {
            CallbackScope scope(*this,execution,call);
            returned=call.handler(request);
        }
        site=call.site;
        if(stopped)fail("VM faulted during a nested invocation");
        if(!returned.completed) {
            if(!returned.returned.empty())fail("incomplete host call cannot return values");
            if(returned.diagnostic.size()>4096)fail("host suspension diagnostic exceeds 4096 bytes");
            execution.result.diagnostic=std::move(returned.diagnostic);return false;
        }
        if(const auto child=call.child.lock();child && !child->finished)fail("complete the owned nested invocation before returning host results");
        host_results(execution,frame,std::move(returned.returned));return true;
    }
    void process(Execution& execution) {
        if(!execution.initialized) {
            execution.initialized=true;
            if(!execution.program)fail("empty program");
            if(execution.resumable)allocate(sizeof(Execution));
            if(std::find(programs.begin(),programs.end(),execution.program)==programs.end()) {
                allocate(execution.program->retained_bytes);programs.push_back(execution.program);
            }
            enter_frame(execution,closure(execution.program,execution.program->root,{}),{});
        }
        while(!execution.finished) {
            if(stopped)fail("VM faulted during a nested invocation");
            auto& frame=*execution.frames.back();
            if(frame.call) {
                if(!pending_call(execution,frame)) {execution.result.stop=Lua4RunStop::host_required;return;}
                continue;
            }
            const auto& program=frame.program;const auto& proto=frame.proto;const auto& captures=frame.captures;
            auto& stack=frame.stack;auto& pc=frame.pc;
            auto need=[&](std::size_t n){if(n>stack.size())fail("stack underflow");};
            auto push=[&](Lua4Value value){if(stack.size()>=proto->stack)fail("declared prototype stack exceeded");stack.push_back(std::move(value));};
            auto local=[&](std::size_t index)->Lua4Value {if(index>=stack.size())fail("local is outside the active stack");return stack[index];};
            auto pop=[&](){need(1);auto value=stack.back();stack.pop_back();return value;};
            if (pc >= proto->code.size()) fail("execution left the prototype code");
            site = {program->info.label, proto->path, pc};
            if (instructions >= limits.max_instructions) fail("instruction budget exhausted");
            ++instructions;
            const auto raw = proto->code[pc++], op = raw & 63, u = raw >> 6, a = raw >> 15, b = (raw >> 6) & 511;
            const auto s = static_cast<std::int64_t>(u) - signed_bias;
            auto jump = [&]() {
                const auto destination = static_cast<std::int64_t>(pc) + s;
                if (destination < 0 || destination >= static_cast<std::int64_t>(proto->code.size())) fail("invalid jump destination");
                pc = static_cast<std::size_t>(destination);
            };
            auto constant = [&]() {
                if (u >= proto->strings.size()) fail("invalid string constant");
                Lua4Value value; value.type_ = Lua4Type::String; value.text_ = proto->strings[u]; return value;
            };
            switch (op) {
            case 0: finish_frame(execution,{});break;
            case 1:
                if(u>stack.size())fail("return begins beyond the active stack");
                finish_frame(execution,{stack.begin()+u,stack.end()});break;
            case 2: case 3: {
                if(a>=stack.size())fail("call target is outside the active stack");
                Execution::Call call;call.function=stack[a];call.arguments.assign(stack.begin()+a+1,stack.end());
                call.site=site;call.base=a;call.results=b;call.tail=op==3;
                if(execution.resumable)allocate(sizeof(Execution::Call)+call.arguments.size()*sizeof(Lua4Value));
                frame.call=std::move(call);break;
            }
            case 4:
                if (!u || u > proto->stack - stack.size()) fail("PUSHNIL exceeds declared stack");
                stack.resize(stack.size() + u); break;
            case 5: need(u); stack.resize(stack.size() - u); break;
            case 6: push(Lua4Value::number(static_cast<double>(s))); break;
            case 7: push(constant()); break;
            case 8: case 9:
                if (u >= proto->numbers.size()) fail("invalid number constant");
                push(Lua4Value::number(op == 8 ? proto->numbers[u] : -proto->numbers[u])); break;
            case 10:
                if (u >= captures.size()) fail("closure upvalue index is unbound"); push(captures[u]); break;
            case 11: push(local(u)); break;
            case 12: push(get_global(constant().as_string())); break;
            case 13: { const auto index = pop(); const auto value = pop(); push(get(value, index)); break; }
            case 14: { const auto value = pop(); push(get(value, constant())); break; }
            case 15: { const auto index = local(u); const auto value = pop(); push(get(value, index)); break; }
            case 16: { const auto receiver = pop(); push(get(receiver, constant())); push(receiver); break; }
            case 17:
                if (u > limits.max_table_entries) fail("table size hint exceeds table limit"); push(table()); break;
            case 18: { const auto value = pop(); if (u >= stack.size()) fail("local assignment exceeds active stack"); stack[u] = value; break; }
            case 19: { const auto value = pop(); set_global(std::string(constant().as_string()), value); break; }
            case 20:
                need(a); need(b);
                if (a < 3) fail("SETTABLE has no table/key/value operands");
                set(stack[stack.size() - a], stack[stack.size() - a + 1], stack.back());
                stack.resize(stack.size() - b); break;
            case 21: {
                need(std::size_t(b) + 1); const auto base = stack.size() - b - 1;
                for (std::size_t n = b; n; --n)
                    set(stack[base], Lua4Value::number(static_cast<double>(a * fields_per_flush + n)), stack[base + n]);
                stack.resize(base + 1); break;
            }
            case 22: {
                const auto count = std::size_t(u); need(count * 2 + 1); const auto base = stack.size() - count * 2 - 1;
                for (std::size_t n = count; n; --n) set(stack[base], stack[base + n * 2 - 1], stack[base + n * 2]);
                stack.resize(base + 1); break;
            }
            case 23: case 25: case 26: case 27: {
                const auto right = numeric(pop()), left = numeric(pop());
                const auto value = op == 23 ? left + right : op == 25 ? left - right : op == 26 ? left * right : left / right;
                push(Lua4Value::number(value)); break;
            }
            case 24: { const auto value = numeric(pop()); push(Lua4Value::number(value + static_cast<double>(s))); break; }
            case 28: fail("POW requires an unsupported Lua 4 arithmetic tag method");
            case 29: {
                need(u); if (u < 2) fail("CONCAT requires at least two operands");
                const auto base = stack.size() - u; std::string joined;
                for (std::size_t n = base; n < stack.size(); ++n) {
                    const auto value = text(stack[n]);
                    if (value.size() > max_value_string - joined.size()) fail("concatenated string exceeds 1 MiB");
                    joined += value;
                }
                stack.resize(base); push(string(std::move(joined))); break;
            }
            case 30: { const auto value = numeric(pop()); push(Lua4Value::number(-value)); break; }
            case 31: { const auto value = pop(); push(value.truthy() ? Lua4Value{} : Lua4Value::number(1)); break; }
            case 32: case 33: case 34: case 35: case 36: case 37: {
                const auto right = pop(), left = pop();
                const bool branch = op == 32 ? !equal(left, right) : op == 33 ? equal(left, right) :
                    op == 34 ? less(left, right) : op == 35 ? !less(right, left) : op == 36 ? less(right, left) : !less(left, right);
                if (branch) jump(); break;
            }
            case 38: case 39: { const auto value = pop(); if (value.truthy() == (op == 38)) jump(); break; }
            case 40: case 41:
                need(1); if (stack.back().truthy() == (op == 40)) jump(); else stack.pop_back(); break;
            case 42: jump(); break;
            case 43: push({}); ++pc; break;
            case 44: case 45: {
                need(3); const auto base = stack.size() - 3;
                auto index = op == 44 ? numeric(stack[base]) : stack[base].as_number();
                const auto end = op == 44 ? numeric(stack[base + 1]) : stack[base + 1].as_number();
                const auto step = op == 44 ? numeric(stack[base + 2]) : stack[base + 2].as_number();
                if (op == 45) index += step;
                stack[base] = Lua4Value::number(index); stack[base + 1] = Lua4Value::number(end); stack[base + 2] = Lua4Value::number(step);
                const bool finished = step > 0 ? index > end : index < end;
                if (finished) { stack.resize(base); if (op == 44) jump(); }
                else if (op == 45) jump();
                break;
            }
            case 46: case 47: fail("LFOR table iteration requires an unverified original hash traversal order");
            case 48: {
                need(b); if (a >= proto->children.size()) fail("invalid closure prototype");
                std::vector<Lua4Value> values(stack.end() - b, stack.end()); stack.resize(stack.size() - b);
                push(closure(program, proto->children[a], std::move(values))); break;
            }
            default: fail("unknown opcode " + std::to_string(op));
            }
        }
    }
};

void Lua4Execution::Impl::abandon() noexcept {
    if(!finished)if(const auto vm=owner.lock())vm->stopped=true;
}
Lua4Execution::Impl::~Impl() {
    abandon();
    if(const auto vm=owner.lock()) {
        vm->release_frames(*this);
    }
}
Lua4Execution::Lua4Execution(std::shared_ptr<Impl> impl):impl_(std::move(impl)) {}
Lua4Execution::Lua4Execution(Lua4Execution&&) noexcept=default;
Lua4Execution& Lua4Execution::operator=(Lua4Execution&& other) noexcept {
    if(this!=&other) {
        if(impl_)impl_->abandon();
        impl_=std::move(other.impl_);
    }
    return *this;
}
Lua4Execution::~Lua4Execution() {if(impl_)impl_->abandon();}
std::uint64_t Lua4Execution::id() const noexcept {return impl_ ? impl_->id : 0;}
bool Lua4Execution::completed() const noexcept {return impl_ && impl_->result.completed;}

Lua4Vm::Lua4Vm(Lua4Limits limits) : impl_(std::make_shared<Impl>(limits)) {}
Lua4Vm::~Lua4Vm() = default;
Lua4Vm::Lua4Vm(Lua4Vm&&) noexcept = default;
Lua4Vm& Lua4Vm::operator=(Lua4Vm&&) noexcept = default;
void Lua4Vm::set_host_handler(Lua4HostHandler handler) {
    if (!impl_ || impl_->running || impl_->stopped || impl_->pending_execution()) fail("cannot modify an inactive/running/stopped VM or suspended VM");
    impl_->host = std::move(handler);
}
void Lua4Vm::set_resumable_host_handler(Lua4ResumableHostHandler handler) {
    if(!impl_ || impl_->running || impl_->stopped || (impl_->pending_execution() && impl_->resumable_host))
        fail("cannot replace the resumable host owner of an inactive/running/stopped or suspended VM");
    impl_->resumable_host=std::move(handler);
}
void Lua4Vm::set_global(std::string name, const Lua4Value& value) {
    if (!impl_ || impl_->running || impl_->stopped || impl_->pending_execution()) fail("cannot modify an inactive/running/stopped VM or suspended VM");
    impl_->import(value); impl_->set_global(std::move(name), value);
}
Lua4Value Lua4Vm::get_global(std::string_view name) const {
    if (!impl_) fail("inactive VM"); return impl_->get_global(name);
}
Lua4Value Lua4Vm::table_get(const Lua4Value& table, const Lua4Value& key) const {
    if (!impl_) fail("inactive VM"); return impl_->table_get(table, key);
}
Lua4Execution Lua4Vm::begin_execution(const Lua4Program& program) {
    if(!impl_ || impl_->running || impl_->stopped || impl_->pending_execution())
        fail("cannot begin another root in an inactive/running/stopped or suspended VM");
    if(!impl_->next_execution)fail("execution identity space exhausted");
    auto execution=std::make_shared<Lua4Execution::Impl>();execution->owner=impl_;execution->program=program.impl_;
    execution->id=impl_->next_execution++;impl_->active_execution=execution;
    impl_->instructions=impl_->host_calls=0;impl_->site={};
    return Lua4Execution(std::move(execution));
}
Lua4Execution Lua4Vm::begin_nested(const Lua4Program& program) {
    if(!impl_ || impl_->stopped || !impl_->running || !impl_->callback || !impl_->callback_execution || !impl_->callback_execution->resumable)
        fail("nested execution requires its currently invoked resumable host callback");
    if(const auto child=impl_->callback->child.lock();child && !child->finished)
        fail("resume the existing nested invocation before beginning another child");
    if(!impl_->next_execution)fail("execution identity space exhausted");
    auto execution=std::make_shared<Lua4Execution::Impl>();execution->owner=impl_;execution->program=program.impl_;
    execution->id=impl_->next_execution++;execution->parent=impl_->callback_execution->shared_from_this();
    execution->parent_call=impl_->callback->id;impl_->callback->child=execution;
    return Lua4Execution(std::move(execution));
}
Lua4RunResult Lua4Vm::resume(Lua4Execution& handle) {
    if(!impl_ || !handle.impl_ || handle.impl_->owner.lock()!=impl_)fail("execution belongs to another or expired VM");
    auto execution=handle.impl_;auto state=impl_;
    if(execution->finished)return execution->result;
    if(execution->executing)fail("an execution cannot recursively resume itself");
    if(execution->parent_call) {
        const auto parent=execution->parent.lock();
        if(!parent || state->callback_execution!=parent.get() || !state->callback || state->callback->id!=execution->parent_call ||
           state->callback->child.lock()!=execution)
            fail("resume the nested invocation through its original owning host callback");
    } else if(state->running || state->active_execution.lock()!=execution)fail("another execution owns the VM");
    struct RunScope {
        Impl& vm;Lua4Execution::Impl& execution;bool previous;
        RunScope(Impl& owner,Lua4Execution::Impl& run):vm(owner),execution(run),previous(owner.running){vm.running=true;execution.executing=true;}
        ~RunScope(){execution.executing=false;vm.running=previous;}
    } scope(*state,*execution);
    execution->result.diagnostic.clear();
    try {
        if(state->stopped)fail("VM is stopped; an incomplete invocation was faulted or abandoned");
        state->process(*execution);
    } catch (const std::exception& error) {
        state->stopped=true;execution->result.diagnostic=error.what();execution->result.stop=Lua4RunStop::faulted;
    } catch (...) {
        state->stopped=true;execution->result.diagnostic="Lua 4: unknown host exception";execution->result.stop=Lua4RunStop::faulted;
    }
    auto& result=execution->result;
    result.site=state->site;result.instructions=state->instructions;result.host_calls=state->host_calls;
    if(result.stop==Lua4RunStop::faulted) {
        result.completed=false;execution->finished=true;state->release_frames(*execution);
        if(!result.site.source.empty())result.diagnostic+=" at "+result.site.source+" "+result.site.prototype+" pc "+std::to_string(result.site.pc);
    }
    return result;
}
Lua4RunResult Lua4Vm::execute(const Lua4Program& program) {
    if(!impl_)fail("inactive VM");
    if(impl_->running || impl_->stopped || impl_->pending_execution()) {
        Lua4RunResult result;result.diagnostic="Lua 4: VM is running or stopped; construct a fresh VM before executing again";return result;
    }
    auto execution=begin_execution(program);execution.impl_->resumable=false;
    return resume(execution);
}
} // namespace dw4
