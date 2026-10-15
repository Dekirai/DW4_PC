#include "dw4/engine/managers.hpp"
#include "dw4/engine/save_menu.hpp"
#include "menu_text.hpp"

#include <algorithm>
#include <cstdio>

namespace dw4::engine {
namespace {

template <class RamT>
void register_rams(Engine& engine, Manager* manager, const ObjectView& info, std::vector<std::unique_ptr<RamT>>& out,
                   std::initializer_list<const char*> categories) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<RamT>();
        ram->manager = manager;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        for (const char* category : categories) engine.messenger().add_row(category, ram->info_name, ram->index, ram.get());
        out.push_back(std::move(ram));
    }
}

bool query(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index,
           std::string_view command, std::optional<std::string_view> argument = std::nullopt) {
    Packet packet;
    if (argument) packet.strings.emplace_back(std::string(*argument));
    engine.messenger().send(engine, category, name, index, command, packet);
    return !packet.bools.empty() && packet.bools[0];
}

std::size_t send(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index,
                 std::string_view command) {
    Packet packet;
    return engine.messenger().send(engine, category, name, index, command, packet);
}

void next_stage(Engine& engine, std::string_view stage) {
    Packet packet;
    packet.strings.emplace_back(std::string(stage));
    engine.messenger().send(engine, "STAGECTL", std::nullopt, -1, "NEXTSTAGE", packet);
}

// Title and LoadIntf2 input: stick trigger/repeat bits or D-pad trigger/repeat
// bits select a direction; face buttons use the trigger mask only.
struct MenuInput {
    bool up{}, down{}, left{}, right{};
    std::uint32_t trigger{};
};

MenuInput menu_input(const Engine& engine, int slot) {
    const auto& pad = engine.pads()[std::size_t(std::clamp(slot, 0, 3))];
    const std::uint32_t sticks = pad.left_directions.trigger | pad.left_directions.repeat;
    const std::uint32_t buttons = pad.buttons.trigger | pad.buttons.repeat;
    MenuInput input;
    input.left = (sticks & dw4::pad_direction::negative_x) || (buttons & dw4::pad_button::left);
    input.right = (sticks & dw4::pad_direction::positive_x) || (buttons & dw4::pad_button::right);
    input.up = (sticks & dw4::pad_direction::positive_y) || (buttons & dw4::pad_button::up);
    input.down = (sticks & dw4::pad_direction::negative_y) || (buttons & dw4::pad_button::down);
    input.trigger = pad.buttons.trigger;
    return input;
}

std::string numbered(const char* prefix, int value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%s%02d", prefix, value);
    return buffer;
}

} // namespace

// ---------------------------------------------------------------------------
// COMMDLCTRL
void CommonModelControl::open(Engine& engine, int index, const std::string& name, const std::string& seq, bool track) {
    send(engine, "MODELCTRL", name, index, "GENERATE");
    send(engine, "MODELCTRL", name, index, seq);
    if (track) sequence_waits_.push_back({index, name, seq});
}

void CommonModelControl::open_at(Engine& engine, int index, const std::string& name, const std::string& seq,
                                 const Vec3f& position, bool track) {
    Packet packet;
    packet.vectors.push_back(position);
    engine.messenger().send(engine, "MODELCTRL", name, index, "GENERATE", packet);
    send(engine, "MODELCTRL", name, index, seq);
    if (track) sequence_waits_.push_back({index, name, seq});
}

bool CommonModelControl::in_sequence(Engine& engine, int index, const std::string& name, const std::string& seq) {
    return query(engine, "MODELCTRL", name, index, "ISSEQNAME", seq);
}

void CommonModelControl::set(Engine& engine, int index, const std::string& name, const std::string& seq, bool track) {
    send(engine, "MODELCTRL", name, index, seq);
    if (track) sequence_waits_.push_back({index, name, seq});
}

void CommonModelControl::close(Engine& engine, int index, const std::string& name, const std::string& seq, bool track) {
    send(engine, "MODELCTRL", name, index, seq);
    if (track) close_waits_.push_back({index, name, seq});
}

bool CommonModelControl::is_live(Engine& engine, int index, const std::string& name) {
    return query(engine, "MODELCTRL", name, index, "ISRUN");
}

void CommonModelControl::update(Engine& engine) {
    // Newest first; the scan stops at the first entry that is still busy.
    while (!sequence_waits_.empty()) {
        const auto& wait = sequence_waits_.back();
        if (query(engine, "MODELCTRL", wait.name, wait.index, "ISSEQNAME", wait.seq)) break;
        sequence_waits_.pop_back();
    }
    while (!close_waits_.empty()) {
        const auto& wait = close_waits_.back();
        if (is_live(engine, wait.index, wait.name)) break;
        close_waits_.pop_back();
    }
}

// ---------------------------------------------------------------------------
// TITLE2
void Title2Manager::add_info(Engine& engine, const ObjectView& info) {
    register_rams(engine, this, info, rams_, {"TITLE2"});
}

bool Title2Manager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    (void)packet;
    auto& ram = static_cast<TitleRam&>(base);
    if (!ram.created) return true; // 8036B1E4: ignored before the first update
    if (command == "RUN") {
        ram.run = true;
        return true;
    }
    if (command == "GAMESTART") {
        if (auto* sys = engine.get<GameSystem>()) sys->game_started = true;
        return true;
    }
    return false;
}

void Title2Manager::update(Engine& engine) {
    for (auto& ram : rams_) update_ram(engine, *ram);
}

void Title2Manager::update_ram(Engine& engine, TitleRam& ram) {
    if (!ram.created) {
        ram.created = true;
        // 8036910C: modes come from GAMERAM once a game was started.
        auto* sys = engine.get<GameSystem>();
        if (sys && sys->game_started) {
            ram.option.camera_mode = sys->camera_mode;
            ram.option.light_mode = sys->light_mode;
        } else {
            ram.option.camera_mode = 0;
            ram.option.light_mode = 1;
        }
        ram.option.kind = 0;
    }
    auto& models = ram.models;
    models.update(engine);
    const auto input = menu_input(engine, 0);
    auto enter = [&](int state) {
        ram.state = state;
        ram.sub = ram.step = ram.detail = 0;
    };

    switch (ram.state) {
    case 0:
        if (ram.run) enter(1);
        return;
    case 1: // logo and "press start"
        if (ram.sub == 0) {
            ram.sub = 1;
            ram.step = ram.detail = 0;
        } else if (ram.sub == 1) {
            if (ram.step == 0) {
                if (!models.is_live(engine, 0, "j_startlogo")) models.open(engine, 0, "j_startlogo", "SQ_OPEN", true);
                ram.step = 1;
                ram.detail = 0;
            } else if (ram.step == 1 && models.sequences_done()) {
                ram.sub = 2;
                ram.step = ram.detail = 0;
            }
        } else if (ram.sub == 2) {
            if (ram.step == 0) {
                if (ram.detail == 0) {
                    models.open(engine, 0, "j_startmenu01", "SQ_OPEN", true);
                    ram.detail = 1;
                } else if (ram.detail == 1 && models.sequences_done()) {
                    ram.step = 1;
                    ram.detail = 0;
                }
            } else if (ram.step == 1) {
                if (ram.detail == 0) {
                    if (input.trigger & dw4::pad_button::start) {
                        ram.step = 2;
                        ram.detail = 0;
                    } else if (!query(engine, "MODELCTRL", "j_startmenu01", 0, "ISSEQNAME", "SQ_LINESL00")) {
                        ram.detail = 1000; // prompt timed out: attract demo
                    }
                } else if (ram.detail == 1000) {
                    ram.detail = 1001;
                    send(engine, "MODELCTRL", "i_fadewn01", 0, "GENERATE");
                    send(engine, "MODELCTRL", "i_fadewn01", 0, "SQ_BLOUT30");
                } else if (ram.detail == 1001) {
                    if (!query(engine, "MODELCTRL", "j_startmenu01", 0, "ISRUN")) ram.detail = 1002;
                } else if (ram.detail == 1002) {
                    next_stage(engine, "stagIS09");
                    ram.detail = 1003;
                }
            } else if (ram.step == 2) {
                if (ram.detail == 0) {
                    models.close(engine, 0, "j_startmenu01", "SQ_LINEON00", true);
                    ram.detail = 1;
                } else if (ram.detail == 1 && models.closes_done()) {
                    enter(2);
                }
            }
        }
        return;
    case 2: // Game Start / Option
        if (ram.sub == 0) {
            if (ram.step == 0) {
                models.open(engine, 0, "j_startmenu02", "SQ_OPEN", true);
                ram.step = 1;
                ram.detail = 0;
            } else if (ram.step == 1 && models.sequences_done()) {
                ram.sub = 1;
                ram.step = ram.detail = 0;
            }
        } else if (ram.sub == 1) {
            if (ram.step == 0) {
                ram.cursor = 0;
                ram.choice = 0;
                ram.step = 1;
                ram.detail = 0;
            } else if (ram.step == 1) {
                if (input.up) {
                    if (ram.cursor == 1) {
                        ram.cursor = 0;
                        models.set(engine, 0, "j_startmenu02", "SQ_LINEUP00", true);
                    }
                    ram.step = 2;
                    ram.detail = 0;
                } else if (input.down) {
                    if (ram.cursor == 0) {
                        ram.cursor = 1;
                        models.set(engine, 0, "j_startmenu02", "SQ_LINEDW01", true);
                    }
                    ram.step = 2;
                    ram.detail = 0;
                } else if (input.trigger & dw4::pad_button::a) {
                    if (ram.cursor) {
                        models.set(engine, 0, "j_startmenu02", "SQ_LINEON01", true);
                        ram.choice = 2;
                    } else {
                        models.set(engine, 0, "j_startmenu02", "SQ_LINEON00", true);
                        ram.choice = 1;
                    }
                    ram.step = 2;
                    ram.detail = 0;
                } else if (input.trigger & dw4::pad_button::b) {
                    models.close(engine, 0, "j_startmenu02", "SQ_CANCEL", true);
                    ram.step = 3;
                    ram.detail = 0;
                }
            } else if (ram.step == 2) {
                if (ram.detail == 0) {
                    if (models.sequences_done()) {
                        if (ram.choice == 3) enter(1);
                        else if (ram.choice == 0) {
                            ram.step = 1;
                            ram.detail = 0;
                        } else {
                            models.close(engine, 0, "j_startlogo", "SQ_CLOSE", true);
                            ram.detail = 1;
                        }
                    }
                } else if (ram.detail == 1 && models.closes_done()) {
                    if (ram.choice == 2) enter(4);
                    else if (ram.choice == 1) enter(7);
                }
            } else if (ram.step == 3) {
                if (models.closes_done()) enter(1);
            }
        }
        return;
    case 4:
        update_option(engine, ram, input.up, input.down, input.left, input.right, input.trigger);
        return;
    case 7: // Game Start: fade out and close the background
        if (ram.sub == 0) {
            models.open(engine, 0, "i_fadewn01", "SQ_WTOUT60", true);
            models.close(engine, 0, "j_startback", "SQ_CLOSE", true);
            ram.sub = 1;
            ram.step = ram.detail = 0;
        } else if (ram.sub == 1 && models.closes_done()) {
            enter(8);
        }
        return;
    case 8:
        if (ram.sub == 0) {
            Packet packet;
            packet.ints = {ram.option.camera_mode, ram.option.light_mode};
            engine.messenger().send(engine, "LOADCTRL2", std::nullopt, 0, "GENERATE_TITLE", packet);
            ram.sub = 1;
            ram.step = ram.detail = 0;
        }
        return;
    default:
        return;
    }
}

void Title2Manager::update_option(Engine& engine, TitleRam& ram, bool up, bool down, bool left, bool right,
                                  std::uint32_t trigger) {
    // Option window (80369284 case 4). Row 0 camera mode, row 1 light mode,
    // row 2 language (kind 1/2 windows of the other regions only), row 3 exit.
    auto& models = ram.models;
    auto& o = ram.option;
    // 8036A0E4 / 8036A8EC: the values (TEXTORDER 0xF0002..0xF000D).
    auto show_values = [&] {
        if (o.kind != 0) {
            static const int language[7] = {0xF0007, 0xF0008, 0xF0009, 0xF000A, 0xF000C, 0xF000B, 0xF000D};
            menu::text_order_show(engine, 0xF0006);
            menu::text_order_show(engine, o.edit_language >= 0 && o.edit_language <= 6 ? language[o.edit_language] : 0xF0007);
        }
        menu::text_order_show(engine, o.edit_camera == 0 ? 0xF0002 : 0xF0003);
        menu::text_order_show(engine, o.edit_light == 0 ? 0xF0004 : 0xF0005);
    };
    auto enter = [&](int state) {
        ram.state = state;
        ram.sub = ram.step = ram.detail = 0;
    };
    auto next_step = [&] { ++ram.step; ram.detail = 0; };
    auto next_sub = [&] { ++ram.sub; ram.step = ram.detail = 0; };
    if (ram.sub == 0) {
        if (ram.step == 0) {
            o.edit_camera = o.camera_mode ? 1 : 0;
            o.edit_light = o.light_mode ? 1 : 0;
            o.keep = true;
            o.edit_language = 1;
            models.open(engine, 0, "j_optionwin02", o.kind ? "SQ_AOPEN" : "SQ_BOPEN", true);
            models.open(engine, 0, "j_optioncursor11", "SQ_OPEN", true);
            models.open(engine, 0, "j_optioncursor12", "SQ_OPEN", true);
            o.row = 0;
            o.confirmed = false;
            next_step();
        } else if (ram.step == 1 && models.sequences_done()) {
            menu::text_order_show(engine, 0xF0001);
            show_values();
            next_sub();
        }
    } else if (ram.sub == 1) {
        if (ram.step == 0) {
            int row = o.row;
            bool moved = false;
            if (up) {
                if (o.kind == 0) {
                    if (row == 3) { row = 1; moved = true; }
                    else if (row) { row = 0; moved = true; }
                } else if (row - 1 >= 0) {
                    --row;
                    moved = true;
                } else {
                    row = 0;
                }
                if (moved) models.set(engine, 0, "j_optioncursor11", "SQ_MOVE", true);
                o.row = row;
                if (moved) next_step();
            } else if (down) {
                if (o.kind == 0 && row == 1) { row = 3; moved = true; }
                else if (++row <= 3) moved = true;
                else row = 3;
                if (moved) models.set(engine, 0, "j_optioncursor11", "SQ_MOVE", true);
                o.row = row;
                if (moved) next_step();
            } else if (right || left) {
                const char* prefix = right ? "SQ_LINERT" : "SQ_LINELF";
                const int delta = right ? 1 : -1;
                if (row >= 0 && row <= 2) {
                    models.set(engine, 0, "j_optioncursor12", numbered(prefix, row), true);
                    if (row == 0) o.edit_camera = o.edit_camera ? 0 : 1;
                    else if (row == 1) o.edit_light = o.edit_light ? 0 : 1;
                    else {
                        const int low = o.kind == 1 ? 2 : 0;
                        int value = o.edit_language + delta;
                        if (value > 6) value = low;
                        if (value < low) value = 6;
                        o.edit_language = value;
                    }
                    next_step();
                }
            } else if (trigger & dw4::pad_button::a) {
                if (row == 3) {
                    o.confirmed = true;
                    next_sub();
                }
            } else if (trigger & dw4::pad_button::b) {
                o.confirmed = true;
                next_sub();
            }
        } else if (ram.step == 1 && models.sequences_done()) {
            const int row = o.row;
            models.set(engine, 0, "j_optioncursor11", numbered("SQ_LINESL", row), false);
            if (row <= 2) {
                const auto seq = numbered("SQ_LINESL", row);
                if (models.is_live(engine, 0, "j_optioncursor12")) models.set(engine, 0, "j_optioncursor12", seq, false);
                else models.open(engine, 0, "j_optioncursor12", seq, false);
            } else if (models.is_live(engine, 0, "j_optioncursor12")) {
                models.close(engine, 0, "j_optioncursor12", "SQ_CLOSE", false);
            }
            show_values();
            ram.step = 0;
            ram.detail = 0;
        }
    } else if (ram.sub == 2) {
        if (ram.step == 0) {
            models.close(engine, 0, "j_optioncursor11", numbered("SQ_LINEON", o.row), true);
            if (o.row != 3) models.close(engine, 0, "j_optioncursor12", "SQ_CLOSE", true);
            menu::text_order_clear(engine, 0xF0001);
            if (o.kind != 0) {
                menu::text_order_clear(engine, 0xF0006);
                menu::text_order_clear(engine, 0xF000D);
            }
            menu::text_order_clear(engine, 0xF0002);
            menu::text_order_clear(engine, 0xF0005);
            next_step();
        } else if (ram.step == 1) {
            if (models.closes_done()) next_step();
        } else if (ram.step == 2) {
            models.close(engine, 0, "j_optionwin02", "SQ_CLOSE", true);
            next_step();
        } else if (ram.step == 3 && models.closes_done()) {
            if (o.confirmed) {
                o.camera_mode = o.edit_camera;
                o.light_mode = o.edit_light;
            }
            if (o.keep) next_sub();
            else {
                ram.sub = 50;
                ram.step = ram.detail = 0;
            }
        }
    } else if (ram.sub == 3) {
        if (ram.step == 0) {
            models.open(engine, 0, "j_startlogo", "SQ_OPEN", true);
            next_step();
        } else if (ram.step == 1 && models.sequences_done()) {
            enter(2);
        }
    } else if (ram.sub == 50) {
        // Apply modes that need a reload and restart the boot stage.
        if (ram.step == 0) {
            send(engine, "MODELCTRL", "i_fadewn01", 0, "GENERATE");
            send(engine, "MODELCTRL", "i_fadewn01", 0, "SQ_BLOUT30");
            next_step();
        } else if (ram.step == 1) {
            if (!query(engine, "MODELCTRL", "j_startmenu01", 0, "ISRUN")) next_step();
        } else if (ram.step == 2) {
            if (auto* sys = engine.get<GameSystem>()) {
                sys->camera_mode = o.camera_mode;
                sys->light_mode = o.light_mode;
                sys->game_started = true;
            }
            send(engine, "SOUND", std::nullopt, -1, "BGMSTOP");
            send(engine, "SOUND", std::nullopt, -1, "ENVSTOP");
            send(engine, "SOUND", std::nullopt, -1, "SESTOP");
            next_stage(engine, "stagIS08");
            ram.step = 3;
        }
    }
}

// ---------------------------------------------------------------------------
// LOADCTRL2
void LoadCtrl2Manager::add_info(Engine& engine, const ObjectView& info) {
    register_rams(engine, this, info, rams_, {"LOADCTRL2"});
}

bool LoadCtrl2Manager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<CtrlRam&>(base);
    if (command == "GENERATE" || command == "GENERATE_TITLE") {
        const bool title = command == "GENERATE_TITLE";
        if (title) {
            ram.camera_mode = packet.integer(0, 0);
            ram.light_mode = packet.integer(1, 0);
        }
        if (ram.created) return true;
        ram.created = true;
        ram.state = 1;
        ram.sub = ram.step = ram.counter = 0;
        auto* sys = engine.get<GameSystem>();
        if (title) {
            ram.title = true;
            // GAMERAM GAMEDATAINIT for every slot.
            if (sys)
                for (auto& player : sys->players) player = GameSystem::PlayerSlot{};
        } else {
            ram.title = false;
            if (sys)
                for (std::size_t slot = 1; slot < sys->players.size(); ++slot)
                    if (!sys->players[slot].joined) sys->players[slot] = GameSystem::PlayerSlot{};
        }
        common_ = SaveCommon{};
        common_.active = true;
        common_.title = title;
        return true;
    }
    if (command == "CLOSE_WIN") return true;
    if (command == "GETCOMMON") return true; // shared through common()
    return false;
}

void LoadCtrl2Manager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.created) continue;
        auto next_sub = [&] { ++ram.sub; ram.step = ram.counter = 0; };
        auto next_step = [&] { ++ram.step; ram.counter = 0; };
        auto enter = [&](int state) {
            ram.state = state;
            ram.sub = ram.step = ram.counter = 0;
        };
        switch (ram.state) {
        case 1: // save utility (memory card) start-up
            if (ram.sub == 0) {
                common_.io.generate(engine); // 80367900
                next_sub();
            } else if (ram.sub == 1) {
                if (common_.io.is_ready(engine)) enter(2); // 80367980
            }
            break;
        case 2: // character select screen
            if (ram.sub == 0) {
                send(engine, "MODELCTRL", "j_chrselback", 0, "GENERATE");
                send(engine, "MODELCTRL", "j_chrselback", 0, "SQ_OPEN");
                next_sub();
            } else if (ram.sub == 1) {
                if (ram.step == 0) {
                    if (!query(engine, "MODELCTRL", "j_chrselback", 0, "ISSEQNAME", "SQ_OPEN")) {
                        if (ram.title) next_step();
                        else next_sub();
                    }
                } else if (ram.step == 1) {
                    if (query(engine, "MODELCTRL", "i_fadewn01", 0, "ISRUN")) send(engine, "MODELCTRL", "i_fadewn01", 0, "SQ_WTIN60");
                    next_step();
                } else if (ram.step == 2) {
                    if (!query(engine, "MODELCTRL", "i_fadewn01", 0, "ISRUN")) next_sub();
                }
            } else if (ram.sub == 2) {
                send(engine, "LOADINTF2_ALL", std::nullopt, -1, "GENERATE");
                next_sub();
            } else if (ram.sub == 3) {
                // 8036322C runs the card worker while the panels work.
                if (common_.state == 0) common_.update(engine);
                else if (common_.state == 3) next_sub();
            } else if (ram.sub == 4) {
                if (ram.step == 0) {
                    for (auto& player : common_.players) {
                        player.closed = false;
                        player.state = 2;
                        player.sub = player.detail = player.detail2 = 0;
                    }
                    next_step();
                } else if (ram.step == 1) {
                    if (std::all_of(common_.players.begin(), common_.players.end(), [](const auto& p) { return p.closed; }))
                        next_step();
                } else if (ram.step == 2) {
                    send(engine, "MODELCTRL", "i_fadewn01", 0, "GENERATE");
                    send(engine, "MODELCTRL", "i_fadewn01", 0, "SQ_BLOUT30");
                    send(engine, "MODELCTRL", "j_chrselback", 0, "SQ_CLOSE");
                    next_step();
                } else if (ram.step == 3) {
                    if (!query(engine, "MODELCTRL", "j_chrselback", 0, "ISRUN")) next_step();
                } else if (ram.step == 4) {
                    send(engine, "LOADINTF2_ALL", std::nullopt, -1, "SETSELECT_DATA");
                    int joined = 0;
                    for (const auto& player : common_.players) joined += player.joined ? 1 : 0;
                    if (auto* sys = engine.get<GameSystem>()) {
                        sys->ninzu = joined;
                        if (ram.title) {
                            sys->difficulty = common_.difficulty;
                            sys->camera_mode = ram.camera_mode;
                            sys->light_mode = ram.light_mode;
                        }
                    }
                    if (ram.title) send(engine, "TITLE2", std::nullopt, 0, "GAMESTART");
                    enter(3);
                }
            }
            break;
        case 3: // leave for the lobby
            if (ram.sub == 0) {
                common_.io.remove(engine); // 80367A18
                next_sub();
            } else if (ram.sub == 1) {
                if (common_.io.gone(engine)) { // 80367A60
                    Packet point;
                    if (ram.title) {
                        engine.run_script("gameStart.lua");
                        point.ints.push_back(0);
                    } else {
                        point.ints.push_back(4);
                    }
                    engine.messenger().send(engine, "PLAYERPOINT", std::nullopt, -1, "SETGENERATEPOS", point);
                    next_stage(engine, "stagML11");
                    enter(4);
                }
            }
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// NDMW logo (beNDMWLogo, update 803B18BC). State 0 checks the memory cards
// before the logos: sub 0 starts the card worker (8033F1CC / 803675D8,
// GENERATE 80367900, ISRDY 80367980); sub 1 opens i_messwn, shows "Checking
// Memory Card. Do not touch the Memory Card or POWER Button." (text 92200)
// once SQ_OPEN has finished and probes slot A, then B (USE_CHECK, FILEFIND,
// GETFREESIZE through 80367AE4; the worker steps every frame, 80367B18)
// until a slot can hold the save; a broken or unformatted card (USE_CHECK
// 7) asks to format it in sub 50; sub 100 removes the worker (80367A18 /
// 80367A60), closes the window, clears the text (803B35C4) and waits until
// i_messwn has stopped. State 1 (no slot can hold the save) reports the
// worst problem and asks whether to go on without saving: yes shows the
// logos (state 2), no checks the cards again (state 0). State 2 runs
// logo.lua once.
// ---------------------------------------------------------------------------
void LogoManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (ram.state == 2) {
            engine.run_script("logo.lua");
            ram.state = 3;
            ram.sub = ram.step = ram.substep = 0;
            continue;
        }
        if (ram.state > 2) continue;
        auto set_state = [&](int state) { ram.state = state; ram.sub = ram.step = ram.substep = 0; };
        auto set_sub = [&](int sub) { ram.sub = sub; ram.step = ram.substep = 0; };
        auto set_step = [&](int step) { ram.step = step; ram.substep = 0; };
        auto messwn = [&](std::string_view command) { send(engine, "MODELCTRL", "i_messwn", 0, command); };
        auto opening = [&] { return query(engine, "MODELCTRL", "i_messwn", 0, "ISSEQNAME", "SQ_OPEN"); };
        auto running = [&] { return query(engine, "MODELCTRL", "i_messwn", 0, "ISRUN"); };
        // 803B3560: the call number for TXTgetCallNo, then txt_Tar.lua.
        auto show_text = [&](int call) {
            if (auto* sys = engine.get<GameSystem>()) sys->text_call = call;
            engine.run_script("txt_Tar.lua");
        };
        // 92204 / 92205: "Slot A" / "Slot B" ahead of a slot's problem.
        auto slot_text = [&](int slot) {
            if (slot == 0) show_text(92204);
            else if (slot == 1) show_text(92205);
        };
        // 803B35C4: clear the three talk lines.
        auto clear_text = [&] {
            for (const char* font : {"font00", "font01", "font02"}) menu::text_command(engine, "SWITCH", {"TALKFONT01", font, "CLR"});
        };
        // SELECTCTRL i_messky (a key) and i_messcr (two answers, ints count
        // and first line, NOCANCEL).
        auto select = [&](const char* cursor, int items, int first, bool no_cancel) {
            Packet packet;
            if (items) packet.ints.push_back(items);
            if (items && first >= 0) packet.ints.push_back(first);
            if (no_cancel) packet.strings.emplace_back("NOCANCEL");
            engine.messenger().send(engine, "SELECTCTRL", std::string(cursor), 0, "GENERATE", packet);
        };
        auto select_end = [&](const char* cursor) { return query(engine, "SELECTCTRL", std::string(cursor), 0, "ISEND"); };
        auto select_remove = [&](const char* cursor) { send(engine, "SELECTCTRL", std::string(cursor), 0, "REMOVE"); };
        auto selection = [&](const char* cursor) {
            Packet packet;
            engine.messenger().send(engine, "SELECTCTRL", std::string(cursor), 0, "GETSELECT", packet);
            return packet.integer(0, 0);
        };
        if (ram.state == 1) {
            switch (ram.sub) {
            case 0:
            case 6:
                messwn("GENERATE");
                messwn("SQ_OPEN");
                set_sub(ram.sub + 1);
                break;
            case 1: {
                if (opening()) break;
                // The problem to report: slot A's worst flag (8 no room, 0x40
                // no file entry, 0x20 wrong device, 4 an error, 0x10 broken),
                // else slot B's (+64 its slot, +68 the flag, 2 none).
                auto worst = [](std::uint32_t f) { return f & 8 ? 8 : f & 0x40 ? 64 : f & 0x20 ? 32 : f & 4 ? 4 : f & 0x10 ? 16 : 2; };
                ram.problem_slot = -1;
                ram.problem = worst(ram.flags[0]);
                if (ram.problem != 2) {
                    ram.problem_slot = 0;
                } else if ((ram.problem = worst(ram.flags[1])) != 2) {
                    ram.problem_slot = 1;
                }
                if (ram.problem == 32 || ram.problem == 64 || ram.problem == 8 || ram.problem == 4) {
                    slot_text(ram.problem_slot);
                    show_text(ram.problem == 32 ? 92209 : ram.problem == 64 ? 92211 : ram.problem == 8 ? 92010 : 92020);
                } else if (((ram.flags[0] | ram.flags[1]) & 0x10) != 0) {
                    show_text(92206); // a broken card that was not formatted
                    ram.problem = 1;
                } else {
                    show_text(92000); // Memory Card not found.
                }
                set_sub(2);
                break;
            }
            case 2:
                if (ram.step == 0) {
                    select("i_messky", 0, -1, false);
                    set_step(1);
                } else if (select_end("i_messky")) {
                    clear_text();
                    set_sub(3);
                }
                break;
            case 3:
                // Yes, continue without saving. / No, use (insert) another Memory Card.
                if (ram.step == 0) {
                    select("i_messcr", 2, -1, true);
                    show_text(ram.problem == 1 ? 92207 : ram.problem == 4 ? 92021 : ram.problem == 8 ? 92011 : 92001);
                    set_step(1);
                } else if (ram.step == 1) {
                    if (select_end("i_messcr")) set_step(2);
                } else {
                    ram.choice = selection("i_messcr");
                    set_sub(4);
                }
                break;
            case 4:
            case 9:
                if (ram.step == 0) {
                    messwn("SQ_CLOSE");
                    clear_text();
                    set_step(1);
                } else if (!running()) {
                    if (ram.sub == 4) set_sub(5);
                    else set_state(0); // check the cards again
                }
                break;
            case 5:
                set_sub(ram.choice != 0 ? 6 : 10);
                break;
            case 7:
                if (!opening()) {
                    show_text(92100);
                    set_sub(8);
                }
                break;
            case 8:
                if (ram.step == 0) {
                    select("i_messky", 0, -1, true);
                    set_step(1);
                } else if (select_end("i_messky")) {
                    set_sub(9);
                }
                break;
            case 10:
                // Without room on the card: "To manage the contents of your
                // Memory Card, use the Memory Card Screen." first.
                switch (ram.step) {
                case 0:
                    if (ram.problem == 8 || ram.problem == 64) {
                        messwn("GENERATE");
                        messwn("SQ_OPEN");
                        set_step(1);
                    } else {
                        set_state(2);
                    }
                    break;
                case 1:
                    if (!opening()) {
                        show_text(92210);
                        set_step(2);
                    }
                    break;
                case 2:
                    select("i_messky", 0, -1, false);
                    set_step(3);
                    break;
                case 3:
                    if (select_end("i_messky")) set_step(4);
                    break;
                case 4:
                    messwn("SQ_CLOSE");
                    clear_text();
                    set_step(5);
                    break;
                default:
                    if (!running()) set_state(2);
                    break;
                }
                break;
            default:
                break;
            }
            continue;
        }
        if (ram.sub == 0) {
            if (ram.step == 0) {
                ram.io = std::make_shared<CardIo>();
                set_step(1);
            } else if (ram.step == 1) {
                ram.io->generate(engine);
                set_step(2);
            } else if (ram.io->is_ready(engine)) {
                set_sub(1);
            }
            continue;
        }
        if (ram.sub == 1) {
            auto& io = *ram.io;
            io.update(engine);
            if (ram.step == 0) {
                if (ram.substep == 0) {
                    ram.flags = {};
                    ram.slot = 0;
                    ram.usable = ram.restart = false;
                    messwn("GENERATE");
                    messwn("SQ_OPEN");
                    ram.substep = 1;
                } else if (!opening()) {
                    show_text(92200);
                    set_step(1);
                }
            } else if (ram.step == 1) {
                // Mount the card.
                if (ram.substep == 0) {
                    io.start(CardIo::kUseCheck, ram.slot);
                    ram.substep = 1;
                } else if (io.done) {
                    auto& flags = ram.flags[std::size_t(ram.slot)];
                    if (io.result == 7) {
                        // Broken or unformatted: slot A (or B after a clean A)
                        // offers to format it.
                        flags |= 0x10;
                        if (ram.slot != 1 || (ram.flags[0] & 0x7C) == 0) set_sub(50);
                        else set_step(3);
                    } else if (io.result == 2) {
                        set_step(2);
                    } else {
                        if (io.result == 11) flags |= 0x20;
                        else if (io.result != 12) flags |= 4;
                        set_step(3);
                    }
                }
            } else if (ram.step == 2) {
                // The save file, else room for one.
                if (ram.substep == 0) {
                    io.start(CardIo::kFileFind, ram.slot);
                    ram.substep = 1;
                } else if (ram.substep == 1) {
                    if (!io.done) continue;
                    if (io.result == 2) {
                        ram.usable = true;
                        set_sub(100);
                    } else {
                        ram.substep = 2;
                    }
                } else if (ram.substep == 2) {
                    io.start(CardIo::kGetFreeSize, ram.slot);
                    ram.substep = 3;
                } else if (io.done) {
                    auto& flags = ram.flags[std::size_t(ram.slot)];
                    if (io.result == 2) {
                        ram.usable = true;
                        set_sub(100);
                    } else {
                        if (io.result == 10) flags |= 0x40;
                        else if (io.result == 9) flags |= 8;
                        set_step(3);
                    }
                }
            } else if (ram.step == 3) {
                if (++ram.slot < 2) set_step(1);
                else set_sub(100);
            }
            continue;
        }
        if (ram.sub == 50) {
            // "The Memory Card in Slot A is corrupted and needs to be
            // formatted." (92030), then the question (92031, cursor on no).
            auto& io = *ram.io;
            io.update(engine);
            const bool card_in = io.slots[std::size_t(ram.slot)].inserted;
            switch (ram.step) {
            case 0:
                if (ram.substep == 0) {
                    messwn("SQ_CLOSE");
                    clear_text();
                    ram.substep = 1;
                } else if (ram.substep == 1) {
                    if (!running()) ram.substep = 2;
                } else if (ram.substep == 2) {
                    messwn("GENERATE");
                    messwn("SQ_OPEN");
                    ram.substep = 3;
                } else if (!opening()) {
                    slot_text(ram.slot);
                    show_text(92030);
                    set_step(1);
                }
                break;
            case 1:
                if (ram.substep == 0) {
                    select("i_messky", 0, -1, false);
                    ram.substep = 1;
                } else if (ram.substep == 1) {
                    if (!card_in) {
                        select_remove("i_messky"); // the card left: check again
                        ram.substep = 2;
                    } else if (select_end("i_messky")) {
                        clear_text();
                        set_step(2);
                    }
                } else if (select_end("i_messky")) {
                    clear_text();
                    ram.restart = true;
                    set_step(6);
                }
                break;
            case 2:
                if (ram.substep == 0) {
                    select("i_messcr", 2, 1, true);
                    show_text(92031);
                    ram.substep = 1;
                } else if (ram.substep == 1) {
                    if (!card_in) {
                        select_remove("i_messcr");
                        ram.substep = 100;
                    } else if (select_end("i_messcr")) {
                        ram.choice = selection("i_messcr");
                        clear_text();
                        ram.substep = 2;
                    }
                } else if (ram.substep == 2) {
                    if (ram.choice != 0) {
                        set_step(6);
                    } else {
                        show_text(92201); // Formatting...
                        set_step(3);
                    }
                } else if (select_end("i_messcr")) {
                    ram.restart = true;
                    set_step(6);
                }
                break;
            case 3:
                if (ram.substep == 0) {
                    io.start(CardIo::kFormat, ram.slot);
                    ram.substep = 1;
                } else if (io.done) {
                    if (io.result == 2) {
                        ram.usable = true;
                        set_step(5);
                    } else {
                        set_step(4);
                    }
                }
                break;
            case 4:
            case 5:
                // 92203 formatting failed / 92202 formatted.
                if (ram.substep == 0) {
                    show_text(ram.step == 4 ? 92203 : 92202);
                    select("i_messky", 0, -1, false);
                    ram.substep = 1;
                } else if (select_end("i_messky")) {
                    set_step(6);
                }
                break;
            default:
                if (ram.substep == 0) {
                    clear_text();
                    messwn("SQ_CLOSE");
                    ram.substep = 1;
                } else if (ram.substep == 1) {
                    if (running()) break;
                    if (ram.restart) set_sub(1);
                    else if (ram.usable) set_sub(100);
                    else ram.substep = 2;
                } else if (ram.substep == 2) {
                    messwn("GENERATE");
                    messwn("SQ_OPEN");
                    ram.substep = 3;
                } else if (!opening()) {
                    // Not formatted: on with the next slot.
                    set_sub(1);
                    set_step(3);
                    show_text(92200);
                }
                break;
            }
            continue;
        }
        if (ram.sub == 100) {
            if (ram.step == 0) {
                ram.io->remove(engine);
                set_step(1);
            } else if (ram.step == 1) {
                if (ram.io->gone(engine)) {
                    ram.io.reset();
                    set_step(2);
                }
            } else if (ram.substep == 0) {
                messwn("SQ_CLOSE");
                clear_text();
                ram.substep = 1;
            } else if (!running()) {
                set_state(ram.usable ? 2 : 1);
            }
        }
    }
}

} // namespace dw4::engine
