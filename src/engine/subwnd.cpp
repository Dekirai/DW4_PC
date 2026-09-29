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
    int selection = -1;        // +48, written by a child select
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
        sub = step = 0;
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

// Placeholder for the equipment / folder / skill pages until their windows
// (beNDMWStatusCtrlEquip / CtrlFolder / CtrlSkill) are reconstructed: B
// returns to the menu.
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
                selection = -1;
                step = 2;
            } else if (children.size() != child_mark) {
                if (selection != -1) last_menu = selection;
                sub = selection == 0 ? 1 : selection == 1 ? 2 : selection == 2 ? 3 : 4;
                step = 0;
            }
            break;
        case 1: page("equipment"); break;
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
