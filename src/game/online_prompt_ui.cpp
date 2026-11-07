#include "online_prompt_ui.hpp"

#include "dw4/pad.hpp"
#include "presence_data.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace dw4::game {
namespace w = online::wire;

namespace {

std::string place_of(std::string_view stage) {
    if (const auto* area = find_area(stage)) return area->place;
    return std::string(stage);
}

std::string header_of(std::string_view stage) {
    if (const auto* area = find_area(stage)) return area_display_name(*area);
    return std::string(stage);
}

const char* state_word(w::RowState s) {
    switch (s) {
    case w::RowState::joinable: return "";
    case w::RowState::boss_fight: return "Boss fight";
    case w::RowState::loading: return "";
    case w::RowState::host_away: return "Host reconnecting";
    case w::RowState::full: return "Full";
    case w::RowState::arriving: return "Arriving now";
    case w::RowState::other_quest: return "Other settings";
    case w::RowState::story_not_here: return "Story not here yet";
    case w::RowState::too_long: return "Too long to catch up";
    }
    return "";
}

std::string visibility_word(w::Visibility v) {
    return v == w::Visibility::friends ? "Friends" : v == w::Visibility::private_ ? "Private" : "Public";
}

} // namespace

std::uint32_t prompt_buttons(std::uint32_t buttons, std::uint32_t stick_directions, float stick_x, float stick_y) {
    std::uint32_t out = buttons;
    if ((stick_directions & pad_direction::positive_y) || stick_y > 0.6f) out |= pad_button::up;
    if ((stick_directions & pad_direction::negative_y) || stick_y < -0.6f) out |= pad_button::down;
    if ((stick_directions & pad_direction::positive_x) || stick_x > 0.6f) out |= pad_button::right;
    if ((stick_directions & pad_direction::negative_x) || stick_x < -0.6f) out |= pad_button::left;
    return out;
}

std::string prompt_row_text(const w::OptionRow& row, std::string_view arrival_stage) {
    (void)arrival_stage;
    std::string text = row.host_name + "'s instance";
    if (row.relation & w::OptionRow::party) text += " (party)";
    else if (row.relation & (w::OptionRow::friend_host | w::OptionRow::friends_in)) text += " (friend)";
    return text;
}

void draw_arrival_prompt(const online::PromptView& p, bool keyboard) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // The gate form stands where the gate's list was (the upper middle of the
    // picture); the loading-screen form in the middle of the loading screen.
    const float y = p.form == online::PromptForm::gate ? display.y * 0.22f : display.y * 0.35f;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("##arrival_prompt", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const ImVec4 grey(0.55f, 0.55f, 0.55f, 1.0f), gold(1.0f, 0.85f, 0.3f, 1.0f), green(0.45f, 0.9f, 0.45f, 1.0f);
    ImGui::TextUnformatted(header_of(p.stage).c_str());
    if (p.rtt_ms >= 0) {
        const std::string ping = "Your ping: " + std::to_string(p.rtt_ms) + " ms";
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 24.0f, 560.0f));
        ImGui::TextUnformatted(ping.c_str());
    }
    if (!p.notice.empty()) ImGui::TextColored(gold, "%s", p.notice.c_str());
    ImGui::Separator();
    if (ImGui::BeginTable("##rows", 6, ImGuiTableFlags_SizingFixedFit)) {
        for (std::size_t i = 0; i < p.rows.size(); ++i) {
            const auto& r = p.rows[i];
            const bool greyed = !w::choosable(r.state);
            const ImVec4 colour = greyed ? grey : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(colour, "%s %s", i == p.cursor ? ">" : " ", prompt_row_text(r, p.stage).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(colour, "%u/4", unsigned(r.players));
            ImGui::TableNextColumn();
            std::string where = place_of(r.host_stage);
            if (r.here > 0) where += ", Here (" + std::to_string(r.here) + ")";
            ImGui::TextColored(colour, "%s", where.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(colour, "%s", r.story_label.c_str());
            ImGui::TableNextColumn();
            if (const char* word = state_word(r.state); *word) ImGui::TextColored(colour, "%s", word);
            else ImGui::TextColored(colour, "%u ms", unsigned(r.ping));
            ImGui::TableNextColumn();
            std::string badges;
            if (r.same_story_point) badges += "Same story point";
            if (r.settings & w::OptionRow::randomizer) badges += std::string(badges.empty() ? "" : "  ") + "Randomizer";
            if (r.settings & w::OptionRow::other_settings) badges += std::string(badges.empty() ? "" : "  ") + "Other settings";
            if (r.visibility <= 2) badges += std::string(badges.empty() ? "" : "  ") + visibility_word(w::Visibility(r.visibility));
            ImGui::TextColored(r.same_story_point ? green : colour, "%s", badges.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Text("%s New instance   < %s >", p.new_instance() ? ">" : " ", visibility_word(p.visibility).c_str());
    if (p.details && !p.new_instance()) {
        // X: the members with form and level, the instance's settings.
        const auto& r = p.rows[p.cursor];
        ImGui::Separator();
        for (const auto& m : r.members) {
            const auto* form = form_by_species(m.species);
            ImGui::Text("  %s  Lv %u  %s", m.name.c_str(), unsigned(m.level), form ? form->name : "");
        }
        ImGui::Text("  %s%s", difficulty_name(r.difficulty), (r.settings & w::OptionRow::randomizer) ? ", Randomizer" : "");
    }
    ImGui::Separator();
    const bool home_back = p.form == online::PromptForm::loading;
    const std::string hint = keyboard ? std::string("Enter: Choose   Left/Right: visibility   X: Details   ") +
                                            (home_back ? "Backspace: Back to the lobby" : "Backspace: Back")
                                      : std::string("A: Choose   Left/Right: visibility   X: Details   ") + (home_back ? "B: Back to the lobby" : "B: Back");
    ImGui::TextUnformatted(hint.c_str());
    ImGui::End();
}

} // namespace dw4::game
