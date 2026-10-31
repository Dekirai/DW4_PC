// Discord Rich Presence without Discord: the IPC framing and JSON, the
// area / form tables, and the presence worker against a fake discord-ipc
// pipe server (Windows), which checks the HANDSHAKE, SET_ACTIVITY with this
// process id, the send interval, the newest activity winning, a refused
// application id and the CLOSE at the end, and that a hung client never
// blocks: one that never answers (the attempts back off, quitting does not
// wait), one that sends a header without its payload, one that stops
// reading (a stuck write ends, quitting takes the CLOSE frame's moment
// only). The real client is never used: the pipes have test names.
#include "discord_ipc.hpp"
#include "discord_presence.hpp"
#include "presence_data.hpp"

#include <atomic>
#include <fstream>
#include <iterator>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace dw4::game;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void framing() {
    const auto frame = discord::encode_frame(discord::Opcode::frame, "{\"a\":1}");
    check(frame.size() == 8 + 7, "header + payload");
    check(frame[0] == 1 && frame[1] == 0 && frame[2] == 0 && frame[3] == 0, "opcode little endian");
    check(frame[4] == 7 && frame[5] == 0 && frame[6] == 0 && frame[7] == 0, "length little endian");
    check(std::memcmp(frame.data() + 8, "{\"a\":1}", 7) == 0, "payload follows");
    const auto header = discord::decode_header(frame);
    check(header && header->opcode == discord::Opcode::frame && header->length == 7, "decode");
    check(!discord::decode_header(std::span<const std::uint8_t>(frame.data(), 7)), "short header");
    std::vector<std::uint8_t> bad = discord::encode_frame(discord::Opcode::pong, "");
    bad[0] = 9;
    check(!discord::decode_header(bad), "unknown opcode");
    bad = discord::encode_frame(discord::Opcode::frame, "");
    bad[6] = 0x10; // 1 MiB
    check(!discord::decode_header(bad), "oversized payload");
}

void json() {
    check(discord::json_escape("a\"b\\c\nd\x01") == "a\\\"b\\\\c\\nd\\u0001", "escapes");
    check(discord::json_escape("Ä€") == "Ä€", "UTF-8 passes");
    check(discord::activity_text("") == "", "empty stays empty");
    check(discord::activity_text("A") == "A ", "one byte is padded to Discord's minimum of two");
    const std::string long_text(127, 'x');
    const std::string cut = discord::activity_text(long_text + "\xC3\xA4" + "yyy"); // an a-umlaut over the 128 bytes
    check(cut.size() == 127, "a UTF-8 sequence over the limit is dropped whole");
    check(discord::activity_text(std::string(200, 'z')).size() == 128, "cut to 128 bytes");
    check(discord::handshake_json("1560921673354453093") == "{\"v\":1,\"client_id\":\"1560921673354453093\"}", "handshake");
    discord::Activity activity{"WarGreymon Lv 42", "Death Valley: Humid Cave", "area_death_valley_dungeons", "Death Valley",
                               "form_wargreymon", "Very Hard", 1761700000};
    const auto set = discord::set_activity_json(4242, activity, 7);
    check(contains(set, "\"cmd\":\"SET_ACTIVITY\""), "command");
    check(contains(set, "\"pid\":4242"), "process id");
    check(contains(set, "\"details\":\"WarGreymon Lv 42\""), "details");
    check(contains(set, "\"state\":\"Death Valley: Humid Cave\""), "state");
    check(contains(set, "\"timestamps\":{\"start\":1761700000}"), "start time");
    check(contains(set, "\"assets\":{\"large_image\":\"area_death_valley_dungeons\",\"large_text\":\"Death Valley\","
                        "\"small_image\":\"form_wargreymon\",\"small_text\":\"Very Hard\"}"),
          "assets");
    check(contains(set, "\"nonce\":\"7\""), "nonce");
    check(contains(discord::set_activity_json(1, std::nullopt, 2), "\"activity\":null"), "clearing");
    check(!contains(set, "party"), "no party offline");
    // An online room's party: a hashed id and the room's size, never the code or a secret.
    discord::Activity room = activity;
    room.party_id = discord::party_id("ABCD12");
    room.party_size = 3;
    room.party_max = 10;
    const auto party = discord::set_activity_json(1, room, 4);
    check(room.party_id.size() == 20 && room.party_id.rfind("dw4-", 0) == 0 && room.party_id == discord::party_id("ABCD12") &&
              room.party_id != discord::party_id("ABCD13"),
          "party id");
    check(contains(party, "\"party\":{\"id\":\"" + room.party_id + "\",\"size\":[3,10]}") && !contains(party, "ABCD12") &&
              !contains(party, "secret"),
          "party size, no room code, no join secret");
    room.party_size = 11;
    check(!contains(discord::set_activity_json(1, room, 5), "party"), "a size above the maximum is left out");
    discord::Activity menus;
    menus.details = "In Menus";
    const auto menu_json = discord::set_activity_json(1, menus, 3);
    check(!contains(menu_json, "state") && !contains(menu_json, "timestamps") && !contains(menu_json, "assets"), "empty fields left out");
    const std::string reply = "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}";
    check(discord::json_string_field(reply, "evt") == "READY", "evt");
    check(discord::json_string_field(reply, "missing").empty(), "missing key");
    const std::string error = "{\"code\": 4000, \"message\": \"Invalid \\\"client\\\" ID\"}";
    check(discord::json_int_field(error, "code") == 4000, "error code");
    check(discord::json_string_field(error, "message") == "Invalid \"client\" ID", "escaped message");
}

void tables() {
    // Every stage folder of the game (the disc's stage packages).
    static const char* folders[] = {
        "dng101b1", "dng101b2", "dng102b1", "dng102b2", "dng102b3", "dng103f1", "dng103f2", "dng204b1", "dng204b2", "dng205b1",
        "dng205b2", "dng205b3", "dng206f1", "dng206f2", "dng307b1", "dng307b2", "dng308b1", "dng308b2", "dng308b3", "dng309b1",
        "dng309b2", "dng309b3", "dng309b4", "dng310f1", "dng310f2", "dng310f3", "dng411b1", "dng411b2", "dng411b3", "dng412b1",
        "dng412b2", "dng412b3", "dng412b4", "dngq1b1e", "dngq1b2e", "dngq2b1e", "dngq2b2e", "dngq2b2h", "dngq3b1e", "dngq3b2e",
        "dngq4b1e", "dngq4b1h", "dngq4b1v", "dngq4b2e", "dngq4b2h", "dngq4b2v", "dngq5b1e", "dngq5b1h", "dngq5b1v", "dngq6b01",
        "dngq7b1e", "dngq7b2e", "dngq8b1e", "dngq8b2e", "dngq8b3e", "dngq8b3h", "dngq8b3v", "stag0001", "stag0101", "stag0102",
        "stag0103", "stag0104", "stag0201", "stag0202", "stag0203", "stag0204", "stag0205", "stag0301", "stag0302", "stag0303",
        "stag0304", "stag0401", "stag0403", "stag0404", "stagBS01", "stagBS02", "stagBS03", "stagBS04", "stagBS05", "stagBS06",
        "stagGF01", "stagGF02", "stagGF03", "stagGF04", "stagIS08", "stagIS09", "stagIS10", "stagIS11", "stagIS12", "stagIS13",
        "stagIS20", "stagML02", "stagML11", "stagML12", "stagML13", "stagML14"};
    check(std::size(folders) == 96, "96 stage folders");
    std::set<std::string> keys;
    for (const auto& image : area_images()) keys.insert(image.key);
    check(keys.size() == area_images().size(), "image keys are unique");
    check(keys.size() >= 15 && keys.size() <= 20, "15-20 area images");
    std::set<std::string> folder_set;
    for (const auto& area : area_table()) folder_set.insert(area.folder);
    check(folder_set.size() == area_table().size(), "no folder twice");
    for (const char* folder : folders) {
        const auto* area = find_area(folder);
        check(area != nullptr, std::string("an area for ") + folder);
        check(keys.count(area->key) == 1, std::string("its image key is listed: ") + area->key);
        for (const char* key : {area->key}) {
            for (const char* c = key; *c; ++c) check((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_', "keys are lower case");
        }
    }
    check(find_area("STAG0103") && std::string(find_area("STAG0103")->place) == "Numenume River", "any letter case");
    check(!find_area("stag9999"), "unknown stage");
    check(area_display_name(*find_area("dng101b1")) == "Death Valley: Humid Cave", "region: place");
    check(area_display_name(*find_area("stag0001")) == "Death Valley: Practice Stage", "the practice stage");
    check(area_display_name(*find_area("stagis10")) == "Menus", "one name when both agree");
    check(is_menu_stage("stagis10") && is_menu_stage("stagIS12") && !is_menu_stage("stagML11") && !is_menu_stage("stagi"), "menu stages");
    check(forms().size() == 16, "16 forms");
    for (int i = 0; i < 16; ++i) {
        check(forms()[std::size_t(i)].species == i, "forms in row order");
        check(form_by_model(forms()[std::size_t(i)].model) == form_by_species(i), "model lookup");
    }
    check(std::string(form_by_species(15)->name) == "Susanomon" && std::string(form_by_model("P_AGUMON")->key) == "form_agumon", "names");
    check(!form_by_species(16) && !form_by_species(-1) && !form_by_model("p_none"), "out of range");
    check(std::string(difficulty_name(0)) == "Normal" && std::string(difficulty_name(2)) == "Very Hard", "difficulty");
    check(ascii_player_name(u"Ａｂｃ　１！") == "Abc 1!", "full-width forms to ASCII");
    check(ascii_player_name(std::u16string(u"ＴＯ\0X", 4)) == "TO", "stops at 0");
    check(ascii_player_name(u"アZ") == "?Z", "other characters");
}

#ifdef _WIN32
// A fake Discord client: one pipe instance, frames in and out. Every
// transfer is overlapped and bounded, so a test never hangs on the game.
class FakeDiscord {
public:
    explicit FakeDiscord(const std::string& name, DWORD buffer = 65536) {
        handle_ = CreateNamedPipeA(("\\\\.\\pipe\\" + name).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, buffer, buffer, 0, nullptr);
        event_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        check(handle_ != INVALID_HANDLE_VALUE && event_, "the fake pipe opens");
    }
    ~FakeDiscord() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle_, nullptr);
            DisconnectNamedPipe(handle_);
            CloseHandle(handle_);
        }
        if (event_) CloseHandle(event_);
    }
    // Waits for the game to connect.
    bool accept(std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event_;
        ResetEvent(event_);
        if (ConnectNamedPipe(handle_, &overlapped)) return true;
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) return true;
        if (error != ERROR_IO_PENDING) return false;
        return finish(overlapped, timeout);
    }
    // Ready for the next client after the last one went away.
    void reset() { DisconnectNamedPipe(handle_); }
    // The game's end is still open.
    bool alive() {
        DWORD available = 0;
        return PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr) != 0;
    }
    // Waits until the game closed its end. Nothing is read for `quiet`
    // (the game sees a client that does not read); after that what arrived
    // is read and dropped, since a closed end shows only once its data is
    // gone.
    bool wait_closed(std::chrono::milliseconds timeout, std::chrono::milliseconds quiet = std::chrono::milliseconds(0)) {
        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD available = 0;
            if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) return true;
            if (available && std::chrono::steady_clock::now() - start >= quiet) {
                std::vector<std::uint8_t> sink(available);
                if (!transfer(false, sink.data(), available, std::chrono::milliseconds(500))) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    // The next frame within `timeout`.
    std::optional<std::pair<discord::Opcode, std::string>> read(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD available = 0;
            if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) return std::nullopt;
            if (available >= 8) {
                std::uint8_t header[8];
                if (!transfer(false, header, 8, std::chrono::seconds(2))) return std::nullopt;
                const auto decoded = discord::decode_header(header);
                if (!decoded) return std::nullopt;
                std::string payload(decoded->length, '\0');
                if (decoded->length && !transfer(false, payload.data(), DWORD(payload.size()), std::chrono::seconds(2))) return std::nullopt;
                return std::make_pair(decoded->opcode, payload);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return std::nullopt;
    }
    bool write(discord::Opcode opcode, const std::string& json) { return write_raw(discord::encode_frame(opcode, json)); }
    bool write_raw(const std::vector<std::uint8_t>& bytes) {
        return transfer(true, const_cast<std::uint8_t*>(bytes.data()), DWORD(bytes.size()), std::chrono::seconds(2));
    }

private:
    bool finish(OVERLAPPED& overlapped, std::chrono::milliseconds timeout) {
        DWORD done = 0;
        if (WaitForSingleObject(event_, DWORD(timeout.count())) == WAIT_OBJECT_0) return GetOverlappedResult(handle_, &overlapped, &done, FALSE) != 0;
        CancelIoEx(handle_, &overlapped);
        GetOverlappedResult(handle_, &overlapped, &done, TRUE);
        return false;
    }
    // All `size` bytes in or out within `timeout`.
    bool transfer(bool writing, void* data, DWORD size, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        DWORD total = 0;
        while (total < size) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = event_;
            ResetEvent(event_);
            auto* at = static_cast<std::uint8_t*>(data) + total;
            const BOOL ok = writing ? WriteFile(handle_, at, size - total, nullptr, &overlapped) : ReadFile(handle_, at, size - total, nullptr, &overlapped);
            if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0 || !finish(overlapped, left)) return false;
            DWORD done = 0;
            GetOverlappedResult(handle_, &overlapped, &done, FALSE);
            if (done == 0) return false;
            total += done;
        }
        return true;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE event_ = nullptr;
};

std::string test_prefix(const char* what) {
    return "dw4test-" + std::to_string(GetCurrentProcessId()) + "-" + what + "-discord-ipc-";
}

void fake_client_session() {
    const std::string prefix = test_prefix("ok");
    // The worker tries pipes 0..9: the fake one is number 3.
    FakeDiscord server(prefix + "3");
    DiscordPresence::Options options;
    options.pipe_prefix = prefix;
    options.retry_interval = std::chrono::milliseconds(200);
    options.send_interval = std::chrono::milliseconds(600);
    std::atomic<bool> server_ok{false};
    std::string failure;
    std::thread fake([&] {
        try {
            check(server.accept(), "the game connects");
            const auto hello = server.read(std::chrono::seconds(5));
            check(hello && hello->first == discord::Opcode::handshake, "HANDSHAKE first");
            check(hello->second == discord::handshake_json("1560921673354453093"), "v 1 and the application id: " + hello->second);
            server.write(discord::Opcode::frame, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1,\"config\":{}},\"evt\":\"READY\",\"nonce\":null}");
            const auto first = server.read(std::chrono::seconds(5));
            check(first && first->first == discord::Opcode::frame, "SET_ACTIVITY in a FRAME");
            check(contains(first->second, "\"cmd\":\"SET_ACTIVITY\""), "the command");
            check(contains(first->second, "\"pid\":" + std::to_string(GetCurrentProcessId())), "this process id");
            check(contains(first->second, "\"details\":\"Agumon Lv 1\""), "the first activity: " + first->second);
            server.write(discord::Opcode::frame, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"evt\":null,\"nonce\":\"1\"}");
            // A ping is answered with a pong carrying the same payload.
            server.write(discord::Opcode::ping, "{\"p\":1}");
            const auto t0 = std::chrono::steady_clock::now();
            std::optional<std::pair<discord::Opcode, std::string>> second;
            bool pong = false;
            while (!second) {
                auto frame = server.read(std::chrono::seconds(5));
                check(frame.has_value(), "more frames");
                if (frame->first == discord::Opcode::pong) {
                    check(frame->second == "{\"p\":1}", "pong payload");
                    pong = true;
                } else {
                    second = frame;
                }
            }
            const auto waited = std::chrono::steady_clock::now() - t0;
            check(pong, "PING answered");
            check(contains(second->second, "\"details\":\"Agumon Lv 3\""), "only the newest activity is sent: " + second->second);
            check(waited >= std::chrono::milliseconds(400), "the send interval holds updates back");
            const auto closing = server.read(std::chrono::seconds(5));
            check(closing && closing->first == discord::Opcode::close, "CLOSE at the end");
            server_ok = true;
        } catch (const std::exception& error) {
            failure = error.what();
        }
    });
    {
        DiscordPresence presence(options);
        discord::Activity activity;
        activity.details = "Agumon Lv 1";
        presence.set_activity(activity);
        presence.configure(true, "1560921673354453093");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (presence.sent_count() < 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(presence.state() == DiscordPresence::State::connected, "connected: " + presence.status());
        // Two changes inside the interval: the second wins.
        activity.details = "Agumon Lv 2";
        presence.set_activity(activity);
        activity.details = "Agumon Lv 3";
        presence.set_activity(activity);
        while (presence.sent_count() < 2 && std::chrono::steady_clock::now() < deadline + std::chrono::seconds(3))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(presence.sent_count() == 2, "two updates sent");
    } // destructor: CLOSE
    fake.join();
    check(server_ok, "fake client: " + failure);
}

void refused() {
    const std::string prefix = test_prefix("refused");
    FakeDiscord server(prefix + "0");
    DiscordPresence::Options options;
    options.pipe_prefix = prefix;
    options.retry_interval = std::chrono::seconds(30);
    std::thread fake([&] {
        if (!server.accept()) return;
        if (server.read(std::chrono::seconds(5))) server.write(discord::Opcode::close, "{\"code\":4000,\"message\":\"Invalid Client ID\"}");
        server.read(std::chrono::seconds(2));
    });
    DiscordPresence presence(options);
    presence.configure(true, "123");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (presence.state() != DiscordPresence::State::rejected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(presence.state() == DiscordPresence::State::rejected, "refused: " + presence.status());
    check(contains(presence.status(), "Invalid Client ID (4000)"), "the reason shows: " + presence.status());
    check(presence.sent_count() == 0, "nothing sent");
    fake.join();
}

void no_discord() {
    // Nothing listens: the worker keeps looking, nothing blocks, the
    // destructor returns at once.
    DiscordPresence::Options options;
    options.pipe_prefix = test_prefix("none");
    const auto t0 = std::chrono::steady_clock::now();
    {
        DiscordPresence presence(options);
        check(presence.state() == DiscordPresence::State::off, "off until configured");
        presence.configure(true, "1560921673354453093");
        presence.set_activity(discord::Activity{"In Menus"});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (presence.state() != DiscordPresence::State::searching && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(presence.state() == DiscordPresence::State::searching, "searching: " + presence.status());
        presence.configure(false, "1560921673354453093");
        const auto off_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (presence.state() != DiscordPresence::State::off && std::chrono::steady_clock::now() < off_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(presence.state() == DiscordPresence::State::off, "the setting off: off");
    }
    check(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5), "no waiting without Discord");
}

using std::chrono::milliseconds;
using std::chrono::steady_clock;

// Joins a fake's thread however the test leaves its scope (a failed check
// must report, not terminate on a joinable thread).
struct Joined {
    std::thread& thread;
    ~Joined() {
        if (thread.joinable()) thread.join();
    }
};

long long ms_since(steady_clock::time_point t0) {
    return std::chrono::duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

// The first 8 bytes of a frame: a header announcing a payload that never
// comes.
std::vector<std::uint8_t> header_only_frame(std::uint32_t length) {
    auto frame = discord::encode_frame(discord::Opcode::frame, std::string(length, ' '));
    frame.resize(discord::kHeaderSize);
    return frame;
}

const std::string kReady = "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1,\"config\":{}},\"evt\":\"READY\",\"nonce\":null}";

// A client that takes the connection and never says a word (not even reads
// the handshake): every attempt ends after reply_timeout, the attempts come
// later and later (backoff), and quitting while the worker waits for the
// answer returns at once.
void silent_discord() {
    const std::string prefix = test_prefix("silent");
    FakeDiscord server(prefix + "0");
    DiscordPresence::Options options;
    options.pipe_prefix = prefix;
    options.retry_interval = milliseconds(200);
    options.max_retry_interval = milliseconds(800);
    options.reply_timeout = milliseconds(150);
    options.io_timeout = milliseconds(150);
    std::vector<steady_clock::time_point> accepted;
    std::atomic<int> count{0};
    std::thread fake([&] {
        while (count < 5) {
            if (!server.accept(milliseconds(5000))) break;
            accepted.push_back(steady_clock::now());
            ++count;
            server.wait_closed(milliseconds(5000), milliseconds(170)); // past the reply time
            server.reset();
        }
    });
    Joined join_fake{fake};
    long long quit_ms = 0;
    {
        auto presence = std::make_unique<DiscordPresence>(options);
        presence->configure(true, "1560921673354453093");
        const auto deadline = steady_clock::now() + std::chrono::seconds(10);
        while (count < 5 && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(5));
        check(count == 5, "the worker keeps trying: " + std::to_string(count.load()) + " attempts, " + presence->status());
        check(presence->failures() >= 4, "four failed attempts counted: " + std::to_string(presence->failures()));
        check(contains(presence->status(), "Discord did not answer; trying again in"), "the status says so: " + presence->status());
        check(presence->sent_count() == 0 && presence->connections() == 0, "never connected");
        // The fifth attempt waits for READY now: quitting does not wait.
        std::this_thread::sleep_for(milliseconds(50));
        const auto t0 = steady_clock::now();
        presence.reset();
        quit_ms = ms_since(t0);
    }
    fake.join();
    check(quit_ms < 700, "quitting during the handshake wait is quick: " + std::to_string(quit_ms) + " ms");
    // Attempt n + 1 starts reply_timeout + the backoff after attempt n:
    // 200, 400, 800, 800 ms of backoff.
    std::vector<long long> gaps;
    for (std::size_t i = 1; i < accepted.size(); ++i)
        gaps.push_back(std::chrono::duration_cast<milliseconds>(accepted[i] - accepted[i - 1]).count());
    check(gaps.size() == 4, "four gaps");
    check(gaps[1] >= gaps[0] + 120 && gaps[2] >= gaps[1] + 250, "the wait doubles: " + std::to_string(gaps[0]) + " / " + std::to_string(gaps[1]) +
                                                              " / " + std::to_string(gaps[2]) + " / " + std::to_string(gaps[3]) + " ms");
    check(gaps[3] < gaps[2] + 200, "up to the cap: " + std::to_string(gaps[2]) + " / " + std::to_string(gaps[3]) + " ms");
}

// A client that answers the handshake with a header and no payload: the
// read of the payload ends after io_timeout (not the whole reply time), the
// connection is dropped and the failure counted.
void header_only_discord() {
    const std::string prefix = test_prefix("half");
    FakeDiscord server(prefix + "0");
    DiscordPresence::Options options;
    options.pipe_prefix = prefix;
    options.retry_interval = std::chrono::seconds(30);
    options.reply_timeout = std::chrono::seconds(5);
    options.io_timeout = milliseconds(300);
    std::atomic<bool> handshake{false};
    std::thread fake([&] {
        if (!server.accept(milliseconds(5000))) return;
        if (server.read(milliseconds(5000))) handshake = true;
        server.write_raw(header_only_frame(100));
        server.wait_closed(milliseconds(5000));
    });
    Joined join_fake{fake};
    const auto t0 = steady_clock::now();
    long long failed_ms = 0, quit_ms = 0;
    {
        auto presence = std::make_unique<DiscordPresence>(options);
        presence->configure(true, "1560921673354453093");
        const auto deadline = steady_clock::now() + std::chrono::seconds(4);
        while (presence->failures() == 0 && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(5));
        failed_ms = ms_since(t0);
        check(presence->failures() == 1, "the half message is a failure: " + presence->status());
        check(contains(presence->status(), "Discord sent half a message; trying again in 30 s"), "the status says so: " + presence->status());
        check(presence->state() == DiscordPresence::State::searching && presence->connections() == 0, "not connected");
        const auto q0 = steady_clock::now();
        presence.reset();
        quit_ms = ms_since(q0);
    }
    fake.join();
    check(handshake, "the handshake arrived");
    check(failed_ms < 2000, "dropped after the payload timeout, not the reply time: " + std::to_string(failed_ms) + " ms");
    check(quit_ms < 700, "quitting is quick: " + std::to_string(quit_ms) + " ms");
}

// A client that answers READY and then stops reading (a one-byte pipe
// buffer): an activity write that cannot finish ends after io_timeout and
// the worker reconnects later; quitting while such a write hangs returns
// after the CLOSE frame's short time.
void deaf_discord() {
    const std::string prefix = test_prefix("deaf");
    discord::Activity big;
    big.details = std::string(120, 'd');
    big.state = std::string(120, 's');
    big.large_image = std::string(120, 'l');
    big.large_text = std::string(120, 't');
    big.small_image = std::string(120, 'i');
    big.small_text = std::string(120, 'x');
    {
        FakeDiscord server(prefix + "0", 1);
        DiscordPresence::Options options;
        options.pipe_prefix = prefix;
        options.retry_interval = milliseconds(150);
        options.io_timeout = milliseconds(300);
        options.send_interval = milliseconds(20);
        std::atomic<int> accepted{0};
        std::thread fake([&] {
            for (int round = 0; round < 2; ++round) {
                if (!server.accept(milliseconds(5000))) return;
                ++accepted;
                if (!server.read(milliseconds(5000))) return;
                server.write(discord::Opcode::frame, kReady);
                server.wait_closed(milliseconds(5000), milliseconds(500)); // past the write timeout
                server.reset();
            }
        });
        Joined join_fake{fake};
        {
            DiscordPresence presence(options);
            presence.set_activity(big);
            presence.configure(true, "1560921673354453093");
            const auto deadline = steady_clock::now() + std::chrono::seconds(5);
            while (presence.failures() == 0 && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(5));
            check(presence.failures() == 1, "the stuck write is a failure: " + presence.status());
            check(contains(presence.status(), "Discord stopped reading; trying again in"), "the status says so: " + presence.status());
            check(presence.connections() == 1, "it had connected");
            while (accepted < 2 && steady_clock::now() < deadline + std::chrono::seconds(2)) std::this_thread::sleep_for(milliseconds(5));
            check(accepted == 2, "it connects again after the backoff");
        }
        fake.join();
    }
    // Quitting right after READY, while the first activity write hangs.
    {
        FakeDiscord server(prefix + "5", 1);
        DiscordPresence::Options options;
        options.pipe_prefix = prefix;
        options.io_timeout = std::chrono::seconds(20);
        options.close_timeout = milliseconds(250);
        options.shutdown_timeout = milliseconds(500);
        std::atomic<bool> ready{false};
        std::thread fake([&] {
            if (!server.accept(milliseconds(5000))) return;
            if (!server.read(milliseconds(5000))) return;
            server.write(discord::Opcode::frame, kReady);
            ready = true;
            server.wait_closed(milliseconds(10000), milliseconds(2000)); // past the CLOSE time
        });
        Joined join_fake{fake};
        long long quit_ms = 0;
        {
            auto presence = std::make_unique<DiscordPresence>(options);
            presence->set_activity(big);
            presence->configure(true, "1560921673354453093");
            const auto deadline = steady_clock::now() + std::chrono::seconds(5);
            while (presence->connections() == 0 && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(5));
            check(presence->connections() == 1, "connected: " + presence->status());
            std::this_thread::sleep_for(milliseconds(100)); // the activity write is stuck now (20 s io timeout)
            check(presence->sent_count() == 0, "the write did not finish");
            const auto t0 = steady_clock::now();
            presence.reset();
            quit_ms = ms_since(t0);
        }
        fake.join();
        check(quit_ms < 900, "quitting with a stuck write takes the CLOSE time only: " + std::to_string(quit_ms) + " ms");
    }
}
#endif

#ifdef _WIN32
// --serve PIPE SECONDS [MODE]: a fake Discord on \\.\pipe\PIPE for an
// end-to-end run of the game (DW4_DISCORD_PIPE=<PIPE without its last
// digit>). It prints every connection and frame until the time is up.
// MODE ready (the default) answers the handshake with READY (and ends at
// the CLOSE), silent takes each connection and never reads or answers,
// header answers the handshake with a header without its payload, deaf
// answers READY and then never reads again.
int serve(const std::string& name, int seconds, const std::string& mode) {
    FakeDiscord server(name, mode == "deaf" ? 1 : 65536);
    std::cout << "fake discord on " << name << " (" << mode << ")" << std::endl;
    const auto deadline = steady_clock::now() + std::chrono::seconds(seconds);
    auto left = [&] { return std::chrono::duration_cast<milliseconds>(deadline - steady_clock::now()); };
    for (int connection = 1; left().count() > 0; ++connection) {
        if (!server.accept(left())) break;
        std::cout << "connection " << connection << std::endl;
        if (mode == "ready") {
            while (left().count() > 0) {
                const auto frame = server.read(std::min(left(), milliseconds(500)));
                if (!frame) {
                    if (!server.alive()) {
                        std::cout << "closed by the game" << std::endl;
                        break;
                    }
                    continue;
                }
                std::cout << "frame " << unsigned(frame->first) << " " << frame->second << std::endl;
                if (frame->first == discord::Opcode::handshake) server.write(discord::Opcode::frame, kReady);
                if (frame->first == discord::Opcode::close) return 0;
            }
        } else {
            if (mode != "silent") {
                if (const auto hello = server.read(left())) std::cout << "frame " << unsigned(hello->first) << " " << hello->second << std::endl;
                if (mode == "header") server.write_raw(header_only_frame(100));
                else server.write(discord::Opcode::frame, kReady);
            }
            // The game's own waits (reply 3 s, a frame 2 s) are over by then.
            server.wait_closed(left(), mode == "header" ? milliseconds(0) : milliseconds(6000));
            std::cout << "closed by the game" << std::endl;
        }
        server.reset();
    }
    return 0;
}
#endif

// The stage names the website's room page reads (data/online/areas.json).
void area_file(const std::string& file) {
    const auto text = dw4::game::area_names_json();
    check(text.find("\"stage\": \"stag0101\", \"name\": \"Death Valley: Goblin Pass\"") != std::string::npos, "an area line");
    check(text.find('\r') == std::string::npos && text.ends_with("]\n}\n"), "LF, complete");
    for (const auto& area : dw4::game::area_table())
        for (const char* c = area.place; *c; ++c) check(*c != '"' && *c != '\\' && static_cast<unsigned char>(*c) < 0x80, "plain ASCII names");
    if (file.empty()) return;
    std::ifstream in(file, std::ios::binary);
    check(bool(in), "the checked-in area names " + file);
    std::string checked_in((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::erase(checked_in, '\r'); // (a checkout with CRLF line ends)
    check(checked_in == text, "data/online/areas.json is the client's table (regenerate: dw4_presence_tests --export-areas FILE)");
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--serve") return serve(argv[2], std::atoi(argv[3]), argc == 5 ? argv[4] : "ready");
#endif
    // --export-areas FILE: writes data/online/areas.json.
    if (argc == 3 && std::string(argv[1]) == "--export-areas") {
        std::ofstream(argv[2], std::ios::binary | std::ios::trunc) << dw4::game::area_names_json();
        std::cout << "wrote " << argv[2] << '\n';
        return 0;
    }
    try {
        framing();
        json();
        tables();
        area_file(argc == 2 ? argv[1] : "");
#ifdef _WIN32
        no_discord();
        fake_client_session();
        refused();
        silent_discord();
        header_only_discord();
        deaf_discord();
#endif
        std::cout << checks << " presence checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Presence test failure: " << error.what() << '\n';
        return 1;
    }
}
