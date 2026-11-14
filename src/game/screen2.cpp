#include "screen2.hpp"

#include "host_view.hpp"
#include "overhead_labels.hpp"
#include "presence_data.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>

// (screen2.hpp says what the panels are; the lower screen's own frame is
// Android's: screen2_android.cpp.)

namespace dw4::game {

Screen2Status screen2_status(engine::Engine& engine, const OverheadLabels& labels, int own_slot) {
    Screen2Status status;
    const auto* sys = engine.get<engine::GameSystem>();
    const std::string stage = engine.stage_name();
    status.loading = engine.loading();
    status.game = sys && !stage.empty() && !is_menu_stage(stage);
    if (!status.game) return status;
    if (const auto* area = find_area(stage)) status.area = area_display_name(*area);
    status.difficulty = difficulty_name(sys->difficulty);
    for (int slot = 0; slot < 4; ++slot) {
        const auto& player = sys->players[std::size_t(slot)];
        if (!player.joined) continue;
        Screen2Player p;
        p.slot = slot;
        p.own = slot == own_slot;
        p.name = ascii_player_name(player.name);
        if (p.name.empty()) p.name = "Player " + std::to_string(slot + 1);
        const FormInfo* form = player.stats.ready ? form_by_species(player.stats.species) : nullptr;
        if (!form) form = form_by_model(player.digimon);
        if (form) p.form = form->name;
        p.level = player.stats.level;
        p.hp = player.hp;
        p.max_hp = player.max_hp;
        p.mp = player.mp;
        p.max_mp = player.max_mp;
        p.exp = player.stats.exp;
        p.next_exp = player.stats.next_exp;
        p.bits = player.bits;
        status.party.push_back(std::move(p));
    }
    // The online members the name tags know (seamless co-op: another
    // machine's players in this stage, from their presence).
    for (const auto& remote : labels.remote_players()) {
        if (remote.stage != stage) continue;
        Screen2Player p;
        p.name = remote.name.empty() ? std::string("Online player") : remote.name;
        if (const FormInfo* form = form_by_species(remote.species)) p.form = form->name;
        p.level = remote.level;
        p.hp = remote.hp;
        p.max_hp = remote.max_hp;
        p.mp = remote.mp;
        p.max_mp = remote.max_mp;
        p.where = remote.busy ? "in a menu" : "here";
        status.party.push_back(std::move(p));
    }
    return status;
}

namespace {

// A bar with its numbers ("HP 120 / 150"), in `colour`.
void stat_bar(const char* name, int value, int maximum, ImU32 colour) {
    const float fraction = maximum > 0 ? std::clamp(float(value) / float(maximum), 0.0f, 1.0f) : 0.0f;
    char text[64];
    std::snprintf(text, sizeof text, "%s %d / %d", name, value, maximum);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colour);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), text);
    ImGui::PopStyleColor();
}

// HP green, yellow below half, red below a quarter.
ImU32 hp_colour(int hp, int max_hp) {
    const float f = max_hp > 0 ? float(hp) / float(max_hp) : 0.0f;
    if (f < 0.25f) return IM_COL32(214, 72, 64, 255);
    if (f < 0.5f) return IM_COL32(222, 180, 60, 255);
    return IM_COL32(76, 176, 92, 255);
}

void status_panel(const Screen2Status& status) {
    ImGui::PushTextWrapPos(0.0f);
    if (!status.game) {
        ImGui::TextUnformatted("No game is running.");
        ImGui::TextDisabled("The party's form, level, HP and MP show here once you play (after Game Start or Call Digimon).");
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::TextUnformatted(status.loading ? "Loading..." : status.area.empty() ? "In the game" : status.area.c_str());
    if (!status.difficulty.empty()) ImGui::TextDisabled("Difficulty: %s", status.difficulty.c_str());
    ImGui::Spacing();
    for (const auto& p : status.party) {
        ImGui::PushID(p.slot >= 0 ? p.slot : 100 + int(&p - status.party.data()));
        ImGui::SeparatorText(p.slot >= 0 ? ("Player " + std::to_string(p.slot + 1)).c_str() : "Online");
        if (p.own) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", p.name.c_str());
        else ImGui::TextUnformatted(p.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s%sLv %d", p.form.c_str(), p.form.empty() ? "" : "  ", p.level);
        if (!p.where.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", p.where.c_str());
        }
        stat_bar("HP", p.hp, p.max_hp, hp_colour(p.hp, p.max_hp));
        stat_bar("MP", p.mp, p.max_mp, IM_COL32(74, 128, 214, 255));
        if (p.exp >= 0 && p.next_exp > 0) {
            if (p.bits >= 0) ImGui::TextDisabled("EXP %d (next level at %d)   %d bits", p.exp, p.next_exp, p.bits);
            else ImGui::TextDisabled("EXP %d (next level at %d)", p.exp, p.next_exp);
        } else if (p.bits >= 0) {
            ImGui::TextDisabled("%d bits", p.bits);
        }
        ImGui::PopID();
    }
    if (status.party.empty()) ImGui::TextDisabled("No player has joined yet.");
    ImGui::PopTextWrapPos();
}

const char* visibility_name(int visibility) {
    static const char* names[] = {"Public", "Friends", "Private"};
    return names[std::clamp(visibility, 0, 2)];
}

// The Online panel: what this machine knows (nothing is sent while it is only
// looked at; "Connect" opens the lobby as the title's Online entry would).
void online_panel(const Screen2Online& online, Screen2Place place, Screen2Menu* menu) {
    ImGui::PushTextWrapPos(0.0f);
    const auto offline = [](const char* why) {
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.8f, 1.0f), "Offline");
        if (why && *why) ImGui::TextDisabled("%s", why);
    };
    // The page with the account screens: the F1 menu's Online tab (here on
    // the lower screen; the overlay names it).
    const auto online_page = [&](const char* label) {
        if (menu) {
            if (ImGui::Button(label)) {
                menu->ask = menu->here || !menu->open ? Screen2Menu::Ask::open : Screen2Menu::Ask::move_here;
                menu->tab = "Online";
            }
        } else {
            ImGui::TextDisabled("F1 > Online: log in, the world, the instance.");
        }
    };
    if (!online.direct.empty()) {
        ImGui::SeparatorText("Direct session");
        ImGui::TextUnformatted(online.direct.c_str());
    }
    auto* lobby = online.lobby;
    if (!lobby) {
        if (online.direct.empty()) offline("This run has no online screens.");
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::SeparatorText("Lobby");
    // (The reasons in F1 > Online's order: Android's refusal first, then the
    // server's state; the PC's CPU refusal once logged in.)
    if (online_refusal_first() && !lobby->refusal().empty()) {
        offline(lobby->refusal().c_str());
    } else if (!lobby->opened()) {
        offline("Not connected: nothing is sent to the server until you connect.");
        if (ImGui::Button("Connect")) lobby->open();
    } else if (lobby->gate() != online::Gate::open) {
        offline(lobby->gate_text().c_str());
        if (lobby->gate() == online::Gate::unreachable && ImGui::Button("Try again")) lobby->open();
    } else if (!lobby->logged_in()) {
        ImGui::TextUnformatted("Connected, not logged in.");
        online_page("Log in...");
    } else {
        ImGui::Text("Logged in as %s", lobby->account().c_str());
        if (!lobby->refusal().empty())
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "This PC cannot play online: %s", lobby->refusal().c_str());
        if (const auto& counts = lobby->world_counts())
            ImGui::TextDisabled("Online now: %d players, %d instances", counts->players, counts->instances);
        if (const auto& friends = lobby->friends_online())
            ImGui::TextDisabled("Friends online: %d", *friends);
        else
            ImGui::TextDisabled("Friends online: not known yet");
        if (!lobby->in_world()) {
            ImGui::TextDisabled("%s", lobby->play_label().c_str());
            online_page("Play...");
        }
    }
    if (lobby->busy() && !lobby->busy_text().empty()) ImGui::TextDisabled("%s", lobby->busy_text().c_str());
    if (!lobby->error().empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", lobby->error().c_str());
    // The running instance (the relay's INSTANCE_INFO).
    if (const RoomLive* live = online.live; live && live->active) {
        ImGui::SeparatorText("Instance");
        if (live->wid == 0) ImGui::TextUnformatted("Your home instance");
        else if (live->is_host) ImGui::Text("Your instance (%s)", visibility_name(live->visibility));
        else ImGui::Text("%s's instance (%s)", live->host_name.c_str(), visibility_name(live->visibility));
        ImGui::TextDisabled("Slot %d, %s, input delay %d%s", live->own_slot + 1, live->mode.c_str(), live->delay,
                            live->in_game ? "" : ", at the title");
        if (!live->story.empty()) ImGui::TextDisabled("%s", live->story.c_str());
        if (!live->members.empty() &&
            ImGui::BeginTable("members", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("Where");
            ImGui::TableSetupColumn("Lv");
            ImGui::TableSetupColumn("HP");
            ImGui::TableHeadersRow();
            for (const auto& m : live->members) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool own = m.member == live->member;
                if (own) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", m.name.c_str());
                else ImGui::TextUnformatted(m.name.c_str());
                if (m.owner) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(host)");
                }
                if (!m.connected || m.lagging) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", m.connected ? "lagging" : "away");
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(area_line(m.stage).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", m.level);
                ImGui::TableNextColumn();
                if (m.max_hp > 0) ImGui::Text("%d / %d", m.hp, m.max_hp);
                else ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }
        if (place == Screen2Place::lower) online_page("Instance page...");
    }
    ImGui::PopTextWrapPos();
}

void menu_panel(Screen2Menu& menu) {
    if (menu.open && menu.here) {
        if (ImGui::Button("Close the menu")) menu.ask = Screen2Menu::Ask::close;
        if (menu.pauses) {
            ImGui::SameLine();
            ImGui::TextDisabled("(the game is paused)");
        }
        if (menu.draw) menu.draw();
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    if (menu.open) {
        ImGui::TextUnformatted("The menu is open on the top screen.");
        if (ImGui::Button("Show it here")) menu.ask = Screen2Menu::Ask::move_here;
    } else {
        ImGui::TextUnformatted("The F1 menu: settings, controls, saves, cheats, achievements.");
        const float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("Open the menu", ImVec2(width, ImGui::GetFrameHeight() * 2.0f))) menu.ask = Screen2Menu::Ask::open;
        ImGui::TextDisabled(menu.pauses ? "The game pauses while the menu is open (F1 > Settings), as on the top screen."
                                        : "The game goes on while the menu is open (F1 > Settings).");
        ImGui::TextDisabled("Back or the touch button open it on the top screen.");
    }
    ImGui::PopTextWrapPos();
}

// A touch drag on an empty part of the panel scrolls it (ImGui scrolls by
// its scrollbar or a wheel only; a finger has neither).
void touch_scroll() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseSource != ImGuiMouseSource_TouchScreen) return;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
}

// The tab a place showed last (a tab chosen elsewhere is selected when it
// changed: the setting, a test run's DW4_SCREEN2_TAB, the Menu tab's asks).
Screen2Tab g_drawn_tab[2] = {Screen2Tab(255), Screen2Tab(255)};

} // namespace

bool draw_screen2_panels(Settings& settings, bool& settings_changed, Screen2Place place, const Screen2Status& status,
                         const Screen2Online& online, Screen2Menu* menu) {
    Screen2Tab& drawn = g_drawn_tab[place == Screen2Place::lower ? 0 : 1];
    // (The overlay has no Menu tab: F1 is the menu there.)
    Screen2Tab wanted = settings.screen2_tab;
    if (!menu && wanted == Screen2Tab::menu) wanted = Screen2Tab::status;
    // The F1 menu opened on this screen shows on the Menu tab.
    if (menu && menu->open && menu->here) wanted = Screen2Tab::menu;
    const bool select = wanted != drawn;
    Screen2Tab shown = drawn;
    bool online_shown = false;
    if (place == Screen2Place::lower) {
        // (Tabs for a finger: as tall as two lines.)
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui_px(16.0f), ui_px(9.0f)));
    }
    if (ImGui::BeginTabBar("screen2", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        const auto tab = [&](const char* name, Screen2Tab id) {
            const bool open = ImGui::BeginTabItem(name, nullptr, select && wanted == id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
            if (open) {
                shown = id;
                ImGui::EndTabItem();
            }
        };
        tab("Status", Screen2Tab::status);
        tab("Online", Screen2Tab::online);
        if (menu) tab("Menu", Screen2Tab::menu);
        ImGui::EndTabBar();
    }
    if (place == Screen2Place::lower) ImGui::PopStyleVar();
    if (shown != drawn) {
        drawn = shown;
        if (settings.screen2_tab != shown && !(menu && menu->open && menu->here && shown == Screen2Tab::menu)) {
            settings.screen2_tab = shown;
            settings_changed = true;
        }
    }
    // Leaving the Menu tab while the F1 menu is open there closes it (the
    // game would stay paused behind a menu nobody sees).
    if (menu && menu->open && menu->here && shown != Screen2Tab::menu && !select) menu->ask = Screen2Menu::Ask::close;
    ImGui::BeginChild("##panel", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    switch (shown) {
    case Screen2Tab::status: status_panel(status); break;
    case Screen2Tab::online:
        online_panel(online, place, menu);
        online_shown = true;
        break;
    case Screen2Tab::menu:
        if (menu) menu_panel(*menu);
        break;
    }
    touch_scroll();
    ImGui::EndChild();
    return online_shown;
}

bool draw_screen2_screen(Settings& settings, bool& settings_changed, const Screen2Status& status, const Screen2Online& online,
                         Screen2Menu& menu) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    bool online_shown = false;
    if (ImGui::Begin("##screen2", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus))
        online_shown = draw_screen2_panels(settings, settings_changed, Screen2Place::lower, status, online, &menu);
    ImGui::End();
    return online_shown;
}

bool draw_screen2_overlay(Settings& settings, bool& settings_changed, const Screen2Status& status, const Screen2Online& online) {
    if (!settings.screen2_overlay) return false;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x - ui_px(12.0f), ui_px(56.0f)), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(ui_px(330.0f), display.x * 0.5f), std::min(ui_px(360.0f), display.y * 0.6f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    bool open = true;
    bool online_shown = false;
    // (It never takes the focus from the game's controller: no navigation,
    // and it stays behind the F1 menu.)
    if (ImGui::Begin("Second screen##screen2", &open,
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoSavedSettings))
        online_shown = draw_screen2_panels(settings, settings_changed, Screen2Place::overlay, status, online, nullptr);
    ImGui::End();
    if (!open) {
        settings.screen2_overlay = false;
        settings_changed = true;
    }
    return online_shown;
}

void settings_screen2_section(Settings& settings, bool& settings_changed, std::string& scroll_to) {
    if (!ImGui::CollapsingHeader("Second screen", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (scroll_to == "Screen2") {
        ImGui::SetScrollHereY(0.0f);
        scroll_to.clear();
    }
    ImGui::PushTextWrapPos(0.0f);
#ifdef SDL_PLATFORM_ANDROID
    {
        int mode = settings.screen2_mode == Screen2Mode::off ? 1 : 0;
        static const char* kModes[] = {"Automatic (a second screen shows the panels)", "Off"};
        if (ImGui::Combo("Lower screen", &mode, kModes, 2)) {
            settings.screen2_mode = mode == 1 ? Screen2Mode::off : Screen2Mode::automatic;
            settings_changed = true;
        }
        const Screen2Display display = screen2_display();
        if (settings.screen2_mode == Screen2Mode::off)
            ImGui::TextDisabled("The panels (Status, Online, Menu) are not shown on a second screen.");
        else if (display.surface)
            ImGui::TextDisabled("Now on display %d \"%s\", %d x %d at %d dpi: the Status, Online and Menu tabs, by touch.", display.id,
                                display.name.c_str(), display.width, display.height, display.dpi);
        else if (display.found)
            ImGui::TextDisabled("Display %d \"%s\" found; its picture is not ready.", display.id, display.name.c_str());
        else
            ImGui::TextDisabled("No second screen now (none, switched off, or the game runs on it): the overlay below shows the panels "
                                "on this screen.");
    }
#endif
    if (ImGui::Checkbox("Panels over the game (Status, Online)", &settings.screen2_overlay)) settings_changed = true;
#ifdef SDL_PLATFORM_ANDROID
    ImGui::TextDisabled("A small window with the second screen's tabs, while no second screen shows them.");
#else
    ImGui::TextDisabled("A small window with the party's HP and MP and the online state, as the AYN Thor's lower screen shows them.");
#endif
    ImGui::PopTextWrapPos();
}

#ifndef SDL_PLATFORM_ANDROID
// The PC has no second display: the panels are the overlay window.
Screen2Display screen2_display() { return {}; }
void screen2_request(bool) {}
bool screen2_frame(const std::function<void()>&, const std::function<void()>&, int, bool) { return false; }
void screen2_device_reset() {}
void screen2_enter_background() {}
bool screen2_take_event(const SDL_Event&) { return false; }
void screen2_release_input() {}
void screen2_shutdown() {}
double screen2_seconds_since_touch() { return 1e9; }
void screen2_request_shot(const std::string&) {}
#endif

} // namespace dw4::game
