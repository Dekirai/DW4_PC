#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cstdio>

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
    std::vector<std::unique_ptr<Window>> children;
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
        auto child = std::make_unique<T>();
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
    void switch_line(Engine& engine, const std::string& layer, const std::string& line, bool on) {
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

namespace {
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

// Title00 (80391154): the dimmed background.
struct Title00 : Title {
    void open(Engine& engine) override {
        open_model(engine, "i_stback", 0, "SQ_OPEN", "SQ_CLOSE");
        open_model(engine, "i_bgclear", 0, "SQ_OPEN", "SQ_CLOSE");
    }
};

// Title01 (803911B8 / 80392AE0 / 80392108): the status page of the player.
struct Title01 : Title {
    void open(Engine& engine) override {
        static const char* labels[] = {"font030", "font031", "font032", "font033", "font034", "font035",
                                       "font036", "font037", "font038", "font039", "font060", "font061"};
        for (const char* line : labels) remember("STWINFONT06", line);
        text(engine, "STWINFONT02", "font000", "", "LEFT");
        text(engine, "STWINFONT04", "font000", "", "CENTER");
        text(engine, "STWINFONT04", "font001", "", "LEFT");
        static const char* values[] = {"font000", "font001", "font010", "font011", "font040", "font041", "font042",
                                       "font043", "font044", "font045", "font046", "font047", "font048", "font049",
                                       "font070", "font071"};
        for (const char* line : values) text(engine, "STWINFONT06", line, "", "RIGHT");
        text_script(engine, "txt_status.lua", 3);
        refresh(engine);
        open_model(engine, "i_stbase", player, "SQ_LOPEN", "SQ_CLOSE");
        static const char* shown[] = {"font030", "font031", "font032", "font033", "font034", "font035", "font036",
                                      "font037", "font038", "font039", "font060", "font061", "font000", "font001",
                                      "font010", "font011", "font040", "font041", "font042", "font043", "font044",
                                      "font045", "font046", "font047", "font048", "font049", "font070", "font071"};
        for (const char* line : shown) switch_line(engine, "STWINFONT06", line, true);
        switch_line(engine, "STWINFONT04", "font001", true);
    }
    // 80392108: SUBWND UPDATE fills the values; the portrait of the species.
    void refresh(Engine& engine) {
        close_models(engine);
        models.clear();
        Packet update;
        update.ints.push_back(player);
        engine.messenger().send(engine, "SUBWND", std::nullopt, -1, "UPDATE", update);
        std::string sequence = "SQ_agumonOPEN";
        if (auto* sys = engine.get<GameSystem>()) {
            const auto& digimon = sys->players[std::size_t(std::clamp(player, 0, 3))].digimon;
            if (digimon.starts_with("p_")) sequence = "SQ_" + digimon.substr(2) + "OPEN";
        }
        open_model(engine, "i_stdigiface", player, sequence, "SQ_CLOSE", {-410.0f, 240.0f, 0.0f});
    }
};

// Title02 (80392D10): the message window frame.
struct Title02 : Title {
    void open(Engine& engine) override { open_model(engine, "i_messwn", 0, "SQ_STOPEN", "SQ_STCLOSE"); }
};

// Title03 (80392D48): the account panel with its labels.
struct Title03 : Title {
    void open(Engine& engine) override {
        open_model(engine, "i_staccount", 0, "SQ_OPEN", "SQ_CLOSE");
        for (const char* line : {"font000", "font001", "font002"}) remember("STWINFONT34", line);
        text_script(engine, "txt_status.lua", 0);
    }
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

// Select00 (80390730): Equipment / Folder / Skill.
struct Select00 : Select {
    void open(Engine& engine) override {
        open_model(engine, "i_stmenu01", 0, "SQ_3LINEUPOPEN", "SQ_CLOSE");
        for (const char* line : {"font000", "font001", "font002"}) remember("STWINFONT08", line);
        text_script(engine, "txt_status.lua", 15);
        cursor = "i_stcursor02";
        count = 3;
        position = {-83.0f, 209.0f, 0.0f};
        no_cancel = false;
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
    void open(Engine& engine) override {
        if (mode > 0) {
            remember("STWINFONT29", "font000");
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
        return p ? &p->devices : nullptr; // BANKDEVICE is not reconstructed
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

// ---------------------------------------------------------------------------
// beNDMWStatusCtrlBaseEquip (update 8037FF8C) and its PowerSocket / MainSlot /
// SubSlot windows (vtables 804EBE7C / 804EBDA8 / 804EBCD4): pick a slot, then
// change, remove or reorder its device or edit the chips in its pockets. Every
// change runs RECALCPLAYER; the stats preview the device under the cursor.
// ---------------------------------------------------------------------------
struct SlotWindow : Window {
    enum Kind { kPower, kMain, kSub } kind = kPower;
    int slot{};     // +84 _kindListPos
    int count = 1;  // +88 _kindListMax
    int pocket{};   // +92 _pocketPos

    static constexpr const char* socket_lines[3] = {"font000", "font001", "font002"};
    static constexpr const char* pocket_lines[5] = {"font100", "font101", "font102", "font103", "font104"};

    std::vector<int>* slots(GameSystem::PlayerSlot& p) const {
        return kind == kPower ? &p.power_socket : kind == kMain ? &p.main_slot : &p.sub_slot;
    }
    std::vector<int>* pockets(GameSystem::PlayerSlot& p) const {
        return kind == kPower ? &p.power_pocket : kind == kMain ? &p.main_pocket : nullptr;
    }
    static int at(const std::vector<int>& list, int index) {
        return index >= 0 && index < int(list.size()) ? list[std::size_t(index)] : -1;
    }
    std::string column(Engine& engine, const ItemDevice& d, const char* name) const {
        auto* db = engine.get<DatabaseManager>();
        return db ? db->text(device_table(d), d.record, name) : std::string();
    }
    // The chip pockets open by the rank of the first slot's device.
    std::string pocket_sequence(GameSystem::PlayerSlot& p) const {
        const auto* first = p.device(at(*slots(p), 0));
        const int rank = first ? first->rank : -1;
        static const char* sequences[6] = {"SQ_0LINEOPEN", "SQ_1LINEOPEN", "SQ_2LINEOPEN",
                                           "SQ_3LINEOPEN", "SQ_4LINEOPEN", "SQ_5LINEOPEN"};
        return sequences[rank >= 1 && rank <= 5 ? rank : 0];
    }

    // 80381D20: put folder device into list[index]; the same device again
    // clears the entry, a device moves out of other entries of the list, a
    // chip replaces chips raising the same status.
    void equip_into(Engine& engine, std::vector<int>& list, int index, int device) {
        auto* p = player_slot(engine);
        if (!p) return;
        auto flag = [&](int d, bool on) {
            if (d >= 0 && d < int(p->devices.size())) p->devices[std::size_t(d)].flag = on;
        };
        const bool valid = index >= 0 && index < int(list.size());
        const int current = at(list, index);
        const int chosen = device >= 0 && device < int(p->devices.size()) ? device : -1;
        if (current == chosen) {
            if (valid) list[std::size_t(index)] = -1;
            flag(current, false);
            return;
        }
        if (current >= 0) {
            flag(current, false);
            list[std::size_t(index)] = -1;
        }
        if (chosen >= 0) {
            for (int& entry : list)
                if (entry == chosen) {
                    flag(entry, false);
                    entry = -1;
                }
            if (p->devices[std::size_t(chosen)].type == 3) {
                static const char* stats[12] = {"TSUYOSA", "KASHIKOSA", "SEISHIN", "SUBAYASA", "RES_FIRE", "RES_ICE",
                                                "RES_THUNDER", "RES_DARK", "CHIMEIRITSU", "HPDRAIN", "MPDRAIN", "CHOHATURITSU"};
                auto* db = engine.get<DatabaseManager>();
                auto raises = [&](int d, const char* stat) {
                    return db && db->number("abChip", p->devices[std::size_t(d)].record, stat).value_or(0) > 0;
                };
                const char* stat = nullptr;
                for (const char* s : stats)
                    if (raises(chosen, s)) {
                        stat = s;
                        break;
                    }
                if (stat)
                    for (int& entry : list)
                        if (entry >= 0 && raises(entry, stat)) {
                            flag(entry, false);
                            entry = -1;
                        }
            }
            flag(chosen, true);
        }
        if (valid) list[std::size_t(index)] = chosen;
    }
    // 80382858: -1 previews list[index] emptied; otherwise the device under
    // the list cursor is equipped for the preview and the list restored.
    void preview(Engine& engine, std::vector<int>& list, int index, int arg) {
        auto* p = player_slot(engine);
        if (!p) return;
        if (arg == -1) {
            const bool valid = index >= 0 && index < int(list.size());
            const int saved = at(list, index);
            if (valid) list[std::size_t(index)] = -1;
            recalc(engine);
            subwnd(engine, "ONPREVIEW");
            if (valid) list[std::size_t(index)] = saved;
            recalc(engine);
            return;
        }
        if (selection == -1) return;
        auto* devices = find_child<DeviceList>();
        if (!devices || devices->state != 1 || devices->sub != 1) return;
        const int hovered = devices->hovered();
        if (hovered == -1 || (hovered == last && last != 999)) return;
        if (hovered >= int(p->devices.size())) return;
        const auto saved = list;
        const int line = devices->selection;
        bool ok = false;
        if (line >= 0 && line < int(devices->decide.size()) && devices->decide[std::size_t(line)])
            ok = p->devices[std::size_t(hovered)].type == 3 || can_equip(engine, hovered);
        if (ok) {
            equip_into(engine, list, index, hovered);
            recalc(engine);
            subwnd(engine, "ONPREVIEW");
            for (int& entry : list) {
                if (auto* d = entry >= 0 && entry < int(p->devices.size()) ? &p->devices[std::size_t(entry)] : nullptr)
                    d->flag = false;
                entry = -1;
            }
            for (std::size_t i = 0; i < list.size() && i < saved.size(); ++i) {
                list[i] = saved[i];
                if (saved[i] >= 0 && saved[i] < int(p->devices.size())) p->devices[std::size_t(saved[i])].flag = true;
            }
            recalc(engine);
        } else {
            subwnd(engine, "OFPREVIEW");
        }
        last = hovered;
    }
    // 80381A30: the i_stcursor04 marker of the chosen slot (-1 hides it).
    void marker(Engine& engine, int line) {
        if (line <= -1) {
            if (model_running(engine, "i_stcursor04", 0)) model(engine, "i_stcursor04", 0, "SQ_CLOSE");
            return;
        }
        char sequence[16];
        std::snprintf(sequence, sizeof sequence, "SQ_OPEN%02d", std::clamp(line, 0, 11));
        if (model_running(engine, "i_stcursor04", 0)) {
            model(engine, "i_stcursor04", 0, sequence);
        } else {
            Packet p;
            p.strings.emplace_back(sequence);
            p.vectors.push_back({-83.0f, 148.0f, 0.0f});
            engine.messenger().send(engine, "MODELCTRL", std::string("i_stcursor04"), 0, "GENERATE", p);
        }
    }
    // 80381BBC: the text lines of the page.
    void blank_lines(Engine& engine) {
        text(engine, "STWINFONT29", "font100", " ", "LEFT");
        text(engine, "STWINFONT10", "font000", " ", "LEFT");
        text(engine, "STWINFONT10", "font001", " ", "LEFT");
        for (const char* line : socket_lines) text(engine, "STWINFONT11", line, " ", "LEFT");
        for (const char* line : pocket_lines) text(engine, "STWINFONT11", line, " ", "LEFT");
        text(engine, "STWINFONT29", "font100", " ", "LEFT");
    }
    void add_select(Engine& engine) {
        auto& select = add_child<SelectE1>(engine);
        select.count = count;
        select.position = {-83.0f, 148.0f, 0.0f};
        select.initial = slot;
    }

    // +96 (80383700 / 80385588 / 80387084): the pocket lines.
    void show_pockets(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p) return;
        if (kind == kSub) {
            static const char* rows[5] = {"font200", "font201", "font202", "font203", "font204"};
            for (int i = 0; i < 5; ++i) {
                switch_line(engine, "STWINFONT11", pocket_lines[i], false);
                switch_line(engine, "STWINFONT11", rows[i], false);
            }
            return;
        }
        model(engine, "i_stpocket", 0, pocket_sequence(*p));
        const auto& list = *pockets(*p);
        for (std::size_t i = 0; i < list.size() && i < 5; ++i) {
            const auto* d = p->device(list[i]);
            if (d) textset(engine, "STWINFONT11", pocket_lines[i], column(engine, *d, "NAME"));
            switch_line(engine, "STWINFONT11", pocket_lines[i], d != nullptr);
        }
    }
    // +100 (80382DC0 / 80384CA8 / 80386BDC): the frame opens, or everything
    // of the page closes.
    void frame(Engine& engine, bool open) {
        if (!open) {
            close_models(engine);
            models.clear();
            clear_lines(engine);
            return;
        }
        open_model(engine, "i_stsoubi", 0, kind == kPower ? "SQ_POWEROPEN" : kind == kMain ? "SQ_MAINOPEN" : "SQ_SUBOPEN",
                   "SQ_CLOSE");
        if (kind != kSub)
            if (auto* p = player_slot(engine)) open_model(engine, "i_stpocket", 0, pocket_sequence(*p), "SQ_CLOSE");
    }
    // +104 (803832D4 / 803851BC / 80386D7C): headers and slot names.
    void refresh(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p) return;
        text_script(engine, "txt_status.lua", kind == kPower ? 136 : 137);
        text_script(engine, "txt_status.lua", kind == kPower ? 38 : kind == kMain ? 40 : 42);
        const auto& list = *slots(*p);
        for (std::size_t i = 0; i < list.size() && i < 3; ++i) {
            const auto* d = p->device(list[i]);
            if (d) {
                textset(engine, "STWINFONT11", socket_lines[i], column(engine, *d, "NAME"));
                const Vec3f rgb = device_color(*d);
                textctl(engine, "STWINFONT11", socket_lines[i], "COLOR", &rgb);
            }
            switch_line(engine, "STWINFONT11", socket_lines[i], d != nullptr);
        }
        show_pockets(engine);
        if (sub == 3) describe(engine);
    }
    // +108: to the action menu; socket slots 1 and 2 have no pockets.
    void chose_slot() {
        set_sub(1);
        if (kind == kPower && (selection == 1 || selection == 2)) last = 1;
    }
    // +112: the description of the slot under the cursor.
    void describe(Engine& engine) {
        auto* p = player_slot(engine);
        const auto* d = p && selection != -1 ? p->device(at(*slots(*p), selection)) : nullptr;
        if (d) {
            std::string txt = column(engine, *d, "TXT");
            if (kind != kSub) txt += char('0' + d->rank);
            textset(engine, "STWINFONT29", "font000", txt);
        }
        switch_line(engine, "STWINFONT29", "font000", d != nullptr);
    }
    // +116: change (or reorder) / chips / remove.
    void open_actions(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p) return;
        auto& menu = add_child<SelectE0>(engine);
        const bool lab = parent && parent->mode == 1;
        if (kind == kPower) {
            menu.mode = lab ? 46 : 124;
            menu.count = lab ? 3 : 2;
        } else {
            menu.mode = 120;
            menu.count = 3;
        }
        const auto& list = *slots(*p);
        const auto* first = p->device(at(list, 0));
        const bool chips = kind != kSub && last == 0 && first && first->rank > 0;
        const bool occupied = at(list, slot) >= 0;
        menu.enabled = {1, std::uint8_t(chips ? 1 : 0)};
        if (kind != kPower || lab) menu.enabled.push_back(occupied && (kind != kPower || slot != 0) ? 1 : 0);
        for (std::size_t i = 0; i < menu.enabled.size(); ++i)
            textctl(engine, "STWINFONT29", socket_lines[i], menu.enabled[i] ? "DEFAULTCOLOR" : "CANCELCOLOR");
    }
    // +120: the chosen action.
    void action(Engine& engine) {
        if (selection == 1) {
            set_sub(2);
        } else if (selection == 0) {
            set_sub(kind == kPower && !(parent && parent->mode) ? 3 : 4);
        } else if (selection == 2) {
            selection = -1;
            equip(engine);
            selection = slot;
            refresh(engine);
            recalc(engine);
            subwnd(engine, "UPDATE");
            next_state(0);
            set_sub(1);
        }
    }
    // +124: the device list of the slot's kind.
    void open_list(Engine& engine) {
        auto& list = add_child<DeviceList>(engine);
        list.remember("STWINFONT29", "font100");
        list.text_script(engine, "txt_status.lua", 139);
        list.kinds = {kind == kPower ? 0 : kind == kMain ? 1 : 2};
        list.equip_disable = true;
        list.unknown_disable = true;
        list.auto_close = false;
    }
    // +128: weapons need the species bit in SOUBI and the SKILL level,
    // boards and accessories the player LEVEL.
    bool can_equip(Engine& engine, int device) {
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        const auto* d = p ? p->device(device) : nullptr;
        if (!d || !db) return false;
        const char* table = device_table(*d);
        if (kind == kPower) {
            const int species = p->stats.species;
            const std::uint32_t soubi = std::uint32_t(db->number(table, d->record, "SOUBI").value_or(0));
            const bool fits = species >= 0 && species < 32 && (soubi & (1u << species)) != 0;
            const int skill = db->number(table, d->record, "SKILL").value_or(0);
            const int level = skill >= 0 && skill < int(p->stats.skill.size()) ? p->stats.skill[std::size_t(skill)] : 0;
            return fits && level >= db->number(table, d->record, "SKILLPOINT").value_or(0);
        }
        return p->stats.level >= db->number(table, d->record, "LVPOINT").value_or(0);
    }
    // +132: selection into the chosen slot; a new first device drops the chips.
    void equip(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p) return;
        equip_into(engine, *slots(*p), slot, selection);
        if (kind != kSub && slot == 0) clear_pockets(engine);
        if (kind == kPower && parent && parent->parent) parent->parent->selection = 100;
    }
    // +136: stats preview of the slot (the socket previews the weapon in use).
    void preview_slot(Engine& engine, int arg) {
        auto* p = player_slot(engine);
        if (!p) return;
        auto& list = *slots(*p);
        if (kind == kPower) {
            if (arg != -1) preview(engine, list, 0, arg);
        } else if (kind == kMain && arg == -1) {
            auto& chips = *pockets(*p);
            const auto saved = chips;
            std::fill(chips.begin(), chips.end(), -1);
            preview(engine, list, slot, -1);
            chips = saved;
        } else {
            preview(engine, list, slot, arg);
        }
    }
    // +140: the pocket cursor, one line per rank.
    void open_pockets(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p) return;
        auto& select = add_child<SelectE1>(engine);
        select.remember("STWINFONT29", "font100");
        select.text_script(engine, "txt_status.lua", 138);
        const auto* d = p->device(at(*slots(*p), slot));
        select.count = d ? d->rank : 1;
        select.position = {-83.0f, 55.0f, 0.0f};
        select.initial = pocket;
    }
    // +144: the description of the chip under the cursor.
    void describe_pocket(Engine& engine) {
        auto* p = player_slot(engine);
        const auto* d = p && selection != -1 && pockets(*p) ? p->device(at(*pockets(*p), selection)) : nullptr;
        if (d) textset(engine, "STWINFONT29", "font000", column(engine, *d, "TXT"));
        switch_line(engine, "STWINFONT29", "font000", d != nullptr);
    }
    // +148: change / remove a chip.
    void open_pocket_actions(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p || !pockets(*p)) return;
        auto& menu = add_child<SelectE0>(engine);
        menu.count = 2;
        menu.text(engine, "STWINFONT29", "font000", " ", "LEFT");
        menu.text(engine, "STWINFONT29", "font001", " ", "LEFT");
        const bool occupied = at(*pockets(*p), pocket) >= 0;
        menu.enabled = {1, std::uint8_t(occupied ? 1 : 0)};
        if (!occupied) textctl(engine, "STWINFONT29", "font001", "CANCELCOLOR");
    }
    // +152: the chip list (red chips for weapons, blue ones for boards).
    void open_chip_list(Engine& engine) {
        auto& list = add_child<DeviceList>(engine);
        list.kinds = {3};
        list.ab_type = kind == kPower ? 0 : 1;
        list.equip_disable = true;
        list.unknown_disable = true;
    }
    // +156 / +160 / +164: the pocket counterparts.
    void equip_pocket(Engine& engine) {
        auto* p = player_slot(engine);
        if (p && pockets(*p)) equip_into(engine, *pockets(*p), pocket, selection);
    }
    void clear_pockets(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p || !pockets(*p)) return;
        for (int& entry : *pockets(*p)) {
            if (entry >= 0 && entry < int(p->devices.size())) p->devices[std::size_t(entry)].flag = false;
            entry = -1;
        }
    }
    void preview_pocket(Engine& engine, int arg) {
        auto* p = player_slot(engine);
        if (p && pockets(*p)) preview(engine, *pockets(*p), pocket, arg);
    }

    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        if (!p) {
            next_state(2);
            return;
        }
        if (state == 0) {
            if (sub) {
                marker(engine, -1);
            } else {
                frame(engine, true);
                blank_lines(engine);
            }
            add_select(engine);
            mark();
            next_state(1);
            return;
        }
        switch (sub) {
        case 0: // the slot cursor
            if (step == 0) {
                refresh(engine);
                set_step(1);
            }
            if (step == 1) {
                describe(engine);
                set_step(2);
                last = selection;
            }
            if (!child_done()) {
                if (selection != -1 && last != selection) set_step(1);
            } else if (selection == -1) {
                next_state(2);
            } else {
                chose_slot();
                slot = selection;
                marker(engine, selection);
            }
            break;
        case 1: // the action menu
            if (step == 0) {
                open_actions(engine);
                mark();
                set_step(1);
            }
            if (!child_done()) {
                if (selection != -1 && last != selection) {
                    if (selection == 2) preview_slot(engine, -1);
                    else subwnd(engine, "OFPREVIEW");
                    last = selection;
                }
            } else {
                subwnd(engine, "OFPREVIEW");
                for (const char* line : socket_lines) textctl(engine, "STWINFONT29", line, "DEFAULTCOLOR");
                if (selection == -1) {
                    next_state(0);
                    set_sub(1);
                } else {
                    action(engine);
                }
            }
            break;
        case 2: // the chip pockets
            pockets_step(engine);
            break;
        case 3: // reorder the socket
            reorder_step(engine);
            break;
        case 4: // change the device
            change_step(engine);
            break;
        }
    }
    void pockets_step(Engine& engine) {
        switch (step) {
        case 0:
            open_pockets(engine);
            mark();
            set_step(1);
            break;
        case 1:
            describe_pocket(engine);
            set_step(2);
            last = selection;
            [[fallthrough]];
        case 2:
            if (!child_done()) {
                if (selection != -1 && selection != last) set_step(1);
            } else if (selection == -1) {
                pocket = 0;
                next_state(0);
                set_sub(1);
            } else {
                pocket = selection;
                set_step(3);
            }
            break;
        case 3:
            open_pocket_actions(engine);
            mark();
            set_step(4);
            break;
        case 4:
            text_script(engine, "txt_status.lua", 127);
            set_step(5);
            last = 999;
            break;
        case 5:
            if (!child_done()) {
                if (selection != -1 && last != selection) {
                    if (selection == 1) preview_pocket(engine, -1);
                    else subwnd(engine, "OFPREVIEW");
                    last = selection;
                }
                break;
            }
            subwnd(engine, "OFPREVIEW");
            textctl(engine, "STWINFONT29", "font001", "DEFAULTCOLOR");
            if (selection == 1) {
                selection = -1;
                equip_pocket(engine);
                selection = pocket;
                refresh(engine);
                recalc(engine);
                subwnd(engine, "UPDATE");
            } else if (selection == 0) {
                set_step(6);
                frame(engine, false);
                marker(engine, -1);
                break;
            }
            set_sub(2);
            break;
        case 6:
            open_chip_list(engine);
            mark();
            set_step(7);
            last = 999;
            [[fallthrough]];
        case 7:
            if (!child_done()) {
                preview_pocket(engine, 0);
                break;
            }
            subwnd(engine, "OFPREVIEW");
            if (selection != -1) {
                equip_pocket(engine);
                recalc(engine);
                subwnd(engine, "UPDATE");
            }
            frame(engine, true);
            blank_lines(engine);
            set_step(8);
            break;
        case 8:
            refresh(engine);
            marker(engine, slot);
            set_sub(2);
            break;
        }
    }
    void reorder_step(Engine& engine) {
        auto* p = player_slot(engine);
        auto& list = *slots(*p);
        switch (step) {
        case 0: {
            // The chosen slot cannot be picked; the weapon in use swaps only
            // with a weapon and an empty slot cannot become the weapon in use.
            auto& select = add_child<SelectE1>(engine);
            select.count = count;
            select.position = {-83.0f, 148.0f, 0.0f};
            select.initial = slot;
            for (int k = 0; k < count; ++k) select.enabled.push_back(k == slot ? 0 : 1);
            if (slot) {
                if (at(list, slot) < 0 && !select.enabled.empty()) select.enabled[0] = 0;
            } else {
                if (at(list, 1) < 0 && select.enabled.size() >= 2) select.enabled[1] = 0;
                if (at(list, 2) < 0 && select.enabled.size() > 2) select.enabled[2] = 0;
            }
            mark();
            set_step(1);
            break;
        }
        case 1:
            refresh(engine);
            set_step(2);
            last = selection;
            [[fallthrough]];
        case 2:
            if (!child_done()) {
                if (selection != -1 && last != selection) {
                    preview_swap(engine);
                    set_step(1);
                }
                break;
            }
            switch_line(engine, "STWINFONT29", "font000", false);
            subwnd(engine, "OFPREVIEW");
            if (selection >= 0 && selection <= 2) {
                const int a = at(list, selection), b = at(list, slot);
                if (selection < int(list.size())) list[std::size_t(selection)] = b;
                if (slot >= 0 && slot < int(list.size())) list[std::size_t(slot)] = a;
                const bool first = selection == 0 || slot == 0;
                if (first) clear_pockets(engine);
                recalc(engine);
                subwnd(engine, "UPDATE");
                if (first && !(parent && parent->mode)) {
                    Packet arms;
                    engine.messenger().send(engine, "PLAYERARMS", p->stats.weapon_model, player, "GENERATE", arms);
                }
                show_pockets(engine);
            }
            subwnd(engine, "UPDATE");
            next_state(0);
            set_sub(1);
            break;
        }
    }
    // 803816B0: preview the swap of the chosen slot with the cursor line.
    void preview_swap(Engine& engine) {
        auto* p = player_slot(engine);
        auto& list = *slots(*p);
        const int a = at(list, slot), b = at(list, selection);
        if (slot != selection && ((slot && selection) || (a >= 0 && b >= 0))) {
            auto swap = [&](int first, int second) {
                if (slot >= 0 && slot < int(list.size())) list[std::size_t(slot)] = first;
                if (selection >= 0 && selection < int(list.size())) list[std::size_t(selection)] = second;
            };
            swap(b, a);
            recalc(engine);
            subwnd(engine, "ONPREVIEW");
            swap(a, b);
            recalc(engine);
        } else {
            subwnd(engine, "OFPREVIEW");
        }
    }
    void change_step(Engine& engine) {
        switch (step) {
        case 0:
            frame(engine, false);
            marker(engine, -1);
            open_list(engine);
            mark();
            set_step(1);
            last = 999;
            [[fallthrough]];
        case 1: {
            auto* list = find_child<DeviceList>();
            if (!list) {
                set_step(10);
                break;
            }
            if (selection == -1) {
                subwnd(engine, "OFPREVIEW");
                list->next_state(2);
                set_step(10);
            } else if (list->state == 1 && list->sub == 2) {
                subwnd(engine, "OFPREVIEW");
                if (can_equip(engine, selection)) {
                    set_step(9);
                    list->next_state(2);
                } else {
                    set_step(12);
                }
            } else {
                preview_slot(engine, 0);
            }
            break;
        }
        case 9:
            equip(engine);
            recalc(engine);
            subwnd(engine, "UPDATE");
            set_step(10);
            break;
        case 10:
            if (child_done()) {
                frame(engine, true);
                blank_lines(engine);
                set_step(11);
            }
            break;
        case 11:
            refresh(engine);
            next_state(0);
            set_sub(1);
            break;
        case 12: {
            // "Cannot equip" until A; then the list gets its cursor back.
            add_child<ShopSelect00>(engine);
            text_script(engine, "txt_status.lua", 130);
            mark();
            const int chosen = selection;
            set_step(13);
            last = chosen;
            break;
        }
        case 13:
            if (!child_done()) break;
            switch_line(engine, "STWINFONT29", "font000", false);
            if (auto* list = find_child<DeviceList>()) {
                selection = last;
                mark();
                list->step = 1;
                list->last = 0;
                set_step(1);
            }
            break;
        }
    }
};

// beNDMWStatusCtrlEquip (8037F7CC): the title, the slot menu with its
// descriptions and one slot window at a time.
struct CtrlEquip : Window {
    int last_menu{}; // +84
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            if (mode == 1) add_child<Title02>(engine);
            else add_child<Title04>(engine).mode = 30;
            last_menu = 0;
            next_state(1);
            return;
        }
        if (sub == 0) {
            if (step == 0) {
                if (mode == 1 ? model_in(engine, "i_messwn", 0, "SQ_STOPEN") : model_in(engine, "i_sttitle", 0, "SQ_OPEN"))
                    return;
                auto& menu = add_child<Select01>(engine);
                menu.mode = 31;
                menu.initial = last_menu;
                menu.text(engine, "STWINFONT29", "font000", " ", "LEFT");
                mark();
                set_step(1);
            }
            if (step == 1) {
                if (selection != -1) text_script(engine, "txt_status.lua", selection + 35);
                set_step(2);
                last = selection;
            }
            if (!child_done()) {
                if (selection != -1 && last != selection) set_step(1);
                return;
            }
            if (selection != -1) last_menu = selection;
            if (selection >= 0 && selection <= 2) set_sub(selection + 1);
            else next_state(2);
            return;
        }
        if (step == 0) {
            auto& window = add_child<SlotWindow>(engine);
            window.kind = sub == 1 ? SlotWindow::kPower : sub == 2 ? SlotWindow::kMain : SlotWindow::kSub;
            if (sub == 1) window.count = 3;
            mark();
            set_step(1);
        }
        if (child_done()) {
            selection = last_menu;
            set_sub(0);
        }
    }
};

// Placeholder for the folder / skill pages until their windows
// (beNDMWStatusCtrlFolder / CtrlSkill) are reconstructed: B returns to the
// menu.
struct PendingPage : Window {
    const char* name = "";
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            engine.note(std::string("SUBWND page ") + name);
            next_state(1);
        }
        const auto& pad = engine.pads()[std::size_t(std::clamp(player, 0, 3))];
        if (pad.buttons.trigger & (pad_button::b | pad_button::a)) next_state(2);
    }
};

// beNDMWStatusCtrl00 (8037E9F8): the STATUS program.
struct StatusCtrl00 : Window {
    int last_menu{}; // +84
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            if (sub == 0) {
                add_child<Title00>(engine);
                ++sub;
            } else if (sub == 1) {
                if (model_in(engine, "i_stback", 0, "SQ_OPEN")) return;
                layers(engine, false);
                add_child<Title01>(engine).mode = mode;
                add_child<Title02>(engine);
                add_child<Title03>(engine);
                last_menu = 0;
                ++sub;
            } else if (!model_in(engine, "i_messwn", 0, "SQ_STOPEN")) {
                next_state(1);
            }
            return;
        }
        auto page = [&](const char* name) {
            if (step == 0) {
                auto& child = add_child<PendingPage>(engine);
                child.name = name;
                child_mark = children.size();
                ++step;
            } else if (children.size() != child_mark) {
                sub = step = 0;
            }
        };
        switch (sub) {
        case 0:
            if (step == 0) {
                auto& select = add_child<Select00>(engine);
                select.initial = last_menu;
                child_mark = children.size();
                step = 2;
            } else if (children.size() != child_mark) {
                if (selection != -1) last_menu = selection;
                sub = selection == 0 ? 1 : selection == 1 ? 2 : selection == 2 ? 3 : 4;
                step = 0;
            }
            break;
        case 1:
            if (step == 0) {
                add_child<CtrlEquip>(engine);
                mark();
                ++step;
            } else if (child_done()) {
                sub = step = 0;
            }
            break;
        case 2: page("folder"); break;
        case 3: page("skill"); break;
        case 4:
            if (step == 0) {
                for (std::size_t i = 1; i < children.size(); ++i)
                    if (children[i]->state < 2) children[i]->next_state(2);
                ++step;
            } else if (!model_running(engine, "i_messwn", 0)) {
                layers(engine, true);
                next_state(2);
            }
            break;
        }
    }
};
} // namespace

// ===========================================================================
// SUBWND manager (beNDMWStatus, vtable 804E80EC)
// ===========================================================================
SubWindowManager::SubWindowManager() : Manager("beNDMWStatus") {}
SubWindowManager::~SubWindowManager() = default;

void SubWindowManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<Ram>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = true;
        engine.messenger().add_row("SUBWND", std::nullopt, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

void SubWindowManager::clear_stage(Engine&) {
    rams_.clear();
    window_.reset();
    running_ = false;
}

bool SubWindowManager::command(Engine& engine, Ram&, std::string_view command, Packet& packet) {
    auto* sys = engine.get<GameSystem>();
    auto& messenger = engine.messenger();
    auto freeze = [&](const char* how) {
        for (const char* category : {"MODELCTRL", "GENERATER", "SOUND"}) {
            Packet p;
            messenger.send(engine, category, std::nullopt, -1, how, p);
        }
    };
    if (command == "GENERATE") {
        if (running_) return true;
        static const char* programs[] = {"STATUS", "XDATA", "LABO", "BANK", "CNOS", "DISK", "WPU", "APU", "CHIP", "JUNK", "CARD"};
        program_ = 0;
        const auto name = packet.string(0);
        for (int i = 0; i < 11; ++i)
            if (name == programs[i]) program_ = i;
        player_ = packet.integer(0, 0);
        running_ = true;
        state_ = 0;
        freeze("ONFREEZE");
        return true;
    }
    if (command == "REMOVE") {
        if (running_) {
            if (window_) window_->next_state(2);
            else running_ = false;
        }
        return true;
    }
    if (command == "ISRUN") {
        packet.bools.push_back(running_ ? 1 : 0);
        return true;
    }
    if (!sys) return true;
    const int slot = std::clamp(packet.integer(0, player_), 0, 3);
    auto& p = sys->players[std::size_t(slot)];
    const auto& s = p.stats;
    auto set = [&](const char* layer, const char* line, const std::string& value) {
        Packet t;
        t.strings = {layer, line, value};
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "TEXTSET", t);
    };
    auto color = [&](const char* line, const Vec3f* rgb) {
        Packet c;
        c.strings = {"STWINFONT06", line};
        if (rgb) c.vectors.push_back(*rgb);
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, rgb ? "COLOR" : "DEFAULTCOLOR", c);
    };
    static const char* stat_lines[] = {"font010", "font011", "font040", "font041", "font042", "font043",
                                       "font044", "font045", "font046", "font047", "font048"};
    auto current = [&]() {
        return std::array<int, 11>{p.max_hp, p.max_mp, s.tsuyosa, s.mamori, s.kashikosa, s.seishin, s.subayasa,
                                   s.resist[0], s.resist[1], s.resist[2], s.resist[3]};
    };
    if (command == "UPDATE") {
        // 8034FCD4: the status texts; the shown values are kept for PREVIEW.
        auto* db = engine.get<DatabaseManager>();
        set("STWINFONT02", "font000", p.name);
        set("STWINFONT04", "font000", std::to_string(std::min(s.level, 999)));
        set("STWINFONT04", "font001", db ? db->text("shinka", s.species, "NAME") : std::string());
        set("STWINFONT06", "font000", std::to_string(p.hp));
        set("STWINFONT06", "font001", std::to_string(p.mp));
        const auto values = current();
        for (std::size_t i = 0; i < 11; ++i) set("STWINFONT06", stat_lines[i], std::to_string(values[i]));
        set("STWINFONT06", "font049", std::to_string(p.xdata));
        set("STWINFONT06", "font070", std::to_string(p.bits));
        const int next = s.next_exp - s.exp;
        set("STWINFONT06", "font071", s.level >= 999 ? "----" : next >= 0 ? std::to_string(next) : "0");
        shown_ = values;
        shown_hp_ = p.hp;
        shown_mp_ = p.mp;
        return true;
    }
    if (command == "ONPREVIEW") {
        // 8034FEE8: cyan above, orange below the kept values.
        static const Vec3f up{0.0f, 0.96f, 1.0f}, down{0.94f, 0.4f, 0.25f}, same{0.9f, 0.9f, 0.9f};
        const auto values = current();
        for (std::size_t i = 0; i < 11; ++i) {
            set("STWINFONT06", stat_lines[i], std::to_string(values[i]));
            color(stat_lines[i], values[i] == shown_[i] ? &same : values[i] > shown_[i] ? &up : &down);
        }
        return true;
    }
    if (command == "OFPREVIEW") {
        for (std::size_t i = 0; i < 11; ++i) {
            set("STWINFONT06", stat_lines[i], std::to_string(shown_[i]));
            color(stat_lines[i], nullptr);
        }
        p.hp = shown_hp_;
        p.mp = shown_mp_;
        return true;
    }
    if (command == "GETSHOPDATA") {
        engine.note("SUBWND GETSHOPDATA");
        return true;
    }
    return false;
}

// 8034F504: create the program window, run it, restore the stage afterwards.
void SubWindowManager::update(Engine& engine) {
    if (!running_) return;
    auto& messenger = engine.messenger();
    if (state_ == 0) {
        Packet pad;
        pad.strings.emplace_back("ENABLE");
        messenger.send(engine, "STAGECTL", std::nullopt, -1, "PADINPUT", pad);
        if (program_ == 0) {
            for (const char* layer : {"WN01", "WN00"}) {
                Packet p;
                p.strings = {layer, "DISPOFF"};
                messenger.send(engine, "LAYERCTL", std::nullopt, -1, "SWITCH", p);
            }
            auto window = std::make_unique<StatusCtrl00>();
            window->manager = this;
            window->player = player_;
            window_ = std::move(window);
        } else {
            engine.note("SUBWND program " + std::to_string(program_) + " (shops, labo, card)");
            window_.reset();
        }
        ++state_;
    }
    if (window_) window_->run(engine);
    if (window_ && window_->running) return;
    window_.reset();
    for (const char* category : {"SOUND", "GENERATER", "MODELCTRL"}) {
        Packet p;
        messenger.send(engine, category, std::nullopt, -1, "OFFREEZE", p);
    }
    running_ = false;
    if (auto* sys = engine.get<GameSystem>())
        for (int slot = 0; slot < 4; ++slot)
            if (sys->players[std::size_t(slot)].joined) {
                sys->change_hp(engine, slot, 0);
                sys->change_mp(engine, slot, 0);
            }
    for (const char* layer : {"WN01", "WN00"}) {
        Packet p;
        p.strings = {layer, "DISPON"};
        messenger.send(engine, "LAYERCTL", std::nullopt, -1, "SWITCH", p);
    }
    if (program_ != 0) engine.run_script("shopexit.lua");
}

} // namespace dw4::engine
