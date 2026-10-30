#include "subwnd_windows.hpp"

namespace dw4::engine::subwnd {
namespace {

constexpr const char* kLines[12] = {"font000", "font001", "font002", "font003", "font004", "font005",
                                    "font006", "font007", "font008", "font009", "font010", "font011"};
const Vec3f kShopList{-121.0f, 128.0f, 0.0f};    // 8045B308 / 8045B30C
const Vec3f kArrowLeft{-146.0f, 25.0f, 0.0f};    // 8045B294 / 8045B298
const Vec3f kArrowRight{236.0f, 25.0f, 0.0f};    // 8045B2B8 / 8045B298

// 803B08F8: a device's shop price. HANBAI (buy) adds 100 per value point,
// KAITORI (sell) 25; weapons and boards add a rank bonus.
int device_price(Engine& engine, const ItemDevice& d, bool buy) {
    auto* db = engine.get<DatabaseManager>();
    if (!db) return 0;
    static const int buy_rank[6] = {0, 500, 1500, 6000, 12000, 24000};
    static const int sell_rank[6] = {0, 250, 750, 3000, 6000, 12000};
    int price = db->number(device_table(d), d.record, buy ? "HANBAI" : "KAITORI").value_or(0);
    if (d.type == 2) price += (buy ? 100 : 25) * d.value;
    else if (d.type <= 1) price += (buy ? 100 : 25) * d.value + (buy ? buy_rank : sell_rank)[std::clamp(d.rank, 0, 5)];
    return price;
}

// ShopTitle00 (803AA6E8): the shop background.
struct ShopTitle00 : Title {
    void open(Engine& engine) override {
        open_model(engine, "j_shback", 0, "SQ_OPEN", "SQ_CLOSE");
        open_model(engine, "i_bgclear", 0, "SQ_OPEN", "SQ_CLOSE");
    }
};

// ShopTitle01 (803AA74C): the title of program mode (txt_shop.lua 1000 + 100 mode).
struct ShopTitle01 : Title {
    void open(Engine& engine) override {
        open_model(engine, "i_sttitle", 0, "SQ_OPEN", "SQ_CLOSE");
        remember("STWINFONT12", "font000");
        if (mode >= 1 && mode <= 10) text_script(engine, "txt_shop.lua", 1000 + 100 * mode);
    }
};

// ShopSelect01 (803A70C8): a two or three line choice in the message window
// with the labels of txt_shop.lua call mode.
struct ShopSelect01 : Select {
    void open(Engine& engine) override {
        static const int three[] = {1150, 1350, 1550, 1650, 1750, 1850, 1950};
        static const int two[] = {1163, 1166, 1171, 1255, 1258, 1272, 1355, 1453, 1556, 1562, 1656, 1756, 1856, 1962};
        const bool is_three = std::find(std::begin(three), std::end(three), mode) != std::end(three);
        const bool is_two = std::find(std::begin(two), std::end(two), mode) != std::end(two);
        if (is_three || is_two) {
            const int lines = is_three ? 3 : 2;
            for (int i = 0; i < lines; ++i) remember("STWINFONT29", kLines[i]);
            text_script(engine, "txt_shop.lua", mode);
            count = lines;
        }
        cursor = "i_stmesscr";
        position = {};
        no_cancel = false;
    }
};

// ShopSelectB0 (803AA1B0): Devices / Disks to sell, the empty kind greyed.
struct ShopSelectB0 : Select {
    void open(Engine& engine) override {
        remember("STWINFONT29", "font100");
        remember("STWINFONT29", "font000");
        remember("STWINFONT29", "font001");
        text_script(engine, "txt_shop.lua", 1573);
        for (std::size_t i = 0; i < enabled.size() && i < 2; ++i)
            textctl(engine, "STWINFONT29", kLines[i], enabled[i] ? "DEFAULTCOLOR" : "CANCELCOLOR");
        cursor = "i_stmesscr";
        count = 2;
        position = {};
        initial = 0;
        no_cancel = false;
    }
};

// A ShopSelect00 message of txt_shop.lua call (the shop windows pass it as mode).
ShopSelect00& shop_message(Window& parent, Engine& engine, int call) {
    auto& note = parent.add_child<ShopSelect00>(engine);
    note.mode = call;
    return note;
}

// 803A0064 / 80397CD4: the i_stcursor04 marker on a shop list line.
void list_marker(Engine& engine, Window& window, int line) {
    if (line <= -1) {
        if (window.model_running(engine, "i_stcursor04", 0)) window.model(engine, "i_stcursor04", 0, "SQ_CLOSE");
        return;
    }
    static const char* opens[12] = {"SQ_OPEN00", "SQ_OPEN01", "SQ_OPEN02", "SQ_OPEN03", "SQ_OPEN04", "SQ_OPEN05",
                                    "SQ_OPEN06", "SQ_OPEN07", "SQ_OPEN08", "SQ_OPEN09", "SQ_OPEN10", "SQ_OPEN11"};
    window.marker_at(engine, "i_stcursor04", opens[std::clamp(line, 0, 11)], kShopList);
}

// Page turns of the shop lists (8037E7A8 / 8037E8C8).
bool turn_page(Engine& engine, Window& window, int& page, int pages, bool right) {
    const auto& pad = engine.pads()[std::size_t(engine.pad_reader_slot(window.player, window.player))]; // (the window owner, or the leader for a leaving one: 6.5)
    const std::uint32_t stick = pad.left_directions.trigger | pad.left_directions.repeat;
    const std::uint32_t buttons = pad.buttons.trigger | pad.buttons.repeat;
    const bool pressed = right ? (stick & pad_direction::positive_x) || (buttons & pad_button::right)
                               : (stick & pad_direction::negative_x) || (buttons & pad_button::left);
    const bool vertical = (pad.left_directions.held & (pad_direction::positive_y | pad_direction::negative_y)) != 0;
    if (!pressed || vertical) return false;
    if (right) {
        if (page >= pages - 1) return false;
        ++page;
        window.model(engine, "i_stcursorright", 0, page == pages - 1 ? "SQ_MOVEEND" : "SQ_MOVENEXT");
        window.model(engine, "i_stcursorleft", 0, "SQ_SELECT");
    } else {
        if (page <= 0) return false;
        --page;
        window.model(engine, "i_stcursorleft", 0, page ? "SQ_MOVENEXT" : "SQ_MOVEEND");
        window.model(engine, "i_stcursorright", 0, "SQ_SELECT");
    }
    return true;
}

bool select_idle(Engine& engine, Window& window, const Select& select) {
    Packet idle;
    engine.messenger().send(engine, "SELECTCTRL", select.cursor, window.player, "ISFO2TOP", idle);
    return !idle.bools.empty() && !idle.bools[0];
}

// ---------------------------------------------------------------------------
// beNDMWShopCtrlDeviceSell (803A01F0): devices sold at KAITORI, 12 a page;
// equipped devices and worthless ones cannot be sold.
// ---------------------------------------------------------------------------
struct DeviceSell : Window {
    int page{};                       // +84
    int pages = 1;                    // +88
    int line{};                       // +92
    std::vector<std::uint8_t> sellable; // +96

    // 803A1338
    int page_lines(Engine& engine) {
        auto* p = player_slot(engine);
        const int size = p ? int(p->devices.size()) : 0;
        pages = size % 12 ? size / 12 + 1 : size / 12;
        const int rest = size - 12 * page;
        const int n = rest < 12 ? rest % 12 : 12;
        sellable.clear();
        for (int i = 0; i < 12; ++i) {
            const int at = i + 12 * page;
            if (at >= size) continue;
            const auto& d = p->devices[std::size_t(at)];
            sellable.push_back(!d.flag && device_price(engine, d, false) >= 1 ? 1 : 0);
        }
        return n;
    }
    // 803A0CDC
    void fill(Engine& engine, bool names, bool description) {
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) return;
        const int size = int(p->devices.size());
        if (names) {
            for (int i = 0; i < 12; ++i) {
                const int at = i + 12 * page;
                const bool shown = at < size;
                if (shown) {
                    const auto& d = p->devices[std::size_t(at)];
                    std::string name = d.flag ? "\x81\xba" : "\x8a\x71";
                    name += db->text(device_table(d), d.record, "NAME");
                    textset(engine, "STWINFONT14", kLines[i], name);
                    textset(engine, "STWINFONT17", kLines[i], std::to_string(device_price(engine, d, false)));
                    const Vec3f rgb = device_color(d);
                    textctl(engine, "STWINFONT14", kLines[i], "COLOR", &rgb);
                    const bool ok = std::size_t(i) < sellable.size() && sellable[std::size_t(i)];
                    textctl(engine, "STWINFONT17", kLines[i], ok ? "DEFAULTCOLOR" : "CANCELCOLOR");
                }
                switch_line(engine, "STWINFONT14", kLines[i], shown);
                switch_line(engine, "STWINFONT17", kLines[i], shown);
            }
            textset(engine, "STWINFONT30", "font000", std::string(1, char('1' + page)));
            textset(engine, "STWINFONT30", "font001", std::string(1, char('0' + pages)));
        }
        if (description) {
            const int at = 12 * page + selection;
            const bool shown = at >= 0 && at < size;
            if (shown) {
                const auto& d = p->devices[std::size_t(at)];
                std::string txt = db->text(device_table(d), d.record, "TXT");
                if ((d.type == 0 && d.record != 276) || (d.type == 1 && d.record != 31)) txt += char('0' + d.rank);
                textset(engine, "STWINFONT29", "font000", txt);
            }
            switch_line(engine, "STWINFONT29", "font000", shown);
        }
    }
    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            for (const char* l : kLines) {
                textctl(engine, "STWINFONT14", l, "DEFAULTCOLOR");
                textctl(engine, "STWINFONT17", l, "DEFAULTCOLOR");
            }
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
            open_model(engine, "j_shwin", 0, "SQ_AWINOPEN", "SQ_CLOSE");
            page_lines(engine);
            open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", kArrowLeft);
            open_model(engine, "i_stcursorright", 0, pages <= 1 ? "SQ_HIDE" : "SQ_SELECT", "SQ_CLOSE", kArrowRight);
            remember("STWINFONT29", "font100");
            text_script(engine, "txt_shop.lua", 1654);
            remember("STWINFONT13", "font000");
            text_script(engine, "txt_shop.lua", 1000);
            for (const char* l : kLines) {
                text(engine, "STWINFONT14", l, " ", "LEFT");
                text(engine, "STWINFONT17", l, " ", "RIGHT");
            }
            text(engine, "STWINFONT30", "font000", " ", "RIGHT");
            text(engine, "STWINFONT30", "font001", " ", "RIGHT");
            line = page = 0;
            next_state(1);
            set_sub(4);
        }
        if (sub == 4) {
            if (step == 0) {
                auto& select = add_child<SelectE1>(engine);
                selection = line;
                select.position = kShopList;
                select.initial = line;
                select.text(engine, "STWINFONT29", "font000", " ", "LEFT");
                select.count = page_lines(engine);
                select.enabled = sellable;
                mark();
                set_step(1);
            }
            if (step == 1) {
                fill(engine, true, true);
                set_step(2);
                return;
            }
            if (!child_done()) {
                if (selection == -1) return;
                if (selection == last) {
                    auto* select = find_child<SelectE1>();
                    if (!select || !select_idle(engine, *this, *select)) return;
                    if (turn_page(engine, *this, page, pages, false) || turn_page(engine, *this, page, pages, true)) {
                        select->next_state(4);
                        select->count = page_lines(engine);
                        select->enabled = sellable;
                        set_step(1);
                    }
                } else {
                    fill(engine, false, true);
                    last = selection;
                }
                return;
            }
            if (selection == -1) {
                if (parent) parent->selection = -1;
                next_state(2);
                return;
            }
            set_sub(5);
            line = selection;
            if (parent) list_marker(engine, *parent, line);
            return;
        }
        // Sub 5: confirm and sell.
        if (step == 0) {
            shop_message(*this, engine, 1561);
            mark();
            set_step(1);
            return;
        }
        if (step == 1) {
            if (!child_done()) return;
            add_child<ShopSelect01>(engine).mode = 1562;
            mark();
            set_step(2);
            return;
        }
        if (!child_done()) return;
        if (selection == 0) {
            const int at = line + 12 * page;
            if (at >= int(p->devices.size())) {
                next_state(2);
            } else {
                const int price = device_price(engine, p->devices[std::size_t(at)], false);
                p->bits = std::min(p->bits + price, 9999999);
                p->remove_device(at);
                subwnd(engine, "UPDATE");
                const int size = int(p->devices.size());
                if (size >= 1) {
                    if (at >= size) {
                        if (line) --line;
                        else if (page > 0) --page;
                    }
                    if (!page) model(engine, "i_stcursorleft", 0, "SQ_HIDE");
                    if (size <= 12 * (page + 1)) model(engine, "i_stcursorright", 0, "SQ_HIDE");
                } else {
                    next_state(2);
                }
            }
        }
        if (state == 1) set_sub(4);
        if (parent) list_marker(engine, *parent, -1);
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrlDiskSell (803A15B0): disks sold at KAITORI with a count.
// ---------------------------------------------------------------------------
struct DiskSell : Window {
    int line{};      // +88
    int held{};      // +92
    int amount{};    // +96
    int price{};     // +100

    // 803A2398
    void fill(Engine& engine, bool names, bool description) {
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) return;
        if (names)
            for (std::size_t i = 0; i < p->disks.size(); ++i) {
                const auto& disk = p->disks[i];
                textset(engine, "STWINFONT14", kLines[i], db->text("conItem", disk.record, "NAME"));
                textset(engine, "STWINFONT15", kLines[i], std::string(1, char('0' + disk.count)));
                textset(engine, "STWINFONT17", kLines[i], std::to_string(db->number("conItem", disk.record, "KAITORI").value_or(0)));
                for (const char* layer : {"STWINFONT14", "STWINFONT15", "STWINFONT17"}) switch_line(engine, layer, kLines[i], true);
            }
        if (description) {
            if (selection >= 0 && selection < int(p->disks.size())) {
                const int record = p->disks[std::size_t(selection)].record;
                textset(engine, "STWINFONT29", "font000", db->text("conItem", record, "TXT"));
                switch_line(engine, "STWINFONT29", "font000", true);
                price = db->number("conItem", record, "KAITORI").value_or(0);
            } else {
                switch_line(engine, "STWINFONT29", "font000", false);
            }
        }
    }
    // 803A2744 / 803A2834: the count and the total.
    void show_amount(Engine& engine) {
        const int count = held - selection;
        textset(engine, "STWINFONT30", "font010", std::to_string(count));
        textset(engine, "STWINFONT30", "font021", std::to_string(count * price));
    }
    void hide_amount(Engine& engine) {
        for (const char* l : {"font020", "font010", "font021"}) switch_line(engine, "STWINFONT30", l, false);
    }
    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            if (parent) list_marker(engine, *parent, -1);
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
            open_model(engine, "j_shwin", 0, "SQ_WAIT", "SQ_CLOSE");
            open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", kArrowLeft);
            open_model(engine, "i_stcursorright", 0, "SQ_HIDE", "SQ_CLOSE", kArrowRight);
            remember("STWINFONT29", "font100");
            remember("STWINFONT13", "font000");
            text_script(engine, "txt_shop.lua", 1001);
            for (std::size_t i = 0; i < p->disks.size(); ++i) {
                text(engine, "STWINFONT14", kLines[i], " ", "LEFT");
                text(engine, "STWINFONT15", kLines[i], " ", "CENTER");
                text(engine, "STWINFONT17", kLines[i], " ", "RIGHT");
            }
            line = 0;
            next_state(1);
            set_sub(6);
        }
        switch (sub) {
        case 6: // the disk list
            if (step == 0) {
                model(engine, "j_shwin", 0, "SQ_EWINOPEN");
                if (parent) list_marker(engine, *parent, -1);
                auto& select = add_child<SelectE1>(engine);
                selection = line;
                select.position = kShopList;
                select.initial = line;
                select.text(engine, "STWINFONT29", "font000", " ", "LEFT");
                select.count = int(p->disks.size());
                for (const auto& disk : p->disks) select.enabled.push_back(disk.count > 0 ? 1 : 0);
                text_script(engine, "txt_shop.lua", 1654);
                mark();
                set_step(1);
            }
            if (step == 1) {
                fill(engine, true, true);
                set_step(2);
                return;
            }
            if (!child_done()) {
                if (selection != -1 && last != selection) {
                    fill(engine, false, true);
                    last = selection;
                }
                return;
            }
            if (selection == -1) {
                if (parent) parent->selection = -1;
                next_state(2);
                return;
            }
            set_sub(7);
            line = selection;
            if (parent) list_marker(engine, *parent, line);
            held = p->disks[std::size_t(std::clamp(line, 0, 11))].count;
            if (!held) set_sub(6);
            return;
        case 7: // how many
            if (step == 0) {
                model(engine, "j_shwin", 0, "SQ_BWINOPEN");
                auto& count = add_child<ShopSelect52>(engine);
                count.mode = held;
                count.position = {52.0f, -104.0f, 0.0f};
                count.remember("STWINFONT29", "font100");
                text_script(engine, "txt_shop.lua", 1572);
                text_script(engine, "txt_shop.lua", 1505);
                selection = held - 1;
                show_amount(engine);
                mark();
                set_step(1);
            }
            if (!child_done()) {
                if (selection != -1) {
                    if (last != selection) show_amount(engine);
                    last = selection;
                }
                return;
            }
            if (selection == -1) {
                hide_amount(engine);
                set_sub(6);
                return;
            }
            set_sub(8);
            amount = held - selection;
            return;
        case 8: // confirm
            if (step == 0) {
                shop_message(*this, engine, 1561);
                mark();
                set_step(1);
                return;
            }
            if (step == 1) {
                if (!child_done()) return;
                add_child<ShopSelect01>(engine).mode = 1562;
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            hide_amount(engine);
            if (selection == 0) {
                p->bits = std::min(p->bits + price * amount, 9999999);
                subwnd(engine, "UPDATE");
                p->disks[std::size_t(std::clamp(line, 0, 11))].count -= amount;
            }
            set_sub(6);
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrlSell (8039F6E0): Devices or Disks, the empty kind greyed.
// ---------------------------------------------------------------------------
struct ShopSell : Window {
    int empty{}; // +40 on entry: 1 no devices, 2 no disks
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        if (!p) {
            next_state(2);
            return;
        }
        if (state == 0) {
            next_state(1);
            const bool devices = !p->devices.empty();
            const bool disks = std::any_of(p->disks.begin(), p->disks.end(), [](const auto& d) { return d.count > 0; });
            if (devices || disks) {
                empty = !devices ? 1 : !disks ? 2 : 0;
                set_sub(0);
            } else {
                set_sub(3);
            }
        }
        switch (sub) {
        case 0:
            if (step == 0) {
                auto& kind = add_child<ShopSelectB0>(engine);
                if (empty == 2) kind.enabled = {1, 0};
                else if (empty == 1) kind.enabled = {0, 1};
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            textctl(engine, "STWINFONT29", "font000", "DEFAULTCOLOR");
            textctl(engine, "STWINFONT29", "font001", "DEFAULTCOLOR");
            if (selection == 1) set_sub(2);
            else if (selection == 0) set_sub(1);
            else next_state(2);
            return;
        case 1:
        case 2:
            if (step == 0) {
                if (sub == 1) add_child<DeviceSell>(engine);
                else add_child<DiskSell>(engine);
                mark();
                set_step(1);
            }
            if (child_done()) next_state(0);
            return;
        case 3:
            if (step == 0) {
                shop_message(*this, engine, 1576);
                mark();
                set_step(1);
            }
            if (child_done()) {
                next_state(2);
                if (parent) parent->selection = -1;
            }
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrl50 (803967A8): the disk shop, buy conItem 0..9 or sell.
// ---------------------------------------------------------------------------
struct ShopDisk : Window {
    int line{};     // +84
    int record{};   // +88
    int held{};     // +92
    int amount{};   // +96
    int price{};    // +100
    int message{};  // +40 for sub 5

    // 80397ABC / 80397BE8: the count and total of the purchase.
    void show_amount(Engine& engine) {
        auto* db = engine.get<DatabaseManager>();
        const int count = 9 - held - selection;
        const int unit = db ? db->number("conItem", record, "HANBAI").value_or(0) : 0;
        textset(engine, "STWINFONT32", "font010", std::to_string(count));
        textset(engine, "STWINFONT32", "font021", std::to_string(count * unit));
    }
    void hide_amount(Engine& engine) {
        for (const char* l : {"font020", "font010", "font021"}) switch_line(engine, "STWINFONT32", l, false);
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) {
            next_state(2);
            return;
        }
        if (state == 0) {
            close_models(engine);
            models.clear();
            clear_lines(engine);
            next_state(1);
        }
        switch (sub) {
        case 0: // Buy / Sell / Exit
            if (step == 0) {
                add_child<ShopSelect01>(engine).mode = 1550;
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            if (selection == 1) {
                set_sub(4);
            } else if (selection == 0) {
                set_sub(1);
                line = 0;
            } else {
                next_state(2);
            }
            return;
        case 1: // the list
            if (step == 0) {
                open_model(engine, "j_shwin", 0, "SQ_WAIT", "SQ_CLOSE");
                open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", kArrowLeft);
                open_model(engine, "i_stcursorright", 0, "SQ_HIDE", "SQ_CLOSE", kArrowRight);
                remember("STWINFONT13", "font000");
                text_script(engine, "txt_shop.lua", 1501);
                for (int i = 0; i < 10; ++i) {
                    text(engine, "STWINFONT14", kLines[i], db->text("conItem", i, "NAME"), "LEFT");
                    text(engine, "STWINFONT17", kLines[i], std::to_string(db->number("conItem", i, "HANBAI").value_or(0)), "RIGHT");
                }
                text(engine, "STWINFONT29", "font000", " ", "LEFT");
                set_step(1);
            }
            if (step == 1) {
                list_marker(engine, *this, -1);
                model(engine, "j_shwin", 0, "SQ_CWINOPEN");
                auto& select = add_child<SelectE1>(engine);
                select.position = kShopList;
                select.initial = line;
                select.count = 10;
                select.remember("STWINFONT29", "font100");
                select.text_script(engine, "txt_shop.lua", 1553);
                mark();
                set_step(2);
                selection = line;
            }
            if (step == 2) {
                if (selection != -1) {
                    textset(engine, "STWINFONT29", "font000", db->text("conItem", selection, "TXT"));
                    switch_line(engine, "STWINFONT29", "font000", true);
                }
                set_step(3);
                last = selection;
            }
            if (!child_done()) {
                if (selection != -1 && selection != last) set_step(2);
                return;
            }
            if (selection == -1) {
                next_state(0);
                return;
            }
            line = selection;
            list_marker(engine, *this, line);
            record = selection;
            held = p->disk_count(record);
            if (held == 9) {
                set_sub(5);
                message = 1571;
            } else {
                set_sub(2);
            }
            return;
        case 2: // how many
            if (step == 0) {
                model(engine, "j_shwin", 0, "SQ_DWINOPEN");
                auto& count = add_child<ShopSelect52>(engine);
                count.mode = 9 - held;
                count.position = {52.0f, -69.0f, 0.0f};
                count.remember("STWINFONT29", "font100");
                text_script(engine, "txt_shop.lua", 1572);
                text_script(engine, "txt_shop.lua", 1502);
                selection = 8 - held;
                show_amount(engine);
                mark();
                set_step(1);
            }
            if (!child_done()) {
                if (selection != -1) {
                    if (last != selection) show_amount(engine);
                    last = selection;
                }
                return;
            }
            if (selection == -1) {
                hide_amount(engine);
                set_sub(1);
                set_step(1);
                return;
            }
            set_sub(3);
            last = 9 - held - selection;
            return;
        case 3: // confirm
            if (step == 0) {
                price = db->number("conItem", record, "HANBAI").value_or(0);
                amount = last;
                if (price * amount > p->bits) {
                    set_sub(5);
                    message = 1570;
                    return;
                }
                set_step(1);
                return;
            }
            if (step == 1) {
                shop_message(*this, engine, 1555);
                mark();
                set_step(2);
            }
            if (step == 2) {
                if (!child_done()) return;
                add_child<ShopSelect01>(engine).mode = 1556;
                mark();
                set_step(3);
                return;
            }
            if (!child_done()) return;
            hide_amount(engine);
            if (selection == 0) {
                p->bits -= price * amount;
                subwnd(engine, "UPDATE");
                p->disk_count(record) += amount;
            }
            set_sub(1);
            set_step(1);
            return;
        case 4: // sell
            if (step == 0) {
                add_child<ShopSell>(engine);
                mark();
                set_step(1);
            }
            if (child_done()) set_sub(0);
            return;
        case 5: // a message, back to the list
            if (step == 0) {
                shop_message(*this, engine, message);
                mark();
                set_step(1);
            }
            if (child_done()) {
                hide_amount(engine);
                set_sub(1);
            }
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrlSales (803A2920): the stock of the weapon, armor, chip and
// junk shops (GETSHOPDATA), 12 lines a page; bought lines turn into the
// sold-out abChip 185 ("----").
// ---------------------------------------------------------------------------
struct ShopSales : Window {
    int page{};                       // +84
    int pages = 1;                    // +88
    int line{};                       // +92
    SubWindowManager::Stock* stock{}; // +96
    std::vector<std::uint8_t> buyable; // +100
    int message{};                    // +40 for sub 2

    static bool sold(const std::optional<ItemDevice>& d) { return d && d->type == 3 && d->record == 185; }
    const std::optional<ItemDevice>* entry(int at) const {
        return stock && at >= 0 && at < int(stock->size()) ? &(*stock)[std::size_t(at)] : nullptr;
    }
    // 803A3F14: pages up to the last stocked line; this page's lines run to
    // the first gap.
    int page_lines() {
        const int size = stock ? int(stock->size()) : 0;
        int used = size;
        for (int i = 0; i < size; ++i)
            if ((*stock)[std::size_t(i)]) used = i + 1;
        pages = used % 12 ? used / 12 + 1 : used / 12;
        int n = 12;
        for (int i = 0; i < 12; ++i) {
            const auto* e = entry(12 * page + i);
            if (!e || !*e) {
                n = i;
                break;
            }
        }
        n = std::clamp(n, 1, 12);
        buyable.clear();
        for (int i = 0; i < n; ++i) {
            const auto* e = entry(12 * page + i);
            buyable.push_back(e && *e && !sold(*e) ? 1 : 0);
        }
        return n;
    }
    // 803A41BC
    bool can_equip(Engine& engine, const ItemDevice& d) {
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) return false;
        if (d.type == 3) return true;
        const char* table = device_table(d);
        if (d.type == 1 || d.type == 2) return p->stats.level >= db->number(table, d.record, "LVPOINT").value_or(0);
        if (d.type != 0) return false;
        const int species = p->stats.species;
        const std::uint32_t soubi = std::uint32_t(db->number(table, d.record, "SOUBI").value_or(0));
        const bool fits = species >= 0 && species < 32 && (soubi & (1u << species)) != 0;
        const int skill = db->number(table, d.record, "SKILL").value_or(0);
        const int have = skill >= 0 && skill < int(p->stats.skill.size()) ? p->stats.skill[std::size_t(skill)] : 0;
        return fits && have >= db->number(table, d.record, "SKILLPOINT").value_or(0);
    }
    // 803A4344: the stats with the cursor's device in the first slot of its
    // kind (weapons and boards without chips).
    void preview(Engine& engine, bool on) {
        auto* p = player_slot(engine);
        const auto* e = on ? entry(12 * page + selection) : nullptr;
        if (!p || !e || !*e) {
            subwnd(engine, "OFPREVIEW");
            return;
        }
        const auto saved_socket = p->power_socket, saved_power = p->power_pocket;
        const auto saved_main = p->main_slot, saved_armor = p->main_pocket, saved_sub = p->sub_slot;
        const int index = int(p->devices.size());
        p->devices.push_back(**e);
        if ((*e)->type == 0) {
            std::fill(p->power_pocket.begin(), p->power_pocket.end(), -1);
            p->power_socket[0] = index;
        } else if ((*e)->type == 1) {
            std::fill(p->main_pocket.begin(), p->main_pocket.end(), -1);
            p->main_slot[0] = index;
        } else if ((*e)->type == 2) {
            p->sub_slot[0] = index;
        }
        recalc(engine);
        subwnd(engine, "ONPREVIEW");
        p->power_socket = saved_socket;
        p->power_pocket = saved_power;
        p->main_slot = saved_main;
        p->main_pocket = saved_armor;
        p->sub_slot = saved_sub;
        p->devices.pop_back();
        recalc(engine);
    }
    // 803A3910
    void fill(Engine& engine, bool names, bool description) {
        auto* db = engine.get<DatabaseManager>();
        if (!db) return;
        if (names) {
            for (int i = 0; i < 12; ++i) {
                const auto* e = entry(12 * page + i);
                const bool shown = e && *e;
                if (shown) {
                    const auto& d = **e;
                    textset(engine, "STWINFONT14", kLines[i], db->text(device_table(d), d.record, "NAME"));
                    const Vec3f rgb = device_color(d);
                    textctl(engine, "STWINFONT14", kLines[i], "COLOR", &rgb);
                    textset(engine, "STWINFONT17", kLines[i], sold(*e) ? std::string("----") : std::to_string(device_price(engine, d, true)));
                }
                switch_line(engine, "STWINFONT14", kLines[i], shown);
                switch_line(engine, "STWINFONT17", kLines[i], shown);
            }
            textset(engine, "STWINFONT30", "font000", std::string(1, char('1' + page)));
            textset(engine, "STWINFONT30", "font001", std::string(1, char('0' + pages)));
        }
        if (description) {
            const auto* e = entry(12 * page + selection);
            const bool shown = e && *e;
            if (shown) {
                const auto& d = **e;
                std::string txt = db->text(device_table(d), d.record, "TXT");
                if (d.type == 0 || d.type == 1) txt += char('0' + d.rank);
                textset(engine, "STWINFONT29", "font000", txt);
            }
            preview(engine, shown);
            switch_line(engine, "STWINFONT29", "font000", shown);
        }
    }
    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            for (const char* l : kLines) textctl(engine, "STWINFONT14", l, "DEFAULTCOLOR");
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
            stock = manager ? manager->shop_stock(mode) : nullptr;
            if (!stock) {
                next_state(2);
                return;
            }
            open_model(engine, "j_shwin", 0, "SQ_AWINOPEN", "SQ_CLOSE");
            page_lines();
            open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", kArrowLeft);
            open_model(engine, "i_stcursorright", 0, pages <= 1 ? "SQ_HIDE" : "SQ_SELECT", "SQ_CLOSE", kArrowRight);
            remember("STWINFONT29", "font100");
            remember("STWINFONT13", "font000");
            const int base = mode == 6 ? 1600 : mode == 7 ? 1700 : mode == 8 ? 1800 : 1900;
            text_script(engine, "txt_shop.lua", base + 53);
            text_script(engine, "txt_shop.lua", base + 1);
            for (const char* l : kLines) {
                text(engine, "STWINFONT14", l, " ", "LEFT");
                text(engine, "STWINFONT17", l, " ", "RIGHT");
            }
            text(engine, "STWINFONT30", "font000", " ", "RIGHT");
            text(engine, "STWINFONT30", "font001", " ", "RIGHT");
            line = 0;
            next_state(1);
        }
        switch (sub) {
        case 0: // the stock
            if (step == 0) {
                auto& select = add_child<SelectE1>(engine);
                selection = line;
                select.position = kShopList;
                select.initial = line;
                select.text(engine, "STWINFONT29", "font000", " ", "LEFT");
                select.count = page_lines();
                select.enabled = buyable;
                mark();
                set_step(1);
            }
            if (step == 1) {
                fill(engine, true, true);
                set_step(2);
                return;
            }
            if (!child_done()) {
                if (selection == -1) return;
                if (selection == last) {
                    auto* select = find_child<SelectE1>();
                    if (!select || !select_idle(engine, *this, *select)) return;
                    if (turn_page(engine, *this, page, pages, false) || turn_page(engine, *this, page, pages, true)) {
                        select->next_state(4);
                        set_step(1);
                        select->count = page_lines();
                        select->enabled = buyable;
                    }
                } else {
                    fill(engine, false, true);
                    last = selection;
                }
                return;
            }
            preview(engine, false);
            if (selection == -1) {
                if (parent) parent->selection = -1;
                next_state(2);
                return;
            }
            if (const auto* e = entry(12 * page + selection); e && *e) {
                const int price = device_price(engine, **e, true);
                if (p->devices.size() >= 36) {
                    set_sub(2);
                    message = 1670;
                } else if (p->bits < price) {
                    set_sub(2);
                    message = 1570;
                } else {
                    set_sub(1);
                    list_marker(engine, *this, selection);
                }
            }
            line = selection;
            return;
        case 1: // confirm the purchase
            if (step == 0) {
                const auto* e = entry(line + 12 * page);
                const ItemDevice d = e && *e ? **e : ItemDevice{};
                if (can_equip(engine, d)) {
                    shop_message(*this, engine, d.type == 0 ? 1655 : d.type == 1 || d.type == 2 ? 1755 : 1855);
                } else {
                    auto& note = shop_message(*this, engine, 0);
                    note.remember("STWINFONT29", "font000");
                    text_script(engine, "txt_shop.lua", 2040);
                }
                mark();
                set_step(1);
                return;
            }
            if (step == 1) {
                if (!child_done()) return;
                const auto* e = entry(line + 12 * page);
                const int type = e && *e ? (*e)->type : 3;
                add_child<ShopSelect01>(engine).mode = type == 0 ? 1656 : type == 1 || type == 2 ? 1756 : 1856;
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            if (selection == 0) {
                const int at = line + 12 * page;
                if (at >= 0 && at < int(stock->size()) && (*stock)[std::size_t(at)]) {
                    const ItemDevice bought = *(*stock)[std::size_t(at)];
                    p->bits -= device_price(engine, bought, true);
                    p->devices.push_back(bought);
                    (*stock)[std::size_t(at)] = ItemDevice{3, 185};
                    subwnd(engine, "UPDATE");
                }
            }
            set_sub(0);
            list_marker(engine, *this, -1);
            return;
        case 2: // a message
            if (step == 0) {
                shop_message(*this, engine, message);
                mark();
                set_step(1);
            }
            if (child_done()) set_sub(0);
            return;
        }
    }
};

// beNDMWShopDevice (803A6914): the weapon (6), armor (7) and chip (8) shops.
struct ShopDevice : Window {
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) next_state(1);
        if (sub == 0) {
            if (step == 0) {
                add_child<ShopSelect01>(engine).mode = mode == 6 ? 1650 : mode == 7 ? 1750 : 1850;
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            if (selection == 1) set_sub(2);
            else if (selection == 0) set_sub(1);
            else next_state(2);
            return;
        }
        if (step == 0) {
            if (sub == 1) add_child<ShopSales>(engine).mode = mode;
            else add_child<ShopSell>(engine);
            mark();
            set_step(1);
        }
        if (child_done()) set_sub(0);
    }
};

// 803A99C8: the other balance on STWINFONT25 (font%03d for mode 1,
// font01%d otherwise).
void show_balance(Engine& engine, const Window& window, int mode, int value) {
    const char* format = mode == 1 ? "font%03d" : "font01%d";
    for (int i = 0; i < 7; ++i) window.switch_line(engine, "STWINFONT25", line_name(format, i), false);
    const std::string text = std::to_string(value);
    const int length = int(text.size());
    for (int k = length - 1; k > -1; --k) {
        const auto line = line_name(format, k);
        window.textset(engine, "STWINFONT25", line, std::string(1, text[std::size_t(length - 1 - k)]));
        window.switch_line(engine, "STWINFONT25", line, true);
    }
}

// ---------------------------------------------------------------------------
// beNDMWShopCtrlEditBit (8039E478): a seven-digit amount; mode 0 deposits,
// 1 withdraws (the bank), 2 donates to the junk shop after a Yes/No. The
// amount goes to the parent's selection, -1 when cancelled.
// ---------------------------------------------------------------------------
struct ShopEditBit : Window {
    int amount{};     // +84
    int amount_max{}; // +88

    const char* digit_lines() const { return mode == 1 ? "font01%d" : "font%03d"; }
    void update(Engine& engine) override {
        if (state == 2) {
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            for (int i = 0; i < 7; ++i)
                for (const char* format : {"font%03d", "font01%d", "font02%d"})
                    switch_line(engine, "STWINFONT25", line_name(format, i), false);
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
            if (mode == 1) {
                open_model(engine, "j_shwin", 0, "SQ_BITDRAWOUTOPEN", "SQ_CLOSE");
                for (const char* line : {"font000", "font001", "font002"}) remember("STWINFONT36", line);
                text_script(engine, "txt_shop.lua", 1375);
                remember("STWINFONT29", "font100");
                text_script(engine, "txt_shop.lua", 1367);
            } else if (mode == 2) {
                open_model(engine, "j_shwin", 0, "SQ_JUNKOPEN", "SQ_CLOSE");
                remember("STWINFONT36", "font000");
                text_script(engine, "txt_shop.lua", 1378);
            } else {
                open_model(engine, "j_shwin", 0, "SQ_BITPUTINOPEN", "SQ_CLOSE");
                for (const char* line : {"font000", "font001"}) remember("STWINFONT36", line);
                text_script(engine, "txt_shop.lua", 1301);
                remember("STWINFONT29", "font100");
                text_script(engine, "txt_shop.lua", 1360);
            }
            next_state(1);
        }
        switch (step) {
        case 0:
            add_child<ShopSelect34>(engine).mode = mode;
            mark();
            set_step(1);
            return;
        case 1: {
            if (children.empty()) return;
            const Window& digits = *children.front();
            amount = 0;
            const int bits = p->bits, bank = p->bank_bits;
            if (mode == 1) {
                show_digits(engine, digits, amount, 1, "font01%d");
                amount_max = std::min(bank, 9999999 - bits);
                show_balance(engine, digits, mode, bits);
                show_digits(engine, digits, bank, 1, "font02%d");
            } else if (mode == 2) {
                show_digits(engine, digits, amount, 1);
                amount_max = std::min(bits, 999999999 - bits);
            } else {
                show_digits(engine, digits, amount, 1);
                amount_max = std::min(bits, 9999999 - bank);
                show_balance(engine, digits, mode, bank);
            }
            set_step(2);
            return;
        }
        case 2:
            if (child_done()) {
                if (selection == -1 || amount < 1) {
                    if (parent) parent->selection = -1;
                    next_state(2);
                } else {
                    if (parent) parent->selection = amount;
                    set_step(3);
                    switch_line(engine, "STWINFONT29", "font100", false);
                }
                return;
            }
            if (selection == -1 || children.empty()) return;
            if (last != selection) {
                show_digits(engine, *children.front(), amount, 7 - selection, digit_lines());
                last = selection;
                return;
            }
            {
                Packet now;
                engine.messenger().send(engine, "SELECTCTRL", std::string("i_stcursor03"), player, "ISSELECTNOW", now);
                if (now.bools.empty() || !now.bools[0]) return;
                const auto& pad = engine.pads()[std::size_t(engine.pad_reader_slot(player, player))]; // (the window owner, or the leader for a leaving one: 6.5)
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
                    show_digits(engine, *children.front(), amount, 7 - selection, digit_lines());
                    Packet sound;
                    sound.strings.emplace_back("CURSOR_MOVE");
                    engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
                }
            }
            return;
        case 3:
            shop_message(*this, engine, mode == 1 ? 1369 : mode == 2 ? 1961 : 1362);
            mark();
            set_step(4);
            return;
        case 4:
            if (!child_done()) return;
            if (mode == 2) set_step(5);
            else next_state(2);
            return;
        case 5:
            add_child<ShopSelect01>(engine).mode = 1962;
            mark();
            set_step(6);
            return;
        case 6:
            if (!child_done()) return;
            if (selection != 0 && parent) parent->selection = -1;
            next_state(2);
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrl90 (80397E60): the junk shop. Donations count into
// COUNTER[1]; each of the thresholds 1000 .. 3956000 opens a better stock
// (junkexit.lua runs when a donation crosses one).
// ---------------------------------------------------------------------------
struct ShopJunk : Window {
    int line{};    // +84: the menu line
    int counter{}; // +88: COUNTER[1] when the stock was made
    SubWindowManager::Stock* stock{}; // +92

    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        auto* sys = engine.get<GameSystem>();
        if (!p || !sys) {
            next_state(2);
            return;
        }
        if (state == 0) {
            if (manager) {
                stock = manager->shop_stock(9);
                counter = manager->junk_counter();
            }
            next_state(1);
        }
        switch (sub) {
        case 0: // Donate / Buy / Exit
            if (step == 0) {
                auto& menu = add_child<ShopSelect01>(engine);
                menu.mode = 1950;
                menu.initial = line;
                const bool has_stock = stock && std::any_of(stock->begin(), stock->end(), [](const auto& e) { return e.has_value(); });
                menu.enabled = {std::uint8_t(counter < 3956000 ? 1 : 0), std::uint8_t(has_stock ? 1 : 0), 1};
                for (std::size_t i = 0; i < 3; ++i)
                    textctl(engine, "STWINFONT29", kLines[i], menu.enabled[i] ? "DEFAULTCOLOR" : "CANCELCOLOR");
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            for (std::size_t i = 0; i < 3; ++i) textctl(engine, "STWINFONT29", kLines[i], "DEFAULTCOLOR");
            if (selection == 1) {
                line = 1;
                set_sub(2);
            } else if (selection == 0) {
                line = 0;
                set_sub(1);
            } else {
                next_state(2);
            }
            return;
        case 1: // donate
            if (step == 0) {
                add_child<ShopEditBit>(engine).mode = 2;
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            if (selection == -1) {
                set_sub(0);
                return;
            }
            {
                int& total = engine.counters()[1];
                const int before = counter;
                total += selection;
                p->bits -= selection;
                subwnd(engine, "UPDATE");
                set_sub(0);
                static const int thresholds[] = {1000, 6000, 26000, 86000, 206000, 456000, 956000, 1956000, 3956000};
                for (const int threshold : thresholds)
                    if (before < threshold && total >= threshold) {
                        engine.run_script("junkexit.lua");
                        set_sub(3);
                        return;
                    }
            }
            return;
        case 2: // buy
            if (step == 0) {
                add_child<ShopSales>(engine).mode = 9;
                mark();
                set_step(1);
            }
            if (child_done()) set_sub(0);
            return;
        case 3: // a new stock
            if (step == 0) {
                shop_message(*this, engine, 1970);
                mark();
                set_step(1);
            }
            if (child_done()) next_state(2);
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrl30 (80394FC4): the bank. Deposit or withdraw devices
// (BANKDEVICE, at most 96) or bits (BANKBIT).
// ---------------------------------------------------------------------------
struct ShopBank : Window {
    int withdraw{}; // +84: 0 deposit, 1 withdraw
    int message{};  // +40 for the messages

    DeviceList* list() { return find_child<DeviceList>(); }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        if (!p) {
            next_state(2);
            return;
        }
        if (state == 0) next_state(1);
        switch (sub) {
        case 0: // Deposit / Withdraw / Exit
            if (step == 0) {
                auto& menu = add_child<ShopSelect01>(engine);
                menu.mode = 1350;
                menu.initial = withdraw;
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            if (selection < 0 || selection > 1) {
                next_state(2);
                return;
            }
            withdraw = selection;
            set_sub(1);
            return;
        case 1: // Devices / Bits
            if (step == 0) {
                add_child<ShopSelect01>(engine).mode = 1355;
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            if (selection == 1) {
                set_sub(withdraw ? 5 : 4);
            } else if (selection == 0) {
                if (!withdraw) {
                    set_sub(2);
                } else if (!p->bank_devices.empty()) {
                    set_sub(3);
                } else {
                    set_sub(6);
                    message = 1374;
                }
            } else {
                set_sub(0);
            }
            return;
        case 2: // devices in (2) or out (3)
        case 3:
            switch (step) {
            case 0: {
                auto& folder = add_child<DeviceList>(engine);
                folder.auto_close = false;
                folder.equip_disable = true;
                folder.bank = sub == 3;
                text_script(engine, "txt_shop.lua", sub == 3 ? 1364 : 1357);
                mark();
                set_step(1);
                return;
            }
            case 1: {
                auto* folder = list();
                if (!folder) return;
                if (selection == -1) {
                    folder->next_state(2);
                    set_step(5);
                    switch_line(engine, "STWINFONT29", "font100", false);
                    return;
                }
                if (folder->state != 1 || folder->sub != 2) return;
                const int at = selection;
                if (sub == 3) {
                    if (int(p->devices.size()) >= 36) {
                        set_step(2);
                        message = 1365;
                        return;
                    }
                    if (at >= 0 && at < int(p->bank_devices.size())) {
                        p->devices.push_back(p->bank_devices[std::size_t(at)]);
                        p->bank_devices.erase(p->bank_devices.begin() + at);
                    }
                    if (!p->bank_devices.empty()) {
                        folder->set_step(1);
                    } else {
                        set_step(2);
                        message = 1374;
                        folder->fill(engine, true, false);
                    }
                } else {
                    if (int(p->bank_devices.size()) >= 96) {
                        set_step(2);
                        message = 1358;
                        return;
                    }
                    if (at >= 0 && at < int(p->devices.size()) && !p->devices[std::size_t(at)].flag) {
                        p->bank_devices.push_back(p->devices[std::size_t(at)]);
                        p->remove_device(at);
                    }
                    folder->set_step(1);
                }
                return;
            }
            case 2:
                switch_line(engine, "STWINFONT29", "font100", false);
                shop_message(*this, engine, message);
                mark();
                set_step(3);
                return;
            case 3:
                if (!child_done()) return;
                if (auto* folder = list()) {
                    folder->next_state(2);
                    set_step(5);
                }
                return;
            case 5:
                if (!list()) set_sub(0);
                return;
            }
            return;
        case 4: // bits in (4) or out (5)
        case 5:
            if (step == 0) {
                add_child<ShopEditBit>(engine).mode = withdraw;
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            if (selection != -1) {
                if (withdraw) {
                    p->bits += selection;
                    p->bank_bits -= selection;
                } else {
                    p->bits -= selection;
                    p->bank_bits += selection;
                }
                subwnd(engine, "UPDATE");
            }
            set_sub(0);
            return;
        case 6: // a message
            if (step == 0) {
                shop_message(*this, engine, message);
                mark();
                set_step(1);
            }
            if (child_done()) set_sub(0);
            return;
        }
    }
};

// The X data stats in list order with their UPCNT slots and limits.
struct XdataStat {
    const char* name;
    int upcnt, limit;
};
constexpr XdataStat kXdataStats[7] = {{"TSUYOSA", 2, 9999}, {"MAMORI", 3, 9999},  {"KASHIKOSA", 4, 9999},
                                      {"SEISHIN", 5, 9999}, {"SUBAYASA", 6, 9999}, {"MHP", 0, 99999},
                                      {"MMP", 1, 99999}};

int xdata_stat(const GameSystem::PlayerSlot& p, int stat) {
    const auto& s = p.stats;
    switch (stat) {
    case 0: return s.tsuyosa;
    case 1: return s.mamori;
    case 2: return s.kashikosa;
    case 3: return s.seishin;
    case 4: return s.subayasa;
    case 5: return p.max_hp;
    default: return p.max_mp;
    }
}

// 80394814: one point costs a fifth of the stat above 45, rounded up, 1..400.
int xdata_cost(int value) {
    const int x = value - 45;
    const int cost = x / 5 + (x % 5 != 0 ? 1 : 0);
    return cost > 400 ? 400 : cost >= 0 ? cost : 1;
}

// ---------------------------------------------------------------------------
// beNDMWShopSelect11 (803A8770): the seven stats with their X data cost,
// "----" and orange once a stat reached its limit.
// ---------------------------------------------------------------------------
struct ShopSelect11 : Select {
    void open(Engine& engine) override {
        open_model(engine, "j_shwin", 0, "SQ_XDATASTATUSOPEN", "SQ_CLOSE");
        remember("STWINFONT29", "font100");
        text_script(engine, "txt_shop.lua", 1153);
        remember("STWINFONT13", "font000");
        remember("STWINFONT35", "font001");
        text_script(engine, "txt_shop.lua", 1101);
        for (int i = 0; i < 7; ++i) remember("STWINFONT14", kLines[i]);
        text_script(engine, "txt_shop.lua", 1103);
        for (int i = 0; i < 7; ++i) text(engine, "STWINFONT19", kLines[i], " ", "RIGHT");
        text(engine, "STWINFONT29", "font000", " ", "LEFT");
        recalc(engine);
        show_costs(engine);
        text_script(engine, "txt_shop.lua", mode + 1154);
        cursor = "i_stcursor01";
        count = 7;
        position = kShopList;
        initial = mode;
        no_cancel = false;
    }
    // 803A9494
    void show_costs(Engine& engine) {
        auto* p = player_slot(engine);
        if (!p) return;
        static const Vec3f limited{0.9f, 0.4f, 0.2f};
        for (int i = 0; i < 7; ++i) {
            const int value = xdata_stat(*p, i);
            const bool open = value < kXdataStats[i].limit;
            textset(engine, "STWINFONT19", kLines[i], open ? std::to_string(xdata_cost(value)) : std::string("----"));
            if (open) textctl(engine, "STWINFONT14", kLines[i], "DEFAULTCOLOR");
            else textctl(engine, "STWINFONT14", kLines[i], "COLOR", &limited);
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrlXdataChip (803A4C94): ability chips raised to their next
// record for NEXTX X data.
// ---------------------------------------------------------------------------
struct XdataChip : Window {
    int page{};             // +84
    int pages = 1;          // +88
    int line{};             // +92
    std::vector<int> chips; // +96: DEVICEFOLDER indices of the chips
    int message{};          // +40 for subs 10 and 11

    // 803A64F8
    int page_lines() {
        const int size = int(chips.size());
        pages = size % 12 ? size / 12 + 1 : size / 12;
        const int rest = size - 12 * page;
        const int n = rest < 12 ? rest % 12 : 12;
        return n < 1 ? 1 : n;
    }
    ItemDevice* chip(Engine& engine, int at) {
        auto* p = player_slot(engine);
        if (!p || at < 0 || at >= int(chips.size())) return nullptr;
        const int index = chips[std::size_t(at)];
        return index >= 0 && index < int(p->devices.size()) ? &p->devices[std::size_t(index)] : nullptr;
    }
    // 803A659C
    void fill(Engine& engine, bool names, bool description) {
        auto* db = engine.get<DatabaseManager>();
        if (!db) return;
        if (names) {
            for (int i = 0; i < 12; ++i) {
                const auto* d = chip(engine, i + 12 * page);
                if (d) {
                    textset(engine, "STWINFONT14", kLines[i], db->text("abChip", d->record, "NAME"));
                    const int next = db->number("abChip", d->record, "NEXTX").value_or(-1);
                    textset(engine, "STWINFONT17", kLines[i], next <= 0 ? std::string("----") : std::to_string(next));
                }
                switch_line(engine, "STWINFONT14", kLines[i], d != nullptr);
                switch_line(engine, "STWINFONT17", kLines[i], d != nullptr);
            }
            textset(engine, "STWINFONT30", "font000", std::string(1, char('1' + page)));
            textset(engine, "STWINFONT30", "font001", std::string(1, char('0' + pages)));
        }
        if (description) {
            if (const auto* d = chip(engine, 12 * page + selection)) {
                textset(engine, "STWINFONT29", "font000", db->text("abChip", d->record, "TXT"));
                switch_line(engine, "STWINFONT29", "font000", true);
            }
        }
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) {
            next_state(2);
            return;
        }
        if (state == 0) {
            remember("STWINFONT13", "font000");
            remember("STWINFONT35", "font000");
            for (const char* l : kLines) {
                text(engine, "STWINFONT14", l, " ", "LEFT");
                text(engine, "STWINFONT17", l, " ", "RIGHT");
            }
            text(engine, "STWINFONT29", "font000", " ", "LEFT");
            text(engine, "STWINFONT29", "font100", " ", "LEFT");
            text(engine, "STWINFONT30", "font000", " ", "CENTER");
            text(engine, "STWINFONT30", "font001", " ", "CENTER");
            next_state(1);
        }
        switch (sub) {
        case 0: {
            chips.clear();
            for (std::size_t i = 0; i < p->devices.size(); ++i)
                if (p->devices[i].type == 3) chips.push_back(int(i));
            if (chips.empty()) {
                set_sub(10);
                message = 1169;
                return;
            }
            const int count = page_lines();
            if (step == 0) {
                open_model(engine, "j_shwin", 0, "SQ_XDATAITEMOPEN", "SQ_CLOSE");
                text_script(engine, "txt_shop.lua", 1110);
                open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", kArrowLeft);
                open_model(engine, "i_stcursorright", 0, pages <= 1 ? "SQ_HIDE" : "SQ_SELECT", "SQ_CLOSE", kArrowRight);
            }
            auto& select = add_child<SelectE1>(engine);
            select.position = kShopList;
            select.count = count;
            select.initial = line;
            selection = line;
            mark();
            set_sub(1);
        }
            [[fallthrough]];
        case 1:
            if (selection != -1) fill(engine, true, true);
            set_sub(2);
            [[fallthrough]];
        case 2:
            if (!child_done()) {
                if (selection == -1) return;
                if (last == selection) {
                    auto* select = find_child<SelectE1>();
                    if (!select || !select_idle(engine, *this, *select)) return;
                    if (turn_page(engine, *this, page, pages, false) || turn_page(engine, *this, page, pages, true)) {
                        select->next_state(4);
                        set_sub(1);
                        select->count = page_lines();
                    }
                } else {
                    fill(engine, false, true);
                    last = selection;
                }
                return;
            }
            if (selection == -1) {
                next_state(2);
                return;
            }
            line = selection;
            set_sub(3);
            return;
        case 3: {
            const auto* d = chip(engine, line + 12 * page);
            const int next = d ? db->number("abChip", d->record, "NEXTX").value_or(-1) : -1;
            if (next == -1) {
                set_sub(11);
                message = 1170;
            } else if (p->xdata >= next) {
                set_sub(4);
            } else {
                set_sub(11);
                message = 1161;
            }
            return;
        }
        case 4:
            if (step == 0) {
                auto* d = chip(engine, line + 12 * page);
                if (!d) {
                    set_sub(0);
                    return;
                }
                auto& note = add_child<ShopSelect00>(engine);
                const int record = d->record;
                if (d->flag) {
                    d->record = record + 1;
                    recalc(engine);
                    subwnd(engine, "ONPREVIEW");
                    d->record = record;
                    recalc(engine);
                }
                note.text(engine, "STWINFONT29", "font000", db->text("abChip", record + 1, "NAME"), "LEFT");
                for (const char* l : {"font001", "font002", "font003"}) note.text(engine, "STWINFONT41", l, " ", "LEFT");
                note.text(engine, "STWINFONT29", "font002", " ", "LEFT");
                // The first stat the next chip raises names the message.
                static const char* raised[12] = {"TSUYOSA",  "KASHIKOSA", "SEISHIN",     "SUBAYASA",
                                                 "RES_FIRE", "RES_ICE",   "RES_THUNDER", "RES_DARK",
                                                 "CHIMEIRITSU", "HPDRAIN", "MPDRAIN",    "CHOHATURITSU"};
                static const int messages[8] = {11765, 11769, 11773, 11777, 11781, 11785, 11789, 11793};
                for (int i = 0; i < 12; ++i) {
                    const int value = db->number("abChip", record + 1, raised[i]).value_or(0);
                    if (value > 0) {
                        note.text(engine, "STWINFONT41", "font011", std::to_string(value), "RIGHT");
                        note.mode = messages[std::min(i, 7)];
                        break;
                    }
                }
                const int cost = db->number("abChip", record, "NEXTX").value_or(0);
                note.text(engine, "STWINFONT41", "font012", std::to_string(p->xdata - cost), "RIGHT");
                mark();
                set_step(1);
                return;
            }
            if (step == 1) {
                if (!child_done()) return;
                add_child<ShopSelect01>(engine).mode = 1171;
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            subwnd(engine, "OFPREVIEW");
            if (selection == 0) {
                if (auto* d = chip(engine, line + 12 * page)) {
                    p->xdata -= db->number("abChip", d->record, "NEXTX").value_or(0);
                    ++d->record;
                    recalc(engine);
                    subwnd(engine, "UPDATE");
                }
            }
            set_sub(0);
            set_step(1);
            return;
        case 10: // no chips
        case 11: // a message, back to the list
            if (step == 0) {
                auto& note = shop_message(*this, engine, message);
                if (sub == 10) note.line = "font100";
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            if (sub == 10) {
                next_state(2);
            } else {
                set_sub(0);
                set_step(1);
            }
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrl10 (80393B74): X data raises a stat by one point or an
// ability chip to its next record; leaving refills HP and MP.
// ---------------------------------------------------------------------------
struct ShopXdata : Window {
    int line{};          // +84
    int stat{};          // +88
    bool can_raise{};    // +40 while the stat message runs

    void refill(GameSystem::PlayerSlot& p) {
        p.hp = p.max_hp;
        p.mp = p.max_mp;
    }
    void update(Engine& engine) override {
        auto* p = player_slot(engine);
        if (state == 2) {
            recalc(engine);
            if (p) refill(*p);
        }
        if (close_states(engine)) return;
        if (!p) {
            next_state(2);
            return;
        }
        if (state == 0) next_state(1);
        switch (sub) {
        case 0: // Stats / Chips / Exit
            if (step == 0) {
                auto& menu = add_child<ShopSelect01>(engine);
                menu.mode = 1150;
                menu.initial = line;
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            stat = 0;
            if (selection == 1) {
                line = 1;
                set_sub(2);
            } else if (selection == 0) {
                line = 0;
                set_sub(1);
            } else {
                next_state(2);
            }
            return;
        case 1: // stats
            switch (step) {
            case 0:
                recalc(engine);
                add_child<ShopSelect11>(engine).mode = stat;
                mark();
                set_step(1);
                last = stat;
                return;
            case 1: {
                if (!child_done()) {
                    if (selection != -1 && selection != last) {
                        text_script(engine, "txt_shop.lua", selection + 1154);
                        last = selection;
                    }
                    return;
                }
                for (int i = 0; i < 7; ++i) textctl(engine, "STWINFONT14", kLines[i], "DEFAULTCOLOR");
                if (selection < 0 || selection > 6) {
                    set_sub(0);
                    return;
                }
                const int value = xdata_stat(*p, selection);
                const int cost = xdata_cost(value);
                stat = selection;
                auto& note = add_child<ShopSelect00>(engine);
                set_step(2);
                can_raise = false;
                if (value >= kXdataStats[stat].limit) {
                    note.mode = 1162;
                } else if (cost > p->xdata) {
                    note.mode = 1161;
                } else {
                    note.text(engine, "STWINFONT41", "font011", "+1", "RIGHT");
                    note.text(engine, "STWINFONT41", "font012", std::to_string(p->xdata - cost), "RIGHT");
                    for (int i = 0; i < 4; ++i) note.remember("STWINFONT41", kLines[i]);
                    note.remember("STWINFONT41", "font011");
                    note.remember("STWINFONT41", "font012");
                    note.remember("STWINFONT29", "font002");
                    note.mode = 11730 + 5 * stat;
                    can_raise = true;
                }
                mark();
                return;
            }
            case 2:
                if (!child_done()) return;
                if (!can_raise) {
                    set_sub(1);
                    return;
                }
                add_child<ShopSelect01>(engine).mode = 1163;
                mark();
                set_step(3);
                return;
            case 3: {
                if (!child_done()) return;
                if (selection != 0) {
                    set_sub(1);
                    return;
                }
                p->xdata -= xdata_cost(xdata_stat(*p, stat));
                ++p->upcnt[std::size_t(kXdataStats[stat].upcnt)];
                recalc(engine);
                refill(*p);
                subwnd(engine, "UPDATE");
                shop_message(*this, engine, 1165);
                mark();
                set_step(4);
                return;
            }
            case 4:
                if (!child_done()) return;
                add_child<ShopSelect01>(engine).mode = 1166;
                mark();
                set_step(5);
                return;
            case 5:
                if (!child_done()) return;
                if (selection != 0) set_sub(0);
                else set_sub(1);
                return;
            }
            return;
        case 2: // chips
            if (step == 0) {
                recalc(engine);
                add_child<XdataChip>(engine);
                mark();
                set_step(1);
            }
            if (!child_done()) return;
            set_sub(0);
            last = 1;
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrl40 (80395CB0): unknown devices identified for 300 bits.
// ---------------------------------------------------------------------------
struct ShopIdentify : Window {
    int device{}; // +84: the DEVICEFOLDER index

    // 803961C0: the record comes from coweapon / coarmor1 / coarmor2 row
    // 100 * band (band by LEVEL: 40, 100, 200, 300, 400, above), whose ID is
    // the count of the RATE rows that follow (out of 10000).
    void identify(Engine& engine, GameSystem::PlayerSlot& p, ItemDevice& d) {
        auto* db = engine.get<DatabaseManager>();
        if (!db) return;
        const int level = p.stats.level;
        const int band = level <= 40 ? 1 : level <= 100 ? 2 : level <= 200 ? 3 : level <= 300 ? 4 : level <= 400 ? 5 : 6;
        static const char* tables[3] = {"coweapon", "coarmor1", "coarmor2"};
        if (d.type < 0 || d.type > 2) return;
        const char* table = tables[d.type];
        const int base = 100 * band;
        const auto count = db->number(table, base, "ID");
        if (!count) {
            engine.note(std::string("CNOS: no ") + table + " table");
            return;
        }
        const int roll = int(fight::random() % 10000u);
        int sum = 0;
        for (int k = 1; k <= *count; ++k) {
            sum += db->number(table, base + k, "RATE").value_or(0);
            if (sum >= roll) {
                d.record = db->number(table, base + k, "ID").value_or(d.record);
                break;
            }
        }
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        if (!p) {
            next_state(2);
            return;
        }
        if (state == 0) {
            text(engine, "STWINFONT29", "font100", " ", "LEFT");
            next_state(1);
        }
        auto* folder = find_child<DeviceList>();
        switch (sub) {
        case 0: // the unknown devices
            if (step == 0) {
                auto& list = add_child<DeviceList>(engine);
                list.auto_close = false;
                list.unknown_enable = true;
                text_script(engine, "txt_shop.lua", 1450);
                mark();
                set_step(1);
                return;
            }
            if (!folder) return;
            if (selection == -1) {
                set_sub(3);
                return;
            }
            if (folder->state != 1 || folder->sub != 2) return;
            device = selection;
            set_sub(p->bits >= 300 ? 1 : 2);
            switch_line(engine, "STWINFONT29", "font100", false);
            return;
        case 1: // pay and identify
            if (step == 0) {
                shop_message(*this, engine, 1452);
                mark();
                set_step(1);
                return;
            }
            if (step == 1) {
                if (!child_done()) return;
                add_child<ShopSelect01>(engine).mode = 1453;
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            if (selection == 0) {
                p->bits -= 300;
                subwnd(engine, "UPDATE");
                if (device >= 0 && device < int(p->devices.size())) identify(engine, *p, p->devices[std::size_t(device)]);
            }
            selection = device;
            if (folder) {
                set_sub(0);
                set_step(1);
                folder->set_step(1);
                switch_line(engine, "STWINFONT29", "font100", false);
            }
            return;
        case 2: // not enough bits
            if (step == 0) {
                shop_message(*this, engine, 1451);
                mark();
                set_step(1);
            }
            if (child_done()) set_sub(3);
            return;
        case 3: // close the list, then the window
            if (step == 0) {
                if (folder) folder->next_state(2);
                set_step(1);
            } else if (!folder) {
                next_state(2);
            }
            return;
        }
    }
};

// ShopSelect02 (803A7840): a choice in the talk window (TALKFONT01,
// i_messcr): the Digi-Lab menu for mode players (3 or 4 lines), two lines of
// txt_shop.lua call mode, or the players (mode -1).
struct ShopSelect02 : Select {
    std::vector<int> players; // +112 for mode -1
    void open(Engine& engine) override {
        static const char* talk[4] = {"font01", "font02", "font03", "font04"};
        if (mode == -1) {
            auto* sys = engine.get<GameSystem>();
            auto* db = engine.get<DatabaseManager>();
            for (std::size_t i = 0; sys && db && i < players.size() && i < 4; ++i) {
                const auto& p = sys->players[std::size_t(std::clamp(players[i], 0, 3))];
                if (!p.name.empty()) text(engine, "TALKFONT01", talk[i], p.name, "LEFT");
                text(engine, "TALKFONT04", talk[i], db->text("shinka", p.stats.species, "NAME"), "LEFT");
            }
            count = int(players.size());
        } else if (mode >= 1 && mode <= 4) {
            const int lines = mode == 1 ? 3 : 4;
            for (int i = 0; i < lines; ++i) remember("TALKFONT01", talk[i]);
            text_script(engine, "txt_shop.lua", mode == 1 ? 2034 : 2030);
            count = lines;
        } else if (mode == 1262 || mode == 1268 || mode == 2003 || mode == 2005 || mode == 2008) {
            remember("TALKFONT01", "font01");
            remember("TALKFONT01", "font02");
            text_script(engine, "txt_shop.lua", mode);
            count = 2;
        }
        cursor = "i_messcr";
        position = {};
        initial = 0;
        no_cancel = mode == 2003;
    }
};

// ShopSelect03 (803A8564): a talk window message of txt_shop.lua call mode.
struct ShopSelect03 : Select {
    void open(Engine& engine) override {
        if (mode > 0) {
            remember("TALKFONT01", "font00");
            text_script(engine, "txt_shop.lua", mode);
        }
        cursor = "i_messky";
        count = 1;
        position = {};
        initial = 0;
        no_cancel = false;
    }
};

// Lines of the form values (804EC7B8): label, value.
constexpr const char* kFormLines[16][2] = {
    {"font000", "font030"}, {"font001", "font031"}, {"font010", "font040"}, {"font011", "font041"},
    {"font012", "font042"}, {"font013", "font043"}, {"font014", "font044"}, {"font020", "font050"},
    {"font021", "font051"}, {"font022", "font052"}, {"font023", "font053"}, {"font024", "font054"},
    {"font025", "font055"}, {"font026", "font056"}, {"font027", "font057"}, {"font028", "font058"}};

// ---------------------------------------------------------------------------
// beNDMWShopCtrlEvolve (8039B154): the forms reached (BASE_ISUSE, 8 a page)
// with their level, stats and weapon skills; choosing one digivolves.
// ---------------------------------------------------------------------------
struct ShopEvolve : Window {
    int page{};    // +84
    int pages = 1; // +88
    int line{};    // +92
    struct Form {
        int species{}, level{};
        std::string name;
        std::array<int, 16> values{}; // MHP, MMP, 5 stats, 9 skills
    };
    std::vector<Form> forms; // +96 (803AC44C / 803AC858), species 3 first

    void build(Engine& engine) {
        forms.clear();
        auto* sys = engine.get<GameSystem>();
        auto* db = engine.get<DatabaseManager>();
        if (!sys || !db) return;
        const auto& p = sys->players[std::size_t(std::clamp(player, 0, 3))];
        for (int i = 0; i < 16; ++i) {
            const auto found = p.base_isuse.find(i);
            if (found == p.base_isuse.end() || !found->second) continue;
            Form form{i, p.base_level[std::size_t(i)], db->text("shinka", i, "NAME"), {}};
            const auto stats = sys->form_stats(engine, player, i);
            for (std::size_t k = 0; k < 7; ++k) form.values[k] = stats[k];
            for (std::size_t k = 0; k < 9; ++k) form.values[7 + k] = p.base_skill[std::size_t(i)][k];
            if (i == 3) forms.insert(forms.begin(), form);
            else forms.push_back(form);
        }
    }
    int page_count() const {
        const int size = int(forms.size());
        if (!size) return 2;
        return page == 1 ? size - 8 : std::min(size, 8);
    }
    const Form* form(int at) const { return at >= 0 && at < int(forms.size()) ? &forms[std::size_t(at)] : nullptr; }
    // 8039C4DC
    void fill(Engine& engine, bool names, bool description) {
        if (names) {
            for (int i = 0; i < 8; ++i) {
                const auto* f = form(i + 8 * page);
                if (f) {
                    textset(engine, "STWINFONT20", kLines[i], f->name);
                    textset(engine, "STWINFONT43", kLines[i], "Lv");
                    textset(engine, "STWINFONT21", kLines[i], std::to_string(f->level));
                }
                for (const char* layer : {"STWINFONT20", "STWINFONT43", "STWINFONT21"})
                    switch_line(engine, layer, kLines[i], f != nullptr);
            }
            if (pages > 1) {
                textset(engine, "STWINFONT31", "font000", std::string(1, char('1' + page)));
                textset(engine, "STWINFONT31", "font001", std::string(1, char('0' + pages)));
            }
        }
        if (description && selection != -1) {
            if (const auto* f = form(8 * page + selection)) {
                for (std::size_t k = 0; k < 16; ++k) textset(engine, "STWINFONT22", kFormLines[k][1], std::to_string(f->values[k]));
                if (auto* db = engine.get<DatabaseManager>())
                    textset(engine, "STWINFONT29", "font000", db->text("shinka", f->species, "EXPLANATION"));
                switch_line(engine, "STWINFONT29", "font000", true);
            }
        }
    }
    // 8039CA4C: CURRENT2BASE, BASE2CURRENT, WEAPONEQUIPSTRONG, boards and
    // chips taken off.
    void evolve(Engine& engine, int species) {
        auto* sys = engine.get<GameSystem>();
        auto* p = player_slot(engine);
        if (!sys || !p) return;
        const int before = p->stats.species;
        sys->store_species(player);
        sys->load_species(engine, player, species);
        if (before != species) engine.port_event({PortEvent::Kind::digivolution, player, species, before, {}}); // observe-only
        recalc(engine);
        sys->equip_strongest_weapon(engine, player);
        for (auto* list : {&p->main_slot, &p->sub_slot, &p->power_pocket, &p->main_pocket})
            for (int& index : *list) {
                if (index >= 0 && index < int(p->devices.size())) p->devices[std::size_t(index)].flag = false;
                index = -1;
            }
        recalc(engine);
        subwnd(engine, "UPDATE");
        if (parent) status_panel(*parent, engine, player, true);
    }
    void cursor_ng(Engine& engine) {
        Packet sound;
        sound.strings.emplace_back("CURSOR_NG");
        engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        auto* p = player_slot(engine);
        auto* sys = engine.get<GameSystem>();
        if (!p || !sys) {
            next_state(2);
            return;
        }
        if (state == 0) {
            if (sub == 0) {
                open_model(engine, "i_messwn", 0, "SQ_STOPEN", "SQ_STCLOSE");
                set_sub(1);
            }
            if (model_in(engine, "i_messwn", 0, "SQ_STOPEN")) return;
            build(engine);
            pages = forms.size() <= 8 ? 1 : 2;
            page = line = 0;
            if (pages <= 1) {
                open_model(engine, "j_shwin", 0, "SQ_SINKAOFFOPEN", "SQ_CLOSE");
            } else {
                open_model(engine, "j_shwin", 0, "SQ_SINKAONOPEN", "SQ_CLOSE");
                open_model(engine, "i_stcursorleft", 0, "SQ_HIDE", "SQ_CLOSE", {-308.0f, 25.0f, 0.0f});
                open_model(engine, "i_stcursorright", 0, "SQ_SELECT", "SQ_CLOSE", {-51.0f, 25.0f, 0.0f});
                text(engine, "STWINFONT31", "font000", " ", "RIGHT");
                text(engine, "STWINFONT31", "font001", " ", "RIGHT");
            }
            remember("STWINFONT29", "font100");
            text_script(engine, "txt_shop.lua", 1254);
            remember("STWINFONT37", "font000");
            remember("STWINFONT37", "font001");
            text_script(engine, "txt_shop.lua", 1218);
            for (int i = 0; i < 8; ++i) {
                text(engine, "STWINFONT20", kLines[i], " ", "LEFT");
                text(engine, "STWINFONT43", kLines[i], " ", "LEFT");
                text(engine, "STWINFONT21", kLines[i], " ", "RIGHT");
            }
            for (const auto& pair : kFormLines) {
                text(engine, "STWINFONT22", pair[0], " ", "LEFT");
                text(engine, "STWINFONT22", pair[1], " ", "RIGHT");
            }
            text_script(engine, "txt_shop.lua", 1202);
            text(engine, "STWINFONT29", "font000", " ", "LEFT");
            next_state(1);
            return;
        }
        switch (sub) {
        case 0: {
            auto& select = add_child<SelectE1>(engine);
            select.position = {-283.0f, 92.0f, 0.0f};
            select.initial = line;
            select.count = page_count();
            mark();
            set_sub(1);
            selection = line;
            fill(engine, true, false);
        }
            [[fallthrough]];
        case 1:
            if (selection != -1) fill(engine, false, true);
            set_sub(2);
            last = selection;
            [[fallthrough]];
        case 2: {
            if (!child_done()) {
                if (selection == -1) return;
                if (last != selection) {
                    set_sub(1);
                    return;
                }
                auto* select = find_child<SelectE1>();
                if (!select || !select_idle(engine, *this, *select)) return;
                if (turn_page(engine, *this, page, pages, false) || turn_page(engine, *this, page, pages, true)) {
                    select->next_state(4);
                    select->count = page_count();
                    set_sub(1);
                    selection = 0;
                    fill(engine, true, false);
                }
                return;
            }
            if (selection == -1) {
                if (parent) parent->selection = -1;
                next_state(2);
                return;
            }
            line = selection;
            const auto* f = form(line + 8 * page);
            const int target = f ? f->species : p->stats.species;
            if (p->stats.species == target) {
                set_sub(4);
                cursor_ng(engine);
            } else if (sys->weapon_usable(engine, player, target)) {
                set_sub(3);
            } else {
                set_sub(4);
                set_step(1);
                cursor_ng(engine);
            }
            return;
        }
        case 3: // confirm twice, then digivolve
            switch (step) {
            case 0:
            case 4:
                add_child<ShopSelect01>(engine).mode = step == 0 ? 1255 : 1258;
                mark();
                set_step(step + 1);
                return;
            case 2:
                shop_message(*this, engine, 1257);
                mark();
                set_step(3);
                return;
            case 1:
            case 3:
            case 5:
                if (!child_done()) return;
                if (selection != 0) set_sub(0);
                else set_step(step + 1);
                return;
            case 6: {
                auto& note = shop_message(*this, engine, 0);
                note.remember("STWINFONT29", "font000");
                note.remember("STWINFONT29", "font100");
                text_script(engine, "txt_shop.lua", 1260);
                if (const auto* f = form(line + 8 * page)) {
                    if (auto* db = engine.get<DatabaseManager>())
                        textset(engine, "STWINFONT29", "font000", db->text("shinka", f->species, "EXPLANATION"));
                    switch_line(engine, "STWINFONT29", "font000", true);
                    evolve(engine, f->species);
                }
                mark();
                set_step(7);
                return;
            }
            default:
                if (!child_done()) return;
                if (parent) parent->selection = 1;
                next_state(2);
                return;
            }
        case 4: // already this form (step 0) or no weapon for it (step 1)
            if (last == 0) {
                auto& note = shop_message(*this, engine, 0);
                note.remember("STWINFONT29", "font000");
                text_script(engine, "txt_shop.lua", step ? 2021 : 2020);
                mark();
                last = 1;
            }
            if (child_done()) set_sub(0);
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrl20 (8039CD70): the Digi-Lab. Digivolve, equip or switch the
// player; leaving after a change plays demo_digilab.lua.
// ---------------------------------------------------------------------------
struct ShopLab : Window {
    std::vector<int> players;              // +84: ISUSE players
    std::vector<int> species;              // +88: their DIGIMONNO on entry
    std::vector<std::vector<int>> sockets; // +92: their POWERSOCKET records on entry
    bool evolved{}, equipped{};            // +96 / +97
    bool demo{};                           // +104

    // 8039DD88
    void snapshot(Engine& engine) {
        players.clear();
        species.clear();
        sockets.clear();
        auto* sys = engine.get<GameSystem>();
        if (!sys) return;
        for (int i = 0; i < 4; ++i) {
            const auto& p = sys->players[std::size_t(i)];
            if (!p.joined) continue;
            players.push_back(i);
            species.push_back(p.stats.species);
            std::vector<int> records;
            for (const int index : p.power_socket)
                if (const auto* d = p.device(index)) records.push_back(d->record);
            sockets.push_back(records);
        }
    }
    // 8039E030: a digivolution or a different socket since entry.
    bool changed(Engine& engine) const {
        if (!evolved && !equipped) return false;
        auto* sys = engine.get<GameSystem>();
        if (!sys) return false;
        for (std::size_t k = 0; k < players.size(); ++k) {
            const auto& p = sys->players[std::size_t(players[k])];
            if (p.stats.species != species[k]) return true;
            auto left = sockets[k];
            int socketed = 0;
            for (const int index : p.power_socket) {
                const auto* d = p.device(index);
                if (!d) continue;
                ++socketed;
                const auto found = std::find(left.begin(), left.end(), d->record);
                if (found != left.end()) *found = -1;
            }
            if (socketed != int(sockets[k].size())) return true;
            if (std::any_of(left.begin(), left.end(), [](int r) { return r != -1; })) return true;
        }
        return false;
    }
    // 8039DFA0
    void recalc_all(Engine& engine) {
        if (auto* sys = engine.get<GameSystem>())
            for (const int k : players) sys->recalc_player(engine, k);
    }
    void open_message(Engine& engine) { open_model(engine, "i_messwn", 0, "SQ_OPEN", "SQ_CLOSE"); } // 8039E440
    void update(Engine& engine) override {
        if (state == 2) {
            model(engine, "i_bgclear", 0, "OFFREEZE");
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            if (demo) engine.run_script("demo_digilab.lua");
            return;
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return;
        }
        auto* sys = engine.get<GameSystem>();
        if (!sys) {
            next_state(2);
            return;
        }
        if (state == 0) {
            evolved = equipped = false;
            snapshot(engine);
            add_child<ShopTitle00>(engine);
            add_child<Title03>(engine);
            add_child<ShopTitle01>(engine).mode = mode;
            next_state(1);
        }
        switch (sub) {
        case 0: // Digivolve / Equip / (Switch) / Exit
            if (step == 0) {
                open_message(engine);
                status_panel(*this, engine, player, true);
                set_step(1);
                return;
            }
            if (step == 1) {
                if (model_in(engine, "i_messwn", 0, "SQ_OPEN")) return;
                add_child<ShopSelect02>(engine).mode = int(players.size());
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            if (selection == 0) set_sub(1);
            else if (selection == 1) set_sub(2);
            else if (selection == 2 && players.size() != 1) set_sub(5);
            else set_sub(6);
            return;
        case 1: // digivolve
            if (step == 0) {
                add_child<ShopEvolve>(engine);
                mark();
                set_step(1);
                return;
            }
            if (!child_done()) return;
            if (selection == 1) {
                evolved = true;
                set_sub(3);
            } else {
                set_sub(0);
            }
            open_message(engine);
            return;
        case 2: // equipment
            if (step == 0) {
                status_panel(*this, engine, player, false);
                set_step(1);
            }
            if (step == 1) {
                add_equip_page(*this, engine, 1).open_model(engine, "i_messwn", 0, "SQ_STOPEN", "SQ_STCLOSE");
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            if (selection == 100) equipped = true;
            set_sub(0);
            open_message(engine);
            return;
        case 3: // after a digivolution: equip now?
            if (step == 0) {
                if (model_in(engine, "i_messwn", 0, "SQ_OPEN")) return;
                add_child<ShopSelect03>(engine).mode = 1261;
                mark();
                set_step(1);
                return;
            }
            if (step == 1) {
                if (!child_done()) return;
                add_child<ShopSelect02>(engine).mode = 1262;
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            set_sub(selection == 0 ? 2 : 0);
            return;
        case 5: // another player
            if (step == 0) {
                if (players.size() <= 1) {
                    set_sub(0);
                    return;
                }
                add_child<ShopSelect03>(engine).mode = 1250;
                mark();
                set_step(1);
                return;
            }
            if (step == 1) {
                if (!child_done()) return;
                auto& choice = add_child<ShopSelect02>(engine);
                choice.mode = -1;
                choice.players = players;
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            if (selection >= 0 && selection < int(players.size())) player = players[std::size_t(selection)];
            set_sub(0);
            return;
        case 6: // leave
            switch (step) {
            case 0:
                if (changed(engine)) {
                    set_step(2);
                    return;
                }
                add_child<ShopSelect03>(engine).mode = 1275;
                mark();
                set_step(1);
                recalc_all(engine);
                for (const int k : players) {
                    Packet arms;
                    engine.messenger().send(engine, "PLAYERARMS", sys->players[std::size_t(k)].weapons[0], k, "GENERATE", arms);
                }
                return;
            case 1:
                if (child_done()) next_state(2);
                return;
            case 2:
                add_child<ShopSelect03>(engine).mode = 1267;
                mark();
                set_step(3);
                return;
            case 3:
                if (!child_done()) return;
                add_child<ShopSelect02>(engine).mode = 1268;
                mark();
                set_step(4);
                return;
            case 4:
                if (!child_done()) return;
                if (selection != 0) set_sub(5);
                else set_step(5);
                return;
            default:
                recalc_all(engine);
                demo = true;
                next_state(2);
                return;
            }
        }
    }
};

// ===========================================================================
// The card keeper (program 10): beNDMWShopCtrlA0 picks a player, A1 opens a
// booster pack (A2) or shows the album of the 50 cards of DATABASE "card".
// ===========================================================================

// ShopTitle03 (803AA938): the talk window.
struct ShopTitle03 : Title {
    void open(Engine& engine) override { open_model(engine, "i_messwn", 0, "SQ_OPEN", "SQ_CLOSE"); }
};

// ShopSelectA0 (803A9E54): the album, j_shcardcursor over its 9 pages.
struct ShopSelectA0 : Select {
    void open(Engine& engine) override {
        open_model(engine, "j_shcardwin", 0, "SQ_OPEN", "SQ_CLOSE");
        remember("STWINFONT34", "font000");
        remember("STWINFONT34", "font001");
        text_script(engine, "txt_shop.lua", 2001);
        text(engine, "STWINFONT33", "font000", " ", "CENTER");
        text(engine, "STWINFONT33", "font001", " ", "CENTER");
        cursor = "j_shcardcursor";
        count = 9;
        position = {};
        initial = 0;
        no_cancel = false;
    }
};

void cursor_ng(Engine& engine) {
    Packet sound;
    sound.strings.emplace_back("CURSOR_NG");
    engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
}

// ---------------------------------------------------------------------------
// beNDMWShopCtrlA2 (8039A798): one booster pack of the mode packs left. Three
// cards are drawn by their KAKURITSU weights (per mille), go into the
// player's CARDLIST and are shown with their names coloured by rarity; the
// parent's selection says where to go next (4 menu, 6 album).
// ---------------------------------------------------------------------------
struct ShopCardPack : Window {
    static constexpr const char* lines[3] = {"font02", "font03", "font04"};
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            next_state(1);
            set_sub(7);
            return;
        }
        if (sub == 7) {
            if (step == 0) {
                // The stack of packs: SQ_OPEN00 for one up to SQ_OPEN08 for nine.
                char open[16];
                std::snprintf(open, sizeof open, "SQ_OPEN%02d", std::clamp(mode, 1, 9) - 1);
                open_model(engine, "j_shbooster", 0, open, "SQ_CLOSE");
                add_child<ShopSelect03>(engine).mode = 2013;
                mark();
                set_step(1);
                return;
            }
            if (step == 1) {
                if (!child_done()) return;
                add_child<ShopSelect02>(engine).mode = 2008;
                mark();
                set_step(2);
                return;
            }
            if (!child_done()) return;
            if (selection == 0) {
                close_models(engine);
                models.clear();
                set_sub(8);
            } else {
                if (parent) parent->selection = selection == 1 ? 6 : 4;
                next_state(2);
            }
            return;
        }
        if (step == 0) {
            open_pack(engine);
            set_step(1);
            return;
        }
        if (!child_done()) return;
        for (const char* line : lines) textctl(engine, "TALKFONT01", line, "DEFAULTCOLOR");
        if (parent) parent->selection = 4;
        next_state(2);
    }
    void open_pack(Engine& engine) {
        auto& note = add_child<ShopSelect03>(engine);
        mark();
        auto* p = player_slot(engine);
        auto* db = engine.get<DatabaseManager>();
        if (!p || !db) return;
        if (int& packs = p->disk_count(10); packs > 0) --packs;
        static const char* opens[3] = {"SQ_CARD06OPEN", "SQ_CARD07OPEN", "SQ_CARD08OPEN"};
        const int rows = db->rows("card");
        int best = 0;
        for (int i = 0; i < 3 && rows > 0; ++i) {
            const std::uint32_t roll = fight::random() % 1000u * 1000u;
            std::uint32_t sum = 0;
            int card = rows - 1;
            for (int row = 0; row < rows; ++row) {
                sum += std::uint32_t(db->number("card", row, "KAKURITSU").value_or(0)) * 1000u;
                if (roll < sum) {
                    card = row;
                    break;
                }
            }
            p->cards[card] = 1;
            const int rare = db->number("card", card, "RARE").value_or(0);
            engine.port_event({PortEvent::Kind::card, player, card, rare, {}}); // observe-only
            best = std::max(best, rare);
            note.text(engine, "TALKFONT01", lines[i], db->text("card", card, "NAME"), "LEFT");
            note.open_model(engine, db->text("card", card, "CARDINFO"), i, opens[i], "SQ_CLOSE");
            // Rare green, uncommon yellow, common white.
            const Vec3f rgb = rare == 2 ? Vec3f{0.0f, 1.0f, 0.0f} : rare == 1 ? Vec3f{1.0f, 1.0f, 0.0f} : Vec3f{1.0f, 1.0f, 1.0f};
            textctl(engine, "TALKFONT01", lines[i], "COLOR", &rgb);
        }
        note.mode = best == 2 ? 2010 : best == 1 ? 2011 : 2012;
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrlA1 (80399314): Open Booster Pack / View Album for a player.
// The album shows six cards a page: owned ones as their CARDINFO model,
// missing ones as the blank j_shcard00 in that place (SQALBUM of the row).
// ---------------------------------------------------------------------------
struct ShopCardMenu : Window {
    // +84 / +88: the model in each of the six places, index -1 for none.
    std::vector<std::pair<std::string, int>> places;

    bool owned(Engine& engine, int card) const {
        auto* p = player_slot(engine);
        if (!p) return false;
        const auto found = p->cards.find(card);
        return found != p->cards.end() && found->second != 0;
    }
    // 80399CC4: the first page.
    void first_page(Engine& engine) {
        auto* db = engine.get<DatabaseManager>();
        if (!db) return;
        for (int slot = 0; slot < 6; ++slot) {
            if (owned(engine, slot)) {
                const auto name = db->text("card", slot, "CARDINFO");
                open_model(engine, name, 0, db->text("card", slot, "SQALBUM"), "");
                places.emplace_back(name, 0);
            } else {
                open_model(engine, "j_shcard00", slot, db->text("card", slot, "SQALBUM"), "");
                places.emplace_back("j_shcard00", slot);
            }
        }
    }
    // 80399FF8: page number and the six places of a page.
    void show_page(Engine& engine, int page) {
        textset(engine, "STWINFONT33", "font000", std::string(1, char('1' + page)));
        auto* db = engine.get<DatabaseManager>();
        if (!db || page < 0 || page >= 9) return;
        for (int slot = 0; slot < 6 && slot < int(places.size()); ++slot) {
            const int card = page * 6 + slot;
            auto& [name, index] = places[std::size_t(slot)];
            if (card > 49) {
                if (index != -1) model(engine, name, index, "SQ_CLOSE");
                name = "blank";
                index = -1;
            } else if (owned(engine, card)) {
                if (index != -1) model(engine, name, index, "SQ_CLOSE");
                name = db->text("card", card, "CARDINFO");
                index = 0;
                open_model(engine, name, 0, db->text("card", card, "SQALBUM"), "");
            } else {
                if (index != -1) {
                    if (name == "j_shcard00") continue; // a blank card stays
                    model(engine, name, index, "SQ_CLOSE");
                }
                name = "j_shcard00";
                index = slot;
                open_model(engine, name, slot, db->text("card", slot, "SQALBUM"), "");
            }
        }
    }
    // 8039A604
    void close_places(Engine& engine) {
        for (const auto& [name, index] : places)
            if (index != -1) model(engine, name, index, "SQ_CLOSE");
        places.clear();
    }
    void generate(Engine& engine, const char* name) {
        Packet p;
        p.strings.emplace_back("SQ_OPEN");
        p.vectors.push_back({});
        engine.messenger().send(engine, "MODELCTRL", std::string(name), 0, "GENERATE", p);
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            next_state(1);
            set_sub(4);
            return;
        }
        switch (sub) {
        case 4: // Open Booster Pack / View Album
            if (step == 0) {
                if (model_in(engine, "j_shbase", player, "SQ_OPEN")) return;
                add_child<ShopSelect02>(engine).mode = 2005;
                mark();
                set_step(1);
                return;
            }
            if (!child_done()) return;
            if (selection == 0) set_sub(5);
            else if (selection == 1) set_sub(6);
            else next_state(2);
            return;
        case 5: // a booster pack (DISKFOLDER record 10)
            if (step == 0) {
                auto* p = player_slot(engine);
                const int packs = p ? p->disk_count(10) : 0;
                if (packs <= 0) {
                    add_child<ShopSelect03>(engine).mode = 2007;
                    cursor_ng(engine);
                } else {
                    add_child<ShopCardPack>(engine).mode = packs;
                }
                mark();
                set_step(1);
                return;
            }
            if (child_done()) set_sub(selection == 6 ? 6 : 4);
            return;
        case 6: // the album
            if (step == 0) {
                model(engine, "i_staccount", 0, "SQ_CLOSE");
                model(engine, "i_messwn", 0, "SQ_CLOSE");
                switch_line(engine, "STWINFONT34", "font002", false);
                add_child<ShopSelectA0>(engine);
                mark();
                first_page(engine);
                set_step(1);
                return;
            }
            if (step == 1) {
                if (model_in(engine, "j_shcardwin", 0, "SQ_OPEN")) return;
                textset(engine, "STWINFONT33", "font000", "1");
                textset(engine, "STWINFONT33", "font001", "9");
                set_step(2);
            }
            if (!child_done()) {
                if (selection != -1 && last != selection) {
                    show_page(engine, selection);
                    last = selection;
                }
                return;
            }
            close_places(engine);
            text_script(engine, "txt_status.lua", 0);
            switch_line(engine, "STWINFONT34", "font002", true);
            generate(engine, "i_staccount");
            generate(engine, "i_messwn");
            set_sub(4);
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrlA0 (803988A0): the card keeper. Select a Digimon / Exit, then
// one of the joined players for A1.
// ---------------------------------------------------------------------------
struct ShopCardKeeper : Window {
    std::vector<int> players; // +88: ISUSE players

    // 803930EC on the account panel (+84): its font002 label greyed or not.
    void account_color(Engine& engine, bool on) const {
        textctl(engine, "STWINFONT34", "font002", on ? "DEFAULTCOLOR" : "CANCELCOLOR");
    }
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            if (sub == 0) {
                open_model(engine, "j_shback", 0, "SQ_OPENCARD", "SQ_CLOSE");
                open_model(engine, "i_bgclear", 0, "SQ_OPEN", "SQ_CLOSE");
                set_sub(1);
                return;
            }
            if (model_in(engine, "j_shback", 0, "SQ_OPENCARD")) return;
            add_child<ShopTitle03>(engine);
            add_child<Title03>(engine);
            account_color(engine, false);
            add_child<ShopTitle01>(engine).mode = mode;
            players.clear();
            if (auto* sys = engine.get<GameSystem>())
                for (int k = 0; k < 4; ++k)
                    if (sys->players[std::size_t(k)].joined) players.push_back(k);
            status_panel(*this, engine, player, true);
            next_state(1);
            return;
        }
        switch (sub) {
        case 0:
            if (!model_in(engine, "i_messwn", 0, "SQ_OPEN") && mode == 10) set_sub(1);
            return;
        case 1: // Select a Digimon / Exit
            if (step == 0) {
                add_child<ShopSelect02>(engine).mode = 2003;
                account_color(engine, false);
                mark();
                set_step(1);
                return;
            }
            if (!child_done()) return;
            if (selection != 0) next_state(2);
            else set_sub(2);
            return;
        case 2: // the players
            if (step == 0) {
                auto& choice = add_child<ShopSelect02>(engine);
                choice.mode = -1;
                choice.players = players;
                account_color(engine, true);
                mark();
                set_step(1);
                return;
            }
            if (!child_done()) return;
            if (selection < 0 || selection >= 4 || selection >= int(players.size())) {
                set_sub(1);
                return;
            }
            player = players[std::size_t(selection)];
            status_panel(*this, engine, player, true);
            add_child<ShopCardMenu>(engine);
            mark();
            set_sub(3);
            return;
        case 3:
            if (child_done()) set_sub(2);
            return;
        }
    }
};

// Unknown programs close at once.
struct ShopPending : Window {
    void update(Engine& engine) override {
        if (close_states(engine)) return;
        if (state == 0) {
            engine.note("SUBWND shop program " + std::to_string(mode) + " not reconstructed");
            next_state(2);
        }
    }
};

// ---------------------------------------------------------------------------
// beNDMWShopCtrl00 (8039334C): the frame of the shop programs 1 and 3..9;
// 8034F504 starts the card keeper (10) directly.
// ---------------------------------------------------------------------------
struct ShopCtrl00 : Window {
    void update(Engine& engine) override {
        if (state == 2) {
            model(engine, "i_bgclear", 0, "OFFREEZE");
            close_models(engine);
            clear_lines(engine);
            close_children();
            next_state(3);
            return;
        }
        if (state == 3) {
            if (closed(engine)) running = false;
            return;
        }
        if (state == 0) {
            // 8039334C: the shop background, the player's full status page
            // (beNDMWStatusTitle01 mode 0, 80393434: the values and their
            // ONPREVIEW colours beside the lists), the message window, the
            // account panel and the program title.
            add_child<ShopTitle00>(engine);
            add_status_page(*this, engine, 0);
            add_child<Title02>(engine);
            add_child<Title03>(engine);
            add_child<ShopTitle01>(engine).mode = mode;
            next_state(1);
            return;
        }
        if (sub == 0) {
            if (model_in(engine, "i_messwn", 0, "SQ_STOPEN")) return;
            if (mode == 1) add_child<ShopXdata>(engine).mode = mode;
            else if (mode == 3) add_child<ShopBank>(engine).mode = mode;
            else if (mode == 4) add_child<ShopIdentify>(engine).mode = mode;
            else if (mode == 5) add_child<ShopDisk>(engine).mode = mode;
            else if (mode >= 6 && mode <= 8) add_child<ShopDevice>(engine).mode = mode;
            else if (mode == 9) add_child<ShopJunk>(engine).mode = mode;
            else add_child<ShopPending>(engine).mode = mode;
            mark();
            set_sub(1);
        }
        if (child_done()) next_state(2);
    }
};

} // namespace

std::unique_ptr<Window> make_shop_program(int program) {
    if (program == 2) {
        auto lab = std::make_unique<ShopLab>();
        lab->mode = program;
        return lab;
    }
    if (program == 10) {
        auto keeper = std::make_unique<ShopCardKeeper>();
        keeper->mode = program;
        return keeper;
    }
    auto window = std::make_unique<ShopCtrl00>();
    window->mode = program;
    return window;
}

} // namespace dw4::engine::subwnd
