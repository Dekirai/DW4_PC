// Discord Rich Presence without Discord: the IPC framing and JSON, the
// area / form tables, and the presence worker against a fake discord-ipc
// pipe server (Windows), which checks the HANDSHAKE, SET_ACTIVITY with this
// process id, the send interval, the newest activity winning, a refused
// application id and the CLOSE at the end. The real client is never used:
// the pipes have test names.
#include "discord_ipc.hpp"
#include "discord_presence.hpp"
#include "presence_data.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
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
// A fake Discord client: one pipe instance, frames in and out.
class FakeDiscord {
public:
    explicit FakeDiscord(const std::string& name) {
        handle_ = CreateNamedPipeA(("\\\\.\\pipe\\" + name).c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                                   65536, 65536, 0, nullptr);
        check(handle_ != INVALID_HANDLE_VALUE, "the fake pipe opens");
    }
    ~FakeDiscord() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(handle_);
            CloseHandle(handle_);
        }
    }
    // Waits for the game to connect (ConnectNamedPipe blocks; the client
    // connects within the test's time).
    bool accept() {
        const BOOL ok = ConnectNamedPipe(handle_, nullptr);
        return ok || GetLastError() == ERROR_PIPE_CONNECTED;
    }
    // The next frame within `timeout`, polling (no blocking read).
    std::optional<std::pair<discord::Opcode, std::string>> read(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD available = 0;
            if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) return std::nullopt;
            if (available >= 8) {
                std::uint8_t header[8];
                DWORD got = 0;
                if (!ReadFile(handle_, header, 8, &got, nullptr) || got != 8) return std::nullopt;
                const auto decoded = discord::decode_header(header);
                if (!decoded) return std::nullopt;
                std::string payload(decoded->length, '\0');
                std::size_t done = 0;
                while (done < payload.size()) {
                    if (!ReadFile(handle_, payload.data() + done, DWORD(payload.size() - done), &got, nullptr) || got == 0) return std::nullopt;
                    done += got;
                }
                return std::make_pair(decoded->opcode, payload);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return std::nullopt;
    }
    void write(discord::Opcode opcode, const std::string& json) {
        const auto frame = discord::encode_frame(opcode, json);
        DWORD wrote = 0;
        WriteFile(handle_, frame.data(), DWORD(frame.size()), &wrote, nullptr);
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
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
#endif

#ifdef _WIN32
// --serve PIPE SECONDS: a fake Discord on \\.\pipe\PIPE for an end-to-end
// run of the game (DW4_DISCORD_PIPE=<PIPE without its last digit>): answers
// the handshake with READY and prints every frame until CLOSE or the time
// is up.
int serve(const std::string& name, int seconds) {
    FakeDiscord server(name);
    std::cout << "fake discord on " << name << std::endl;
    if (!server.accept()) return 1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto frame = server.read(std::chrono::milliseconds(500));
        if (!frame) continue;
        std::cout << "frame " << unsigned(frame->first) << " " << frame->second << std::endl;
        if (frame->first == discord::Opcode::handshake)
            server.write(discord::Opcode::frame, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}");
        if (frame->first == discord::Opcode::close) return 0;
    }
    return 0;
}
#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc == 4 && std::string(argv[1]) == "--serve") return serve(argv[2], std::atoi(argv[3]));
#else
    (void)argc;
    (void)argv;
#endif
    try {
        framing();
        json();
        tables();
#ifdef _WIN32
        no_discord();
        fake_client_session();
        refused();
#endif
        std::cout << checks << " presence checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Presence test failure: " << error.what() << '\n';
        return 1;
    }
}
