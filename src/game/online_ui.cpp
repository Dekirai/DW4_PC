#include "online_ui.hpp"

#include "presence_data.hpp"

#include "dw4/engine/randomizer.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dw4::game {

namespace {
using online::Screen;

const ImVec4 kWarn(1.0f, 0.78f, 0.35f, 1.0f);
const ImVec4 kError(1.0f, 0.47f, 0.42f, 1.0f);
const ImVec4 kGood(0.55f, 0.9f, 0.55f, 1.0f);
const ImVec4 kAccent(1.0f, 0.85f, 0.3f, 1.0f);
constexpr float kLabelWidth = 150.0f;

const char* kDifficulties[] = {"Normal", "Hard", "Very Hard"};
const char* kEnemyModes[] = {"Off", "Every enemy on its own", "One family per wave"};
const char* kZoneModes[] = {"Off", "Dungeon entrances within an area"};

void copy(char* buffer, std::size_t size, const std::string& text) { std::snprintf(buffer, size, "%s", text.c_str()); }
template <std::size_t N> void copy(char (&buffer)[N], const std::string& text) { copy(buffer, N, text); }
template <std::size_t N> void wipe(char (&buffer)[N]) { std::memset(buffer, 0, N); }

void wrapped(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}
void wrapped_colored(const ImVec4& color, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    wrapped(text.c_str());
    ImGui::PopStyleColor();
}
void wrapped_disabled(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    wrapped(text);
    ImGui::PopStyleColor();
}

// A labelled text field across the line; true when Enter was pressed in it.
bool field(const char* label, const char* id, char* buffer, std::size_t size, ImGuiInputTextFlags flags = 0) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1);
    return ImGui::InputText(id, buffer, size, flags | ImGuiInputTextFlags_EnterReturnsTrue);
}
template <std::size_t N> bool field(const char* label, const char* id, char (&buffer)[N], ImGuiInputTextFlags flags = 0) {
    return field(label, id, buffer, N, flags);
}
bool secret(const char* label, const char* id, char* buffer, std::size_t size) {
    return field(label, id, buffer, size, ImGuiInputTextFlags_Password);
}
template <std::size_t N> bool secret(const char* label, const char* id, char (&buffer)[N]) { return secret(label, id, buffer, N); }

void label_cell(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
}

// A button that is greyed out (and does nothing) when not enabled.
bool button(const char* label, bool enabled = true, bool primary = false) {
    ImGui::BeginDisabled(!enabled);
    if (primary) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.78f, 1.0f));
    const bool pressed = ImGui::Button(label);
    if (primary) {
        ImGui::PopStyleColor();
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndDisabled();
    return pressed && enabled;
}

std::string play_time(std::int64_t seconds) {
    char text[48];
    std::snprintf(text, sizeof text, "%lld h %02lld min", (long long)(seconds / 3600), (long long)(seconds / 60 % 60));
    return text;
}

std::string clears_text(int clears) {
    std::string text;
    for (int i = 0; i < 3; ++i)
        if (clears & (1 << i)) text += std::string(text.empty() ? "" : ", ") + kDifficulties[i];
    return text.empty() ? "none yet" : text;
}

std::string settings_line(const online::RoomSettings& s) {
    std::string text = std::string(kDifficulties[std::clamp(s.difficulty, 0, 2)]) + ", camera " + camera_mode_name(s.camera_mode) + ", light " +
                       light_mode_name(s.light_mode);
    if (s.fast_loading) text += ", fast loading";
    if (s.randomizer()) text += ", randomizer (seed " + std::to_string(s.rand_seed) + ")";
    return text;
}

std::string lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}

// The banner over every screen: the gate, a newer version, this PC's refusal.
void banner(online::OnlineLobby& lobby) {
    using online::Gate;
    const auto gate = lobby.gate();
    if (gate != Gate::open && gate != Gate::unknown) {
        wrapped_colored(gate == Gate::checking ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kWarn, lobby.gate_text());
        if (gate == Gate::unreachable || gate == Gate::maintenance) {
            if (button("Try again", !lobby.busy())) lobby.open();
        }
        ImGui::Spacing();
    }
    if (gate == Gate::open && lobby.newer_version())
        wrapped_colored(kAccent, "A newer version of the game is available. This one still works with the server.");
    if (!lobby.refusal().empty() && lobby.logged_in()) wrapped_colored(kWarn, "This PC cannot play online: " + lobby.refusal());
}

// The busy line and the message lines under the screen.
void footer(online::OnlineLobby& lobby) {
    if (lobby.busy()) {
        const int dots = int(std::fmod(ImGui::GetTime() * 3.0, 4.0));
        ImGui::TextDisabled("%s%.*s", lobby.busy_text().c_str(), dots, "...");
    }
    if (!lobby.error().empty()) wrapped_colored(kError, lobby.error());
    if (!lobby.info().empty()) wrapped_colored(kGood, lobby.info());
}

void welcome(online::OnlineLobby& lobby) {
    wrapped("Play Digimon World 4 together over the DW4Online server: rooms of up to ten players. Everyone plays an online "
            "character that starts from zero and is saved on the server; your own save on this PC is not used online.");
    ImGui::Spacing();
    const bool open = lobby.gate() == online::Gate::open;
    if (button("Log in", open && !lobby.busy(), true)) lobby.go(Screen::login);
    ImGui::SameLine();
    if (button("Register", open && !lobby.busy() && lobby.status().registration_open)) lobby.go(Screen::register_account);
    ImGui::SameLine();
    if (button("Forgot the password?", open && !lobby.busy())) lobby.go(Screen::recover);
    if (open && !lobby.status().registration_open) wrapped_disabled("Registration is closed on this server.");
}

void login(online::OnlineLobby& lobby, OnlineUiState& s) {
    ImGui::SeparatorText("Log in");
    if (!lobby.recover_name().empty() && !s.login_name[0]) copy(s.login_name, lobby.recover_name());
    bool submit = field("User name", "##login_name", s.login_name);
    submit |= secret("Password", "##login_password", s.login_password);
    label_cell("");
    ImGui::Checkbox("Remember me on this PC", &s.login_remember);
    ImGui::Spacing();
    if (button("Log in", !lobby.busy(), true) || (submit && !lobby.busy())) {
        lobby.login(s.login_name, s.login_password, s.login_remember);
        wipe(s.login_password);
    }
    ImGui::SameLine();
    if (button("Back", !lobby.busy())) lobby.back();
    if (s.login_remember)
        wrapped_disabled("Remember me keeps the login protected for your Windows account in saves/online; no password is stored.");
}

void register_screen(online::OnlineLobby& lobby, OnlineUiState& s) {
    ImGui::SeparatorText("Register");
    bool submit = field("User name", "##reg_name", s.reg_name);
    submit |= secret("Password", "##reg_password", s.reg_password);
    submit |= secret("Password again", "##reg_again", s.reg_again);
    submit |= field("E-mail (optional)", "##reg_email", s.reg_email);
    label_cell("");
    ImGui::Checkbox("Remember me on this PC", &s.reg_remember);
    wrapped_disabled("User names: 3 to 20 letters, digits, '_' and '-'. Passwords: at least 10 characters. The e-mail address is "
                     "only for password resets and never shown to others.");
    ImGui::Spacing();
    if (button("Register", !lobby.busy(), true) || (submit && !lobby.busy())) {
        lobby.register_account(s.reg_name, s.reg_password, s.reg_again, s.reg_email, s.reg_remember);
        if (lobby.busy()) {
            wipe(s.reg_password);
            wipe(s.reg_again);
        }
    }
    ImGui::SameLine();
    if (button("Back", !lobby.busy())) lobby.back();
}

void recovery_code(online::OnlineLobby& lobby, bool registered) {
    ImGui::SeparatorText(registered ? "Account created" : "New password set");
    wrapped(registered ? "Your account is ready. This is its recovery code:" : "Your password is changed. This is your new recovery code "
                                                                                "(the old one no longer works):");
    ImGui::Spacing();
    static char code[96];
    copy(code, lobby.recovery_code());
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##recovery_code", code, sizeof code, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    if (ImGui::Button("Copy")) ImGui::SetClipboardText(lobby.recovery_code().c_str());
    ImGui::Spacing();
    wrapped_colored(kWarn, "Write it down or keep it somewhere safe. It is the only way back into the account if you forget the "
                           "password, and it is shown only now.");
    ImGui::Spacing();
    if (button("I saved it", true, true)) lobby.acknowledge_code();
}

void recover(online::OnlineLobby& lobby, OnlineUiState& s) {
    ImGui::SeparatorText("Forgot the password");
    wrapped_disabled("With the recovery code you got at registration you set a new password (and get a new code).");
    bool submit = field("User name", "##rec_name", s.rec_name);
    submit |= field("Recovery code", "##rec_code", s.rec_code);
    submit |= secret("New password", "##rec_password", s.rec_password);
    submit |= secret("Password again", "##rec_again", s.rec_again);
    ImGui::Spacing();
    if (button("Set the new password", !lobby.busy(), true) || (submit && !lobby.busy())) {
        lobby.recover(s.rec_name, s.rec_code, s.rec_password, s.rec_again);
        if (lobby.busy()) {
            wipe(s.rec_password);
            wipe(s.rec_again);
            wipe(s.rec_code);
        }
    }
    ImGui::SameLine();
    if (button("Back", !lobby.busy())) lobby.back();
}

void account(online::OnlineLobby& lobby) {
    ImGui::SeparatorText("Account");
    label_cell("User name");
    ImGui::TextUnformatted(lobby.account().c_str());
    if (const auto& info = lobby.account_info()) {
        label_cell("E-mail");
        if (info->email.empty()) ImGui::TextDisabled("none");
        else ImGui::TextUnformatted(info->email.c_str());
        if (!info->created_at.empty()) {
            label_cell("Since");
            ImGui::TextUnformatted(info->created_at.substr(0, 10).c_str());
        }
    }
    label_cell("This PC");
    ImGui::TextUnformatted(lobby.remembered() ? "remembers the login" : "forgets the login when the game ends");
    ImGui::SeparatorText("Online character");
    if (const auto& c = lobby.character()) {
        if (c->exists) {
            label_cell("Character");
            ImGui::Text("%s, %s, level %d", c->name.empty() ? "(no name)" : c->name.c_str(), c->digimon.c_str(), c->level);
            label_cell("Played");
            ImGui::TextUnformatted(play_time(c->play_seconds).c_str());
            label_cell("Story clears");
            ImGui::TextUnformatted(clears_text(c->clears).c_str());
        } else {
            wrapped("No online character yet: your first online game starts a new one at level 1 (your local save stays as it is).");
        }
        label_cell("Your rooms");
        ImGui::Text("up to %s", kDifficulties[lobby.max_difficulty()]);
    } else {
        ImGui::TextDisabled("Loading...");
    }
    ImGui::Spacing();
    if (button("Change password", !lobby.busy())) lobby.go(Screen::change_password);
    ImGui::SameLine();
    if (button("Log out", !lobby.busy() && !lobby.in_room())) lobby.logout();
    ImGui::SameLine();
    if (button("Log out everywhere", !lobby.busy() && !lobby.in_room())) lobby.logout_everywhere();
    ImGui::SameLine();
    if (button("Back", !lobby.busy(), true)) lobby.back();
    if (lobby.in_room()) wrapped_disabled("Leave the room to log out.");
}

void change_password(online::OnlineLobby& lobby, OnlineUiState& s) {
    ImGui::SeparatorText("Change password");
    bool submit = secret("Current password", "##pw_current", s.pw_current);
    submit |= secret("New password", "##pw_next", s.pw_next);
    submit |= secret("Password again", "##pw_again", s.pw_again);
    wrapped_disabled("Your other devices are logged out; this one stays logged in.");
    ImGui::Spacing();
    if (button("Change", !lobby.busy(), true) || (submit && !lobby.busy())) {
        lobby.change_password(s.pw_current, s.pw_next, s.pw_again);
        if (lobby.busy()) {
            wipe(s.pw_current);
            wipe(s.pw_next);
            wipe(s.pw_again);
        }
    }
    ImGui::SameLine();
    if (button("Back", !lobby.busy())) lobby.back();
}

void rooms(online::OnlineLobby& lobby, OnlineUiState& s, int protocol, std::uint64_t data_hash) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Logged in as %s", lobby.account().c_str());
    ImGui::SameLine();
    if (button("Account", !lobby.busy())) lobby.go(Screen::account);
    ImGui::SeparatorText("Rooms");
    const auto& list = lobby.rooms();
    const online::api::RoomListing* chosen = nullptr;
    if (!lobby.rooms_loaded()) {
        ImGui::TextDisabled("Loading the room list...");
    } else if (list.empty()) {
        wrapped_disabled("No open rooms right now. Create one and tell your friends its name.");
    } else if (ImGui::BeginTable("rooms", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                                                 ImGuiTableFlags_ScrollY,
                                 ImVec2(0, std::min(12.0f, float(list.size()) + 1.5f) * ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();
        for (const auto& r : list) {
            const bool compatible = r.compatible(protocol, data_hash);
            const bool mine = lower(r.owner) == lower(lobby.account());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(r.code.c_str());
            if (!compatible) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const std::string label = std::string(r.has_password ? "[locked] " : "") + r.name + (mine ? "  (yours)" : "");
            if (ImGui::Selectable(label.c_str(), s.selected == r.code, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                s.selected = r.code;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && compatible && !lobby.busy()) lobby.join_room(r.code);
            }
            if (ImGui::IsItemHovered() && !compatible)
                ImGui::SetTooltip("Another game version or other game data (version %s): this room cannot be joined.", r.client_version.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", r.owner.c_str(), r.owner_present ? "" : " (away)");
            ImGui::TableNextColumn();
            ImGui::Text("%d / %d", r.players, r.max_players);
            ImGui::TableNextColumn();
            if (!compatible) ImGui::TextUnformatted("other version");
            else {
                std::string game = r.difficulty ? kDifficulties[std::clamp(*r.difficulty, 0, 2)] : "Normal";
                if (r.randomizer) game += ", randomizer";
                if (r.story_frozen) game += ", story paused";
                ImGui::TextUnformatted(game.c_str());
            }
            if (!compatible) ImGui::PopStyleColor();
            ImGui::PopID();
            if (s.selected == r.code) chosen = &r;
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    const bool can = lobby.can_play() && !lobby.busy();
    const bool chosen_ok = chosen && chosen->compatible(protocol, data_hash);
    const bool chosen_mine = chosen && lower(chosen->owner) == lower(lobby.account());
    if (button(chosen_mine ? "Rejoin" : "Join", can && chosen_ok, chosen_ok)) lobby.join_room(chosen->code);
    ImGui::SameLine();
    if (button("Create room", can, !chosen_ok)) {
        if (!s.room_name[0]) copy(s.room_name, lobby.account() + "'s room");
        s.room_difficulty = std::min(s.room_difficulty, lobby.max_difficulty());
        lobby.go(Screen::create_room);
    }
    ImGui::SameLine();
    if (button("Refresh", !lobby.busy())) lobby.refresh_rooms();
    if (chosen_mine) {
        ImGui::SameLine();
        if (button("Close room", !lobby.busy())) lobby.close_room(chosen->code);
    }
    if (chosen && !chosen->compatible(protocol, data_hash))
        wrapped_disabled("That room plays another version of the game or other game data and cannot be joined.");
    else if (chosen && chosen->has_password && !chosen_mine)
        wrapped_disabled("That room has a password: its owner can tell you.");
}

void create_room(online::OnlineLobby& lobby, OnlineUiState& s) {
    ImGui::SeparatorText("Create a room");
    bool submit = field("Room name", "##room_name", s.room_name);
    submit |= field("Password", "##room_password", s.room_password, ImGuiInputTextFlags_Password);
    label_cell("");
    ImGui::TextDisabled("(optional: only players who know it can join)");
    label_cell("Players");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##room_players", &s.room_players, 2, 10, "up to %d");
    label_cell("Difficulty");
    ImGui::SetNextItemWidth(-1);
    const int unlocked = lobby.max_difficulty();
    s.room_difficulty = std::clamp(s.room_difficulty, 0, unlocked);
    if (ImGui::BeginCombo("##room_difficulty", kDifficulties[s.room_difficulty])) {
        for (int d = 0; d < 3; ++d) {
            const std::string name = std::string(kDifficulties[d]) + (d > unlocked ? "  (locked)" : "");
            if (ImGui::Selectable(name.c_str(), s.room_difficulty == d, d > unlocked ? ImGuiSelectableFlags_Disabled : 0)) s.room_difficulty = d;
        }
        ImGui::EndCombo();
    }
    label_cell("Camera");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##room_camera", camera_mode_name(s.room_camera))) {
        for (int m = 0; m < 2; ++m)
            if (ImGui::Selectable(camera_mode_name(m), s.room_camera == m)) s.room_camera = m;
        ImGui::EndCombo();
    }
    label_cell("Light");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##room_light", light_mode_name(s.room_light))) {
        for (int m = 0; m < 2; ++m)
            if (ImGui::Selectable(light_mode_name(m), s.room_light == m)) s.room_light = m;
        ImGui::EndCombo();
    }
    label_cell("");
    ImGui::Checkbox("Fast loading", &s.room_fast);
    if (ImGui::TreeNode("Randomizer (the same for everyone in the room)")) {
        label_cell("Seed (0: off)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##rand_seed", &s.rand_seed);
        s.rand_seed = std::max(0, s.rand_seed);
        label_cell("Enemies");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##rand_enemies", &s.rand_enemies, kEnemyModes, 3);
        label_cell("Loading zones");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##rand_zones", &s.rand_zones, kZoneModes, 2);
        label_cell("");
        ImGui::Checkbox("Item drops", &s.rand_drops);
        ImGui::TreePop();
    }
    if (unlocked < 2)
        wrapped_disabled(unlocked == 0 ? "Hard and Very Hard open once your online character has cleared the story on Normal."
                                       : "Very Hard opens once your online character has cleared the story on Hard.");
    wrapped_disabled("Online there are no cheats and no rebalancing; everyone's story progress counts for the room owner's story.");
    ImGui::Spacing();
    if (button("Create", !lobby.busy(), true) || (submit && !lobby.busy())) {
        online::CreateForm form;
        form.name = s.room_name;
        form.password = s.room_password;
        form.max_players = s.room_players;
        form.settings.difficulty = s.room_difficulty;
        form.settings.camera_mode = s.room_camera;
        form.settings.light_mode = s.room_light;
        form.settings.fast_loading = s.room_fast;
        form.settings.rand_seed = std::uint32_t(s.rand_seed);
        form.settings.rand_enemies = s.rand_enemies;
        form.settings.rand_zones = s.rand_zones ? engine::randomizer::kZoneRuleCurrent : 0;
        form.settings.rand_drops = s.rand_drops;
        lobby.create_room(form);
    }
    ImGui::SameLine();
    if (button("Back", !lobby.busy())) lobby.back();
}

void join_room(online::OnlineLobby& lobby, OnlineUiState& s) {
    ImGui::SeparatorText("Join a room");
    if (const auto& target = lobby.join_target())
        wrapped(("\"" + target->name + "\" (owner " + target->owner + ") has a password.").c_str());
    const bool submit = secret("Password", "##join_password", s.join_password);
    ImGui::Spacing();
    if (button("Join", !lobby.busy(), true) || (submit && !lobby.busy())) {
        const auto code = lobby.join_target() ? lobby.join_target()->code : s.selected;
        lobby.join_room(code, std::string(s.join_password));
        wipe(s.join_password);
    }
    ImGui::SameLine();
    if (button("Back", !lobby.busy())) lobby.back();
}

void confirm(online::OnlineLobby& lobby) {
    ImGui::SeparatorText("Start online?");
    wrapped("The game you are playing now ends: progress since your last save is lost.");
    ImGui::Spacing();
    wrapped_disabled("Online you play your online character, saved on the server in the save room, at every stage change and when "
                     "you leave the room. Your local save stays as it is.");
    ImGui::Spacing();
    if (button("Start online", !lobby.busy(), true)) lobby.confirm_start();
    ImGui::SameLine();
    if (button("Cancel", !lobby.busy())) lobby.cancel_start();
}

void starting(online::OnlineLobby& lobby) {
    ImGui::SeparatorText("Starting");
    if (!lobby.busy()) ImGui::TextDisabled("Starting the online game...");
    if (button("Back", !lobby.busy())) lobby.back();
}

void in_room(online::OnlineLobby& lobby, OnlineUiState& s, const RoomLive* live) {
    const auto& view = lobby.room();
    const std::string name = view ? view->room.name : live && !live->name.empty() ? live->name : lobby.room_name();
    ImGui::TextColored(kAccent, "%s", name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(room %s)", lobby.room_code().c_str());
    if (view) {
        ImGui::TextDisabled("Owner %s%s - %s", view->room.owner.c_str(), view->room.owner_present ? "" : " (away: story progress is not saved)",
                            settings_line(view->settings).c_str());
    }
    if (live && live->active)
        ImGui::TextDisabled("You: player %d in instance %u (%s), input delay %d", live->own_slot + 1, live->inst, live->mode.c_str(), live->delay);

    // Members: the room's list (REST, every three seconds) with each one's
    // area, level and connection from the relay's roster (once a second).
    ImGui::SeparatorText("Members");
    struct Row {
        int slot = 0;
        std::string name, stage;
        bool owner = false, you = false, connected = false, lagging = false, live = false;
        int level = 0, hp = 0, max_hp = 0;
        std::uint32_t inst = 0;
    };
    std::vector<Row> rows;
    if (view)
        for (const auto& m : view->members) {
            Row r;
            r.slot = m.slot;
            r.name = m.name;
            r.owner = m.owner;
            r.you = m.account_id == view->you.account_id;
            r.connected = m.instance.has_value();
            rows.push_back(r);
        }
    if (live)
        for (const auto& m : live->members) {
            auto found = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.slot == int(m.member); });
            if (found == rows.end()) {
                Row r;
                r.slot = int(m.member);
                r.name = m.name;
                r.owner = m.owner;
                rows.push_back(r);
                found = rows.end() - 1;
            }
            found->stage = m.stage;
            found->level = m.level;
            found->hp = m.hp;
            found->max_hp = m.max_hp;
            found->connected = m.connected;
            found->lagging = m.lagging;
            found->inst = m.inst;
            found->live = true;
            found->you = found->you || m.member == live->member;
        }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.slot < b.slot; });
    const bool owner = lobby.room_owner();
    if (rows.empty()) {
        ImGui::TextDisabled("Loading...");
    } else if (ImGui::BeginTable("members", owner ? 5 : 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 1.7f);
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthStretch, 3.2f);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        if (owner) ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();
        for (const auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (r.you) ImGui::TextColored(kAccent, "%s (you)", r.name.c_str());
            else ImGui::TextUnformatted(r.name.c_str());
            if (r.owner) {
                ImGui::SameLine();
                ImGui::TextDisabled("owner");
            }
            ImGui::TableNextColumn();
            if (r.live) ImGui::TextUnformatted(area_line(r.stage).c_str());
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (r.live && r.level > 0) ImGui::Text("%d", r.level);
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (!r.connected) ImGui::TextColored(kWarn, "disconnected");
            else if (r.lagging) ImGui::TextColored(kWarn, "lagging");
            else if (r.live && r.max_hp > 0 && r.hp == 0 && !r.stage.empty()) ImGui::TextColored(kError, "defeated");
            else ImGui::TextColored(kGood, "online");
            if (owner) {
                ImGui::TableNextColumn();
                if (!r.you) {
                    ImGui::PushID(r.slot);
                    if (ImGui::SmallButton("Kick")) lobby.kick(r.name);
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndTable();
    }

    // Instances: who plays together where (the relay's groups).
    if (view && !view->instances.empty()) {
        ImGui::SeparatorText("Where everyone plays");
        for (const auto& i : view->instances) {
            std::string names;
            for (const auto& m : i.members) names += (names.empty() ? "" : ", ") + m.name;
            const std::string mode = i.mode == "sealed" ? "together" : i.mode == "hub" ? "alone (hub)" : i.mode == "solo" ? "alone" : i.mode;
            ImGui::BulletText("%s: %s (%s%s%s)", area_line(i.stage).c_str(), names.c_str(), mode.c_str(), i.boss_lock ? ", boss fight" : "",
                              i.departing ? ", leaving" : "");
        }
    }
    ImGui::Spacing();
    if (live && live->leave_prompt) wrapped_colored(kAccent, "Leave room? (you held B + X + START)");
    if (!s.leave_asked && !(live && live->leave_prompt)) {
        if (button("Leave room", !lobby.busy())) s.leave_asked = true;
    } else {
        wrapped("Leave the room? The online game ends and you return to the title; your online character is saved first.");
        if (button("Leave", !lobby.busy(), true)) {
            lobby.request_leave();
            s.leave_asked = false;
        }
        ImGui::SameLine();
        if (button("Stay", !lobby.busy())) s.leave_asked = false;
    }
}

} // namespace

bool set_ui_field(OnlineUiState& s, const std::string& f, const std::string& value) {
    const auto number = [&](int& out, int lo, int hi) {
        char* end = nullptr;
        const long v = std::strtol(value.c_str(), &end, 10);
        if (!end || *end || v < lo || v > hi) return false;
        out = int(v);
        return true;
    };
    const auto flag = [&](bool& out) {
        if (value != "0" && value != "1" && value != "on" && value != "off") return false;
        out = value == "1" || value == "on";
        return true;
    };
    if (f == "login.name") copy(s.login_name, value);
    else if (f == "login.password") copy(s.login_password, value);
    else if (f == "login.remember") return flag(s.login_remember);
    else if (f == "register.name") copy(s.reg_name, value);
    else if (f == "register.password") copy(s.reg_password, value);
    else if (f == "register.again") copy(s.reg_again, value);
    else if (f == "register.email") copy(s.reg_email, value);
    else if (f == "recover.name") copy(s.rec_name, value);
    else if (f == "recover.code") copy(s.rec_code, value);
    else if (f == "recover.password") copy(s.rec_password, value);
    else if (f == "recover.again") copy(s.rec_again, value);
    else if (f == "password.current") copy(s.pw_current, value);
    else if (f == "password.next") copy(s.pw_next, value);
    else if (f == "password.again") copy(s.pw_again, value);
    else if (f == "create.name") copy(s.room_name, value);
    else if (f == "create.password") copy(s.room_password, value);
    else if (f == "create.players") return number(s.room_players, 2, 10);
    else if (f == "create.difficulty") return number(s.room_difficulty, 0, 2);
    else if (f == "create.camera") return number(s.room_camera, 0, 1);
    else if (f == "create.light") return number(s.room_light, 0, 1);
    else if (f == "create.fast") return flag(s.room_fast);
    else if (f == "join.password") copy(s.join_password, value);
    else if (f == "select") s.selected = value;
    else if (f == "leave.asked") return flag(s.leave_asked);
    else return false;
    return true;
}

std::string area_line(const std::string& stage) {
    if (stage.empty()) return "Loading or in a scene";
    if (is_menu_stage(stage)) return "Menus";
    if (const auto* area = find_area(stage)) return area_display_name(*area);
    return stage;
}

// The title's Options names them by the camera data they pick (aaaCamera /
// aaaCameraL) and the light switch; the server keeps 0 / 1.
const char* camera_mode_name(int mode) { return mode == 1 ? "Type B" : "Type A"; }
const char* light_mode_name(int mode) { return mode == 1 ? "On" : "Off"; }

void draw_online_screens(online::OnlineLobby& lobby, OnlineUiState& state, const RoomLive* live) {
    // A new screen: the controller's focus moves to its first (or primary) item.
    if (lobby.screen() != state.last_screen) {
        state.last_screen = lobby.screen();
        state.focus_pending = true;
        if (lobby.screen() != Screen::in_room) state.leave_asked = false;
    }
    if (state.focus_pending && (ImGui::GetIO().ConfigFlags & (ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard))) {
        if (ImGuiWindow* window = ImGui::GetCurrentWindow(); window && window == GImGui->NavWindow) ImGui::NavInitWindow(window, true);
        state.focus_pending = false;
    }
    banner(lobby);
    if (lobby.gate() == online::Gate::unknown) {
        ImGui::TextDisabled("Connecting to the server...");
        return;
    }
    // (The in-room view also while the server cannot be reached: the room runs on.)
    const Screen screen = lobby.in_room() && !lobby.logged_in() ? Screen::in_room : lobby.screen();
    switch (screen) {
    case Screen::welcome: welcome(lobby); break;
    case Screen::login: login(lobby, state); break;
    case Screen::register_account: register_screen(lobby, state); break;
    case Screen::registered: recovery_code(lobby, true); break;
    case Screen::recover: recover(lobby, state); break;
    case Screen::recovered: recovery_code(lobby, false); break;
    case Screen::account: account(lobby); break;
    case Screen::change_password: change_password(lobby, state); break;
    case Screen::rooms: rooms(lobby, state, lobby.protocol(), lobby.data_hash()); break;
    case Screen::create_room: create_room(lobby, state); break;
    case Screen::join_room: join_room(lobby, state); break;
    case Screen::confirm: confirm(lobby); break;
    case Screen::starting: starting(lobby); break;
    case Screen::in_room: in_room(lobby, state, live); break;
    }
    ImGui::Spacing();
    footer(lobby);
}

void draw_title_panel(online::OnlineLobby& lobby, OnlineUiState& state, TitlePanel& panel) {
    const ImGuiIO& io = ImGui::GetIO();
    const float margin = std::max(16.0f, io.DisplaySize.y * 0.025f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - margin, io.DisplaySize.y - margin), ImGuiCond_Always, ImVec2(1, 1));
    const float width = std::clamp(io.DisplaySize.x * 0.3f, 380.0f, 560.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, io.DisplaySize.y - 2 * margin));
    ImGui::SetNextWindowBgAlpha(panel.active ? 0.94f : 0.72f);
    if (panel.focus) {
        ImGui::SetNextWindowFocus();
        panel.focus = false;
    }
    ImGui::Begin("Online##title", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | (panel.active ? 0 : ImGuiWindowFlags_NoNav));
    panel.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (!panel.active) {
        // The compact box: who is logged in, and how to open it.
        if (lobby.gate() != online::Gate::open && lobby.gate() != online::Gate::unknown) wrapped_colored(kWarn, lobby.gate_text());
        else if (lobby.logged_in()) {
            ImGui::Text("Logged in as %s", lobby.account().c_str());
            const auto count = std::count_if(lobby.rooms().begin(), lobby.rooms().end(),
                                             [&](const online::api::RoomListing& r) { return r.compatible(lobby.protocol(), lobby.data_hash()); });
            if (lobby.rooms_loaded()) ImGui::TextDisabled("%d open room%s", int(count), count == 1 ? "" : "s");
        } else if (lobby.busy()) {
            ImGui::TextDisabled("%s", lobby.busy_text().c_str());
        } else {
            wrapped("Play together online: rooms of up to ten players.");
        }
        ImGui::TextDisabled("Click here or press the controller's View button");
    } else {
        draw_online_screens(lobby, state, nullptr);
        ImGui::Separator();
        if (ImGui::Button("Close")) panel.active = false;
        ImGui::SameLine();
        ImGui::TextDisabled("(View button, Esc)");
    }
    ImGui::End();
}

} // namespace dw4::game
