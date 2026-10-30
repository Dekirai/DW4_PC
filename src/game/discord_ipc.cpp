#include "discord_ipc.hpp"

#include <cstdio>

namespace dw4::game::discord {
namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(value >> (8 * i)));
}

std::uint32_t get_u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 | std::uint32_t(bytes[at + 2]) << 16 |
           std::uint32_t(bytes[at + 3]) << 24;
}

void field(std::string& json, bool& first, std::string_view key, std::string_view value) {
    const std::string text = activity_text(value);
    if (text.empty()) return;
    json += first ? "" : ",";
    first = false;
    json += '"';
    json += key;
    json += "\":\"";
    json += json_escape(text);
    json += '"';
}

// The position just past `"key"` and the colon after it, or npos.
std::size_t value_start(std::string_view json, std::string_view key) {
    const std::string quoted = "\"" + std::string(key) + "\"";
    for (std::size_t at = json.find(quoted); at != std::string_view::npos; at = json.find(quoted, at + 1)) {
        std::size_t p = at + quoted.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n')) ++p;
        if (p < json.size() && json[p] == ':') {
            ++p;
            while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n')) ++p;
            return p;
        }
    }
    return std::string_view::npos;
}

} // namespace

std::vector<std::uint8_t> encode_frame(Opcode opcode, std::string_view json) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + json.size());
    put_u32(out, std::uint32_t(opcode));
    put_u32(out, std::uint32_t(json.size()));
    out.insert(out.end(), json.begin(), json.end());
    return out;
}

std::optional<FrameHeader> decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) return std::nullopt;
    const std::uint32_t opcode = get_u32(bytes, 0);
    const std::uint32_t length = get_u32(bytes, 4);
    if (opcode > std::uint32_t(Opcode::pong) || length > kMaxPayload) return std::nullopt;
    return FrameHeader{Opcode(opcode), length};
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char code[8];
                std::snprintf(code, sizeof code, "\\u%04x", unsigned(static_cast<unsigned char>(c)));
                out += code;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string activity_text(std::string_view text) {
    if (text.empty()) return {};
    std::string out(text.substr(0, 128));
    // Not inside a UTF-8 sequence: drop continuation bytes and the lead byte
    // of a cut sequence.
    if (text.size() > 128) {
        std::size_t end = out.size();
        while (end > 0 && (static_cast<unsigned char>(out[end - 1]) & 0xC0) == 0x80) --end;
        if (end > 0 && static_cast<unsigned char>(out[end - 1]) >= 0xC0) {
            const unsigned char lead = static_cast<unsigned char>(out[end - 1]);
            const std::size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
            if (out.size() - (end - 1) < need) out.resize(end - 1);
        }
    }
    if (out.size() < 2) out += ' ';
    return out;
}

std::string handshake_json(std::string_view client_id) {
    return "{\"v\":1,\"client_id\":\"" + json_escape(client_id) + "\"}";
}

std::string set_activity_json(std::uint32_t pid, const std::optional<Activity>& activity, std::uint64_t nonce) {
    std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":";
    if (!activity) {
        json += "null";
    } else {
        json += '{';
        bool first = true;
        field(json, first, "details", activity->details);
        field(json, first, "state", activity->state);
        if (activity->start > 0) {
            json += first ? "" : ",";
            first = false;
            json += "\"timestamps\":{\"start\":" + std::to_string(activity->start) + "}";
        }
        std::string assets;
        bool assets_first = true;
        field(assets, assets_first, "large_image", activity->large_image);
        field(assets, assets_first, "large_text", activity->large_text);
        field(assets, assets_first, "small_image", activity->small_image);
        field(assets, assets_first, "small_text", activity->small_text);
        if (!assets.empty()) {
            json += first ? "" : ",";
            first = false;
            json += "\"assets\":{" + assets + "}";
        }
        if (!activity->party_id.empty() && activity->party_size >= 1 && activity->party_max >= activity->party_size) {
            json += first ? "" : ",";
            first = false;
            json += "\"party\":{\"id\":\"" + json_escape(activity->party_id) + "\",\"size\":[" + std::to_string(activity->party_size) + "," +
                    std::to_string(activity->party_max) + "]}";
        }
        // Type 0: "Playing".
        json += first ? "" : ",";
        json += "\"type\":0}";
    }
    json += "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
    return json;
}

std::string party_id(std::string_view room_code) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const unsigned char c : room_code) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    static const char* digits = "0123456789abcdef";
    std::string id = "dw4-";
    for (int shift = 60; shift >= 0; shift -= 4) id += digits[(hash >> shift) & 15];
    return id;
}

std::string json_string_field(std::string_view json, std::string_view key) {
    std::size_t p = value_start(json, key);
    if (p == std::string_view::npos || p >= json.size() || json[p] != '"') return {};
    std::string out;
    for (++p; p < json.size() && json[p] != '"'; ++p) {
        if (json[p] == '\\' && p + 1 < json.size()) {
            ++p;
            out += json[p] == 'n' ? '\n' : json[p] == 't' ? '\t' : json[p];
        } else {
            out += json[p];
        }
    }
    return out;
}

std::optional<long long> json_int_field(std::string_view json, std::string_view key) {
    std::size_t p = value_start(json, key);
    if (p == std::string_view::npos || p >= json.size()) return std::nullopt;
    const bool negative = json[p] == '-';
    if (negative) ++p;
    if (p >= json.size() || json[p] < '0' || json[p] > '9') return std::nullopt;
    long long value = 0;
    for (; p < json.size() && json[p] >= '0' && json[p] <= '9'; ++p) value = value * 10 + (json[p] - '0');
    return negative ? -value : value;
}

} // namespace dw4::game::discord
