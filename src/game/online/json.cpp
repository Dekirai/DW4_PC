#include "json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace dw4::game::online::json {

namespace {
const Value kNull{};

struct Parser {
    std::string_view s;
    std::size_t at = 0;
    int depth = 0;
    bool failed = false;

    void ws() {
        while (at < s.size() && (s[at] == ' ' || s[at] == '\n' || s[at] == '\r' || s[at] == '\t')) ++at;
    }
    bool literal(std::string_view word) {
        if (s.substr(at, word.size()) != word) return false;
        at += word.size();
        return true;
    }
    static void utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(std::uint32_t& v) {
        if (at + 4 > s.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s[at++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= std::uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (at >= s.size() || s[at] != '"') return false;
        ++at;
        while (at < s.size()) {
            const char c = s[at++];
            if (c == '"') return true;
            if (std::uint8_t(c) < 0x20) return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= s.size()) return false;
            const char e = s[at++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && at + 6 <= s.size() && s[at] == '\\' && s[at + 1] == 'u') {
                    at += 2;
                    std::uint32_t low = 0;
                    if (!hex4(low) || low < 0xDC00 || low >= 0xE000) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(Value& v) {
        if (++depth > 32) return false;
        ws();
        if (at >= s.size()) return false;
        const char c = s[at];
        bool ok = true;
        if (c == '{') {
            v.kind = Value::Kind::object;
            ++at;
            ws();
            if (at < s.size() && s[at] == '}') {
                ++at;
            } else {
                for (;;) {
                    ws();
                    std::string key;
                    if (!string(key)) return false;
                    ws();
                    if (at >= s.size() || s[at++] != ':') return false;
                    Value member;
                    if (!value(member)) return false;
                    v.members[key] = std::move(member);
                    ws();
                    if (at < s.size() && s[at] == ',') {
                        ++at;
                        continue;
                    }
                    if (at < s.size() && s[at] == '}') {
                        ++at;
                        break;
                    }
                    return false;
                }
            }
        } else if (c == '[') {
            v.kind = Value::Kind::array;
            ++at;
            ws();
            if (at < s.size() && s[at] == ']') {
                ++at;
            } else {
                for (;;) {
                    Value item;
                    if (!value(item)) return false;
                    v.items.push_back(std::move(item));
                    ws();
                    if (at < s.size() && s[at] == ',') {
                        ++at;
                        continue;
                    }
                    if (at < s.size() && s[at] == ']') {
                        ++at;
                        break;
                    }
                    return false;
                }
            }
        } else if (c == '"') {
            v.kind = Value::Kind::string;
            ok = string(v.text);
        } else if (literal("true")) {
            v.kind = Value::Kind::boolean;
            v.boolean = true;
        } else if (literal("false")) {
            v.kind = Value::Kind::boolean;
        } else if (literal("null")) {
            v.kind = Value::Kind::null;
        } else {
            const std::size_t start = at;
            if (at < s.size() && s[at] == '-') ++at;
            while (at < s.size() && ((s[at] >= '0' && s[at] <= '9') || s[at] == '.' || s[at] == 'e' || s[at] == 'E' || s[at] == '+' || s[at] == '-'))
                ++at;
            if (at == start) return false;
            v.kind = Value::Kind::number;
            v.text = std::string(s.substr(start, at - start));
            char* end = nullptr;
            v.number = std::strtod(v.text.c_str(), &end);
            ok = end == v.text.c_str() + v.text.size();
        }
        --depth;
        return ok;
    }
};
} // namespace

const Value* Value::find(std::string_view key) const {
    if (kind != Kind::object) return nullptr;
    const auto it = members.find(std::string(key));
    return it == members.end() ? nullptr : &it->second;
}
const Value& Value::operator[](std::string_view key) const {
    const auto* v = find(key);
    return v ? *v : kNull;
}
std::optional<std::string> Value::str() const {
    if (kind != Kind::string) return std::nullopt;
    return text;
}
std::optional<std::int64_t> Value::i64() const {
    if (kind != Kind::number) return std::nullopt;
    std::int64_t v = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (error == std::errc{} && end == text.data() + text.size()) return v;
    return std::nullopt;
}
std::optional<std::uint64_t> Value::u64() const {
    if (kind != Kind::number) return std::nullopt;
    std::uint64_t v = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (error == std::errc{} && end == text.data() + text.size()) return v;
    return std::nullopt;
}
std::optional<bool> Value::flag() const {
    if (kind != Kind::boolean) return std::nullopt;
    return boolean;
}

std::optional<Value> parse(std::string_view text) {
    if (text.size() > (4u << 20)) return std::nullopt;
    Parser p{text};
    Value v;
    if (!p.value(v)) return std::nullopt;
    p.ws();
    if (p.at != text.size()) return std::nullopt;
    return v;
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (std::uint8_t(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(std::uint8_t(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

} // namespace dw4::game::online::json
