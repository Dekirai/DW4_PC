#include "subwnd_windows.hpp"

namespace dw4::engine::subwnd {
namespace {

constexpr const char* kLines[9] = {"font000", "font001", "font002", "font003", "font004",
                                   "font005", "font006", "font007", "font008"};

// ---------------------------------------------------------------------------
// beNDMWStatusCtrlSkill (8038D368): the forms reached (8 a page), the weapon
// skills against the level's limit and the technique list.
// ---------------------------------------------------------------------------
struct CtrlSkill : Window {
    int page{};  // +84
    int pages = 1; // +88
    int kind{};  // +96: 0 forms, 1 skills, 2 techniques
    struct Form {
        int row{}, level{};
        std::string name;
    };
    std::vector<Form> forms; // +100 (803AC5A8)

    // 803AC5A8: BASE_ISUSE species with their BASE_LEVEL; species 3 first.
    void build_forms(Engine& engine) {
        forms.clear();
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) return;
        for (int i = 0; i < 16; ++i) {
            const auto found = p->base_isuse.find(i);
            if (found == p->base_isuse.end() || !found->second) continue;
            Form form{i, p->base_level[std::size_t(i)], db->text("shinka", i, "NAME")};
            if (i == 3) forms.insert(forms.begin(), form);
            else forms.push_back(form);
        }
    }
    // 8038E1A4
    void fill(Engine& engine, bool names, bool description) {
        if (selection == -1) return;
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) return;
        if (names) {
            for (int i = 0; i < 8; ++i) {
                const int at = i + 8 * page;
                const bool shown = at < int(forms.size());
                if (shown) {
                    const auto& form = forms[std::size_t(at)];
                    textset(engine, "STWINFONT14", kLines[i], form.name);
                    textset(engine, "STWINFONT42", kLines[i], "Lv");
                    textset(engine, "STWINFONT18", kLines[i], std::to_string(form.level));
                }
                for (const char* layer : {"STWINFONT14", "STWINFONT42", "STWINFONT18"})
                    switch_line(engine, layer, kLines[i], shown);
            }
            if (pages > 1) {
                textset(engine, "STWINFONT24", "font000", std::string(1, char('1' + page)));
                textset(engine, "STWINFONT24", "font001", std::string(1, char('0' + pages)));
            }
        }
        if (!description) return;
        if (kind == 1) {
            text_script(engine, "txt_status.lua", selection + 111);
        } else if (kind == 2) {
            if (selection < int(p->wazalist.size()))
                textset(engine, "STWINFONT29", "font000", db->text("pcTech", p->wazalist[std::size_t(selection)], "TXT"));
        } else {
            const int at = 8 * page + selection;
            if (at < int(forms.size()))
                textset(engine, "STWINFONT29", "font000", db->text("shinka", forms[std::size_t(at)].row, "EXPLANATION"));
        }
    }
    // 8037E7A8 / 8037E8C8 on this page's arrows.
    bool turn(Engine& engine, bool right) {
        const auto& pad = engine.pads()[std::size_t(std::clamp(player, 0, 3))];
        const std::uint32_t stick = pad.left_directions.trigger | pad.left_directions.repeat;
        const std::uint32_t buttons = pad.buttons.trigger | pad.buttons.repeat;
        const bool pressed = right ? (stick & pad_direction::positive_x) || (buttons & pad_button::right)
                                   : (stick & pad_direction::negative_x) || (buttons & pad_button::left);
        const bool vertical = (pad.left_directions.held & (pad_direction::positive_y | pad_direction::negative_y)) != 0;
        if (!pressed || vertical) return false;
        if (right) {
            if (page >= pages - 1) return false;
            ++page;
            model(engine, "i_stcursorright", 0, page == pages - 1 ? "SQ_MOVEEND" : "SQ_MOVENEXT");
            model(engine, "i_stcursorleft", 0, "SQ_SELECT");
        } else {
            if (page <= 0) return false;
            --page;
            model(engine, "i_stcursorleft", 0, page ? "SQ_MOVENEXT" : "SQ_MOVEEND");
            model(engine, "i_stcursorright", 0, "SQ_SELECT");
        }
        return true;
    }
    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            model(engine, "i_sttitle", 0, "SQ_CLOSE");
            switch_line(engine, "STWINFONT12", "font000", false);
            return;
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return;
        }
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) {
            next_state(2);
            return;
        }
        if (state == 0) {
            open_model(engine, "i_sttitle", 0, "SQ_OPEN", "");
            text_script(engine, "txt_status.lua", 90);
            kind = 0;
            next_state(1);
        }
        if (sub < 2) {
            if (sub == 0) {
                auto& menu = add_child<Select01>(engine);
                menu.mode = 91;
                menu.initial = kind;
                menu.text(engine, "STWINFONT29", "font000", " ", "LEFT");
                mark();
            }
            if (selection != -1) text_script(engine, "txt_status.lua", selection + 98);
            set_sub(2);
            last = selection;
        }
        if (sub == 2) {
            if (!child_done()) {
                if (selection != -1 && last != selection) set_sub(1);
                return;
            }
            if (selection < 0 || selection > 2) {
                next_state(2);
                return;
            }
            if (selection == 0) {
                build_forms(engine);
                pages = forms.size() <= 8 ? 1 : 2;
                page = 0;
            }
            kind = selection;
            set_sub(3);
            return;
        }
        if (step == 0) {
            std::string frame;
            if (kind == 1) {
                frame = "SQ_BWINOFFOPEN";
            } else if (kind == 2) {
                frame = "SQ_WAZAOPEN";
            } else {
                frame = pages > 1 ? "SQ_BWINONOPEN" : "SQ_BWINOFFOPEN";
                open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", {-146.0f, 65.0f, 0.0f});
                open_model(engine, "i_stcursorright", 0, pages <= 1 ? "SQ_HIDE" : "SQ_SELECT", "SQ_CLOSE", {127.0f, 65.0f, 0.0f});
            }
            open_model(engine, "i_stfolder", 0, frame, "SQ_CLOSE");
            auto& select = add_child<SelectE1>(engine);
            select.position = {-121.0f, 128.0f, 0.0f};
            select.initial = 0;
            remember("STWINFONT13", "font000");
            text_script(engine, "txt_status.lua", kind + 95);
            if (kind == 1) {
                // A skill reaches its limit for the level: 30 per level up to 36,
                // then 20L+360, 10L+1430 and 8L+1884.
                const int level = p->stats.level;
                const int limit = level > 227 ? 8 * level + 1884 : level > 107 ? 10 * level + 1430
                                : level > 36  ? 20 * level + 360 : 30 * level;
                static const Vec3f limited{0.9f, 0.4f, 0.2f};
                for (std::size_t i = 0; i < p->stats.skill.size(); ++i) {
                    remember("STWINFONT14", kLines[i]);
                    text(engine, "STWINFONT18", kLines[i], std::to_string(p->stats.skill[i]), "RIGHT");
                    if (p->stats.skill[i] < limit) textctl(engine, "STWINFONT14", kLines[i], "DEFAULTCOLOR");
                    else textctl(engine, "STWINFONT14", kLines[i], "COLOR", &limited);
                }
                text_script(engine, "txt_status.lua", 101);
                select.count = int(p->stats.skill.size());
            } else if (kind == 2) {
                for (std::size_t i = 0; i < p->wazalist.size() && i < 9; ++i)
                    text(engine, "STWINFONT14", kLines[i], db->text("pcTech", p->wazalist[i], "NAME"), "LEFT");
                select.count = p->wazalist.empty() ? 1 : int(p->wazalist.size());
            } else {
                int count = 8;
                if (pages <= 1) {
                    count = forms.empty() ? 2 : int(forms.size());
                } else {
                    text(engine, "STWINFONT24", "font000", " ", "CENTER");
                    text(engine, "STWINFONT24", "font001", " ", "CENTER");
                }
                for (int i = 0; i < count && i < 9; ++i) {
                    text(engine, "STWINFONT14", kLines[i], " ", "LEFT");
                    text(engine, "STWINFONT42", kLines[i], " ", "LEFT");
                    text(engine, "STWINFONT18", kLines[i], " ", "RIGHT");
                }
                fill(engine, true, false);
                select.count = count;
            }
            text(engine, "STWINFONT29", "font000", " ", "LEFT");
            mark();
            set_step(1);
            selection = 0;
        }
        if (step == 1) {
            if (selection != -1) fill(engine, false, true);
            set_step(2);
            last = selection;
        }
        if (!child_done()) {
            if (selection == -1) return;
            if (last != selection) {
                set_step(1);
                return;
            }
            if (kind != 0) return;
            auto* select = find_child<SelectE1>();
            if (!select) return;
            Packet idle;
            engine.messenger().send(engine, "SELECTCTRL", select->cursor, player, "ISFO2TOP", idle);
            if (idle.bools.empty() || idle.bools[0]) return;
            if (turn(engine, false) || turn(engine, true)) {
                select->next_state(4);
                select->count = page == 1 ? int(forms.size()) - 8 : 8;
                set_step(1);
                selection = 0;
                fill(engine, true, false);
            }
            return;
        }
        if (kind == 1)
            for (const char* line : kLines) textctl(engine, "STWINFONT14", line, "DEFAULTCOLOR");
        close_models(engine);
        models.clear();
        clear_lines(engine);
        selection = kind;
        set_sub(0);
    }
};

} // namespace

Window& add_skill_page(Window& parent, Engine& engine) { return parent.add_child<CtrlSkill>(engine); }

} // namespace dw4::engine::subwnd
