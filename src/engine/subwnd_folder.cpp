#include "subwnd_windows.hpp"

#include <numeric>

namespace dw4::engine::subwnd {
namespace {

// ISUSE players other than self: the partners of the give menus.
std::vector<int> other_players(Engine& engine, int self) {
    std::vector<int> out;
    if (auto* sys = engine.get<GameSystem>())
        for (int k = 0; k < 4; ++k)
            if (sys->players[std::size_t(k)].joined && k != self) out.push_back(k);
    return out;
}

GameSystem::PlayerSlot* slot_of(Engine& engine, int k) {
    auto* sys = engine.get<GameSystem>();
    return sys ? &sys->players[std::size_t(std::clamp(k, 0, 3))] : nullptr;
}

std::string digit(int value) { return std::string(1, char('0' + value)); }

constexpr const char* kLines[12] = {"font000", "font001", "font002", "font003", "font004", "font005",
                                    "font006", "font007", "font008", "font009", "font010", "font011"};
// Partner lines of the give menus (8045A578 / 8045A334 / 8045A40C).
constexpr const char* kPartnerLines[3] = {"font001", "font002", "font003"};
constexpr const char* kOpen[12] = {"SQ_OPEN00", "SQ_OPEN01", "SQ_OPEN02", "SQ_OPEN03", "SQ_OPEN04", "SQ_OPEN05",
                                   "SQ_OPEN06", "SQ_OPEN07", "SQ_OPEN08", "SQ_OPEN09", "SQ_OPEN10", "SQ_OPEN11"};
const Vec3f kListCursor{-121.0f, 128.0f, 0.0f};   // 8045A270 / 8045A274
const Vec3f kPartnerCursor{-121.0f, 109.0f, 0.0f}; // 8045A270 / 8045A3EC
const Vec3f kMenuCursor{115.0f, 145.0f, 0.0f};     // 8045A368 / 8045A36C

// The i_stmenu02 frame for one to three partners (SQ_1xOPEN .. SQ_3xOPEN).
std::string partner_frame(const char* kind, std::size_t partners) {
    const int lines = partners == 2 ? 2 : partners == 3 ? 3 : 1;
    return "SQ_" + std::to_string(lines) + kind + "OPEN";
}

// ---------------------------------------------------------------------------
// beNDMWStatusSubMenu (8038E638): discard, sort or give the chosen device.
// ---------------------------------------------------------------------------
struct DeviceMenu : Window {
    int device{};                   // +84
    std::shared_ptr<Window> list;   // +88: the device list, closed to give
    int target{};                   // +92
    std::vector<int> partners;      // +96
    std::vector<std::size_t> sizes; // +100: their folder sizes
    int message{};                  // +40: -1 once the give message runs

    ShopSelect00& add_message(Engine& engine, int call) {
        auto& note = add_child<ShopSelect00>(engine);
        note.remember("STWINFONT29", "font000");
        note.text_script(engine, "txt_status.lua", call);
        return note;
    }
    // 803900A4 (record ascending) and 8038FD5C (value descending, the unknown
    // records moved behind), both stable selections.
    static void by_record(const std::vector<ItemDevice>& devices, std::vector<int>& order) {
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return devices[std::size_t(a)].record < devices[std::size_t(b)].record; });
    }
    static void by_value(const std::vector<ItemDevice>& devices, std::vector<int>& order) {
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return devices[std::size_t(a)].value > devices[std::size_t(b)].value; });
        for (int i = int(order.size()) - 1; i > -1; --i) {
            if (!DeviceList::unknown(devices[std::size_t(order[std::size_t(i)])])) continue;
            order.push_back(order[std::size_t(i)]);
            order.erase(order.begin() + i);
        }
    }
    // Sub 2: equipped devices first (each slot followed by the pockets of its
    // first slot), then weapons, boards, accessories and chips.
    void sort_folder(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p) return;
        std::vector<int> order(p->devices.size());
        std::iota(order.begin(), order.end(), 0);
        by_record(p->devices, order);
        std::vector<int> equipped;
        auto add = [&](const std::vector<int>& slots, const std::vector<int>* pockets) {
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (slots[i] >= 0) equipped.push_back(slots[i]);
                if (i == 0 && pockets)
                    for (const int chip : *pockets)
                        if (chip >= 0) equipped.push_back(chip);
            }
        };
        add(p->power_socket, &p->power_pocket);
        add(p->main_slot, &p->main_pocket);
        add(p->sub_slot, nullptr);
        for (const int index : equipped)
            if (auto found = std::find(order.begin(), order.end(), index); found != order.end()) order.erase(found);
        std::vector<int> kinds[4];
        for (const int index : order) {
            const int type = p->devices[std::size_t(index)].type;
            if (type >= 0 && type < 4) kinds[type].push_back(index);
        }
        for (int k = 0; k < 3; ++k) by_value(p->devices, kinds[k]);
        by_record(p->devices, kinds[3]);
        std::vector<int> sorted = equipped;
        for (const auto& kind : kinds) sorted.insert(sorted.end(), kind.begin(), kind.end());
        std::vector<ItemDevice> devices;
        std::vector<int> moved(p->devices.size(), -1);
        for (const int index : sorted) {
            moved[std::size_t(index)] = int(devices.size());
            devices.push_back(p->devices[std::size_t(index)]);
        }
        p->devices = std::move(devices);
        for (auto* slots : {&p->power_socket, &p->power_pocket, &p->main_slot, &p->main_pocket, &p->sub_slot})
            for (int& entry : *slots)
                if (entry >= 0) entry = entry < int(moved.size()) ? moved[std::size_t(entry)] : -1;
    }

    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            if (message == -1) textctl(engine, "STWINFONT14", "font000", "DEFAULTCOLOR");
            next_state(3);
            return;
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return;
        }
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db || device < 0 || device >= int(p->devices.size())) {
            next_state(2);
            return;
        }
        if (state == 0) {
            open_model(engine, "i_stcursor04", 0, kOpen[mode % 12], "SQ_CLOSE", kListCursor);
            open_model(engine, "i_stmenu02", 0, "SQ_3LINEOPEN", "SQ_CLOSE");
            for (const char* line : {"font000", "font001", "font002"}) remember("STWINFONT26", line);
            text_script(engine, "txt_status.lua", 67);
            auto& select = add_child<SelectE1>(engine);
            select.count = 3;
            select.position = kMenuCursor;
            select.initial = 0;
            mark();
            next_state(1);
        }
        const ItemDevice& d = p->devices[std::size_t(device)];
        switch (sub) {
        case 0:
            if (!child_done()) return;
            if (selection >= 0 && selection <= 2) set_sub(selection + 1);
            else next_state(2);
            return;
        case 1: // discard
            if (step == 0) {
                if (d.flag) {
                    add_message(engine, 144);
                    set_step(10);
                } else if (d.type != 0 || db->number(device_table(d), d.record, "ISHAKAI").value_or(0)) {
                    add_message(engine, 78);
                    set_step(1);
                } else {
                    add_message(engine, 145);
                    set_step(10);
                }
                mark();
            }
            if (step == 1) {
                if (!child_done()) return;
                auto& ask = add_child<SelectE0>(engine);
                ask.mode = 79;
                ask.count = 2;
                mark();
                set_step(2);
            }
            if (step == 2) {
                if (!child_done()) return;
                if (selection == 0) p->remove_device(device);
            }
            if (child_done()) next_state(2);
            return;
        case 2: // sort
            sort_folder(engine);
            next_state(2);
            return;
        case 3: // give
            give(engine, *p, *db, d);
            return;
        }
    }
    void give(Engine& engine, GameSystem::PlayerSlot& p, DatabaseManager& db, const ItemDevice& d) {
        if (step == 0) {
            partners = other_players(engine, player);
            int call = 0;
            if (d.flag) call = 146;
            else if (db.number(device_table(d), d.record, "ISJYOUDO").value_or(0)) call = partners.empty() ? 148 : 0;
            else call = 147;
            if (call) {
                add_message(engine, call);
                mark();
                set_step(4);
                return;
            }
            if (list) {
                list->next_state(2);
                model(engine, "i_stcursor04", 0, "SQ_CLOSE");
                std::erase_if(models, [](const Model& m) { return m.name == "i_stcursor04"; });
                set_step(1);
            } else {
                next_state(2);
            }
            clear_lines(engine);
            return;
        }
        if (step == 1) {
            if (list && !list->closed(engine)) return;
            list.reset();
            open_model(engine, "i_stmenu02", 0, partner_frame("CHIP", partners.size()), "SQ_CLOSE");
            remember("STWINFONT39", "font000");
            text_script(engine, "txt_status.lua", 73);
            text(engine, "STWINFONT14", "font000", db.text(device_table(d), d.record, "NAME"), "LEFT");
            const Vec3f rgb = device_color(d);
            textctl(engine, "STWINFONT14", "font000", "COLOR", &rgb);
            sizes.clear();
            for (std::size_t k = 0; k < partners.size() && k < 3; ++k) {
                auto* other = slot_of(engine, partners[k]);
                text(engine, "STWINFONT14", kPartnerLines[k], other ? other->name : std::string(), "LEFT");
                sizes.push_back(other ? other->devices.size() : 36);
            }
            set_step(2);
            return;
        }
        if (step == 2) {
            auto& select = add_child<SelectE1>(engine);
            select.position = kPartnerCursor;
            select.count = int(partners.size());
            select.initial = target;
            mark();
            set_step(3);
        }
        if (step == 3) {
            if (!child_done()) return;
            if (selection == -1) {
                next_state(2);
                return;
            }
            auto& note = add_child<ShopSelect00>(engine);
            note.remember("STWINFONT29", "font000");
            mark();
            set_step(4);
            message = -1;
            target = selection;
            if (target >= 0 && target < int(sizes.size()) && sizes[std::size_t(target)] < 36) {
                note.text_script(engine, "txt_status.lua", 131);
                if (auto* other = slot_of(engine, partners[std::size_t(target)])) {
                    ItemDevice moved = p.devices[std::size_t(device)];
                    p.remove_device(device);
                    other->devices.push_back(moved);
                }
            } else {
                note.text_script(engine, "txt_status.lua", 149);
            }
            marker_at(engine, "i_stcursor04", kOpen[std::clamp(selection, 0, 2)], kPartnerCursor);
            return;
        }
        if (step >= 4 && child_done()) {
            model(engine, "i_stcursor04", 0, "SQ_CLOSE");
            const int kept = message;
            next_state(2);
            message = kept;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWStatusCtrlFlag (80389760): the key items of the common FLAGFOLDER.
// ---------------------------------------------------------------------------
struct CtrlFlag : Window {
    std::vector<int> flags; // +84

    // 80389D38
    void fill(Engine& engine, bool names, bool description) {
        auto* db = engine.get<DatabaseManager>();
        if (!db) return;
        if (names) {
            for (std::size_t i = 0; i < flags.size() && i < 12; ++i)
                textset(engine, "STWINFONT14", kLines[i], db->text("impItem", flags[i], "NAME"));
            for (std::size_t i = flags.size(); i < 12; ++i) switch_line(engine, "STWINFONT14", kLines[i], false);
        }
        if (description && !flags.empty() && selection >= 0 && selection < int(flags.size()))
            textset(engine, "STWINFONT29", "font000", db->text("impItem", flags[std::size_t(selection)], "TXT"));
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            flags.clear();
            if (auto* sys = engine.get<GameSystem>())
                for (const auto& [flag, set] : sys->flag_folder)
                    if (set) flags.push_back(flag);
            remember("STWINFONT13", "font000");
            text_script(engine, "txt_status.lua", 71);
            for (const char* line : kLines) text(engine, "STWINFONT14", line, " ", "LEFT");
            text(engine, "STWINFONT23", "font000", " ", "RIGHT");
            text(engine, "STWINFONT23", "font001", " ", "RIGHT");
            text(engine, "STWINFONT29", "font000", " ", "LEFT");
            auto& select = add_child<SelectE1>(engine);
            select.count = flags.empty() ? 1 : int(flags.size());
            select.position = kListCursor;
            select.initial = 0;
            select.remember("STWINFONT29", "font100");
            select.text_script(engine, "txt_status.lua", 135);
            mark();
            open_model(engine, "i_stfolder", 0, "SQ_FLAGOPEN", "SQ_CLOSE");
            next_state(1);
        }
        if (sub == 0) {
            fill(engine, true, true);
            set_sub(1);
        }
        if (!child_done()) {
            if (selection != -1 && step != selection) {
                fill(engine, false, true);
                step = selection;
                last = 0;
            }
        } else {
            next_state(2);
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWStatusCtrlDisk (80389F0C): the disks, moved within the folder or
// given to a partner.
// ---------------------------------------------------------------------------
struct CtrlDisk : Window {
    int line{};                     // +84
    std::vector<int> partners;      // +88
    int target{};                   // +92
    std::vector<int> target_slots;  // +96: the same disk in each partner's folder
    int amount_max{};               // +104

    GameSystem::PlayerSlot::Disk* partner_disk(Engine& engine, int index) {
        if (index < 0 || index >= int(partners.size()) || index >= int(target_slots.size())) return nullptr;
        auto* other = slot_of(engine, partners[std::size_t(index)]);
        return other ? &other->disks[std::size_t(target_slots[std::size_t(index)])] : nullptr;
    }
    // 8038BB14
    void fill(Engine& engine, bool names, bool description) {
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) return;
        if (names)
            for (std::size_t i = 0; i < p->disks.size(); ++i) {
                textset(engine, "STWINFONT14", kLines[i], db->text("conItem", p->disks[i].record, "NAME"));
                textset(engine, "STWINFONT15", kLines[i], digit(p->disks[i].count));
            }
        if (description) {
            if (selection >= 0 && selection < int(p->disks.size()))
                textset(engine, "STWINFONT29", "font000", db->text("conItem", p->disks[std::size_t(selection)].record, "TXT"));
            switch_line(engine, "STWINFONT29", "font000", selection != -1);
        }
    }
    ShopSelect00& add_message(Engine& engine, int call) {
        auto& note = add_child<ShopSelect00>(engine);
        note.remember("STWINFONT29", "font000");
        note.text_script(engine, "txt_status.lua", call);
        return note;
    }
    void back_to_list(Engine& engine) {
        model(engine, "i_stcursor04", 0, "SQ_CLOSE");
        set_sub(0);
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        if (!p) {
            next_state(2);
            return;
        }
        if (state == 0) {
            if (sub == 0) line = 0;
            open_model(engine, "i_stfolder", 0, "SQ_AWINOFFOPEN", "SQ_CLOSE");
            remember("STWINFONT13", "font000");
            text_script(engine, "txt_status.lua", 72);
            for (const char* l : kLines) text(engine, "STWINFONT14", l, " ", "LEFT");
            for (const char* l : kLines) text(engine, "STWINFONT15", l, " ", "RIGHT");
            text(engine, "STWINFONT29", "font000", " ", "LEFT");
            next_state(1);
        }
        auto& own = p->disks[std::size_t(std::clamp(line, 0, 11))];
        switch (sub) {
        case 0: {
            auto& select = add_child<SelectE1>(engine);
            select.count = 10;
            select.position = kListCursor;
            select.initial = line;
            select.remember("STWINFONT29", "font100");
            select.text_script(engine, "txt_status.lua", 43);
            mark();
            set_sub(2);
            step = line;
            last = 999;
            fill(engine, true, false);
            return;
        }
        case 2:
            if (!child_done()) {
                if (selection != -1 && (step != selection || last == 999)) {
                    fill(engine, false, true);
                    step = selection;
                    last = 0;
                }
            } else if (selection == -1) {
                next_state(2);
            } else {
                set_sub(3);
                line = selection;
            }
            return;
        case 3: // move / give
            if (step == 0) {
                marker_at(engine, "i_stcursor04", kOpen[std::clamp(line, 0, 11)], kListCursor);
                auto& select = add_child<SelectE1>(engine);
                select.count = 2;
                select.position = kMenuCursor;
                select.initial = 0;
                select.open_model(engine, "i_stmenu02", 0, "SQ_2LINEOPEN", "SQ_CLOSE");
                select.text(engine, "STWINFONT26", "font000", " ", "LEFT");
                select.text(engine, "STWINFONT26", "font001", " ", "LEFT");
                mark();
                set_step(1);
            }
            if (step == 1) {
                text_script(engine, "txt_status.lua", 65);
                set_step(2);
            }
            if (!child_done()) return;
            if (selection == 1) set_sub(5);
            else if (selection == 0) set_sub(4);
            else back_to_list(engine);
            return;
        case 4: // move to another slot
            if (step == 0) {
                auto& select = add_child<SelectE1>(engine);
                select.count = 10;
                select.position = kListCursor;
                select.initial = line;
                for (int j = 0; j < 10; ++j) select.enabled.push_back(j == line ? 0 : 1);
                mark();
                set_step(1);
            }
            if (step == 1) {
                set_step(2);
                last = line;
                return;
            }
            if (child_done()) {
                if (selection >= 0 && selection < 12) std::swap(p->disks[std::size_t(line)], p->disks[std::size_t(selection)]);
                back_to_list(engine);
            } else if (selection != -1 && last != selection) {
                fill(engine, false, true);
                last = selection;
            }
            return;
        case 5: // give: find partners holding a slot for this disk
            if (step == 0) {
                if (own.count >= 1) {
                    partners.clear();
                    target_slots.clear();
                    for (const int k : other_players(engine, player)) {
                        auto* other = slot_of(engine, k);
                        partners.push_back(k);
                        int found = 0;
                        for (std::size_t s = 0; other && s < other->disks.size(); ++s)
                            if (other->disks[s].record == own.record) {
                                found = int(s);
                                break;
                            }
                        target_slots.push_back(found);
                    }
                    if (!partners.empty()) {
                        set_sub(6);
                        target = 0;
                        return;
                    }
                    add_message(engine, 142);
                } else {
                    add_message(engine, 141);
                }
                set_step(1);
                return;
            }
            if (step == 1) {
                mark();
                set_step(2);
                return;
            }
            if (child_done()) back_to_list(engine);
            return;
        case 6:
            give(engine, own);
            return;
        case 7:
            if (step == 0) {
                close_models(engine);
                models.clear();
                clear_lines(engine);
            }
            next_state(0);
            set_sub(1);
            return;
        }
    }
    void give(Engine& engine, GameSystem::PlayerSlot::Disk& own) {
        auto* db = engine.get<DatabaseManager>();
        switch (step) {
        case 0:
            model(engine, "i_stcursor04", 0, "SQ_CLOSE");
            close_models(engine);
            models.clear();
            clear_lines(engine);
            set_step(1);
            return;
        case 1:
            set_step(2);
            return;
        case 2: {
            open_model(engine, "i_stmenu02", 0, partner_frame("ITEM", partners.size()), "SQ_CLOSE");
            remember("STWINFONT39", "font000");
            remember("STWINFONT39", "font001");
            text_script(engine, "txt_status.lua", 74);
            text(engine, "STWINFONT14", "font000", db ? db->text("conItem", own.record, "NAME") : std::string(), "LEFT");
            for (std::size_t k = 0; k < partners.size() && k < 3; ++k) {
                auto* other = slot_of(engine, partners[k]);
                text(engine, "STWINFONT14", kPartnerLines[k], other ? other->name : std::string(), "LEFT");
                auto* disk = partner_disk(engine, int(k));
                text(engine, "STWINFONT15", kPartnerLines[k], digit(disk ? disk->count : 0), "CENTER");
            }
            text(engine, "STWINFONT38", "font000", digit(own.count), "CENTER");
            text(engine, "STWINFONT38", "font001", "0", "CENTER");
            set_step(3);
        }
            [[fallthrough]];
        case 3: {
            auto& select = add_child<SelectE1>(engine);
            select.position = kPartnerCursor;
            select.count = int(partners.size());
            select.initial = target;
            mark();
            set_step(4);
        }
            [[fallthrough]];
        case 4: {
            if (!child_done()) return;
            if (selection == -1) {
                set_sub(7);
                return;
            }
            target = selection;
            auto* disk = partner_disk(engine, target);
            if (disk && disk->count < 9) {
                marker_at(engine, "i_stcursor04", kOpen[std::clamp(target, 0, 2)], kPartnerCursor);
                set_step(5);
            } else {
                set_step(10);
            }
            return;
        }
        case 5: {
            auto* disk = partner_disk(engine, target);
            amount_max = std::min(own.count, 9 - (disk ? disk->count : 9));
            auto& amount = add_child<ShopSelect52>(engine);
            amount.mode = amount_max;
            amount.position = {68.0f, -3.0f, 0.0f};
            mark();
            set_step(6);
            return;
        }
        case 6:
            textset(engine, "STWINFONT38", "font001", digit(amount_max - selection));
            set_step(7);
            last = selection;
            [[fallthrough]];
        case 7:
            if (!child_done()) {
                if (selection != -1 && selection != last) set_step(6);
                return;
            }
            if (selection == -1) {
                textset(engine, "STWINFONT38", "font001", "0");
                model(engine, "i_stcursor04", 0, "SQ_CLOSE");
                set_step(3);
                return;
            }
            if (auto* disk = partner_disk(engine, target)) {
                const int given = amount_max - selection;
                own.count -= given;
                disk->count += given;
            }
            add_child<ShopSelect00>(engine);
            text_script(engine, "txt_status.lua", 132);
            mark();
            set_step(8);
            return;
        case 8:
            if (!child_done()) return;
            switch_line(engine, "STWINFONT29", "font000", false);
            textset(engine, "STWINFONT38", "font001", "0");
            model(engine, "i_stcursor04", 0, "SQ_CLOSE");
            set_sub(7);
            return;
        case 10:
            add_message(engine, 143);
            mark();
            set_step(11);
            [[fallthrough]];
        case 11:
            if (!child_done()) return;
            textset(engine, "STWINFONT38", "font001", "0");
            model(engine, "i_stcursor04", 0, "SQ_CLOSE");
            set_step(3);
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWStatusCtrlSendBit (8038C31C): bits sent to a partner, the amount
// entered digit by digit.
// ---------------------------------------------------------------------------
struct CtrlSendBit : Window {
    int amount{};              // +84
    int amount_max{};          // +88
    std::vector<int> partners; // +92
    int target{};              // +96

    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            for (int i = 0; i < 7; ++i) switch_line(engine, "STWINFONT28", line_name("font%03d", i), false);
            return;
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return;
        }
        auto* p = player_slot(engine);
        if (!p) {
            next_state(2);
            return;
        }
        if (state == 0) {
            open_model(engine, "i_stmenu02", 0, partner_frame("BIT", partners.size()), "SQ_CLOSE");
            remember("STWINFONT39", "font000");
            remember("STWINFONT39", "font001");
            text_script(engine, "txt_status.lua", 76);
            text(engine, "STWINFONT28", "font000", " ", "CENTER");
            text(engine, "STWINFONT27", "font000", " ", "RIGHT");
            for (std::size_t k = 0; k < partners.size() && k < 3; ++k) {
                text(engine, "STWINFONT14", kPartnerLines[k], " ", "LEFT");
                text(engine, "STWINFONT27", kPartnerLines[k], " ", "RIGHT");
            }
            target = 0;
            next_state(1);
        }
        switch (step) {
        case 0: {
            for (int i = 0; i < 7; ++i) switch_line(engine, "STWINFONT28", line_name("font%03d", i), false);
            amount = 0;
            align(engine, "STWINFONT28", "font000", "CENTER");
            textset(engine, "STWINFONT28", "font000", "0");
            switch_line(engine, "STWINFONT28", "font000", true);
            textset(engine, "STWINFONT27", "font000", std::to_string(p->bits));
            for (std::size_t k = 0; k < partners.size() && k < 3; ++k) {
                auto* other = slot_of(engine, partners[k]);
                textset(engine, "STWINFONT14", kPartnerLines[k], other ? other->name : std::string());
                textset(engine, "STWINFONT27", kPartnerLines[k], std::to_string(other ? other->bits : 0));
            }
            auto& select = add_child<SelectE1>(engine);
            select.position = kPartnerCursor;
            select.count = int(partners.size());
            select.initial = target;
            mark();
            set_step(1);
        }
            [[fallthrough]];
        case 1: {
            if (!child_done()) return;
            if (selection == -1) {
                next_state(2);
                return;
            }
            target = selection;
            marker_at(engine, "i_stcursor04", kOpen[std::clamp(target, 0, 2)], kPartnerCursor);
            auto& digits = add_child<ShopSelect34>(engine);
            digits.mode = 3;
            show_digits(engine, digits, amount, 1);
            const int own = std::min(p->bits, 9999999);
            auto* other = slot_of(engine, partners[std::size_t(std::clamp(target, 0, int(partners.size()) - 1))]);
            amount_max = std::min(own, 9999999 - (other ? other->bits : 9999999));
            mark();
            set_step(2);
            return;
        }
        case 2:
            if (!child_done()) {
                if (selection == -1 || children.empty()) return;
                if (last != selection) {
                    show_digits(engine, *children.front(), amount, 7 - selection);
                    last = selection;
                    return;
                }
                Packet now;
                engine.messenger().send(engine, "SELECTCTRL", std::string("i_stcursor03"), player, "ISSELECTNOW", now);
                if (now.bools.empty() || !now.bools[0]) return;
                const auto& pad = engine.pads()[std::size_t(std::clamp(player, 0, 3))];
                const std::uint32_t stick = pad.left_directions.trigger | pad.left_directions.repeat;
                const std::uint32_t buttons = pad.buttons.trigger | pad.buttons.repeat;
                const bool up = (stick & pad_direction::positive_y) || (buttons & pad_button::up);
                const bool down = (stick & pad_direction::negative_y) || (buttons & pad_button::down);
                if (!up && !down) return;
                int step_value = 1;
                for (int k = 1; k < 7 - selection; ++k) step_value *= 10;
                const int before = amount;
                if (up) amount = std::min(amount + step_value, amount_max);
                else amount = std::max(amount - step_value, 0);
                if (amount != before) {
                    show_digits(engine, *children.front(), amount, 7 - selection);
                    Packet sound;
                    sound.strings.emplace_back("CURSOR_MOVE");
                    engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
                }
                return;
            }
            if (selection == -1 || amount < 1) {
                model(engine, "i_stcursor04", 0, "SQ_CLOSE");
                set_step(0);
                return;
            }
            switch_line(engine, "STWINFONT29", "font100", false);
            p->bits -= amount;
            if (auto* other = slot_of(engine, partners[std::size_t(std::clamp(target, 0, int(partners.size()) - 1))]))
                other->bits += amount;
            set_step(3);
            return;
        case 3:
            add_child<ShopSelect00>(engine);
            text_script(engine, "txt_status.lua", 133);
            mark();
            set_step(4);
            return;
        case 4:
            if (!child_done()) return;
            switch_line(engine, "STWINFONT29", "font000", false);
            subwnd(engine, "UPDATE");
            model(engine, "i_stcursor04", 0, "SQ_CLOSE");
            set_step(0);
            return;
        }
    }
};

// beNDMWStatusCtrlBit (8038BD88): sending bits needs a partner.
struct CtrlBit : Window {
    std::vector<int> partners; // +84
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            partners = other_players(engine, player);
            next_state(1);
            set_sub(partners.empty() ? 0 : 1);
        }
        if (sub == 0) {
            if (step == 0) {
                auto& note = add_child<ShopSelect00>(engine);
                note.remember("STWINFONT29", "font000");
                text_script(engine, "txt_status.lua", 88);
                mark();
                set_step(1);
                return;
            }
        } else if (step == 0) {
            add_child<CtrlSendBit>(engine).partners = partners;
            mark();
            set_step(1);
        }
        if (child_done()) next_state(2);
    }
};

// ---------------------------------------------------------------------------
// beNDMWStatusCtrlFolder (80388B74): Devices / Key Items / Disks / Bits.
// ---------------------------------------------------------------------------
struct CtrlFolder : Window {
    int last_menu{}; // +84
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            add_child<Title04>(engine).mode = 60;
            last_menu = 0;
            next_state(1);
        }
        if (sub == 0) menu(engine);
        else if (sub == 1) devices(engine);
        else page(engine);
    }
    void menu(Engine& engine) {
        if (step == 0) {
            auto& select = add_child<Select01>(engine);
            select.mode = 61;
            select.initial = last_menu;
            select.text(engine, "STWINFONT29", "font000", " ", "LEFT");
            mark();
            set_step(1);
        }
        if (step == 1) {
            if (selection != -1) text_script(engine, "txt_status.lua", selection + 81);
            set_step(2);
            last = selection;
        }
        if (!child_done()) {
            if (selection != -1 && last != selection) set_step(1);
            return;
        }
        if (selection != -1) last_menu = selection;
        if (selection >= 0 && selection <= 3) set_sub(selection + 1);
        else next_state(2);
    }
    void devices(Engine& engine) {
        if (step == 0) {
            add_child<DeviceList>(engine).auto_close = false;
            mark();
            set_step(1);
        }
        if (step == 1) {
            if (!last) {
                text_script(engine, "txt_status.lua", 43);
                last = 1;
            }
            auto* list = find_child<DeviceList>();
            if (!list) return;
            if (selection == -1) {
                switch_line(engine, "STWINFONT29", "font100", false);
                list->next_state(2);
                set_step(3);
            } else if (list->state == 1 && list->sub == 2) {
                switch_line(engine, "STWINFONT29", "font100", false);
                auto& menu = add_child<DeviceMenu>(engine);
                menu.mode = selection;
                menu.device = selection;
                menu.list = shared_child(list);
                mark();
                set_step(2);
            }
            return;
        }
        if (step == 2) {
            if (!child_done()) return;
            mark();
            selection = 0;
            close_models(engine);
            models.clear();
            clear_lines(engine);
            if (auto* list = find_child<DeviceList>()) {
                list->step = 1;
                list->last = 0;
                set_step(1);
            } else {
                set_step(4);
            }
            return;
        }
        if (step == 3) {
            if (!find_child<DeviceList>()) {
                selection = last_menu;
                set_sub(0);
            }
            return;
        }
        if (step == 4 && child_done()) set_step(0);
    }
    void page(Engine& engine) {
        if (step == 0) {
            if (sub == 2) add_child<CtrlFlag>(engine);
            else if (sub == 3) add_child<CtrlDisk>(engine);
            else add_child<CtrlBit>(engine);
            mark();
            set_step(1);
        }
        if (child_done()) {
            selection = last_menu;
            set_sub(0);
        }
    }
};

} // namespace

Window& add_folder_page(Window& parent, Engine& engine) { return parent.add_child<CtrlFolder>(engine); }

} // namespace dw4::engine::subwnd
