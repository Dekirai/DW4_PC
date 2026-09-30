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
    const auto& pad = engine.pads()[std::size_t(std::clamp(window.player, 0, 3))];
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

// Programs whose windows are not reconstructed yet close at once.
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
// beNDMWShopCtrl00 (8039334C): the frame of the shop programs 1 and 3..9.
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
            add_child<ShopTitle00>(engine);
            add_child<Title00>(engine);
            add_child<Title02>(engine);
            add_child<Title03>(engine);
            add_child<ShopTitle01>(engine).mode = mode;
            next_state(1);
            return;
        }
        if (sub == 0) {
            if (model_in(engine, "i_messwn", 0, "SQ_STOPEN")) return;
            if (mode == 5) add_child<ShopDisk>(engine).mode = mode;
            else add_child<ShopPending>(engine).mode = mode;
            mark();
            set_sub(1);
        }
        if (child_done()) next_state(2);
    }
};

} // namespace

std::unique_ptr<Window> make_shop_program(int program) {
    auto window = std::make_unique<ShopCtrl00>();
    window->mode = program;
    return window;
}

} // namespace dw4::engine::subwnd
