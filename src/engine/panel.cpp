#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace dw4::engine {
namespace {
constexpr std::int64_t kFireNg = 266'666'666; // 0.2667 s "FIRE" / "EMGHEAL" timers

// The named RAM timers (8031DE44 start, 8031E238 check: a missing timer counts
// as run out, otherwise end < the 2^18 ns quantised clock since the start).
bool timer_out(const std::optional<NodeTimer>& timer, std::int64_t world) { return !timer || timer_expired(*timer, world); }
void timer_restart(std::optional<NodeTimer>& timer, std::int64_t world, std::int64_t end) {
    if (!timer) timer.emplace();
    timer_start(*timer, world, end);
}

// 804E70A4: open (4+, 3, 1-2 lines) and close sequences per panel kind.
const char* const kBaseSequences[3][4] = {{"SQ_OPENWZ03", "SQ_OPENWZ02", "SQ_OPENWZ01", "SQ_CLOSEWZ"},
                                          {"SQ_OPENIT03", "SQ_OPENIT02", "SQ_OPENIT01", "SQ_CLOSEIT"},
                                          {"SQ_OPENST03", "SQ_OPENST02", "SQ_OPENST01", "SQ_CLOSEST"}};
const char* const kCursorSequences[4] = {"SQ_LINESL00", "SQ_LINESL01", "SQ_LINESL02", "SQ_LINESL03"};
const char* const kFonts[3] = {"WZSELFONT", "WZMPFONT", "WZXFONT"};
// 80536860: line colours (normal, short of MP, blocked, status panel).
const Vec3f kLineColors[4] = {{0.9f, 0.9f, 0.9f}, {0.5f, 0.5f, 0.7f}, {1.0f, 0.68f, 1.0f}, {0.0f, 0.96f, 1.0f}};

void play_sound(Engine& engine, const char* name) {
    Packet sound;
    sound.strings.emplace_back(name);
    engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
}
} // namespace

void PanelManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<PanelRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = true;
        for (int kind = 0; kind < 3; ++kind) ram->lists[std::size_t(kind)].kind = kind;
        engine.messenger().add_row("NDMWPANEL", std::nullopt, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

void PanelManager::clear_stage(Engine&) { rams_.clear(); }

// 8034AE34: (re)open the panel of a kind: the base model is generated again
// on the next update and the cursor hidden.
void PanelManager::open(PanelRam& ram, int kind) {
    ram.running = true;
    ram.reopen = true;
    ram.cursor_shown = false;
    ram.state = ram.fire = ram.counter = 0;
    ram.kind = kind;
    ram.decided = false;
}

bool PanelManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<PanelRam&>(base);
    static const char* const names[] = {"GENERATEITEM", "GENERATEWAZA", "GENERATESTAT", "GENEEMGHEAL", "ISCHECK", "REMOVE",
                                        "NOGATEDISK", "NOHEALING", "NOPARAUP", "NOCURELING", "ISRUN"};
    int id = -1;
    for (int i = 0; i < 11; ++i)
        if (command == names[i]) id = i;
    if (id < 0) return false;
    // Opening waits while the status window (SUBWND) runs: 8034AF60 asks
    // SUBWND RAM 0 (0x8034B284..0x8034B28C: no name, index 0); a stage
    // without the status window (stag0001, stag0203) gives no answer, which
    // counts as not running (0x8034B294).
    if (id < 4) {
        Packet run;
        engine.messenger().send(engine, "SUBWND", std::nullopt, 0, "ISRUN", run);
        if (!run.bools.empty() && run.bools[0]) return true;
    }
    switch (id) {
    case 0: case 1: case 2:
        open(ram, id == 0 ? 1 : id == 1 ? 0 : 2);
        for (auto& list : ram.lists) list.fire_request = false;
        packet.bools.push_back(1);
        return true;
    case 3: {
        // Emergency heal: the best HP disk, else the last affordable healing
        // technique, preselected and fired at once; otherwise CURSOR_NG.
        if (emergency_item(engine, ram)) open(ram, 1);
        else if (emergency_waza(engine, ram)) open(ram, 0);
        else if (timer_out(ram.emgheal_timer, engine.time())) {
            play_sound(engine, "CURSOR_NG");
            timer_restart(ram.emgheal_timer, engine.time(), kFireNg);
        }
        packet.bools.push_back(1);
        return true;
    }
    case 4:
        packet.bools.push_back(ram.running ? 1 : 0);
        if (ram.kind == 1) {
            packet.bools.push_back(1);
            packet.bools.push_back(ram.decided ? 1 : 0);
        } else if (ram.kind == 2) {
            packet.bools.push_back(0);
            packet.bools.push_back(0);
        } else {
            packet.bools.push_back(0);
            packet.bools.push_back(ram.decided ? 1 : 0);
            packet.strings.emplace_back(ram.label);
        }
        ram.decided = false;
        return true;
    case 5:
        if (ram.running) ram.state = 2, ram.fire = ram.counter = 0, ram.decided = false;
        return true;
    case 6: ram.no_gate_disk = packet.integer(0, 0) == 1; return true;
    case 7: ram.no_healing = packet.integer(0, 0) == 1; return true;
    case 8: ram.no_para_up = packet.integer(0, 0) == 1; return true;
    case 9: ram.no_cureling = packet.integer(0, 0) == 1; return true;
    case 10: packet.bools.push_back(ram.running ? 1 : 0); return true;
    }
    return false;
}

// 8034ABA0: the highest HP disk (records 0..2 and 9) the player holds.
bool PanelManager::emergency_item(Engine& engine, PanelRam& ram) {
    if (ram.no_healing) return false;
    auto* sys = engine.get<GameSystem>();
    if (!sys) return false;
    const auto& disks = sys->players[std::size_t(ram.index)].disks;
    int best = -1, position = -1, held = 0;
    for (std::size_t slot = 0; slot + 2 < disks.size(); ++slot) {
        const int record = disks[slot].record;
        if (disks[slot].count <= 0) continue;
        if ((record <= 2 || record == 9) && record > best) {
            best = record;
            position = held;
        }
        ++held;
    }
    if (best < 0) return false;
    auto& list = ram.lists[1];
    display(engine, ram, list);
    list.cursor = std::min(position, 3);
    list.scroll = position - list.cursor;
    list.fire_request = true;
    return true;
}

// 8034ACE4: the last healing technique (pcTech 40..49) the MP covers.
bool PanelManager::emergency_waza(Engine& engine, PanelRam& ram) {
    if (ram.no_healing) return false;
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    if (!sys || !db) return false;
    const auto& player = sys->players[std::size_t(ram.index)];
    int position = -1;
    for (int i = int(player.wazalist.size()) - 1; i >= 0; --i) {
        const int row = player.wazalist[std::size_t(i)];
        if (row >= 40 && row <= 49 && player.mp >= db->number("pcTech", row, "MP").value_or(0)) {
            position = i;
            break;
        }
    }
    if (position < 0) return false;
    auto& list = ram.lists[0];
    display(engine, ram, list);
    list.cursor = std::min(position, 3);
    list.scroll = position - list.cursor;
    list.fire_request = true;
    return true;
}

// 80348658: one text line (font kind, line) with its colour; cached so only
// changes reach TEXTCTL.
void PanelManager::set_line(Engine& engine, PanelRam& ram, int font, int line, const std::string* text, int color) {
    auto& messenger = engine.messenger();
    const int key = line + 16 * font;
    auto packet = [&](std::string argument) {
        char name[8];
        std::snprintf(name, sizeof(name), "font%02d", std::clamp(line, 0, 15));
        Packet p;
        p.strings.emplace_back(kFonts[font]);
        p.strings.emplace_back(name);
        if (!argument.empty()) p.strings.emplace_back(std::move(argument));
        return p;
    };
    auto& cached = ram.line_text[std::size_t(key)];
    auto& cached_color = ram.line_color[std::size_t(key)];
    if (!text) {
        cached.reset();
        cached_color = -1;
        Packet off = packet("CLR");
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", off);
        return;
    }
    if (!cached || *cached != *text) {
        cached = *text;
        Packet set = packet(*text);
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "TEXTSET", set);
        if (font == 1) {
            Packet align = packet("CENTER");
            messenger.send(engine, "TEXTCTL", std::nullopt, -1, "ALIGN", align);
        }
        Packet on = packet("SET");
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", on);
    }
    if (color != cached_color) {
        cached_color = color;
        Packet tint = packet({});
        tint.vectors.push_back(kLineColors[std::clamp(color, 0, 3)]);
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "COLOR", tint);
    }
}

void PanelManager::model(Engine& engine, PanelRam& ram, const char* name, const char* command, bool place, const char* sequence) {
    Packet p;
    // 80348B5C: the panel column of the player.
    if (place) p.vectors.push_back({150.0f * float(ram.index) - 260.0f, -160.0f, 0.0f});
    if (sequence) p.strings.emplace_back(sequence);
    engine.messenger().send(engine, "MODELCTRL", name, ram.index, command, p);
}

// 80348FD0 (techniques), 80349684 (disks), 80349D80 (status): the list
// rows, their colours and the four visible lines.
void PanelManager::display(Engine& engine, PanelRam& ram, List& list) {
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    if (!sys || !db) return;
    const auto& player = sys->players[std::size_t(ram.index)];
    std::vector<int> rows;
    std::vector<std::string> names, counts;
    std::vector<int> colors;
    if (list.kind == 0) {
        for (const int row : player.wazalist) {
            rows.push_back(row);
            names.push_back(db->text("pcTech", row, "NAME"));
            int color = player.mp < db->number("pcTech", row, "MP").value_or(0) ? 1 : 0;
            if (ram.no_healing && row >= 40 && row <= 49) color = 2;
            if (ram.no_para_up && ((row >= 65 && row <= 69) || (row >= 75 && row <= 84))) color = 2;
            if (ram.no_cureling && row >= 50 && row <= 64) color = 2;
            colors.push_back(color);
        }
    } else if (list.kind == 1) {
        // 80349684: the first 10 DISKFOLDER slots in folder order.
        for (std::size_t slot = 0; slot + 2 < player.disks.size(); ++slot) {
            const int record = player.disks[slot].record;
            if (player.disks[slot].count <= 0) continue;
            rows.push_back(record);
            names.push_back(db->text("conItem", record, "NAME"));
            char count[16];
            std::snprintf(count, sizeof(count), "x%d", player.disks[slot].count);
            counts.emplace_back(count);
            int color = 0;
            if (ram.no_gate_disk && record == 8) color = 2;
            if (ram.no_healing && (record <= 5 || record == 9)) color = 2;
            if (ram.no_cureling && record >= 6 && record <= 7) color = 2;
            colors.push_back(color);
        }
    } else if (list.kind == 2) {
        // 80349D80: the player's active status effects (status timers +F8,
        // slots 0..9) with their wazaPanel names and colours.
        auto* models = engine.get<ModelCtrlManager>();
        const auto* model = models ? models->player(ram.index) : nullptr;
        if (model && model->status)
            for (int k = 0; k < 10; ++k)
                if (model->status->timer[std::size_t(k)] != 0) {
                    rows.push_back(k);
                    names.push_back(db->text("wazaPanel", k, "NAME"));
                    colors.push_back(db->number("wazaPanel", k, "COLOR").value_or(0));
                }
    }
    if (int(rows.size()) != list.count) {
        list.cursor = list.scroll = 0;
        list.count = int(rows.size());
    }
    list.rows = rows;
    list.colors = colors;
    for (int i = 0; i < 4; ++i) {
        const int line = i + 4 * ram.index;
        const int at = i + list.scroll - list.line_mode;
        const bool empty = list.line_mode > i || at >= list.count || at < 0;
        if (empty) set_line(engine, ram, 0, line, nullptr, 0);
        else set_line(engine, ram, 0, line, &names[std::size_t(at)], colors[std::size_t(at)]);
        if (list.kind == 1 && !empty) set_line(engine, ram, 2, line, &counts[std::size_t(at)], colors[std::size_t(at)]);
        else set_line(engine, ram, 2, line, nullptr, 0);
        if (i == 0) {
            const int selected = list.cursor + list.scroll;
            if (list.kind == 0 && selected < list.count) {
                const std::string mp = std::to_string(db->number("pcTech", rows[std::size_t(selected)], "MP").value_or(0));
                set_line(engine, ram, 1, ram.index, &mp, 0);
            } else {
                set_line(engine, ram, 1, ram.index, nullptr, 0);
            }
        }
    }
}

// 80349474 / 80349B0C: use the selected technique or disk. Returns the
// cooldown: > 0 used, 0 refused (CURSOR_NG), < 0 used and the panel closes.
std::optional<int> PanelManager::disk_under_cursor(int slot) const {
    for (const auto& ram : rams_) {
        if (ram->index != slot || !ram->running || ram->kind != 1) continue;
        const auto& list = ram->lists[1];
        const int at = list.cursor + list.scroll;
        if (at >= 0 && at < list.count && std::size_t(at) < list.rows.size()) return list.rows[std::size_t(at)];
    }
    return std::nullopt;
}

std::int64_t PanelManager::select(Engine& engine, PanelRam& ram, List& list) {
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    const int at = list.cursor + list.scroll;
    if (!sys || !db || at >= list.count || list.colors[std::size_t(at)] != 0) {
        play_sound(engine, "CURSOR_NG");
        return 0;
    }
    auto& player = sys->players[std::size_t(ram.index)];
    const int row = list.rows[std::size_t(at)];
    if (list.kind == 0) {
        const int cost = db->number("pcTech", row, "MP").value_or(0);
        if (player.mp < cost) {
            play_sound(engine, "CURSOR_NG");
            return 0;
        }
        ram.label = db->text("pcTech", row, "LABEL");
        // 80349584..8034961C: the MP drops at once; the HUD meter (whole
        // percent, 100 * MP / MMP) and number are posted with 1 frame.
        if (!engine.cheats().infinite_mp) {
            player.mp -= cost;
            Packet percent, value;
            percent.floats.push_back(float(100 * player.mp / std::max(player.max_mp, 1)));
            value.ints.push_back(player.mp);
            engine.messenger().post(engine.time() + kFrameNs, "METERCTRL", "mpmt", ram.index, "SETPERCENT", std::move(percent));
            engine.messenger().post(engine.time() + kFrameNs, "NUMBERCTRL", "mpnm", ram.index, "SETVALUE", std::move(value));
        }
        return std::int64_t(db->number("pcTech", row, "DELAY").value_or(0)) * 1'000'000'000 / 60;
    }
    auto& count = player.disk_count(row);
    if (count <= 0) {
        play_sound(engine, "CURSOR_NG");
        return 0;
    }
    // 803B111C: 0..2 HP, 3..5 MP, 9 both, 6 cure, 7 para up, 8 gate disk.
    bool close = false;
    const int hp = db->number("conItem", row, "HP").value_or(0);
    const int mp = db->number("conItem", row, "MP").value_or(0);
    if (row <= 2 || row == 9) sys->change_hp(engine, ram.index, hp);
    if ((row >= 3 && row <= 5) || row == 9) sys->change_mp(engine, ram.index, mp);
    if (row == 6) {
        // 803B1288: a cure disk ends the poison when 8 - its strength
        // (gedoku, +104) reaches a roll of ten.
        if (auto* models = engine.get<ModelCtrlManager>())
            if (auto* model = models->find(player.digimon, ram.index); model && model->status && model->status->timer[0] != 0 &&
                8 - model->status->gedoku >= int(fight::random() % 10u))
                models->clear_poison(*model);
    }
    // 803B1328: the raise disk, P2PKOUKAMAGIC kind -50.
    if (row == 7) fight::support_magic(engine, ram.index, ram.index, -50);
    if (row == 8) {
        // 803B1390: 802EBCFC("Player", index) sets GAMERAM GETCOMMON
        // GATESTAGENAME / GATESTAGENO before gatedisc.lua; 1 closes the panel.
        sys->gate_stage_name = "Player";
        sys->gate_stage_no = ram.index;
        const Engine::TriggerScope trigger(engine, ram.index);
        engine.run_script("gatedisc.lua");
        close = true;
    }
    --count;
    // 80349BC4..80349D1C: records 0..5, 7 and 9 heal, 6 cures, 8 and the rest
    // show nothing. The effect is posted (0x80349C88 / 0x80349D1C): sent at
    // once it would be lost to the SQ_ITEM the player selects in this frame.
    if ((row >= 0 && row <= 7) || row == 9)
        post_effect(engine, "PLAYER", std::nullopt, ram.index, row == 6 ? "x_250curehit00" : "x_250healhit00",
                    row == 6 ? "SQ_CUREEFONLY" : "SQ_HEALEFONLY");
    return close ? -1 : 1'000'000'000;
}

// 8034A210: panel frame.
void PanelManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.running) continue;
        auto& list = ram.lists[std::size_t(ram.kind)];
        const auto& pad = engine.pads()[std::size_t(std::clamp(ram.index, 0, 3))];
        const auto trigger = pad.buttons.trigger;
        const auto press = pad.buttons.trigger | pad.buttons.repeat;
        // 8034A24C..8034A294: the cursor takes the C-stick (bePadData +30 /
        // +34, the right stick's trigger and repeat masks) and the D-pad
        // (+48 / +4C, buttons 0x10 / 0x40), never the main stick, so the
        // player walks with a panel open without moving its cursor.
        const auto c_stick = pad.right_directions.trigger | pad.right_directions.repeat;
        const bool up = (c_stick & pad_direction::positive_y) || (press & pad_button::up);
        const bool down = (c_stick & pad_direction::negative_y) || (press & pad_button::down);
        int previous_count = list.count;
        int previous_line = list.cursor + list.line_mode;
        if (ram.state == 2) {
            play_sound(engine, "WINDOW_CLOSE");
            model(engine, ram, "i_wzslbase00", kBaseSequences[list.kind][3], false, nullptr);
            for (int i = 0; i < 4; ++i) {
                set_line(engine, ram, 0, i + 4 * ram.index, nullptr, 0);
                set_line(engine, ram, 2, i + 4 * ram.index, nullptr, 0);
            }
            set_line(engine, ram, 1, ram.index, nullptr, 0);
            if (ram.cursor_shown) model(engine, ram, "i_wzslcursel", "SQ_CLOSE", false, nullptr);
            ram.cursor_shown = false;
            ram.state = 3;
            ram.fire = ram.counter = 0;
            continue;
        }
        if (ram.state == 3) {
            ram.running = false;
            continue;
        }
        if (ram.state == 0) {
            ram.line_text.fill(std::nullopt);
            ram.line_color.fill(-1);
            previous_count = previous_line = -1;
            ram.state = 1;
            ram.fire = ram.counter = 0;
        }
        if (ram.fire == 1) {
            if (timer_out(ram.fire_timer, engine.time())) ram.fire = ram.counter = 0;
        } else if (timer_out(ram.fire_timer, engine.time())) {
            const bool press_a = (trigger & pad_button::a) && list.count > 0 && ram.kind != 2;
            if (list.fire_request || press_a) {
                if (ram.counter >= 2) {
                    const std::int64_t delay = select(engine, ram, list);
                    if (delay == 0) {
                        timer_restart(ram.fire_timer, engine.time(), kFireNg);
                        ++ram.fire;
                        ram.counter = 0;
                    } else if (delay < 0) {
                        ram.state = 2;
                        ram.fire = ram.counter = 0;
                    } else {
                        ram.decided = true;
                        timer_restart(ram.fire_timer, engine.time(), delay);
                        ++ram.fire;
                        ram.counter = 0;
                        list.fire_request = false;
                    }
                }
            } else if (up) {
                if (list.cursor > 0) --list.cursor, play_sound(engine, "CURSOR_MOVE");
                else if (list.scroll > 0) --list.scroll, play_sound(engine, "CURSOR_MOVE");
            } else if (down) {
                if (list.count && list.cursor != list.count - 1) {
                    if (list.cursor == 3) {
                        if (list.scroll + 4 < list.count) ++list.scroll, play_sound(engine, "CURSOR_MOVE");
                    } else {
                        ++list.cursor;
                        play_sound(engine, "CURSOR_MOVE");
                    }
                }
            } else if (trigger & (pad_button::r | pad_button::l | pad_button::z)) {
                const int wanted = (trigger & pad_button::r) ? 0 : (trigger & pad_button::l) ? 1 : 2;
                if (ram.kind != wanted) {
                    ram.kind = wanted;
                    ram.state = 0;
                    ram.fire = ram.counter = 0;
                } else {
                    ram.state = 2;
                    ram.fire = ram.counter = 0;
                    play_sound(engine, "CURSOR_CANCEL");
                }
            }
            if (ram.counter < 2) ++ram.counter;
        }
        // 8034A9A4 shows the list picked at the start of the frame (8034A298,
        // r29): after a switch the new kind's base and lines follow on the
        // next frame, when state 0 restarts with the new list.
        auto& shown = list;
        display(engine, ram, shown);
        if (previous_count != shown.count) {
            // 80348C64: base model with the sequence for the line count. The
            // new line mode moves the cursor at once, the rows only with the
            // next frame's display (8034A9A4 shows once, before this).
            shown.line_mode = shown.count == 3 ? 1 : shown.count > 3 ? 0 : 2;
            const char* sequence = kBaseSequences[shown.kind][shown.line_mode];
            if (ram.reopen) model(engine, ram, "i_wzslbase00", "GENERATE", true, sequence);
            else model(engine, ram, "i_wzslbase00", sequence, false, nullptr);
            if (!(shown.fire_request && ram.fire == 1)) play_sound(engine, "WINDOW_OPEN");
            ram.reopen = false;
        }
        const bool cursor = !(ram.state == 1 && ram.fire == 1) && shown.count > 0;
        if (cursor) {
            const int line = std::clamp(shown.cursor + shown.line_mode, 0, 3);
            if (!ram.cursor_shown) {
                model(engine, ram, "i_wzslcursel", "GENERATE", true, kCursorSequences[line]);
                ram.cursor_shown = true;
            } else if (previous_line != line) {
                model(engine, ram, "i_wzslcursel", kCursorSequences[line], false, nullptr);
            }
        } else if (ram.cursor_shown) {
            model(engine, ram, "i_wzslcursel", "SQ_CLOSE", false, nullptr);
            ram.cursor_shown = false;
        }
        // DW4_TRACE_PANEL (test runs): the selected line whenever it changes.
        if (static const bool trace = std::getenv("DW4_TRACE_PANEL") != nullptr; trace) {
            static std::array<std::array<int, 6>, 4> traced{};
            const int at = shown.cursor + shown.scroll;
            const int row = at >= 0 && at < int(shown.rows.size()) ? shown.rows[std::size_t(at)] : -1;
            const std::array<int, 6> now{ram.kind, shown.count, shown.cursor, shown.scroll, row, ram.decided ? 1 : 0};
            if (auto& last = traced[std::size_t(std::clamp(ram.index, 0, 3))]; now != last) {
                last = now;
                std::printf("[%llu] panel %d kind %d count %d cursor %d scroll %d row %d%s\n", static_cast<unsigned long long>(engine.frame()),
                            ram.index, ram.kind, shown.count, shown.cursor, shown.scroll, row, ram.decided ? " decided" : "");
            }
        }
    }
}

} // namespace dw4::engine
