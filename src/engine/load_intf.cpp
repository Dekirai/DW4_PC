#include "dw4/engine/managers.hpp"
#include "dw4/engine/save_menu.hpp"
#include "menu_text.hpp"

#include <algorithm>
#include <cstdio>
#include <random>

namespace dw4::engine {
using namespace menu;

namespace {

struct PanelInput {
    bool up{}, down{}, left{}, right{};
    std::uint32_t trigger{};
};

// LOADINTF2 constants (804E8A0C / 804E8A3C / 804E8A4C / 804EA5A8).
constexpr Vec3f kPanelPositions[4] = {{-315, 240, 0}, {-5, 240, 0}, {-315, 20, 0}, {-5, 20, 0}};
constexpr const char* kPreviewModels[4] = {"h_agumon", "h_vmon", "h_girumon", "h_dorumon"};
constexpr const char* kPartners[4] = {"p_agumon", "p_vmon", "p_girumon", "p_dorumon"};
} // namespace

// ---------------------------------------------------------------------------
// MESWIN
void MessageWindow::open() {
    open_ = true;
    closing_ = answered_ = prompt_ = pending_ = clear_request_ = false;
}

void MessageWindow::close() {
    clear();
    closing_ = true;
}

void MessageWindow::show(int id) {
    pending_ = true;
    id_ = id;
}

void MessageWindow::question(int id, int cursor) {
    pending_ = prompt_ = true;
    id_ = id;
    cursor_ = cursor;
    mode_ = 0;
    step_ = 0;
}

void MessageWindow::next(int id) {
    pending_ = prompt_ = true;
    id_ = id;
    mode_ = 1;
    step_ = 0;
}

void MessageWindow::clear() {
    clear_request_ = true;
    pending_ = true;
}

void MessageWindow::cancel(Engine& engine, int player) {
    prompt_ = false;
    send(engine, "MODELCTRL", "j_chrselmesswin", player, "SQ_WAIT");
    clear();
}

void MessageWindow::interrupt(int id) {
    pending_ = interrupted_ = true;
    interrupt_id_ = id;
    interrupt_count_ = 0;
}

void MessageWindow::resume() {
    interrupted_ = false;
    if (!shown_) clear();
    else if (!prompt_) show(id_);
    else if (mode_ == 1) next(id_);
    else question(id_, cursor_);
}

void MessageWindow::update(Engine& engine, int player) {
    if (!open_) return;
    auto seq = [&](const std::string& name) {
        sequence_ = name;
        send(engine, "MODELCTRL", "j_chrselmesswin", player, name);
    };
    auto in_seq = [&] { return query(engine, "MODELCTRL", "j_chrselmesswin", player, "ISSEQNAME", sequence_); };
    if (interrupted_) {
        // 80362B44: an interrupt message parks the prompt until resume().
        if (interrupt_count_++ == 0) send(engine, "MODELCTRL", "j_chrselmesswin", player, "SQ_WAIT");
        if (pending_) {
            text_order_show(engine, interrupt_id_, true);
            pending_ = false;
        }
        return;
    }
    auto finish = [&] {
        clear();
        answered_ = true;
        prompt_ = false;
    };
    if (prompt_) {
        const auto& pad = engine.pads()[std::size_t(std::clamp(player, 0, 3))];
        if (mode_ == 0) {
            if (step_ == 0) {
                seq(cursor_ ? "SQ_LINESL01" : "SQ_LINESL00");
                step_ = 1;
            } else if (step_ == 1) {
                const std::uint32_t sticks = pad.left_directions.trigger | pad.left_directions.repeat;
                const std::uint32_t buttons = pad.buttons.trigger | pad.buttons.repeat;
                const bool up = (sticks & dw4::pad_direction::positive_y) || (buttons & dw4::pad_button::up);
                const bool down = (sticks & dw4::pad_direction::negative_y) || (buttons & dw4::pad_button::down);
                if (up) {
                    if (cursor_ == 1) {
                        cursor_ = 0;
                        seq("SQ_LINEUP00");
                        step_ = 2;
                    }
                } else if (down) {
                    if (cursor_ == 0) {
                        cursor_ = 1;
                        seq("SQ_LINEDW01");
                        step_ = 3;
                    }
                } else if (pad.buttons.trigger & dw4::pad_button::a) {
                    seq(cursor_ ? "SQ_LINEON01" : "SQ_LINEON00");
                    step_ = 4;
                }
            } else if (step_ == 2 || step_ == 3) {
                if (!in_seq()) step_ = 1;
            } else if (step_ == 4) {
                if (!in_seq()) finish();
            }
        } else {
            if (step_ == 0) {
                seq("SQ_NEXTSL00");
                step_ = 1;
            } else if (step_ == 1) {
                if (pad.buttons.trigger & dw4::pad_button::a) {
                    seq("SQ_NEXTON00");
                    step_ = 2;
                }
            } else if (step_ == 2 && !in_seq()) {
                finish();
            }
        }
    }
    if (pending_) {
        if (clear_request_) {
            for (int i = 0; i < 4; ++i) text_command(engine, "SWITCH", {"CHRSELFONT06", player_font(player, i), "CLR"});
            for (int i = 0; i < 2; ++i) text_command(engine, "SWITCH", {"CHRSELFONT16", player_font(player, i), "CLR"});
            clear_request_ = false;
            shown_ = false;
            if (closing_) open_ = false;
        } else {
            shown_ = true;
            text_order_show(engine, id_, true);
        }
        pending_ = false;
    }
}

// ---------------------------------------------------------------------------
// DATABASE
void DatabaseManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<Ram>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        engine.messenger().add_row("DATABASE", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
    if (const auto table = info.ref(7)) tables_[std::string(info.str(2))] = table;
}

std::string DatabaseManager::text(std::string_view table, int row, std::string_view column) const {
    const auto found = tables_.find(table);
    if (found == tables_.end() || row < 0) return {};
    const auto names = found->second.list(12);
    const auto values = found->second.list(13);
    for (std::size_t c = 0; c < names.size(); ++c) {
        if (!names[c] || names[c].str(2) != column) continue;
        const std::size_t at = std::size_t(row) * names.size() + c;
        return at < values.size() && values[at] ? std::string(values[at].str(2)) : std::string();
    }
    return {};
}

std::optional<std::int32_t> DatabaseManager::number(std::string_view table, int row, std::string_view column) const {
    const auto found = tables_.find(table);
    if (found == tables_.end() || row < 0) return std::nullopt;
    // beDataBase column/data pairs: char, short, int (float skipped), bool.
    struct Group { std::uint16_t names, data; std::size_t width; bool is_signed; };
    static constexpr Group groups[] = {{2, 3, 1, true}, {4, 5, 2, true}, {6, 7, 4, true}, {10, 11, 1, false}};
    for (const auto& group : groups) {
        const auto names = found->second.list(group.names);
        for (std::size_t c = 0; c < names.size(); ++c) {
            if (!names[c] || names[c].str(2) != column) continue;
            const auto data = found->second.ref(group.data);
            if (!data) return std::nullopt;
            const auto bytes = data.memory(4);
            const std::size_t offset = (std::size_t(row) * names.size() + c) * group.width;
            if (offset + group.width > bytes.size()) return std::nullopt;
            if (group.width == 1) return group.is_signed ? std::int32_t(std::int8_t(bytes[offset])) : std::int32_t(bytes[offset]);
            if (group.width == 2) return std::int32_t(std::int16_t(read_u16le(bytes, offset)));
            return std::int32_t(read_u32le(bytes, offset));
        }
    }
    return std::nullopt;
}

int DatabaseManager::find_row(std::string_view table, std::string_view column, std::string_view value) const {
    const auto found = tables_.find(table);
    if (found == tables_.end()) return -1;
    const auto names = found->second.list(12);
    const auto values = found->second.list(13);
    for (std::size_t c = 0; c < names.size(); ++c) {
        if (!names[c] || names[c].str(2) != column) continue;
        for (std::size_t at = c, row = 0; at < values.size(); at += names.size(), ++row)
            if (values[at] && values[at].str(2) == value) return int(row);
    }
    return -1;
}

int DatabaseManager::rows(std::string_view table) const {
    const auto found = tables_.find(table);
    if (found == tables_.end()) return 0;
    const auto names = found->second.list(12);
    return names.empty() ? 0 : int(found->second.list(13).size() / names.size());
}

// ---------------------------------------------------------------------------
// LOADINTF2
void LoadIntf2Manager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<PanelRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        engine.messenger().add_row("LOADINTF2_ALL", ram->info_name, ram->index, ram.get());
        engine.messenger().add_row("LOADINTF2", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

bool LoadIntf2Manager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    (void)packet;
    auto& ram = static_cast<PanelRam&>(base);
    auto* ctrl = engine.get<LoadCtrl2Manager>();
    if (!ctrl) return false;
    auto& common = ctrl->common();
    const auto slot = std::size_t(std::clamp(ram.index, 0, 3));
    if (command == "GENERATE") {
        if (ram.created) return true;
        ram.created = true;
        ram.state = ram.sub = ram.detail = ram.detail2 = 0;
        ram.joined_flag = slot == 0; // 803612DC: CTRL +18 preset for controller 1
        ram.position = kPanelPositions[slot];
        ram.digimon = 3;
        return true;
    }
    if (command == "SETSELECT_DATA") {
        // The characters were made (GAMESTART) or called in state 8; a panel
        // that did not confirm leaves its slot empty (GAMEREMOVE).
        if (auto* sys = engine.get<GameSystem>(); sys && !common.players[slot].joined)
            sys->players[slot] = GameSystem::PlayerSlot{};
        return true;
    }
    return false;
}

void LoadIntf2Manager::update(Engine& engine) {
    auto* ctrl = engine.get<LoadCtrl2Manager>();
    if (!ctrl || !ctrl->common().active) return;
    for (auto& ram : rams_)
        if (ram->created) update_panel(engine, *ram, ctrl->common());
}

void LoadIntf2Manager::update_panel(Engine& engine, PanelRam& ram, SaveCommon& common) {
    const int idx = std::clamp(ram.index, 0, 3);
    auto& entry = common.players[std::size_t(idx)];
    ram.models.update(engine);
    ram.message.update(engine, idx);
    auto window_done = [&](int i) { return ram.windows[std::size_t(i)].update(engine, ram.models, idx, ram.position, common); };
    std::uint16_t trigger = 0;
    if (entry.active) trigger = std::uint16_t(engine.pads()[std::size_t(idx)].buttons.trigger);
    auto set_entry_state = [&](SaveCommon::Player& p, int state) {
        p.state = state;
        p.sub = p.detail = p.detail2 = 0;
    };
    auto set_entry_sub = [&](int sub) {
        entry.sub = sub;
        entry.detail = entry.detail2 = 0;
    };

    switch (entry.state) {
    case 0: update_ctrl(engine, ram, common, trigger); break;
    case 1: { // another panel uses the card
        bool frozen = false;
        if (entry.sub == 1) {
            if (--entry.counter <= 0) {
                entry.counter = 0;
                frozen = true;
            }
        } else if (entry.sub == 0) {
            if (ram.state == 2) {
                set_entry_state(entry, 0); // the join prompt does not wait
            } else {
                ram.message.interrupt((idx << 16) | 0x51C);
                set_entry_sub(1);
            }
        }
        if (!frozen) update_ctrl(engine, ram, common, trigger);
        break;
    }
    case 2: // LOADCTRL2 closes the panels
        if (entry.sub == 0) {
            if (ram.state == 8) {
                ram.models.close(engine, idx, "j_chrselmesswin", "SQ_CLOSE", true);
                ram.message.close();
                ram.windows[0].hide(1);
                set_entry_sub(2);
            } else {
                set_entry_sub(1);
            }
            ram.models.close(engine, idx, "j_chrselbase", "SQ_CLOSE", true);
        } else if ((entry.sub == 1 || (entry.sub == 2 && window_done(0))) && ram.models.closes_done()) {
            entry.closed = true;
            set_entry_sub(3);
        }
        break;
    case 5:
        card_open_flow(engine, common, idx, ram.message, ram.slots, true);
        break;
    case 6:
        card_error_flow(engine, common, idx, ram.message, ram.slots, ram.models, ram.position, true);
        break;
    case 7: // release the card: every panel resumes
        for (auto& p : common.players) set_entry_state(p, 8);
        break;
    case 8:
        ram.message.resume();
        set_entry_state(entry, 0);
        entry.active = true;
        break;
    default: break;
    }
}

void LoadIntf2Manager::update_ctrl(Engine& engine, PanelRam& ram, SaveCommon& common, std::uint16_t trigger) {
    const int idx = std::clamp(ram.index, 0, 3);
    const int id = idx << 16;
    auto& entry = common.players[std::size_t(idx)];
    auto& models = ram.models;
    auto& message = ram.message;
    auto* sys = engine.get<GameSystem>();
    DataSelect::Context ctx{engine, models, message, ram.windows, common, ram.position, idx};
    auto window_done = [&](int i) { return ram.windows[std::size_t(i)].update(engine, models, idx, ram.position, common); };

    PanelInput in;
    const auto& pad = engine.pads()[std::size_t(idx)];
    if (entry.active) {
        const std::uint32_t sticks = pad.left_directions.trigger | pad.left_directions.repeat;
        const std::uint32_t buttons = pad.buttons.trigger | pad.buttons.repeat;
        in.up = (sticks & dw4::pad_direction::positive_y) || (buttons & dw4::pad_button::up);
        in.down = (sticks & dw4::pad_direction::negative_y) || (buttons & dw4::pad_button::down);
        in.left = (sticks & dw4::pad_direction::negative_x) || (buttons & dw4::pad_button::left);
        in.right = (sticks & dw4::pad_direction::positive_x) || (buttons & dw4::pad_button::right);
        in.trigger = trigger;
    }
    auto set_state = [&](int state) {
        ram.state = state;
        ram.sub = ram.detail = ram.detail2 = 0;
    };
    auto set_sub = [&](int sub) {
        ram.sub = sub;
        ram.detail = ram.detail2 = 0;
    };
    auto next_sub = [&] { set_sub(ram.sub + 1); };
    auto next_detail = [&] { ++ram.detail; ram.detail2 = 0; };
    auto show_name = [&] {
        std::string name;
        if (auto* db = engine.get<DatabaseManager>()) name = db->text("shinka", ram.digimon, "NAME");
        text_set_text(engine, "CHRSELFONT14", idx, 0, name, 1, false);
    };
    auto preview = std::string(kPreviewModels[std::size_t(ram.digimon)]);
    auto spawn_preview = [&] {
        const auto model = std::string(kPreviewModels[std::size_t(ram.digimon)]);
        send(engine, "MODELCTRL", model, idx, "GENERATE");
        Packet probe;
        const bool live = engine.messenger().send(engine, "MODELCTRL", model, idx, "ISRUN", probe) > 0 &&
                          !probe.bools.empty() && probe.bools[0];
        if (live) {
            Packet move;
            move.vectors = {Vec3f{}, Vec3f{}};
            engine.messenger().send(engine, "MODELCTRL", model, idx, "MOVETRANS", move);
            send(engine, "MODELCTRL", model, idx, "SQ_ENTR");
            ram.no_model = false;
        } else {
            ram.no_model = true;
        }
    };

    switch (ram.state) {
    case 0: // open the panel
        if (ram.sub == 0) {
            models.open_at(engine, idx, "j_chrselbase", "SQ_OPEN", ram.position, true);
            next_sub();
        } else if (ram.sub == 1) {
            if (models.sequences_done()) next_sub();
        } else if (ram.sub == 2) {
            // GAMERAM GETPLAYER ISUSE: a character already in the party goes
            // straight to its status window.
            if (sys && sys->players[std::size_t(idx)].joined) {
                common.records[std::size_t(idx)] = sys->make_record(engine, idx);
                next_sub();
            } else {
                if (idx != 0) set_state(pad.connected ? 2 : 1);
                else set_state(3);
                ram.fresh = true;
            }
        } else if (ram.sub == 3) {
            models.open_at(engine, idx, "j_chrselmesswin", "SQ_OPEN", ram.position, true);
            message.open();
            entry.entered = true;
            next_sub();
        } else if (ram.sub == 4) {
            if (models.sequences_done()) {
                ram.fresh = false;
                set_state(8);
            }
        }
        break;
    case 1: // wait for a controller
        if (entry.active && pad.connected) set_state(2);
        break;
    case 2: // "press A" join prompt
        if (ram.sub == 0) {
            models.open_at(engine, idx, "j_linewin01", "SQ_OPEN", ram.position, true);
            next_sub();
        } else if (ram.sub == 1) {
            if (models.sequences_done()) {
                text_order_show(engine, id | 0x400);
                next_sub();
            }
        } else if (ram.sub == 2) {
            bool leave = false;
            if (entry.active && pad.connected) {
                if (in.trigger & dw4::pad_button::a) {
                    entry.entered = true;
                    ram.joined_flag = true;
                    leave = true;
                }
            } else {
                leave = true;
            }
            if (leave) {
                models.close(engine, idx, "j_linewin01", ram.joined_flag ? "SQ_LINEON00" : "SQ_CLOSE", true);
                text_order_clear(engine, id | 0x400);
                next_sub();
            }
        } else if (ram.sub == 3) {
            if (models.closes_done()) set_state(ram.joined_flag ? 3 : 1);
        }
        break;
    case 3: // Create Digimon / Call Digimon
        if (ram.sub == 0) {
            if (!ram.message_open) {
                models.open_at(engine, idx, "j_chrselmesswin", "SQ_OPEN", ram.position, true);
                message.open();
                ram.message_open = true;
            }
            entry.entered = true;
            models.open_at(engine, idx, "j_linewin02", "SQ_OPEN", ram.position, true);
            ram.cursor = 0;
            next_sub();
        } else if (ram.sub == 1) {
            if (ram.detail == 0 && !models.in_sequence(engine, idx, "j_linewin02", "SQ_OPEN")) {
                text_order_show(engine, id | 0x401);
                next_detail();
                message.show(id | (idx ? 0x524 : 0x523));
            }
            if (models.sequences_done()) next_sub();
        } else if (ram.sub == 2) {
            if (ram.detail == 1) {
                if (models.sequences_done()) ram.detail = 0;
            } else if (ram.detail == 0) {
                if (in.up) {
                    int c = ram.cursor - 1;
                    if (c >= 0) models.set(engine, idx, "j_linewin02", "SQ_LINEUP00", true);
                    else c = 0;
                    ram.cursor = c;
                    next_detail();
                } else if (in.down) {
                    int c = ram.cursor + 1;
                    if (c <= 1) models.set(engine, idx, "j_linewin02", "SQ_LINEDW01", true);
                    else c = 1;
                    ram.cursor = c;
                    next_detail();
                } else if (in.trigger & dw4::pad_button::a) {
                    models.set(engine, idx, "j_linewin02", ram.cursor ? "SQ_LINEON01" : "SQ_LINEON00", true);
                    next_sub();
                } else if ((in.trigger & dw4::pad_button::b) && idx != 0) {
                    models.close(engine, idx, "j_linewin02", "SQ_CANCEL", true);
                    models.close(engine, idx, "j_chrselmesswin", "SQ_CLOSE", true);
                    text_order_clear(engine, id | 0x401);
                    message.close();
                    set_sub(ram.sub + 2);
                }
            }
        } else if (ram.sub == 3) {
            if (models.sequences_done()) {
                text_order_clear(engine, id | 0x401);
                set_state(ram.cursor ? 6 : 4);
            }
        } else if (ram.sub == 4) {
            if (models.closes_done()) {
                entry.entered = false;
                ram.joined_flag = false;
                ram.message_open = false;
                set_state(2);
            }
        }
        break;
    case 4: // partner selection
        if (ram.sub == 0) {
            models.open_at(engine, idx, "j_digiselcursorleft", "SQ_OPEN", ram.position, true);
            models.open_at(engine, idx, "j_digiselcursorright", "SQ_OPEN", ram.position, true);
            models.open_at(engine, idx, "j_namewin02", "SQ_OPEN", ram.position, true);
            ram.digimon = 3;
            message.show(id | 0x511);
            show_name();
            models.open_at(engine, idx, "j_digifacewin", kPartnerRows[ram.digimon].open, ram.position, true);
            spawn_preview();
            next_sub();
            ram.cancel = false;
        } else if (ram.sub == 1) {
            if (models.sequences_done()) {
                send(engine, "MODELCTRL", "j_namewin02", idx, "SQ_LINESL00");
                next_sub();
            }
        } else if (ram.sub == 2) {
            if (ram.detail == 1) {
                if (models.sequences_done()) ram.detail = 0;
            } else if (ram.detail == 0) {
                if (in.right || in.left) {
                    if (!ram.no_model && query(engine, "MODELCTRL", preview, idx, "ISRUN"))
                        send(engine, "MODELCTRL", preview, idx, "SQ_EXIT");
                    ram.digimon = in.right ? (ram.digimon + 1) % 4 : (ram.digimon + 3) % 4;
                    show_name();
                    models.set(engine, idx, in.right ? "j_digiselcursorright" : "j_digiselcursorleft", "SQ_ON00", true);
                    models.set(engine, idx, "j_namewin02", "SQ_LINECG00", false);
                    models.set(engine, idx, "j_digifacewin", kPartnerRows[ram.digimon].change, false);
                    next_detail();
                } else {
                    Packet probe;
                    const bool shown = engine.messenger().send(engine, "MODELCTRL", preview, idx, "ISRUN", probe) > 0
                                           ? !probe.bools.empty() && probe.bools[0]
                                           : ram.no_model;
                    if (shown) {
                        if (in.trigger & dw4::pad_button::a) next_sub();
                        else if (in.trigger & dw4::pad_button::b) {
                            ram.cancel = true;
                            next_sub();
                        }
                    } else {
                        spawn_preview();
                    }
                }
            }
        } else if (ram.sub == 3) {
            models.close(engine, idx, "j_namewin02", ram.cancel ? "SQ_CANCEL" : "SQ_LINEON00", true);
            if (!ram.no_model) models.close(engine, idx, preview, "SQ_EXIT", true);
            text_line_clear(engine, "CHRSELFONT14", idx, 0);
            next_sub();
        } else if (ram.sub == 4) {
            if (models.closes_done()) next_sub();
        } else if (ram.sub == 5) {
            models.close(engine, idx, "j_digifacewin", "SQ_CLOSE", true);
            models.close(engine, idx, "j_digiselcursorleft", "SQ_CLOSE", true);
            models.close(engine, idx, "j_digiselcursorright", "SQ_CLOSE", true);
            next_sub();
        } else if (ram.sub == 6) {
            if (models.closes_done()) {
                if (ram.cancel) {
                    set_state(3);
                } else {
                    set_state(5);
                    ram.names.open(true);
                }
            }
        }
        break;
    case 5: // name entry (NAMESEL)
        if (ram.sub == 0 || ram.sub >= 6) {
            ram.names.set_digimon(ram.digimon);
            next_sub();
            message.show(id | 0x517); // Enter your Digimon's name.
        } else if (ram.sub == 1) {
            if (ram.names.update(engine, models, idx, ram.position, entry.active)) {
                if (ram.names.confirmed()) {
                    ram.new_game = true;
                    set_state(8);
                } else {
                    set_state(4);
                }
            } else if (ram.names.asking()) {
                next_sub();
            }
        } else if (ram.sub == 2) {
            message.next(id | 0x519); // Do you want to assign this name?
            next_sub();
        } else if (ram.sub == 3) {
            if (!message.busy()) next_sub();
        } else if (ram.sub == 4) {
            message.question(id | 0x51B, 0);
            next_sub();
        } else if (ram.sub == 5) {
            if (!message.busy()) {
                if (message.answer() == 1) {
                    message.show(id | 0x517);
                    ram.names.answer(false);
                } else if (message.answer() == 0) {
                    ram.names.answer(true);
                }
                set_sub(1);
            }
        }
        break;
    case 6: // Call Digimon: memory card slot
        if (ram.sub == 0) {
            if (ram.detail == 0) {
                common.detach(idx);
                ram.slots.reset();
                ram.slots.set_phase(0);
                message.show(id | 0x2009);
                next_detail();
            } else if (ram.detail == 1 && ram.slots.open(engine, models, idx, ram.position, common.io)) {
                ram.slots.set_phase(1);
                next_sub();
            }
        } else if (ram.sub == 1) {
            if (ram.detail == 0) {
                if (ram.slots.select(engine, models, idx, ram.position, entry.active, common)) {
                    if (!ram.slots.skipped() && !ram.slots.cancelled()) next_detail();
                    else next_sub();
                }
            } else if (ram.detail == 1) {
                if (ram.detail2 == 0) {
                    message.show(id | 0x2004);
                    common.lock(idx, 5);
                    ++ram.detail2;
                } else if (ram.detail2 == 1) {
                    const int result = entry.result;
                    if (result == 0) {
                        next_sub();
                    } else if (result == 1 || result == 2 || result == 4) {
                        message.show(id | 0x2009);
                        ram.slots.reselect(engine, models, idx);
                        set_sub(1);
                    }
                }
            }
        } else if (ram.sub == 2) {
            if (ram.detail == 0) {
                ram.slots.set_phase(2);
                next_detail();
            } else if (ram.detail == 1 && ram.slots.close(engine, models, idx)) {
                if (ram.slots.skipped()) set_state(6);
                else set_state(ram.slots.cancelled() ? 3 : 7);
                message.clear();
            }
        }
        break;
    case 7: // Call Digimon: the file's boxes
        if (ram.sub == 0) {
            ram.boxes.open(idx, 0);
            next_sub();
        } else if (ram.sub == 1) {
            if (ram.detail == 0 || ram.detail == 2) {
                ram.boxes.reset();
                next_detail();
            } else if (ram.detail == 1) {
                if (ram.boxes.show_boxes(ctx)) next_detail();
            } else if (ram.detail == 3) {
                if (entry.card_missing) {
                    ram.boxes.reset();
                    next_detail();
                    ram.detail2 = 1;
                } else if (ram.boxes.select(ctx)) {
                    ram.boxes.reset();
                    next_detail();
                    ram.detail2 = 0;
                }
            } else if (ram.detail == 4 && ram.boxes.close_all(ctx)) {
                if (ram.detail2 == 1) set_sub(100);
                else next_sub();
            }
        } else if (ram.sub == 2) {
            if (ram.boxes.cancelled()) {
                set_state(6);
            } else {
                ram.new_game = false;
                set_state(8);
            }
        } else if (ram.sub == 100) { // the card left
            if (ram.detail == 0) {
                ram.boxes.reset();
                next_detail();
            } else if (ram.detail == 1 && ram.boxes.close_all(ctx)) {
                common.records[std::size_t(idx)] = save::Record{};
                set_state(6);
            }
        }
        break;
    case 8: // the character, status window, ready, host START
        if (ram.sub == 0) {
            if (ram.detail == 0) {
                if (ram.detail2 == 0) {
                    if (ram.fresh && sys) {
                        auto& target = sys->players[std::size_t(idx)];
                        if (ram.new_game) {
                            // GAMEDATAINIT, GAMESTART with the partner and a
                            // new UNIQUE, then the record for the windows.
                            target = GameSystem::PlayerSlot{};
                            target.digimon = kPartners[std::size_t(ram.digimon)];
                            target.name = ram.names.name();
                            target.joined = true;
                            target.unique = std::random_device{}();
                            sys->prepare_player(engine, idx);
                            common.records[std::size_t(idx)] = sys->make_record(engine, idx);
                        } else if (entry.card) {
                            // GAMEDATAINIT, then the card box (803657D8,
                            // BASE2CURRENT, RECALCPLAYER); the host's box
                            // also brings the story flags.
                            const auto record = entry.card->records[std::size_t(ram.boxes.cursor())];
                            common.records[std::size_t(idx)] = record;
                            common.detach(idx);
                            target = GameSystem::PlayerSlot{};
                            target.joined = true;
                            sys->apply_record(engine, idx, record, idx == 0);
                            engine.log("player " + std::to_string(idx + 1) + " called box " + std::to_string(ram.boxes.cursor()) + " (" +
                                       record.digimon + " level " + std::to_string(record.level) + ")");
                        }
                    }
                    int unlocked = 0;
                    if (idx == 0 && ram.fresh) {
                        common.difficulty = 0;
                        if (sys) sys->difficulty_max = 0;
                        engine.run_script("hostLoad.lua"); // LISsetDifficultMax from the story flags
                        unlocked = sys ? sys->difficulty_max : 0;
                    }
                    if (unlocked > 0) {
                        ram.difficulty.open(unlocked >= 2 ? 1 : 0, idx);
                        ++ram.detail2;
                    } else {
                        ram.windows[0].show(1, idx);
                        next_detail();
                    }
                } else if (ram.detail2 == 1) {
                    if (ram.difficulty.update(engine, models, message, ram.position, entry.active)) {
                        if (ram.difficulty.cancelled()) {
                            common.records[std::size_t(idx)] = save::Record{};
                            entry.joined = false;
                            set_state(6);
                        } else {
                            common.difficulty = ram.difficulty.selection();
                            ram.windows[0].show(1, idx);
                            next_detail();
                        }
                    }
                }
            } else if (ram.detail == 1) {
                if (window_done(0)) {
                    entry.joined = true;
                    set_sub(idx == 0 && common.all_confirmed() ? 3 : 1);
                }
            }
        } else if (ram.sub == 1) {
            bool cancel = false;
            const bool back = (in.trigger & dw4::pad_button::b) && ram.fresh;
            if (ram.detail == 0) {
                message.show(id | (ram.fresh ? 0x530 : 0x52F));
                next_detail();
            } else if (ram.detail == 1) {
                if (ram.detail2 == 1) {
                    if (!common.all_confirmed()) {
                        message.show(id | (ram.fresh ? 0x530 : 0x52F));
                        ram.detail2 = 0;
                    } else {
                        cancel = back;
                    }
                } else if (ram.detail2 == 0) {
                    if (idx == 0) {
                        if (common.all_confirmed()) set_sub(3);
                        else cancel = back;
                    } else if (common.all_confirmed()) {
                        message.show(id | (ram.fresh ? 0x51F : 0x522));
                        ++ram.detail2;
                    } else {
                        cancel = back;
                    }
                }
            }
            if (cancel) {
                entry.joined = false;
                next_sub();
            }
        } else if (ram.sub == 2) { // back out of the status window
            if (ram.detail == 0) {
                models.set(engine, idx, "j_mainstatuswin", "SQ_CANCEL", true);
                next_detail();
            } else if (ram.detail == 1) {
                if (models.sequences_done()) next_detail();
            } else if (ram.detail == 2) {
                ram.windows[0].hide(0);
                next_detail();
            } else if (ram.detail == 3 && window_done(0)) {
                if (ram.new_game) {
                    set_state(5);
                    ram.names.open(false);
                } else {
                    common.records[std::size_t(idx)] = save::Record{};
                    set_state(6);
                }
            }
        } else if (ram.sub == 3) { // controller 1 starts the game
            if (ram.detail == 0) {
                message.show(id | (ram.fresh ? 0x51D : 0x521));
                next_detail();
            } else if (ram.detail == 1) {
                if ((in.trigger & dw4::pad_button::b) && ram.fresh) {
                    entry.joined = false;
                    set_sub(2);
                } else if (common.all_confirmed()) {
                    if (in.trigger & dw4::pad_button::start) {
                        common.state = 3;
                        common.deactivate_all();
                        next_detail();
                    }
                } else {
                    set_sub(1);
                }
            }
        }
        break;
    default:
        break;
    }
}

} // namespace dw4::engine
