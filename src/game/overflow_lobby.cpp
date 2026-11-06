#include "overflow_lobby.hpp"

#include "overflow_text.hpp"

#include "dw4/pad.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dw4::game::overflow {

NpcTestMode npc_test_mode(const char* value) {
    if (!value || !*value) return NpcTestMode::off;
    if (std::strcmp(value, "show") == 0) return NpcTestMode::show;
    if (std::strcmp(value, "0") == 0) return NpcTestMode::off;
    return NpcTestMode::talk;
}

NpcState npc_state(const NpcConditions& c) {
    NpcState s;
    if (!c.lobby || !c.offline || !c.setting) return s;
    if (c.test_run && c.test == NpcTestMode::off) return s;
    // The story must be far enough (3.2) unless a test asks for the NPC.
    if (!(c.test_run && c.test != NpcTestMode::off) && !(c.intro_seen && c.gate_open)) return s;
    s.shown = true;
    s.talk = !(c.test_run && c.test == NpcTestMode::show);
    s.crowd = c.players > 1;
    return s;
}

bool gate_open(const std::map<std::int32_t, std::uint8_t>& flags) {
    auto on = [&](std::int32_t flag) {
        const auto found = flags.find(flag);
        return found != flags.end() && found->second != 0;
    };
    return on(409) || on(2) || on(4) || on(5) || on(6) || on(86);
}

bool talk_reaches(engine::Vec3f centre, engine::Vec3f npc) {
    const float reach = (kTalkCube + kTalkBox) * 0.5f;
    return std::fabs(centre.x - npc.x) <= reach && std::fabs(centre.y - npc.y) <= reach && std::fabs(centre.z - npc.z) <= reach;
}

namespace {
std::string clock(double seconds) {
    const int whole = std::max(0, int(seconds));
    char text[16];
    std::snprintf(text, sizeof text, "%d:%02d", whole / 60, whole % 60);
    return text;
}

Text form_text(std::string_view form, int which) {
    // which: 0 name, 1 class line, 2 Art, 3 signature, 4 techniques, 5 trait
    static constexpr Text table[4][6] = {
        {Text::form_agumon, Text::class_agumon, Text::art_agumon, Text::sign_agumon, Text::tech_agumon, Text::trait_agumon},
        {Text::form_vmon, Text::class_vmon, Text::art_vmon, Text::sign_vmon, Text::tech_vmon, Text::trait_vmon},
        {Text::form_girumon, Text::class_girumon, Text::art_girumon, Text::sign_girumon, Text::tech_girumon, Text::trait_girumon},
        {Text::form_dorumon, Text::class_dorumon, Text::art_dorumon, Text::sign_dorumon, Text::tech_dorumon, Text::trait_dorumon},
    };
    for (std::size_t i = 0; i < kSelectForms.size(); ++i)
        if (form == kSelectForms[i]) return table[i][which];
    return table[0][which];
}

// HP / MP at level 1 (5.1).
std::pair<int, int> start_meters(std::string_view form) {
    if (form == "p_vmon") return {160, 70};
    if (form == "p_girumon") return {150, 80};
    if (form == "p_dorumon") return {170, 60};
    return {180, 50};
}

std::string gear_name(std::string_view key) {
    if (key.empty()) return std::string(text(Text::select_none));
    for (const auto& entry : all_texts())
        if (entry.id.size() == key.size() + 5 && entry.id.substr(0, 5) == "gear_" && entry.id.substr(5) == key) return std::string(entry.text);
    return std::string(key);
}

int wrap(int value, int count) { return count <= 0 ? 0 : ((value % count) + count) % count; }
} // namespace

std::string welcome_back(const RunSummary& run) {
    if (run.end == RunEnd::cleared) return std::string(text(Text::back_clear));
    if (run.defeated_by == "e_geko" || run.defeated_by == "e_tonogeko") return std::string(text(Text::back_gekomon));
    if (run.end == RunEnd::time_up) return std::string(text(Text::back_time_up));
    return format(Text::back_time, clock(run.seconds).c_str());
}

// ---------------------------------------------------------------------------
// LobbyMenu
// ---------------------------------------------------------------------------
void LobbyMenu::open(bool crowd, const LobbyMeta& meta, const std::optional<RunSummary>& back) {
    pages_.clear();
    page_ = 0;
    cursor_ = 0;
    closing_ = false;
    crowd_ = crowd;
    intro_ = false;
    frames_ = 0;
    screen_ = LobbyScreen::talk;
    if (crowd) {
        pages_.push_back(std::string(text(Text::crowd)));
        closing_ = true;
        return;
    }
    if (!meta.seen_intro) {
        intro_ = true;
        for (const Text t : {Text::intro_1, Text::intro_2, Text::intro_3, Text::intro_4, Text::intro_5, Text::intro_6}) pages_.push_back(std::string(text(t)));
    } else if (back) {
        pages_.push_back(welcome_back(*back));
    }
    if (pages_.empty()) to_choice();
}

void LobbyMenu::open_screen(LobbyScreen screen, const LobbyMeta& meta) {
    open(false, meta, std::nullopt);
    if (screen == LobbyScreen::talk) return;
    to_choice();
    if (screen == LobbyScreen::help) {
        pages_.clear();
        for (const Text t : {Text::help_1, Text::help_2, Text::help_3, Text::help_4}) pages_.push_back(std::string(text(t)));
        page_ = 0;
    }
    screen_ = screen;
}

void LobbyMenu::to_choice() {
    screen_ = LobbyScreen::choice;
    pages_ = {std::string(text(Text::ask))};
    page_ = 0;
    cursor_ = 0;
}

void LobbyMenu::load_loadout(const LobbyMeta& meta) {
    const auto found = meta.loadouts.find(kSelectForms[std::size_t(face_)]);
    if (found == meta.loadouts.end()) {
        armor_ = chip_ = 0;
        return;
    }
    auto index = [](const std::vector<std::string>& keys, const std::string& key) {
        const auto at = std::find(keys.begin(), keys.end(), key);
        return at == keys.end() ? 0 : int(at - keys.begin());
    };
    armor_ = index(meta.armors, found->second.first);
    chip_ = index(meta.chips, found->second.second);
}

void LobbyMenu::close() {
    screen_ = LobbyScreen::none;
    pages_.clear();
    confirming_ = false;
}

LobbyAction LobbyMenu::input(const MenuKeys& keys, const LobbyMeta& meta) {
    LobbyAction action;
    switch (screen_) {
    case LobbyScreen::none: break;
    case LobbyScreen::talk:
    case LobbyScreen::help:
        if (keys.a || keys.b) {
            if (page_ + 1 < int(pages_.size())) {
                ++page_;
            } else if (closing_) {
                close();
                action.kind = LobbyAction::Kind::closed;
            } else {
                if (intro_) {
                    intro_ = false;
                    action.kind = LobbyAction::Kind::intro_seen;
                }
                to_choice();
            }
        }
        break;
    case LobbyScreen::choice:
        if (keys.up) cursor_ = wrap(cursor_ - 1, 5);
        if (keys.down) cursor_ = wrap(cursor_ + 1, 5);
        if (keys.b) cursor_ = 4;
        if (keys.a || keys.b) {
            switch (cursor_) {
            case 0:
                screen_ = LobbyScreen::select;
                line_ = 0;
                confirming_ = false;
                load_loadout(meta);
                break;
            case 1:
                screen_ = LobbyScreen::plug_lab;
                cursor_ = 0;
                break;
            case 2:
                screen_ = LobbyScreen::records;
                records_page_ = 0;
                break;
            case 3:
                screen_ = LobbyScreen::help;
                pages_.clear();
                for (const Text t : {Text::help_1, Text::help_2, Text::help_3, Text::help_4}) pages_.push_back(std::string(text(t)));
                page_ = 0;
                break;
            default:
                screen_ = LobbyScreen::talk;
                pages_ = {std::string(text(Text::goodbye))};
                page_ = 0;
                closing_ = true;
                break;
            }
        }
        break;
    case LobbyScreen::select: {
        const int faces = int(kSelectForms.size());
        if (confirming_) {
            if (keys.left || keys.right || keys.up || keys.down) confirm_ = 1 - confirm_;
            if (keys.b) confirming_ = false;
            if (keys.a) {
                if (confirm_ == 0) {
                    action.kind = LobbyAction::Kind::start_run;
                    action.request.form = kSelectForms[std::size_t(face_)];
                    action.request.field = "stagbs02";
                    action.request.seed = 0;
                    action.request.options["tier"] = "normal";
                    action.request.options["armor"] = armor_ < int(meta.armors.size()) ? meta.armors[std::size_t(armor_)] : "";
                    action.request.options["chip"] = chip_ < int(meta.chips.size()) ? meta.chips[std::size_t(chip_)] : "";
                    close();
                } else {
                    confirming_ = false;
                }
            }
            break;
        }
        if (keys.up) line_ = wrap(line_ - 1, 5);
        if (keys.down) line_ = wrap(line_ + 1, 5);
        const int dx = (keys.right ? 1 : 0) - (keys.left ? 1 : 0);
        if (dx != 0) {
            if (line_ == 0) {
                face_ = wrap(face_ + dx, faces);
                load_loadout(meta);
            }
            else if (line_ == 1) armor_ = wrap(armor_ + dx, int(meta.armors.size()));
            else if (line_ == 2) chip_ = wrap(chip_ + dx, int(meta.chips.size()));
        }
        if (keys.b) to_choice();
        else if (keys.a) {
            if (line_ == 4 || line_ == 0) {
                confirming_ = true;
                confirm_ = 0;
            } else {
                line_ = wrap(line_ + 1, 5);
            }
        }
        break;
    }
    case LobbyScreen::plug_lab:
        if (keys.up) cursor_ = wrap(cursor_ - 1, 7);
        if (keys.down) cursor_ = wrap(cursor_ + 1, 7);
        if (keys.l || keys.left) plug_form_ = wrap(plug_form_ - 1, int(kSelectForms.size()));
        if (keys.r || keys.right) plug_form_ = wrap(plug_form_ + 1, int(kSelectForms.size()));
        if (keys.a) {
            action.kind = LobbyAction::Kind::buy_plug;
            action.form = kSelectForms[std::size_t(plug_form_)];
            action.stat = cursor_;
        }
        if (keys.b) to_choice();
        break;
    case LobbyScreen::records:
        if (keys.left || keys.right) records_page_ = 1 - records_page_;
        if (keys.b || keys.a) to_choice();
        break;
    }
    return action;
}

// ---------------------------------------------------------------------------
// Drawing (9.1-9.2): talk window, choice list, status window, shop list
// ---------------------------------------------------------------------------
namespace {
constexpr float kSize = 18.0f; // the talk window's line height

void talk_window(SimUi& ui, const std::string& page, bool prompt, std::uint64_t frame, float open, const engine::Attribute* face) {
    // The talk window's look at the bottom: the name tab, the face in its
    // cyan frame, three lines, the prompt.
    ui.talk_frame(332, 124, text(Text::npc_name), open);
    if (open < 1.0f) return;
    ui.rect(46, 346, 76, 76, ui_colors::window_line);
    ui.gradient(48, 348, 72, 72, UiColor{0.2f, 0.55f, 0.35f, 1}, UiColor{0.08f, 0.3f, 0.2f, 1});
    if (face) ui.image(52, 352, 64, 64, face);
    else ui.text(52, 375, "Geko", ui_colors::yellow, 16.0f, 2, 64);
    ui.text(140, 352, page, ui_colors::white, 19.0f);
    if (prompt) ui.prompt(320, 440, frame);
}

void list_window(SimUi& ui, float x, float y, float w, const std::vector<std::string>& lines, int cursor, std::string_view title = {}) {
    const float row = 24.0f;
    const float top = title.empty() ? 10.0f : 36.0f;
    ui.window(x, y, w, top + row * float(lines.size()) + 10.0f);
    if (!title.empty()) ui.text(x + 16, y + 10, title, ui_colors::yellow, kSize);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float ly = y + top + row * float(i);
        if (int(i) == cursor) ui.cursor_bar(x + 8, ly - 1, w - 16, row - 2);
        ui.text(x + 22, ly + 2, lines[i], int(i) == cursor ? ui_colors::white : ui_colors::grey, kSize);
    }
}

void draw_select(SimUi& ui, const LobbyMenu& menu, const LobbyMeta& meta) {
    // The status window frame with the 4 x 4 faces (locked: dark with "?").
    ui.window(24, 24, 592, 432);
    ui.text(44, 34, text(Text::select_title), ui_colors::yellow, 22.0f);
    const float cell = 54.0f, gx = 44, gy = 70;
    for (std::size_t i = 0; i < kFaceOrder.size(); ++i) {
        const float x = gx + cell * float(i % 4), y = gy + cell * float(i / 4);
        const bool open = std::find(kSelectForms.begin(), kSelectForms.end(), std::string_view(kFaceOrder[i])) != kSelectForms.end();
        const bool on = open && kSelectForms[std::size_t(menu.face())] == std::string_view(kFaceOrder[i]);
        ui.rect(x, y, cell - 6, cell - 6, on ? ui_colors::yellow : ui_colors::window_frame);
        ui.gradient(x + 2, y + 2, cell - 10, cell - 10, open ? UiColor{0.25f, 0.4f, 0.8f, 1} : UiColor{0.08f, 0.08f, 0.12f, 1},
                    open ? UiColor{0.1f, 0.18f, 0.45f, 1} : UiColor{0.04f, 0.04f, 0.06f, 1});
        if (!open) ui.text(x, y + 13, text(Text::select_locked), ui_colors::grey, 20.0f, 2, cell - 6);
        else {
            const std::string name(text(form_text(kFaceOrder[i], 0)));
            ui.text(x, y + 15, name.substr(0, 4), on ? ui_colors::white : ui_colors::cyan, 14.0f, 2, cell - 6);
        }
    }
    // Beside the faces: the form's lines.
    const std::string form = kSelectForms[std::size_t(menu.face())];
    const float tx = 270, ty = 70;
    ui.text(tx, ty, text(form_text(form, 0)), ui_colors::yellow, 24.0f);
    ui.text(tx, ty + 30, text(form_text(form, 1)), ui_colors::cyan, 15.0f);
    ui.text(tx, ty + 54, format(Text::select_art, std::string(text(form_text(form, 2))).c_str(), std::string(text(form_text(form, 3))).c_str()),
            ui_colors::white, 15.0f);
    ui.text(tx, ty + 76, format(Text::select_techniques, std::string(text(form_text(form, 4))).c_str()), ui_colors::white, 13.0f);
    ui.text(tx, ty + 98, format(Text::select_trait, std::string(text(form_text(form, 5))).c_str()), ui_colors::green, 15.0f);
    const auto [hp, mp] = start_meters(form);
    ui.text(tx, ty + 120, format(Text::select_stats, hp, mp), ui_colors::white, 15.0f);
    const auto record = meta.forms.find(form);
    if (record != meta.forms.end() && record->second.runs > 0)
        ui.text(tx, ty + 142, format(Text::select_best, clock(record->second.best_seconds).c_str(), record->second.best_level), ui_colors::white, 15.0f);
    else
        ui.text(tx, ty + 142, text(Text::select_best_none), ui_colors::grey, 15.0f);
    // The loadout lines with the status window's left / right arrows.
    const std::string armor = menu.armor() < int(meta.armors.size()) ? meta.armors[std::size_t(menu.armor())] : "";
    const std::string chip = menu.chip() < int(meta.chips.size()) ? meta.chips[std::size_t(menu.chip())] : "";
    const std::vector<std::string> lines{format(Text::select_armor, gear_name(armor).c_str()), format(Text::select_chip, gear_name(chip).c_str()),
                                         format(Text::select_tier, std::string(text(Text::tier_normal)).c_str()),
                                         std::string(text(Text::select_start))};
    const float ly = 300;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const bool on = menu.line() == int(i) + 1;
        const float y = ly + 26.0f * float(i);
        if (on) ui.cursor_bar(52, y - 1, 536, 24);
        if (on && i < 2) {
            ui.text(60, y + 2, "<", ui_colors::yellow, 18.0f);
            ui.text(560, y + 2, ">", ui_colors::yellow, 18.0f);
        }
        ui.text(84, y + 2, lines[i], on ? ui_colors::white : ui_colors::grey, 18.0f);
    }
    ui.text(44, 420, text(Text::select_help), ui_colors::grey, 14.0f);
    if (menu.confirming()) {
        // The stage change window's look: "Enter the simulator?  Yes / No".
        ui.window(170, 190, 300, 92);
        ui.text(170, 204, text(Text::select_confirm), ui_colors::white, 20.0f, 2, 300);
        const float y = 240;
        for (int i = 0; i < 2; ++i) {
            const float x = 200 + 130.0f * float(i);
            if (menu.confirm_cursor() == i) ui.cursor_bar(x, y - 2, 110, 26);
            ui.text(x, y + 2, text(i == 0 ? Text::yes : Text::no), menu.confirm_cursor() == i ? ui_colors::white : ui_colors::grey, 20.0f, 2, 110);
        }
    }
}

void draw_plug_lab(SimUi& ui, const LobbyMenu& menu, const LobbyMeta& meta) {
    ui.window(60, 40, 520, 400);
    const std::string form = kSelectForms[std::size_t(menu.plug_form())];
    ui.text(80, 52, text(Text::plug_title), ui_colors::yellow, 22.0f);
    ui.text(80, 52, format(Text::plug_bits, meta.sim_bits), ui_colors::cyan, 18.0f, 1, 480);
    ui.text(80, 84, std::string("< ") + std::string(text(form_text(form, 0))) + " >", ui_colors::white, 20.0f);
    static constexpr Text stats[7] = {Text::plug_str, Text::plug_def, Text::plug_mhp, Text::plug_mmp, Text::plug_wis, Text::plug_spi, Text::plug_spd};
    const auto found = meta.plugs.find(form);
    for (int i = 0; i < 7; ++i) {
        const float y = 124 + 34.0f * float(i);
        const bool on = menu.cursor() == i;
        if (on) ui.cursor_bar(72, y - 3, 496, 28);
        const int steps = found != meta.plugs.end() ? found->second[std::size_t(i)] : 0;
        ui.text(88, y, text(stats[i]), on ? ui_colors::white : ui_colors::grey, 18.0f);
        ui.bar(240, y + 4, 200, 12, float(steps) / 10.0f, ui_colors::green);
        ui.text(450, y, steps >= 10 ? std::string(text(Text::plug_full)) : std::to_string(100 * (steps + 1)), on ? ui_colors::white : ui_colors::grey,
                18.0f, 1, 100);
    }
    const int steps = found != meta.plugs.end() ? found->second[std::size_t(menu.cursor())] : 0;
    if (steps < 10) ui.text(80, 370, format(Text::plug_next, 100 * (steps + 1)), ui_colors::white, 16.0f);
    ui.text(80, 404, text(Text::plug_help), ui_colors::grey, 14.0f);
}

void draw_records(SimUi& ui, const LobbyMenu& menu, const LobbyMeta& meta) {
    ui.window(40, 40, 560, 400);
    ui.text(60, 52, menu.records_page() == 0 ? text(Text::records_title) : text(Text::records_digimon), ui_colors::yellow, 22.0f);
    ui.text(60, 52, format(Text::records_earned, meta.earned_total), ui_colors::cyan, 16.0f, 1, 520);
    if (menu.records_page() == 0) {
        static constexpr std::pair<Text, const char*> records[] = {
            {Text::record_gate_keeper, "gate_keeper"}, {Text::record_ten_minutes, "ten_minutes"}, {Text::record_fifteen_minutes, "fifteen_minutes"},
            {Text::record_first_clear, "first_clear"}, {Text::record_level_25, "run_level_25"}, {Text::record_thousand, "thousand"}};
        float y = 92;
        for (const auto& [line, key] : records) {
            const bool done = std::find(meta.records.begin(), meta.records.end(), key) != meta.records.end();
            ui.text(64, y, text(line), done ? ui_colors::white : ui_colors::grey, 17.0f);
            y += 28;
        }
    } else {
        float y = 92;
        for (const char* form : kSelectForms) {
            const auto found = meta.forms.find(form);
            const FormRecord record = found != meta.forms.end() ? found->second : FormRecord{};
            ui.text(64, y, format(Text::records_form_line, std::string(text(form_text(form, 0))).c_str(), record.runs, clock(record.best_seconds).c_str(),
                                  record.best_level),
                    ui_colors::white, 17.0f);
            y += 28;
        }
    }
    ui.text(60, 404, text(Text::records_help), ui_colors::grey, 14.0f);
}
} // namespace

std::unique_ptr<engine::Attribute> model_texture(const IgbDocument& document, std::string_view attr_set) {
    for (const auto& object : document.objects) {
        if (document.metadata.object_types[object.type_index].name != "igAttrSet") continue;
        engine::ObjectView set(&document, object.reference_index);
        if (set.name() != attr_set) continue;
        for (const auto& attr : set.list(8)) {
            if (!attr || attr.type() != "igTextureBindAttr") continue;
            auto bind = std::make_unique<engine::Attribute>();
            bind->kind = engine::AttrKind::texture_bind;
            bind->object = attr;
            bind->texture = attr.ref(4);
            if (!bind->texture) continue;
            bind->image = bind->texture.ref(12);
            bind->sampler = engine::texture_sampler(bind->texture);
            return bind;
        }
    }
    return nullptr;
}

void draw_lobby_menu(SimUi& ui, const LobbyMenu& menu, const LobbyMeta& meta, std::uint64_t frame, const engine::Attribute* face) {
    const float open = std::min(1.0f, float(menu.opened_frames() + 1) / 8.0f);
    switch (menu.screen()) {
    case LobbyScreen::none: break;
    case LobbyScreen::talk:
    case LobbyScreen::help:
        if (!menu.pages().empty()) talk_window(ui, menu.pages()[std::size_t(menu.page())], true, frame, open, face);
        break;
    case LobbyScreen::choice: {
        talk_window(ui, menu.pages().empty() ? std::string() : menu.pages().front(), false, frame, 1.0f, face);
        const std::vector<std::string> choices{std::string(text(Text::choice_enter)), std::string(text(Text::choice_plug_lab)),
                                               std::string(text(Text::choice_records)), std::string(text(Text::choice_help)),
                                               std::string(text(Text::choice_leave))};
        list_window(ui, 360, 170, 240, choices, menu.cursor());
        break;
    }
    case LobbyScreen::select: draw_select(ui, menu, meta); break;
    case LobbyScreen::plug_lab: draw_plug_lab(ui, menu, meta); break;
    case LobbyScreen::records: draw_records(ui, menu, meta); break;
    }
}

// ---------------------------------------------------------------------------
// OverflowLobby
// ---------------------------------------------------------------------------
OverflowLobby::OverflowLobby() : actor_("stag0302.igb", "n_gekoa") { actor_.set_place(kNpcPlace, kNpcYaw); }

void OverflowLobby::set_resources(const MountedResources& resources) {
    ui_.set_resources(resources);
    if (!resources_) resources_ = std::make_unique<MountedResources>(resources);
}

void OverflowLobby::update(const NpcConditions& conditions, std::optional<engine::Vec3f> own, double now) {
    state_ = npc_state(conditions);
    own_ = own;
    if (!state_.shown && menu_.is_open()) menu_.close();
    if (!state_.shown) pending_ = 0;
    if (state_.shown && test_screen_ && !menu_.is_open()) {
        menu_.open_screen(*test_screen_, meta_);
        test_screen_.reset();
    }
    // The model loads the first time the NPC is there (stag0302's main
    // member, on the actor's own thread).
    if (state_.shown && resources_) actor_.load(*resources_);
    actor_.set_visible(state_.shown, now);
}

void OverflowLobby::take_events(std::vector<engine::PortEvent>& events) {
    for (const auto& e : events)
        if (e.kind == engine::PortEvent::Kind::talk_box && e.slot == 0 && state_.talk && !menu_.is_open() &&
            talk_reaches(engine::Vec3f{e.x, e.y, e.z})) {
            pending_ = 2;
            std::printf("overflow: talk box at (%.1f, %.1f, %.1f) reaches Gekomon\n", double(e.x), double(e.y), double(e.z));
        }
    std::erase_if(events, [](const engine::PortEvent& e) { return e.kind == engine::PortEvent::Kind::talk_box; });
}

void OverflowLobby::opened_or_closed() {
    const bool open = menu_.is_open();
    if (open == was_open_) return;
    was_open_ = open;
    story_latch_ = true;
    if (open) held_latch_ = true;
}

void OverflowLobby::stepped(bool player_has_control) {
    // "Welcome back" (13.3 step 4): once, when the NPC talks and player 1
    // has the control (never over an original talk, never in "show").
    if (welcome_ && back_ && state_.talk && player_has_control && !menu_.is_open() && pending_ == 0) {
        menu_.open(false, meta_, back_);
        welcome_ = false;
        back_.reset();
        opened_or_closed();
        std::printf("overflow: Gekomon's Welcome back opens\n");
        return;
    }
    if (pending_ <= 0) return;
    if (!player_has_control) {
        pending_ = 0; // an original talk took the press
        return;
    }
    if (--pending_ == 0 && state_.talk) {
        menu_.open(state_.crowd, meta_, back_);
        back_.reset();
        welcome_ = false;
        opened_or_closed();
        std::printf("overflow: Gekomon's dialog opens\n");
    }
}

LobbyAction OverflowLobby::input(std::uint32_t buttons, float stick_x, float stick_y) {
    opened_or_closed(); // (a test screen or update() may have opened or closed it)
    const int sy = stick_y > 0.5f ? -1 : stick_y < -0.5f ? 1 : 0; // (the stick's y is up)
    const int sx = stick_x > 0.5f ? 1 : stick_x < -0.5f ? -1 : 0;
    // 3.3: what was held when the dialog opened (the A of the talk, a
    // stick) acts only after everything was released.
    if (held_latch_) {
        if (buttons != 0 || sx != 0 || sy != 0) {
            last_buttons_ = buttons;
            last_stick_x_ = sx;
            last_stick_y_ = sy;
            if (menu_.is_open()) menu_.tick();
            return {};
        }
        held_latch_ = false;
    }
    const std::uint32_t trigger = buttons & ~last_buttons_;
    last_buttons_ = buttons;
    const int my = sy != last_stick_y_ ? sy : 0, mx = sx != last_stick_x_ ? sx : 0;
    last_stick_y_ = sy;
    last_stick_x_ = sx;
    if (!menu_.is_open()) return {};
    MenuKeys keys;
    keys.up = (trigger & pad_button::up) || my < 0;
    keys.down = (trigger & pad_button::down) || my > 0;
    keys.left = (trigger & pad_button::left) || mx < 0;
    keys.right = (trigger & pad_button::right) || mx > 0;
    keys.a = trigger & pad_button::a;
    keys.b = trigger & pad_button::b;
    keys.l = trigger & pad_button::l;
    keys.r = trigger & pad_button::r;
    menu_.tick();
    auto action = menu_.input(keys, meta_);
    if (action.kind == LobbyAction::Kind::intro_seen) {
        meta_.seen_intro = true;
        if (on_intro_seen) on_intro_seen();
    }
    if (action.kind == LobbyAction::Kind::start_run) back_.reset();
    opened_or_closed();
    return action;
}

void OverflowLobby::close() {
    menu_.close();
    opened_or_closed();
}

void OverflowLobby::returned(const RunSummary& summary) {
    back_ = summary;
    // Back in the lobby: Gekomon's "Welcome back" page opens (13.3 step 4)
    // at the first step the NPC talks and player 1 has the control.
    welcome_ = true;
}

bool OverflowLobby::needs_present() const { return actor_.alpha() > 0.0f || state_.shown || menu_.is_open(); }

void OverflowLobby::present(std::vector<engine::DrawItem>& items, engine::ShadowFrame* shadow, const engine::FrameCamera& camera,
                            std::optional<engine::Vec3f> own, double now, std::uint64_t frame) {
    // The NPC turns to the player and plays SQ_TALK while the dialog is open.
    if (menu_.is_open() && own) {
        actor_.face(*own, now);
        actor_.set_sequence("SQ_TALK");
    } else {
        actor_.face(actor_.position() + engine::Vec3f{std::sin(kNpcYaw), 0, std::cos(kNpcYaw)}, now, 1.5f);
        actor_.set_sequence("SQ_WAIT");
    }
    actor_.draw(items, shadow, camera, own, now);
    if (menu_.is_open() && ui_.ready()) {
        ui_.begin();
        // The face icon's picture from the NPC's own package (once loaded).
        if (!face_tried_ && actor_.ready()) {
            face_tried_ = true;
            if (const auto document = actor_.document()) face_ = model_texture(*document, "n_gekoaic");
        }
        draw_lobby_menu(ui_, menu_, meta_, frame, face_.get());
        ui_.finish(items);
    }
}

} // namespace dw4::game::overflow
