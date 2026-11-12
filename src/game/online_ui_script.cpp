#include "online_ui_script.hpp"

#include "dw4/pad.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dw4::game {

namespace {

std::string lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}

// "$NAME" (a whole word or its value after '=') is the environment variable NAME.
std::string expand(const std::string& word) {
    const auto at = word.find('$');
    if (at == std::string::npos) return word;
    const char* value = std::getenv(word.substr(at + 1).c_str());
    return word.substr(0, at) + (value ? value : "");
}

double number(const std::vector<std::string>& words, std::size_t index, double fallback) {
    return index < words.size() ? std::atof(words[index].c_str()) : fallback;
}

// key=value options after the first words of a command.
std::string option(const std::vector<std::string>& words, std::size_t from, const std::string& key, const std::string& fallback = {}) {
    for (std::size_t i = from; i < words.size(); ++i) {
        if (words[i] == key) return "1";
        if (words[i].rfind(key + "=", 0) == 0) return words[i].substr(key.size() + 1);
    }
    return fallback;
}


} // namespace

bool OnlineUiScript::load(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file);
    if (!in) {
        error = "cannot read " + file.string();
        return false;
    }
    std::string text;
    int number = 0;
    while (std::getline(in, text)) {
        ++number;
        if (const auto hash = text.find('#'); hash != std::string::npos) text.erase(hash);
        std::istringstream words(text);
        Line line;
        line.number = number;
        // Words; "quoted words" keep their spaces.
        std::string word;
        while (words >> std::ws && !words.eof()) {
            if (words.peek() == '"') {
                words.get();
                std::getline(words, word, '"');
            } else {
                words >> word;
            }
            line.words.push_back(word);
        }
        if (!line.words.empty()) lines_.push_back(std::move(line));
    }
    loaded_ = true;
    return true;
}

void OnlineUiScript::update(UiScriptHost& host) {
    if (!active()) return;
    ++passes_;
    if (at_ >= lines_.size()) {
        done_ = true;
        return;
    }
    const Line& line = lines_[at_];
    // A command starts once the lobby has nothing in flight.
    if (!sent_ && (host.lobby.busy() || host.lobby.gate() == online::Gate::checking)) return;
    if (!run(line, host)) return;
    const auto& w = line.words;
    // (Secrets stay out of the log: only the command and its first word.)
    const bool secret = w[0] == "login" || w[0] == "register" || w[0] == "recover" || w[0] == "password" || w[0] == "fill";
    std::string shown = w[0];
    // (reset-character's only word is the password; a join's password= option is not shown either.)
    for (std::size_t i = 1; i < w.size() && (!secret || i < 2) && w[0] != "reset-character"; ++i)
        shown += " " + (w[i].rfind("password=", 0) == 0 ? std::string("password=...") : w[i]);
    if (w[0] == "fill" && w.size() > 2 && w[1].find("password") == std::string::npos && w[1].find("again") == std::string::npos &&
        w[1].find("code") == std::string::npos)
        shown += " " + w[2];
    std::printf("online-ui: [%d] line %d %s -> screen %s, gate %s%s%s%s%s\n", host.frame, line.number, shown.c_str(),
                online::screen_name(host.lobby.screen()), online::gate_name(host.lobby.gate()), host.lobby.error().empty() ? "" : ", error: ",
                host.lobby.error().c_str(), host.lobby.info().empty() ? "" : ", info: ", host.lobby.info().c_str());
    std::fflush(stdout);
    ++at_;
    sent_ = false;
    deadline_ = -1;
    if (at_ >= lines_.size()) done_ = true;
}

bool OnlineUiScript::run(const Line& line, UiScriptHost& host) {
    const auto& w = line.words;
    auto& lobby = host.lobby;
    const std::string& c = w[0];
    auto arg = [&](std::size_t i) { return i < w.size() ? expand(w[i]) : std::string(); };
    auto fail = [&](const std::string& why) {
        throw std::runtime_error("online-ui line " + std::to_string(line.number) + " (" + c + "): " + why +
                                 " (screen " + online::screen_name(lobby.screen()) + ", gate " + online::gate_name(lobby.gate()) +
                                 (lobby.error().empty() ? "" : ", error: " + lobby.error()) + ")");
    };
    // A wait-* command's limit.
    auto timed = [&](std::size_t index, double fallback) {
        if (deadline_ < 0) deadline_ = host.now + number(w, index, fallback);
        if (host.now > deadline_) fail("timed out");
    };
    // A command that sends a request: once, then until the answer.
    auto once = [&](auto&& action) {
        if (!sent_) {
            action();
            sent_ = true;
            return false;
        }
        return !lobby.busy();
    };

    if (c == "wait") {
        if (!sent_) {
            wait_until_ = passes_ + (long long)number(w, 1, 1);
            sent_ = true;
        }
        return passes_ >= wait_until_;
    }
    if (c == "sleep") {
        if (deadline_ < 0) deadline_ = host.now + number(w, 1, 1);
        return host.now >= deadline_;
    }
    if (c == "wait-title") {
        timed(1, 120);
        return host.title_shown;
    }
    if (c == "wait-menu") {
        timed(1, 120);
        return host.menu_waiting && !host.screens.active;
    }
    if (c == "to-menu") {
        // START at the title (again every second) until its menu waits.
        timed(1, 120);
        if (host.menu_waiting) return true;
        if (host.title_shown && host.now >= next_press_) {
            host.pad = dw4::pad_button::start;
            next_press_ = host.now + 1.0;
        }
        return false;
    }
    if (c == "wait-screens") {
        timed(1, 30);
        return host.screens_shown;
    }
    if (c == "expect-screens") {
        if (host.screens.active != (arg(1) == "open")) fail("expected the online screens " + arg(1));
        return true;
    }
    if (c == "expect-cursor") {
        if (!host.title_entry) fail("the title menu has no Online entry");
        if (host.cursor != std::atoi(arg(1).c_str())) fail("expected the title menu's cursor at " + arg(1) + ", it is at " + std::to_string(host.cursor));
        return true;
    }
    if (c == "pad") {
        // One press per word, a quarter second apart (the engine steps at
        // its own pace in a UI run; the host holds each for three steps).
        if (!sent_) {
            key_at_ = 1;
            next_press_ = host.now;
            sent_ = true;
        }
        if (host.now < next_press_) return false;
        if (key_at_ >= w.size()) return true;
        // (The title menu takes a press only while it waits: its cursor
        // slides take a third of a second.)
        if (host.menu_open && !host.menu_waiting) return false;
        static const std::pair<const char*, std::uint32_t> names[] = {
            {"A", dw4::pad_button::a},       {"B", dw4::pad_button::b},       {"X", dw4::pad_button::x},
            {"Y", dw4::pad_button::y},       {"START", dw4::pad_button::start}, {"UP", dw4::pad_button::up},
            {"DOWN", dw4::pad_button::down}, {"LEFT", dw4::pad_button::left}, {"RIGHT", dw4::pad_button::right},
        };
        const auto& name = w[key_at_];
        const auto found = std::find_if(std::begin(names), std::end(names), [&](const auto& n) { return name == n.first; });
        if (found == std::end(names)) fail("unknown pad button " + name);
        host.pad = found->second;
        ++key_at_;
        next_press_ = host.now + 0.25;
        return false;
    }
    if (c == "panel") {
        if (arg(1) != "close") host.open_screens = true;
        else host.screens.active = false;
        return true;
    }
    if (c == "menu") {
        host.show_menu = arg(1) != "close";
        if (host.show_menu && arg(1) == "cheats") {
            host.menu_tab = "Cheats"; // (SEAMLESS.md 9.1.10: the tab in a room)
        } else if (host.show_menu) {
            host.menu_tab = "Online";
            if (!lobby.opened()) lobby.open();
        }
        return true;
    }
    if (c == "open") {
        // The lobby without a panel or menu (the version gate, the remembered login).
        if (!sent_) {
            lobby.open();
            sent_ = true;
            return false;
        }
        return lobby.gate() != online::Gate::checking && !lobby.busy();
    }
    if (c == "screen") {
        const auto screen = online::screen_from_name(arg(1));
        if (!screen) fail("unknown screen " + arg(1));
        lobby.go(*screen);
        return true;
    }
    if (c == "back") {
        lobby.back();
        return true;
    }
    if (c == "view") {
        host.view_button = true;
        return true;
    }
    if (c == "key") {
        // One key per pass, so each is pressed and released on its own.
        if (!sent_) {
            key_at_ = 1;
            sent_ = true;
        }
        if (key_at_ >= w.size()) return true;
        static const std::pair<const char*, ImGuiKey> names[] = {
            {"up", ImGuiKey_UpArrow},        {"down", ImGuiKey_DownArrow},        {"left", ImGuiKey_LeftArrow},
            {"right", ImGuiKey_RightArrow},  {"enter", ImGuiKey_Enter},           {"space", ImGuiKey_Space},
            {"escape", ImGuiKey_Escape},     {"tab", ImGuiKey_Tab},               {"pad-up", ImGuiKey_GamepadDpadUp},
            {"pad-down", ImGuiKey_GamepadDpadDown}, {"pad-left", ImGuiKey_GamepadDpadLeft}, {"pad-right", ImGuiKey_GamepadDpadRight},
            {"pad-a", ImGuiKey_GamepadFaceDown}, {"pad-b", ImGuiKey_GamepadFaceRight}, {"pad-x", ImGuiKey_GamepadFaceLeft},
            {"pad-y", ImGuiKey_GamepadFaceUp}, {"pad-start", ImGuiKey_GamepadStart},
        };
        const auto& name = w[key_at_];
        const auto found = std::find_if(std::begin(names), std::end(names), [&](const auto& n) { return name == n.first; });
        if (found == std::end(names)) fail("unknown key " + name);
        // (Every other pass: the key goes up in between.)
        if ((passes_ & 1) == 0) return false;
        host.keys.push_back(int(found->second));
        ++key_at_;
        return false;
    }
    if (c == "fill") {
        std::string value;
        for (std::size_t i = 2; i < w.size(); ++i) value += (i > 2 ? " " : "") + expand(w[i]);
        if (!set_ui_field(host.state, arg(1), value)) fail("unknown field or value " + arg(1));
        return true;
    }
    if (c == "login") return once([&] { lobby.login(arg(1), arg(2), arg(3) == "remember"); });
    if (c == "register") return once([&] { lobby.register_account(arg(1), arg(2), arg(2), arg(3), true); });
    if (c == "ack") {
        lobby.acknowledge_code();
        return true;
    }
    if (c == "recover") return once([&] { lobby.recover(arg(1), arg(2), arg(3), arg(3)); });
    if (c == "logout") return once([&] { lobby.logout(); });
    if (c == "logout-all") return once([&] { lobby.logout_everywhere(); });
    if (c == "password") return once([&] { lobby.change_password(arg(1), arg(2), arg(2)); });
    if (c == "reset-character") return once([&] { lobby.reset_character(arg(1)); });
    if (c == "refresh") {
        if (!sent_) {
            serial_ = lobby.counts_serial();
            lobby.refresh_world();
            sent_ = true;
            return false;
        }
        timed(w.size(), 30);
        return lobby.counts_serial() != serial_;
    }
    if (c == "play") {
        if (!sent_) {
            lobby.play();
            sent_ = true;
            return false;
        }
        return !lobby.busy() && lobby.screen() != online::Screen::starting;
    }
    if (c == "settings") {
        if (!sent_) {
            online::api::OnlinePrefs prefs = lobby.prefs().value_or(online::api::OnlinePrefs{});
            prefs.default_visibility = online::api::visibility_key(std::atoi(option(w, 1, "visibility", "0").c_str()));
            prefs.settings.camera_mode = std::atoi(option(w, 1, "camera", "0").c_str());
            prefs.settings.light_mode = std::atoi(option(w, 1, "light", "1").c_str());
            prefs.settings.fast_loading = !option(w, 1, "fast").empty();
            prefs.settings.rand_seed = std::uint32_t(std::strtoul(option(w, 1, "seed", "0").c_str(), nullptr, 10));
            prefs.settings.rand_enemies = std::atoi(option(w, 1, "enemies", "0").c_str());
            prefs.settings.rand_zones = std::atoi(option(w, 1, "zones", "0").c_str());
            prefs.settings.rand_drops = !option(w, 1, "drops").empty();
            lobby.save_settings(prefs);
            sent_ = true;
            return false;
        }
        return !lobby.busy();
    }
    if (c == "confirm" || c == "cancel") {
        if (!sent_) {
            if (c == "confirm") lobby.confirm_start();
            else lobby.cancel_start();
            sent_ = true;
            return false;
        }
        return !lobby.busy() && lobby.screen() != online::Screen::starting;
    }
    if (c == "wait-session") {
        timed(1, 60);
        return host.online_active && lobby.screen() == online::Screen::playing;
    }
    if (c == "wait-instance") {
        const bool home = arg(1) == "home";
        timed(home ? 2 : 1, 60);
        return host.live && (home ? host.live->wid == 0 : host.live->wid != 0);
    }
    if (c == "wait-members") {
        timed(2, 60);
        if (!host.live || host.live->wid == 0) return false;
        // N: at least N players; <=N: at most N (the others left).
        const int members = int(host.live->members.size());
        const auto& want = arg(1);
        return want.starts_with("<=") ? members <= std::atoi(want.c_str() + 2) : members >= std::atoi(want.c_str());
    }
    if (c == "visibility") {
        host.state.visibility_requested = std::clamp(std::atoi(arg(1).c_str()), 0, 2);
        return true;
    }
    if (c == "leave-instance") {
        host.state.leave_instance_requested = true;
        return true;
    }
    if (c == "card" || c == "remove" || c == "report") {
        // card NAME: the player card of that member opens (a screenshot shows it);
        // remove NAME / report NAME REASON: its buttons, confirmed (round I1b).
        if (!host.live) fail("no world session");
        const RoomLive::Member* who = nullptr;
        for (const auto& m : host.live->members)
            if (lower(m.name) == lower(arg(1))) who = &m;
        if (!who) fail("no member " + arg(1) + " in the own instance");
        if (c == "card") {
            host.state.card_member = who->member;
            host.state.card_remove_asked = host.state.card_report_open = false;
            host.state.card_focus = true; // (as A on the row: the controller's focus on the card's first button)
            host.state.card_cursor = true;
        } else if (c == "remove") {
            host.state.remove_requested = who->member;
        } else {
            static constexpr const char* reasons[] = {"griefing", "cheating", "name", "other"};
            int reason = 0;
            for (int r = 0; r < 4; ++r)
                if (lower(arg(2)) == reasons[r]) reason = r + 1;
            if (reason == 0) fail("unknown report reason " + arg(2));
            host.state.report_requested = who->member;
            host.state.report_reason_requested = reason;
        }
        return true;
    }
    if (c == "wait-stage") {
        timed(2, 120);
        return lower(host.stage) == lower(arg(1));
    }
    if (c == "leave") {
        lobby.request_leave();
        return true;
    }
    if (c == "wait-left") {
        timed(1, 60);
        return !host.online_active && !lobby.in_world();
    }
    if (c == "expect-screen") {
        if (lobby.screen() != online::screen_from_name(arg(1))) fail("expected screen " + arg(1));
        return true;
    }
    if (c == "expect-gate") {
        if (arg(1) != online::gate_name(lobby.gate())) fail("expected gate " + arg(1));
        return true;
    }
    if (c == "expect-error" || c == "expect-info") {
        std::string text;
        for (std::size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
        const auto& have = c == "expect-error" ? lobby.error() : lobby.info();
        if (have.find(text) == std::string::npos) fail("expected \"" + text + "\" in \"" + have + "\"");
        return true;
    }
    if (c == "expect-logged-in" || c == "expect-logged-out") {
        if (lobby.logged_in() != (c == "expect-logged-in")) fail("login state");
        return true;
    }
    if (c == "shot") {
        host.shot = arg(1);
        return true;
    }
    if (c == "end") {
        host.end = true;
        return true;
    }
    fail("unknown command");
    return true;
}

} // namespace dw4::game
