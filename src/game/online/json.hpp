#pragma once

// A small JSON reader and writer for the DW4Online API answers (bounded
// depth and size, UTF-8 passed through, \u escapes decoded to UTF-8).
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game::online::json {

struct Value {
    enum class Kind { null, boolean, number, string, array, object } kind = Kind::null;
    bool boolean{};
    double number{};
    std::string text;               // string value, or the number's literal
    std::vector<Value> items;       // array
    std::map<std::string, Value> members; // object

    const Value* find(std::string_view key) const;     // object member or null
    const Value& operator[](std::string_view key) const; // a null value when missing
    std::optional<std::string> str() const;
    std::optional<std::int64_t> i64() const;
    std::optional<std::uint64_t> u64() const;
    std::optional<bool> flag() const;
    bool is_null() const { return kind == Kind::null; }
};

// Nothing for malformed text, trailing garbage, depth above 32 or above 4 MiB.
std::optional<Value> parse(std::string_view text);

// A quoted, escaped JSON string.
std::string quote(std::string_view text);

} // namespace dw4::game::online::json
