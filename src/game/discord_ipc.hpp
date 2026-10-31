#pragma once

// Discord's local RPC framing (no SDK, no library): the desktop client
// listens on the named pipes \\?\pipe\discord-ipc-0..9. Every message is an
// 8-byte header (two little-endian uint32: opcode, payload length) and a
// UTF-8 JSON payload. A connection starts with HANDSHAKE {"v":1,
// "client_id":...}; the client answers with a READY dispatch (or a CLOSE
// frame with an error). Rich Presence is the SET_ACTIVITY command in a FRAME,
// with the process id and the activity (or null to clear it). Discord's
// documentation: docs.discord.com/developers/topics/rpc (opcodes,
// SET_ACTIVITY) and the Rich Presence guides; the header layout is the one
// discord-rpc (MIT, deprecated) used and the client still accepts.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game::discord {

enum class Opcode : std::uint32_t { handshake = 0, frame = 1, close = 2, ping = 3, pong = 4 };

constexpr std::size_t kHeaderSize = 8;
// Larger payloads are refused (Discord's own limit is 64 KiB).
constexpr std::uint32_t kMaxPayload = 64 * 1024;

struct FrameHeader {
    Opcode opcode{};
    std::uint32_t length{};
};

// Header + payload, ready to write.
std::vector<std::uint8_t> encode_frame(Opcode opcode, std::string_view json);
// The header of the first 8 bytes; nullopt when fewer bytes, an unknown
// opcode or a length above kMaxPayload.
std::optional<FrameHeader> decode_header(std::span<const std::uint8_t> bytes);

// JSON string contents: quotes, backslashes and control characters escaped;
// other UTF-8 passes unchanged.
std::string json_escape(std::string_view text);
// Discord's activity strings take 2..128 bytes: longer text is cut at a
// UTF-8 boundary, a one-byte text padded with a space; empty stays empty
// (the field is then left out).
std::string activity_text(std::string_view text);

struct Activity {
    std::string details;     // first line ("WarGreymon Lv 42")
    std::string state;       // second line ("Death Valley: Humid Cave")
    std::string large_image; // art asset key
    std::string large_text;  // its hover text
    std::string small_image;
    std::string small_text;
    std::int64_t start = 0;  // unix seconds of the session start, 0: none
    // An online room's party (docs/SEAMLESS.md 13): an id that names no room
    // (party_id below) and the room's size, members of the whole room (up to
    // ten), not of the instance. Discord shows "(n of max)" after the state.
    // No join secret: joining goes through the room list (a secret would need
    // Discord's join events and could not carry a room's password).
    std::string party_id;
    int party_size = 0, party_max = 0;
    bool operator==(const Activity&) const = default;
};

// "dw4-" and 16 hex digits of FNV-1a 64 over the room code: the same party for
// every member of a room, the code itself never leaves the game.
std::string party_id(std::string_view room_code);

std::string handshake_json(std::string_view client_id);
// SET_ACTIVITY for the process; no activity clears it.
std::string set_activity_json(std::uint32_t pid, const std::optional<Activity>& activity, std::uint64_t nonce);

// The string value of "key" anywhere in a flat JSON text (the first match,
// escapes resolved for \" and \\); empty when missing. Enough for the
// replies' "cmd", "evt" and error "message".
std::string json_string_field(std::string_view json, std::string_view key);
// The integer value of "key" (error "code"); nullopt when missing.
std::optional<long long> json_int_field(std::string_view json, std::string_view key);

} // namespace dw4::game::discord
