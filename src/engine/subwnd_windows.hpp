#pragma once

// Private to the SUBWND windows (subwnd*.cpp): the window framework and the
// windows shared by the status pages.

#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace dw4::engine {

// ===========================================================================
// Window framework (8036B278..8036C170): a window runs its state machine
// (+28: 0 open, 1 run, 2 close, 3 wait), owns child windows, and remembers
// the text lines and models it opened so closing clears and closes them.
// ===========================================================================
struct SubWindowManager::Window {
    SubWindowManager* manager{};
    Window* parent{};
    int player{};              // +20
    int mode{};                // +44
    bool running = true;       // +24
    int state{}, sub{}, step{}; // +28 / +32 / +36
    int selection{};           // +48 _retCode, written by a child select
    int last{};                // +40: cleared with every state change
    std::size_t child_mark{};  // +80: children count when a child was started
    // Shared like the original reference-counted windows: a window may keep a
    // sibling alive while it waits for it to close.
    std::vector<std::shared_ptr<Window>> children;
    std::vector<std::pair<std::string, std::string>> lines;
    struct Model {
        std::string name, close;
        int index{};
    };
    std::vector<Model> models;

    virtual ~Window() = default;
    virtual void update(Engine& engine) = 0;

    void next_state(int value) {
        state = value;
        sub = step = last = 0;
    }
    void set_sub(int value) {
        sub = value;
        step = last = 0;
    }
    void set_step(int value) {
        step = value;
        last = 0;
    }
    void mark() { child_mark = children.size(); }
    bool child_done() const { return children.size() != child_mark; }
    std::shared_ptr<Window> shared_child(const Window* window) const {
        for (const auto& child : children)
            if (child.get() == window) return child;
        return nullptr;
    }
    void align(Engine& engine, const std::string& layer, const std::string& line, const char* how) const {
        Packet p;
        p.strings = {layer, line, how};
        engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, "ALIGN", p);
    }
    // 8036BA64 without remembering: a marker generated or re-sequenced.
    void marker_at(Engine& engine, const std::string& name, const std::string& sequence, Vec3f position) {
        open_model(engine, name, 0, sequence, "", position);
    }
    template <class T> T* find_child() {
        for (auto& child : children)
            if (auto* found = dynamic_cast<T*>(child.get())) return found;
        return nullptr;
    }
    GameSystem::PlayerSlot* player_slot(Engine& engine) const {
        auto* sys = engine.get<GameSystem>();
        return sys ? &sys->players[std::size_t(std::clamp(player, 0, 3))] : nullptr;
    }
    // GAMERAM RECALCPLAYER / SUBWND UPDATE, ONPREVIEW, OFPREVIEW for the player.
    void recalc(Engine& engine) const {
        if (auto* sys = engine.get<GameSystem>()) sys->recalc_player(engine, player);
    }
    void subwnd(Engine& engine, const char* command) const {
        Packet p;
        p.ints.push_back(player);
        engine.messenger().send(engine, "SUBWND", std::nullopt, -1, command, p);
    }
    void model(Engine& engine, const std::string& name, int index, const std::string& sequence) const {
        Packet p;
        engine.messenger().send(engine, "MODELCTRL", name, index, sequence, p);
    }
    void textctl(Engine& engine, const std::string& layer, const std::string& line, const char* command,
                 const Vec3f* rgb = nullptr) const {
        Packet p;
        p.strings = {layer, line};
        if (rgb) p.vectors.push_back(*rgb);
        engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, command, p);
    }
    void textset(Engine& engine, const std::string& layer, const std::string& line, const std::string& value) const {
        Packet p;
        p.strings = {layer, line, value};
        engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, "TEXTSET", p);
    }
    template <class T> T& add_child(Engine& engine) {
        (void)engine;
        auto child = std::make_shared<T>();
        child->manager = manager;
        child->parent = this;
        child->player = player;
        T& ref = *child;
        children.push_back(std::move(child));
        return ref;
    }
    // 8036C170: update, close children of a closing window, run children and
    // drop the finished ones.
    void run(Engine& engine) {
        if (running) update(engine);
        if (!running || state >= 2)
            for (auto& child : children)
                if (child->state < 2) child->next_state(2);
        for (std::size_t i = 0; i < children.size();) {
            children[i]->run(engine);
            if (!children[i]->running) children.erase(children.begin() + std::ptrdiff_t(i));
            else ++i;
        }
    }
    // 8036C06C
    void close_children() {
        for (auto& child : children)
            if (child->state < 2) child->next_state(2);
    }
    // 8036BA64: a running model gets the open sequence, otherwise it is
    // generated at the position; with a close sequence it is remembered.
    void open_model(Engine& engine, const std::string& name, int index, const std::string& open, const std::string& close,
                    Vec3f position = {}) {
        auto& messenger = engine.messenger();
        Packet run;
        messenger.send(engine, "MODELCTRL", name, index, "ISRUN", run);
        if (!run.bools.empty() && run.bools[0]) {
            Packet p;
            messenger.send(engine, "MODELCTRL", name, index, open, p);
        } else {
            Packet p;
            p.strings.emplace_back(open);
            p.vectors.push_back(position);
            messenger.send(engine, "MODELCTRL", name, index, "GENERATE", p);
        }
        if (!close.empty()) models.push_back({name, close, index});
    }
    // 8036BD14: send every remembered model its close sequence.
    void close_models(Engine& engine) {
        for (const auto& model : models) {
            Packet p;
            engine.messenger().send(engine, "MODELCTRL", model.name, model.index, model.close, p);
        }
    }
    // 8036B864 / 8036B7D0: switch the remembered text lines off.
    void clear_lines(Engine& engine) {
        for (const auto& [layer, line] : lines) {
            Packet p;
            p.strings = {layer, line, "CLR"};
            engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", p);
        }
        lines.clear();
    }
    // 8036BDF0: closed once no remembered model runs and no child is left.
    bool closed(Engine& engine) {
        for (const auto& model : models) {
            Packet run;
            engine.messenger().send(engine, "MODELCTRL", model.name, model.index, "ISRUN", run);
            if (!run.bools.empty() && run.bools[0]) return false;
        }
        if (!children.empty()) return false;
        models.clear();
        return true;
    }
    // 8036B278: align, set and show a text line and remember it.
    void text(Engine& engine, const std::string& layer, const std::string& line, const std::string& value,
              const std::string& align) {
        auto& messenger = engine.messenger();
        Packet a, t, s;
        a.strings = {layer, line, align};
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "ALIGN", a);
        t.strings = {layer, line, value};
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "TEXTSET", t);
        s.strings = {layer, line, "SET"};
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", s);
        lines.emplace_back(layer, line);
    }
    void remember(const std::string& layer, const std::string& line) { lines.emplace_back(layer, line); }
    void switch_line(Engine& engine, const std::string& layer, const std::string& line, bool on) const {
        Packet p;
        p.strings = {layer, line, on ? "DISPON" : "DISPOFF"};
        engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", p);
    }
    // 8036B740: run a text script with TXTgetCallNo = call.
    void text_script(Engine& engine, const char* script, int call) {
        if (auto* sys = engine.get<GameSystem>()) sys->text_call = call;
        engine.run_script(script);
    }
    bool model_in(Engine& engine, const std::string& name, int index, const std::string& sequence) {
        Packet p;
        p.strings.emplace_back(sequence);
        engine.messenger().send(engine, "MODELCTRL", name, index, "ISSEQNAME", p);
        return !p.bools.empty() && p.bools[0];
    }
    bool model_running(Engine& engine, const std::string& name, int index) {
        Packet p;
        engine.messenger().send(engine, "MODELCTRL", name, index, "ISRUN", p);
        return !p.bools.empty() && p.bools[0];
    }
    void layers(Engine& engine, bool on) {
        for (const char* layer : {"BG01", "BG02", "BG03", "GM01", "GM03"}) {
            Packet p;
            p.strings = {layer, on ? "DISPON" : "DISPOFF"};
            engine.messenger().send(engine, "LAYERCTL", std::nullopt, -1, "SWITCH", p);
        }
    }
    // The common close states 2 / 3 (8039108C).
    bool close_states(Engine& engine) {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            return true;
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return true;
        }
        return false;
    }
};

namespace subwnd {
using Window = SubWindowManager::Window;

// beNDMWStatusTitle (8039108C): open once, then idle until closed.
struct Title : Window {
    virtual void open(Engine& engine) = 0;
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            open(engine);
            next_state(1);
        }
    }
};

// Title02 (80392D10): the message window frame.
struct Title02 : Title {
    void open(Engine& engine) override { open_model(engine, "i_messwn", 0, "SQ_STOPEN", "SQ_STCLOSE"); }
};

// beNDMWStatusSelect base (80390418): a SELECTCTRL cursor whose choice goes
// to the parent's selection (-1 when cancelled).
struct Select : Window {
    std::string cursor = "i_stcursor02";
    int count = 3, initial = 0;
    Vec3f position{};
    bool no_cancel{};
    std::vector<std::uint8_t> enabled;
    virtual void open(Engine& engine) = 0;
    void update(Engine& engine) override {
        auto& messenger = engine.messenger();
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            next_state(3);
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return;
        }
        if (state == 4) {
            // A new page (803906D0 / FO2TOP): the idle cursor returns to the
            // top with the new count and enables.
            Packet idle;
            messenger.send(engine, "SELECTCTRL", cursor, player, "ISFO2TOP", idle);
            if (!idle.bools.empty() && !idle.bools[0]) {
                Packet p;
                p.ints = {count};
                p.bools = enabled;
                messenger.send(engine, "SELECTCTRL", cursor, player, "FO2TOP", p);
                Packet select;
                messenger.send(engine, "SELECTCTRL", cursor, player, "GETSELECT", select);
                if (parent && !select.ints.empty()) parent->selection = select.ints[0];
            }
            next_state(1);
            return;
        }
        if (state == 0) {
            open(engine);
            Packet p;
            p.ints = {count, initial};
            p.vectors.push_back(position);
            if (no_cancel) p.strings.emplace_back("NOCANCEL");
            p.bools = enabled;
            messenger.send(engine, "SELECTCTRL", cursor, player, "GENERATE", p);
            next_state(1);
        }
        Packet select, end;
        messenger.send(engine, "SELECTCTRL", cursor, player, "GETSELECT", select);
        if (parent && !select.ints.empty()) parent->selection = select.ints[0];
        messenger.send(engine, "SELECTCTRL", cursor, player, "ISEND", end);
        if (!end.bools.empty() && end.bools[0]) {
            next_state(2);
            close_models(engine);
            clear_lines(engine);
            next_state(3);
            if (closed(engine)) running = false;
        }
    }
};

// SelectE1 (80390F8C): the plain i_stcursor01 cursor; count, position,
// initial line and enables come from the creator.
struct SelectE1 : Select {
    void open(Engine&) override {
        cursor = "i_stcursor01";
        no_cancel = false;
    }
};

// SelectE0 (80390E48): a choice in the message window (i_stmesscr), the
// labels from txt_status.lua call mode.
struct SelectE0 : Select {
    void open(Engine& engine) override {
        text(engine, "STWINFONT29", "font100", " ", "LEFT");
        for (const char* line : {"font000", "font001", "font002"}) text(engine, "STWINFONT29", line, " ", "LEFT");
        if (mode > 0) text_script(engine, "txt_status.lua", mode);
        cursor = "i_stmesscr";
        position = {};
        initial = 0;
        no_cancel = false;
    }
};

// Select01 (80390B78): Power Socket / Main Slot / Sub Slot.
struct Select01 : Select {
    void open(Engine& engine) override {
        if (mode == 61) {
            open_model(engine, "i_stmenu01", 0, "SQ_4LINEOPEN", "SQ_CLOSE");
            count = 4;
        } else {
            open_model(engine, "i_stmenu01", 0, mode == 31 || mode == 91 ? "SQ_3LINEDOWNOPEN" : "", "SQ_CLOSE");
            if (mode == 31 || mode == 91) count = 3;
        }
        text(engine, "STWINFONT29", "font100", " ", "LEFT");
        for (const char* line : {"font000", "font001", "font002", "font003"}) text(engine, "STWINFONT09", line, " ", "LEFT");
        if (mode > 0) text_script(engine, "txt_status.lua", mode);
        cursor = "i_stcursor02";
        position = {-83.0f, 172.0f, 0.0f};
        no_cancel = false;
    }
};

// beNDMWShopSelect00 (803A6E64): a one-line message acknowledged with A.
struct ShopSelect00 : Select {
    std::string line = "font000"; // +112: the message line
    void open(Engine& engine) override {
        if (mode > 0) {
            remember("STWINFONT29", line);
            text_script(engine, "txt_shop.lua", mode);
        }
        cursor = "i_stmessky";
        count = 1;
        position = {};
        initial = 0;
        no_cancel = false;
    }
};

// Title04 (80393178): the page title.
struct Title04 : Title {
    void open(Engine& engine) override {
        open_model(engine, "i_sttitle", 0, "SQ_OPEN", "SQ_CLOSE");
        remember("STWINFONT12", "font000");
        text_script(engine, "txt_status.lua", mode);
    }
};

// ---------------------------------------------------------------------------
// beNDMWStatusCtrlD0 (update 803878D0): the DEVICEFOLDER list, 12 devices a
// page with the kinds allowed by _kindList; the choice goes to the parent as
// a folder index.
// ---------------------------------------------------------------------------
struct DeviceList : Window {
    int page{};                      // +84
    int pages = 1;                   // +88
    int last_line{};                 // +92
    bool bank{};                     // +96 _isBankFolder (BANKDEVICE)
    std::vector<int> kinds;          // +100 _kindList
    int ab_type{};                   // +104 _isAbType: chips 0 WU, 1 BLUEAB
    bool equip_disable{};            // +108 _isEquipDisable
    bool unknown_enable{};           // +109 _isUnknownEnable
    bool unknown_disable{};          // +110 _isUnknownDisable
    bool auto_close = true;          // +111 _isAutoClose
    std::vector<std::uint8_t> decide; // +112 _decideList

    static constexpr const char* lines[12] = {"font000", "font001", "font002", "font003", "font004", "font005",
                                               "font006", "font007", "font008", "font009", "font010", "font011"};
    const std::vector<ItemDevice>* folder(Engine& engine) const {
        auto* p = player_slot(engine);
        return p ? (bank ? &p->bank_devices : &p->devices) : nullptr;
    }
    static bool unknown(const ItemDevice& d) {
        return (d.type == 0 && d.record == 276) || (d.type == 1 && d.record == 31) || (d.type == 2 && d.record == 44);
    }
    int hovered() const { return selection == -1 ? -1 : 12 * page + selection; } // 80388B50
    // 803887CC: page count, the devices of this page and their enables.
    int page_lines(Engine& engine) {
        const auto* devices = folder(engine);
        const int size = devices ? int(devices->size()) : 0;
        pages = size % 12 ? size / 12 + 1 : size / 12;
        const int rest = size - 12 * page;
        const int n = rest < 12 ? rest % 12 : 12;
        decide.clear();
        auto* db = engine.get<DatabaseManager>();
        for (int i = 0; i < n; ++i) {
            const auto& d = (*devices)[std::size_t(12 * page + i)];
            bool ok = true;
            if (!kinds.empty()) {
                ok = false;
                for (const int kind : kinds) {
                    if (kind == 3) {
                        if (d.type != 3 || !db) continue;
                        const bool wu = db->number("abChip", d.record, "WU").value_or(0) != 0;
                        const bool blue = db->number("abChip", d.record, "BLUEAB").value_or(0) != 0;
                        if (ab_type == 1 ? blue : ab_type == 0 ? wu : true) {
                            ok = true;
                            break;
                        }
                    } else if (kind == d.type) {
                        ok = true;
                        break;
                    }
                }
            }
            if (equip_disable && d.flag) ok = false;
            if (unknown_disable && unknown(d)) ok = false;
            if (unknown_enable) ok = unknown(d);
            decide.push_back(ok ? 1 : 0);
        }
        if (pages == 0) pages = 1;
        return n ? n : 1;
    }
    // 803881CC: names (equipped mark, rank colors) and page numbers; the
    // description of the cursor line.
    void fill(Engine& engine, bool names, bool description) {
        const auto* devices = folder(engine);
        const int size = devices ? int(devices->size()) : 0;
        auto* db = engine.get<DatabaseManager>();
        const char* pages_layer = bank ? "STWINFONT40" : "STWINFONT23";
        if (names) {
            for (int i = 0; i < 12; ++i) {
                const int at = i + 12 * page;
                const bool shown = at < size;
                if (shown) {
                    const auto& d = (*devices)[std::size_t(at)];
                    // SJIS 81BA marks an equipped device, 8A71 is its blank.
                    std::string name = d.flag ? "\x81\xba" : "\x8a\x71";
                    if (db) name += db->text(device_table(d), d.record, "NAME");
                    textset(engine, "STWINFONT14", lines[i], name);
                    if (std::size_t(i) < decide.size() && decide[std::size_t(i)]) {
                        const Vec3f rgb = device_color(d);
                        textctl(engine, "STWINFONT14", lines[i], "COLOR", &rgb);
                    } else {
                        textctl(engine, "STWINFONT14", lines[i], "CANCELCOLOR");
                    }
                    textset(engine, "STWINFONT16", lines[i], " ");
                }
                switch_line(engine, "STWINFONT14", lines[i], shown);
                switch_line(engine, "STWINFONT16", lines[i], shown);
            }
            textset(engine, pages_layer, "font000", std::string(1, char('1' + page)));
            textset(engine, pages_layer, "font001", std::string(1, char('0' + pages)));
        }
        if (description) {
            const int at = 12 * page + selection;
            const bool shown = at >= 0 && at < size;
            if (shown && db) {
                const auto& d = (*devices)[std::size_t(at)];
                std::string txt = db->text(device_table(d), d.record, "TXT");
                // Weapons and boards end in "Mods:" followed by the rank.
                if ((d.type == 0 && d.record != 276) || (d.type == 1 && d.record != 31)) txt += char('0' + d.rank);
                textset(engine, "STWINFONT29", "font000", txt);
            }
            switch_line(engine, "STWINFONT29", "font000", shown);
        }
    }
    // 8037E7A8 / 8037E8C8: the page turns with left / right, not while the
    // stick holds up or down.
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
    void add_select(Engine& engine, int initial) {
        auto& select = add_child<SelectE1>(engine);
        select.count = page_lines(engine);
        select.enabled = decide;
        select.position = {-121.0f, 128.0f, 0.0f};
        select.initial = initial;
    }
    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            for (const char* line : lines) {
                textctl(engine, "STWINFONT14", line, "DEFAULTCOLOR");
                textctl(engine, "STWINFONT16", line, "DEFAULTCOLOR");
            }
            return;
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return;
        }
        if (state == 0) {
            remember("STWINFONT13", "font000");
            text_script(engine, bank ? "txt_shop.lua" : "txt_status.lua", bank ? 1303 : 70);
            for (const char* line : lines) text(engine, "STWINFONT14", line, " ", "LEFT");
            for (const char* line : lines) text(engine, "STWINFONT16", line, " ", "RIGHT");
            const char* pages_layer = bank ? "STWINFONT40" : "STWINFONT23";
            text(engine, pages_layer, "font000", " ", "RIGHT");
            text(engine, pages_layer, "font001", " ", "RIGHT");
            text(engine, "STWINFONT29", "font000", " ", "LEFT");
            add_select(engine, 0);
            mark();
            open_model(engine, "i_stfolder", 0, "SQ_AWINONOPEN", "SQ_CLOSE");
            // 8037E6BC: the page arrows.
            open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", {-146.0f, 25.0f, 0.0f});
            open_model(engine, "i_stcursorright", 0, pages <= 1 ? "SQ_HIDE" : "SQ_SELECT", "SQ_CLOSE", {85.0f, 25.0f, 0.0f});
            next_state(1);
        }
        if (sub == 0) {
            fill(engine, true, true);
            set_sub(1);
        }
        if (sub == 1) {
            if (!child_done()) {
                if (selection == -1) return;
                if (selection == last) {
                    auto* select = find_child<SelectE1>();
                    if (!select) return;
                    Packet idle;
                    engine.messenger().send(engine, "SELECTCTRL", select->cursor, player, "ISFO2TOP", idle);
                    if (idle.bools.empty() || idle.bools[0]) return;
                    if (turn(engine, false) || turn(engine, true)) {
                        select->next_state(4);
                        select->count = page_lines(engine);
                        select->enabled = decide;
                        set_sub(0);
                    }
                } else {
                    fill(engine, false, true);
                    last = selection;
                }
                return;
            }
            if (selection == -1) {
                if (parent) parent->selection = -1;
            } else {
                if (parent) parent->selection = 12 * page + selection;
                last_line = selection;
            }
            if (auto_close) next_state(2);
            else set_sub(2);
            return;
        }
        // Sub 2: the parent handled the choice and sets step 1 for a new cursor.
        if (sub == 2 && step == 1) {
            const auto* devices = folder(engine);
            const int size = devices ? int(devices->size()) : 0;
            if (last_line + 12 * page >= size) {
                if (last_line) --last_line;
                else if (page > 0) --page;
            }
            if (page == 0) model(engine, "i_stcursorleft", 0, "SQ_HIDE");
            if (size <= 12 * (page + 1)) model(engine, "i_stcursorright", 0, "SQ_HIDE");
            add_select(engine, last_line);
            next_state(1);
        }
    }
};

// Title00 (80391154): the dimmed background.
struct Title00 : Title {
    void open(Engine& engine) override {
        open_model(engine, "i_stback", 0, "SQ_OPEN", "SQ_CLOSE");
        open_model(engine, "i_bgclear", 0, "SQ_OPEN", "SQ_CLOSE");
    }
};

// Title03 (80392D48): the account panel with its labels.
struct Title03 : Title {
    void open(Engine& engine) override {
        open_model(engine, "i_staccount", 0, "SQ_OPEN", "SQ_CLOSE");
        for (const char* line : {"font000", "font001", "font002"}) remember("STWINFONT34", line);
        text_script(engine, "txt_status.lua", 0);
    }
};

// ShopSelect52 (803A9D90): a count picker (i_stcursor05) with mode lines,
// starting on the last one.
struct ShopSelect52 : Select {
    void open(Engine&) override {
        cursor = "i_stcursor05";
        count = mode;
        initial = mode - 1;
        no_cancel = false;
    }
};

inline std::string line_name(const char* format, int value) {
    char line[16];
    std::snprintf(line, sizeof line, format, value);
    return line;
}

// ShopSelect34 (803A971C): the seven-digit cursor (i_stcursor03).
struct ShopSelect34 : Select {
    void open(Engine& engine) override {
        for (int i = 0; i < 7; ++i) {
            if (mode == 3) {
                align(engine, "STWINFONT28", line_name("font%03d", i), "CENTER");
            } else {
                for (const char* format : {"font%03d", "font01%d", "font02%d"})
                    align(engine, "STWINFONT25", line_name(format, i), "CENTER");
            }
        }
        cursor = "i_stcursor03";
        count = 7;
        initial = 6;
        no_cancel = false;
        if (mode == 3) position = {147.0f, -2.0f, 0.0f};
        else if (mode == 1) position = {-20.0f, -1.0f, 0.0f};
        else if (mode == 0 || mode == 2) position = {-20.0f, 138.0f, 0.0f};
    }
};

// 803A9B48: value as width digits on the format lines, the units on line 0
// (STWINFONT28 for a mode 3 window, STWINFONT25 otherwise).
inline void show_digits(Engine& engine, const Window& window, int value, int width, const char* format = "font%03d") {
    const char* layer = window.mode == 3 ? "STWINFONT28" : "STWINFONT25";
    for (int i = 0; i < 7; ++i) window.switch_line(engine, layer, line_name(format, i), false);
    if (width < 1 || width > 7) return;
    char text[16];
    std::snprintf(text, sizeof text, "%0*d", width, value);
    const int length = int(std::char_traits<char>::length(text));
    for (int k = length - 1; k > -1; --k) {
        const auto line = line_name(format, k);
        window.textset(engine, layer, line, std::string(1, text[length - 1 - k]));
        window.switch_line(engine, layer, line, true);
    }
}

// 8039E320 / 80392AE0: point the parent's status panel (StatusTitle01) at
// player, the lobby variant (created in mode 1 when missing) or the full page.
void status_panel(Window& parent, Engine& engine, int player, bool shop);
// beNDMWStatusCtrlEquip (subwnd.cpp); mode 1 is the Digi-Lab's variant.
Window& add_equip_page(Window& parent, Engine& engine, int mode);

// The shop programs 1 and 3..10 (subwnd_shop.cpp).
std::unique_ptr<Window> make_shop_program(int program);

// The folder page (beNDMWStatusCtrlFolder, subwnd_folder.cpp) and the skill
// page (beNDMWStatusCtrlSkill, subwnd_skill.cpp).
Window& add_folder_page(Window& parent, Engine& engine);
Window& add_skill_page(Window& parent, Engine& engine);

} // namespace subwnd
} // namespace dw4::engine
