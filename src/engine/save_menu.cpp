// The save room (docs/SAVES.md): SAVECTRL, the SAVEINTF_ALL panels and the
// memory card helpers they share with the character select.
#include "dw4/engine/save_menu.hpp"
#include "menu_text.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace dw4::engine {
using namespace menu;

namespace {

// SAVEINTF constants (804EB478) and the DATASELINTF / SELSLOT tables.
constexpr Vec3f kPanelPositions[4] = {{-315, 240, 0}, {-5, 240, 0}, {-315, 20, 0}, {-5, 20, 0}};
constexpr const char* kBoxModels[4] = {"j_saveareawin01", "j_saveareawin02", "j_saveareawin03", "j_saveareawin04"}; // 804E8180
constexpr const char* kBoxCursor[4] = {"SQ_LINESL00", "SQ_LINESL01", "SQ_LINESL02", "SQ_LINESL03"}; // 804E8190
constexpr const char* kBoxPick[4] = {"SQ_LINEON00", "SQ_LINEON01", "SQ_LINEON02", "SQ_LINEON03"};   // 804E81A0
constexpr const char* kBoxReopen[4] = {"SQ_AAREAOPEN", "SQ_BAREAOPEN", "SQ_CAREAOPEN", "SQ_DAREAOPEN"};
constexpr const char* kSlotCursor[2] = {"SQ_SELECT01", "SQ_SELECT02"};  // 804E89FC (GameCube)
constexpr const char* kSlotMarkOpen[2] = {"SQ_OPEN01", "SQ_OPEN02"};
constexpr std::uint64_t kCardWaitFrames = 180; // MakeDataTime / SaveDataTime: 3 s

struct Pad {
    bool up{}, down{}, left{}, right{};
    std::uint32_t trigger{};
};

// Menu input (80362B44 / 803599AC): stick or D-pad trigger/repeat for the
// directions, the trigger mask for the buttons.
Pad read_pad(Engine& engine, int player) {
    const auto& pad = engine.pads()[std::size_t(std::clamp(player, 0, 3))];
    const std::uint32_t sticks = pad.left_directions.trigger | pad.left_directions.repeat;
    const std::uint32_t buttons = pad.buttons.trigger | pad.buttons.repeat;
    Pad in;
    in.up = (sticks & dw4::pad_direction::positive_y) || (buttons & dw4::pad_button::up);
    in.down = (sticks & dw4::pad_direction::negative_y) || (buttons & dw4::pad_button::down);
    in.left = (sticks & dw4::pad_direction::negative_x) || (buttons & dw4::pad_button::left);
    in.right = (sticks & dw4::pad_direction::positive_x) || (buttons & dw4::pad_button::right);
    in.trigger = pad.buttons.trigger;
    return in;
}

// The card steps keep their counter in one of the player's four levels; a
// write resets the levels below it (8036356C and siblings).
int step_at(const SaveCommon::Player& p, int level) {
    return level == 0 ? p.state : level == 1 ? p.sub : level == 2 ? p.detail : p.detail2;
}
void store_step(SaveCommon::Player& p, int level, int value) {
    switch (level) {
    case 0: p.state = value; p.sub = p.detail = p.detail2 = 0; break;
    case 1: p.sub = value; p.detail = p.detail2 = 0; break;
    case 2: p.detail = value; p.detail2 = 0; break;
    default: p.detail2 = value; break;
    }
}
// 803611C8 / 803611DC / 80360BE0 / 80360C24 on a common player entry.
void set_state(SaveCommon::Player& p, int state) { p.state = state; p.sub = p.detail = p.detail2 = 0; }
void set_sub(SaveCommon::Player& p, int sub) { p.sub = sub; p.detail = p.detail2 = 0; }
void next_sub(SaveCommon::Player& p) { set_sub(p, p.sub + 1); }
void next_detail(SaveCommon::Player& p) { ++p.detail; p.detail2 = 0; }

bool shown(const save::Record& record) { return record.used && !record.digimon.empty(); }

} // namespace

// ---------------------------------------------------------------------------
// McUtilCtrl
void CardIo::generate(Engine& engine) {
    Packet packet;
    packet.ints.push_back(4);
    available = engine.messenger().send(engine, "SAVEUTILITY", std::nullopt, -1, "GENERATE", packet) > 0;
}

bool CardIo::is_ready(Engine& engine) {
    if (available && !query(engine, "SAVEUTILITY", std::nullopt, -1, "ISRDY")) return false;
    ready = true;
    return true;
}

void CardIo::remove(Engine& engine) {
    if (available) send(engine, "SAVEUTILITY", std::nullopt, -1, "REMOVE");
}

bool CardIo::gone(Engine& engine) { return !available || !query(engine, "SAVEUTILITY", std::nullopt, -1, "ISRDY"); }

void CardIo::start(int kind, int card) {
    slot = card;
    request = kind;
    result = 0;
    done = false;
    pending = true;
    formatting = false;
    step = 0;
}

void CardIo::update(Engine& engine) {
    if (ready) {
        if (available) {
            Packet packet;
            engine.messenger().send(engine, "SAVEUTILITY", std::nullopt, -1, "GETDELIVER", packet);
            if (!packet.objects.empty()) {
                const auto& deliver = *static_cast<const SaveUtilManager::Deliver*>(packet.objects[0]);
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    const auto& from = deliver.slots[i];
                    auto& to = slots[i];
                    to.inserted = from.state == 1;
                    if (to.inserted) {
                        to.free_bytes = from.free_bytes;
                        to.need_bytes = from.need_bytes;
                        to.enough = from.enough;
                    }
                    to.state = from.state;
                }
                all_ready = deliver.ready;
            }
        } else {
            all_ready = true;
            for (auto& s : slots) s.inserted = true;
        }
    }
    if (!pending) return;
    if (!available) {
        result = 2;
        pending = false;
        done = true;
        return;
    }
    static constexpr std::string_view kCommands[] = {"REQUEST_FILEMAKE",   "REQUEST_FILEFIND",    "REQUEST_FORMAT",
                                                     "REQUEST_USE_CHECK",  "REQUEST_READ_MEDIA",  "REQUEST_WRITE_MEDIA",
                                                     "REQUEST_GETFREESIZE"};
    if (step == 0) {
        Packet packet;
        packet.ints.push_back(slot);
        if (request == kFileMake) std::fill(buffer.begin(), buffer.end(), 0);
        if (request == kFileMake || request == kReadMedia || request == kWriteMedia) packet.objects.push_back(&buffer);
        if (request >= 0 && request < int(std::size(kCommands)))
            engine.messenger().send(engine, "SAVEUTILITY", std::nullopt, -1, kCommands[request], packet);
        ++step;
    } else if (step == 1) {
        if (!query(engine, "SAVEUTILITY", std::nullopt, -1, "ISBUSY")) ++step;
    } else if (step == 2) {
        Packet packet;
        engine.messenger().send(engine, "SAVEUTILITY", std::nullopt, -1, "GETRESULT", packet);
        result = packet.integer(0, 3);
        if (std::getenv("DW4_TRACE_SAVE"))
            engine.log("card request " + std::to_string(request) + " slot " + std::to_string(slot) + " -> " + std::to_string(result));
        pending = false;
        done = true;
        ++step;
    }
}

// ---------------------------------------------------------------------------
// COMMONINTF (save room)
bool SaveCommon::all_confirmed() const {
    int entered = 0, joined = 0;
    for (const auto& p : players) {
        entered += p.entered ? 1 : 0;
        joined += p.joined ? 1 : 0;
    }
    return entered == joined;
}

void SaveCommon::deactivate_all() {
    for (auto& p : players) p.active = false;
}

void SaveCommon::pause_others(int player) {
    for (auto& p : players) {
        set_state(p, 1);
        p.active = false;
        p.counter = 60;
    }
    auto& me = players[std::size_t(player)];
    set_state(me, 0);
    me.active = true;
    me.counter = 0;
}

void SaveCommon::lock(int player, int state) {
    for (auto& p : players) {
        set_state(p, 1);
        p.active = false;
        if (p.counter <= 0) p.counter = 10;
    }
    auto& me = players[std::size_t(player)];
    set_state(me, state);
    me.active = true;
    me.counter = 0;
    owner = player;
}

void SaveCommon::detach(int player) {
    for (auto it = cards.rbegin(); it != cards.rend(); ++it) {
        auto& users = (*it)->users;
        if (const auto found = std::find(users.begin(), users.end(), player); found != users.end()) {
            users.erase(found);
            std::erase_if(cards, [](const auto& card) { return card->users.empty(); });
            break;
        }
    }
    players[std::size_t(player)].card.reset();
}

void SaveCommon::update(Engine& engine) {
    io.update(engine);
    for (auto& p : players) p.card_missing = false;
    for (int slot = 0; slot < int(io.slots.size()); ++slot) {
        if (io.slots[std::size_t(slot)].inserted) continue;
        for (const auto& card : cards)
            if (card->slot == slot)
                for (const int user : card->users) players[std::size_t(user)].card_missing = true;
    }
}

int SaveCommon::check(Engine& engine, int player, int card, int level) {
    (void)engine;
    auto& p = players[std::size_t(player)];
    int step = step_at(p, level), outcome = 0;
    if (step == 0) {
        io.start(CardIo::kUseCheck, card);
        step = 1;
    } else if (step == 1) {
        if (io.done) {
            if (io.result == 2) ++step;
            else outcome = 2;
        }
    } else if (step == 2) {
        io.start(CardIo::kFileFind, card);
        step = 3;
    } else if (step == 3) {
        if (io.done) outcome = io.result == 2 ? 1 : 2;
    }
    store_step(p, level, step);
    return outcome;
}

int SaveCommon::ask(Engine& engine, int player, MessageWindow& message, int text, int prompt, int cursor, int level) {
    (void)engine;
    auto& p = players[std::size_t(player)];
    int step = step_at(p, level), outcome = 0;
    if (step == 0) {
        message.next(text);
        step = 1;
    } else if (step == 1) {
        if (!message.busy()) ++step;
    } else if (step == 2) {
        message.question(prompt, cursor);
        step = 3;
    } else if (step == 3 && !message.busy()) {
        outcome = message.answer() == 1 ? 2 : 1;
    }
    store_step(p, level, step);
    return outcome;
}

int SaveCommon::create(Engine& engine, int player, MessageWindow& message, int card, int level) {
    auto& p = players[std::size_t(player)];
    int step = step_at(p, level), outcome = 0;
    auto finished = [&] {
        if (io.done) outcome = io.result == 2 ? 1 : 2;
    };
    if (step == 0) {
        message.show((player << 16) | 0x2003);
        io.start(CardIo::kGetFreeSize, card);
        step = 1;
    } else if (step == 1) {
        if (io.done) {
            if (io.result == 2) ++step;
            else outcome = 2;
        }
    } else if (step == 2) {
        io.start(CardIo::kFileMake, card);
        timer_end = engine.frame() + kCardWaitFrames;
        step = 3;
    } else if (step == 3) {
        if (!io.slots[std::size_t(card)].inserted || engine.frame() >= timer_end) {
            ++step;
            finished();
        }
    } else if (step == 4) {
        finished();
    }
    store_step(p, level, step);
    return outcome;
}

int SaveCommon::format(Engine& engine, int player, int card, int level) {
    (void)engine;
    auto& p = players[std::size_t(player)];
    int step = step_at(p, level), outcome = 0;
    if (step == 0) {
        io.start(CardIo::kFormat, card);
        io.formatting = true;
        step = 1;
    } else if (step == 1 && io.done) {
        outcome = io.result == 2 ? 1 : 2;
    }
    store_step(p, level, step);
    return outcome;
}

int SaveCommon::read(Engine& engine, int player, int card, int level) {
    (void)engine;
    auto& p = players[std::size_t(player)];
    int step = step_at(p, level), outcome = 0;
    if (step == 0) {
        io.start(CardIo::kReadMedia, card);
        step = 1;
    } else if (step == 1 && io.done) {
        if (io.result == 2) {
            auto data = std::make_shared<CardData>();
            data->slot = card;
            data->users.push_back(player);
            for (std::size_t i = 0; i < data->records.size(); ++i) {
                const std::size_t at = i * save::kRecordStride;
                if (at + save::kRecordSize <= io.buffer.size())
                    data->records[i] = save::decode(std::span<const std::uint8_t>(io.buffer).subspan(at, save::kRecordSize));
            }
            cards.push_back(data);
            p.card = data;
            outcome = 1;
        } else {
            outcome = 2;
        }
    }
    store_step(p, level, step);
    return outcome;
}

int SaveCommon::write(Engine& engine, int player, int card, int box, int level) {
    auto& p = players[std::size_t(player)];
    int step = step_at(p, level), outcome = 0;
    auto finished = [&] {
        if (!io.done) return;
        if (io.result == 2) {
            outcome = 1;
            if (p.card) p.card->records[std::size_t(box)] = records[std::size_t(player)]; // 80364854
        } else {
            outcome = 2;
        }
    };
    if (step == 0) {
        // 803674C0: the file's boxes with this player's character in `box`.
        std::fill(io.buffer.begin(), io.buffer.end(), 0);
        for (std::size_t i = 0; i < save::kRecordsPerCopy; ++i) {
            const auto& record = int(i) == box || !p.card ? records[std::size_t(player)] : p.card->records[i];
            if (int(i) != box && !p.card) continue;
            const auto bytes = save::encode(record);
            std::copy(bytes.begin(), bytes.end(), io.buffer.begin() + std::ptrdiff_t(i * save::kRecordStride));
        }
        io.start(CardIo::kWriteMedia, card);
        timer_end = engine.frame() + kCardWaitFrames;
        ++step;
    } else if (step == 1) {
        if (!io.slots[std::size_t(card)].inserted || engine.frame() >= timer_end) {
            step = 2;
            finished();
        }
    } else if (step == 2) {
        finished();
    }
    store_step(p, level, step);
    return outcome;
}

// ---------------------------------------------------------------------------
// DEGISTATECTRL
const save::Record* RecordWindow::record(const SaveCommon& common, int player) const {
    const auto slot = std::size_t(std::clamp(player, 0, 3));
    if (mode_) return &common.records[slot];
    const auto& card = common.players[slot].card;
    if (!card || index_ < 0 || index_ >= int(card->records.size())) return nullptr;
    return &card->records[std::size_t(index_)];
}

bool RecordWindow::update(Engine& engine, CommonModelControl& models, int player, const Vec3f& position,
                          const SaveCommon& common) {
    if (done_) return true;
    const auto* rec = record(common, player);
    const bool used = rec && rec->used;
    const auto& row = kPartnerRows[used ? partner_row(rec->digimon) : 0];
    const std::string face = check_ ? "j_savecheckwin" : "j_digifacewin";
    // 803531FC: clear the window's lines.
    auto clear_values = [&] {
        if (check_) {
            text_line_clear(engine, "CHRSELFONT03", player, 10);
            text_line_clear(engine, "CHRSELFONT04", player, 11);
            text_line_clear(engine, "CHRSELFONT04", player, 1);
            for (const int line : {2, 12, 3, 13}) text_line_clear(engine, "CHRSELFONT05", player, line);
            text_order_clear(engine, (player << 16) | 0x110);
        } else {
            text_line_clear(engine, "CHRSELFONT03", player, 0);
            text_line_clear(engine, "CHRSELFONT04", player, 0);
            text_line_clear(engine, "CHRSELFONT04", player, 10);
            for (const int line : {0, 10, 1, 11}) text_line_clear(engine, "CHRSELFONT05", player, line);
            text_order_clear(engine, (player << 16) | 0x10);
        }
    };
    // 80352A38: player name, Digimon name, level, HP and MP.
    auto fill_values = [&] {
        if (!used) {
            text_order_show(engine, (player << 16) | (check_ ? 0x110 : 0x10)); // NO DATA
            return;
        }
        std::string species;
        if (auto* db = engine.get<DatabaseManager>()) species = db->text("shinka", partner_row(rec->digimon), "NAME");
        const int base = check_ ? 10 : 0;
        text_set_string(engine, "CHRSELFONT03", player, base, rec->name, 0, false);
        text_set_text(engine, "CHRSELFONT04", player, check_ ? 11 : 10, species, 0, false);
        const int lines[5][2] = {{4, check_ ? 1 : 0}, {5, check_ ? 2 : 0}, {5, check_ ? 12 : 10}, {5, check_ ? 3 : 1},
                                 {5, check_ ? 13 : 11}};
        const std::int32_t values[5] = {rec->level, rec->hp, rec->max_hp, rec->mp, rec->max_mp};
        for (int i = 0; i < 5; ++i)
            text_set_text(engine, lines[i][0] == 4 ? "CHRSELFONT04" : "CHRSELFONT05", player, lines[i][1],
                          std::to_string(values[i]), i == 0 ? 1 : 2, false);
    };
    auto finish = [&] {
        done_ = true;
        phase_ = 0;
        step_ = 0;
    };
    if (phase_ == 1) {
        if (step_ == 0) {
            models.open_at(engine, player, face, used ? row.open : "SQ_nofaceOPEN", position, true);
            if (!check_) models.open_at(engine, player, "j_mainstatuswin", used ? "SQ_OPEN" : "SQ_NOOPEN", position, true);
            step_ = 1;
        } else if (models.sequences_done()) {
            fill_values();
            finish();
        }
    } else if (phase_ == 3) {
        models.set(engine, player, face, used ? row.change : "SQ_noface", false);
        if (!check_) models.set(engine, player, "j_mainstatuswin", used ? "SQ_WAIT" : "SQ_NOWAIT", false);
        clear_values();
        fill_values();
        finish();
    } else if (phase_ == 2) {
        if (step_ == 0) {
            models.close(engine, player, face, "SQ_CLOSE", true);
            if (!check_) {
                const char* seq = !used ? "SQ_NOCLOSE" : style_ == 1 ? "SQ_LINEON00" : style_ == 2 ? "SQ_CANCEL" : "SQ_CLOSE";
                models.close(engine, player, "j_mainstatuswin", seq, true);
            }
            clear_values();
            step_ = 1;
        } else if (models.closes_done()) {
            finish();
        }
    } else {
        done_ = true;
    }
    return done_;
}

// ---------------------------------------------------------------------------
// SELSLOT (GameCube path)
void SlotSelect::clear_flags() {
    opened_ = selected_ = cancelled_ = closed_ = false;
    phase_ = step_ = 0;
}

void SlotSelect::reset() {
    clear_flags();
    cursor_ = 0;
}

void SlotSelect::reselect(Engine& engine, CommonModelControl& models, int player) {
    phase_ = 1;
    step_ = 0;
    selected_ = false;
    models.set(engine, player, "j_slotcursor", kSlotCursor[cursor_], false);
}

void SlotSelect::slot_marks(Engine& engine, CommonModelControl& models, int player, const Vec3f& position,
                            const CardIo& io) {
    for (int i = 0; i < 2; ++i) {
        const int index = 2 * player + i;
        if (io.slots[std::size_t(i)].state != 0) {
            if (models.is_live(engine, index, "j_slotmark02")) models.set(engine, index, "j_slotmark02", kSlotMarkOpen[i], false);
            else models.open_at(engine, index, "j_slotmark02", kSlotMarkOpen[i], position, false);
        } else if (models.is_live(engine, index, "j_slotmark02")) {
            models.close(engine, index, "j_slotmark02", "SQ_CLOSE", false);
        }
    }
}

void SlotSelect::refresh(Engine& engine, int player, const CardIo& io) {
    const int id = player << 16;
    // 803597A4: the slot label, or "wrong device" for a foreign card.
    auto label = [&](int kind) {
        if (kind == -1) {
            text_order_clear(engine, id | 0x202E);
            text_order_show(engine, id | (0x2200 + cursor_));
        } else {
            text_order_show(engine, id | (0x2100 + cursor_));
        }
    };
    const int state = io.slots[std::size_t(cursor_)].state;
    if (state == 1) {
        if (status_line_ != 1) {
            label(0);
            text_order_clear(engine, id | 0x202E);
        }
        status_line_ = 1;
    } else if (state == -1) {
        if (status_line_ != -1) label(-1);
        status_line_ = -1;
    } else {
        if (status_line_ != 0) {
            label(0);
            text_order_show(engine, id | 0x202E); // Memory Card is not inserted.
        }
        status_line_ = 0;
    }
}

bool SlotSelect::open(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, const CardIo& io,
                      int slot) {
    if (phase_ != 0) return false;
    if (step_ == 0) {
        if (slot != -1) cursor_ = slot;
        models.open_at(engine, player, "j_slotbase", "SQ_OPENPS01", position, true);
        models.open_at(engine, player, "j_memoryiconwin", "SQ_OPEN", position, true);
        step_ = 1;
    } else if (step_ == 1) {
        if (models.sequences_done()) {
            text_order_show(engine, (player << 16) | 0x200C);
            text_order_clear(engine, (player << 16) | 0x202E);
            status_line_ = -2;
            refresh(engine, player, io);
            models.open_at(engine, player, "j_slotcursor", kSlotCursor[cursor_], position, false);
            slot_marks(engine, models, player, position, io);
            step_ = 2;
        }
    } else if (step_ == 2) {
        if (models.sequences_done()) {
            opened_ = true;
            ++phase_;
            step_ = 0;
        }
    }
    return opened_;
}

bool SlotSelect::select(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, bool input,
                        SaveCommon& common) {
    if (phase_ != 1) return false;
    if (selected_) return true;
    const Pad in = input ? read_pad(engine, player) : Pad{};
    slot_marks(engine, models, player, position, common.io);
    refresh(engine, player, common.io);
    if (step_ == 1) {
        if (models.sequences_done()) {
            models.set(engine, player, "j_slotcursor", kSlotCursor[cursor_], false);
            status_line_ = -2;
            refresh(engine, player, common.io);
            step_ = 0;
        }
        return selected_;
    }
    if (step_ == 2) {
        if (models.sequences_done()) {
            models.set(engine, player, "j_slotcursor", kSlotCursor[cursor_], false);
            selected_ = true;
            ++phase_;
            step_ = 0;
        }
        return selected_;
    }
    if (in.down) {
        if (++cursor_ >= 2) {
            cursor_ = 1;
            return selected_;
        }
    } else if (in.up) {
        if (--cursor_ < 0) {
            cursor_ = 0;
            return selected_;
        }
    } else {
        if (in.trigger & dw4::pad_button::a) {
            if (common.io.slots[std::size_t(cursor_)].inserted) {
                common.pause_others(player);
                chosen_ = cursor_;
                models.set(engine, player, "j_slotcursor", "SQ_ON", true);
                step_ = 2;
            }
        } else if (in.trigger & dw4::pad_button::b) {
            cancelled_ = true;
            models.set(engine, player, "j_slotcursor", "SQ_CANCEL", true);
            step_ = 2;
        }
        return selected_;
    }
    models.set(engine, player, "j_slotcursor", "SQ_MOVE", true);
    step_ = 1;
    return selected_;
}

bool SlotSelect::close(Engine& engine, CommonModelControl& models, int player) {
    if (phase_ != 2) return false;
    if (closed_) return true;
    if (step_ == 0) {
        models.close(engine, player, "j_slotbase", "SQ_CLOSE", true);
        models.close(engine, player, "j_slotcursor", "SQ_CLOSE", true);
        models.close(engine, player, "j_memoryiconwin", "SQ_CLOSE", true);
        text_order_clear(engine, (player << 16) | 0x200C);
        text_order_clear(engine, (player << 16) | 0x2100);
        text_order_clear(engine, (player << 16) | 0x202E);
        for (int i = 0; i < 2; ++i)
            if (models.is_live(engine, 2 * player + i, "j_slotmark02"))
                models.close(engine, 2 * player + i, "j_slotmark02", "SQ_CLOSE", false);
        step_ = 1;
    } else if (models.closes_done()) {
        closed_ = true;
    }
    return closed_;
}

void SlotSelect::mark(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, int failed,
                      const CardIo& io) {
    for (int i = 0; i < 2; ++i) {
        const int index = 2 * player + i;
        const bool inserted = i != failed && io.slots[std::size_t(i)].inserted;
        if (inserted) {
            if (models.is_live(engine, index, "j_slotmark02")) models.set(engine, index, "j_slotmark02", kSlotMarkOpen[i], false);
            else models.open_at(engine, index, "j_slotmark02", kSlotMarkOpen[i], position, false);
        } else if (models.is_live(engine, index, "j_slotmark02")) {
            models.close(engine, index, "j_slotmark02", "SQ_CLOSE", false);
        }
    }
    refresh(engine, player, io);
}

// ---------------------------------------------------------------------------
// DATASELINTF
void DataSelect::open(int player, int mode) {
    player_ = player;
    mode_ = mode;
    cursor_ = 0;
    boxes_ = window0_ = window1_ = cursor_open_ = false;
    step_ = 0;
}

void DataSelect::open_boxes(Context& c) {
    const auto& card = c.common.players[std::size_t(player_)].card;
    for (int i = 0; i < 4; ++i) {
        const save::Record* rec = card ? &card->records[std::size_t(i)] : nullptr;
        const char* seq = rec && shown(*rec) ? kPartnerRows[partner_row(rec->digimon)].open : "SQ_NOOPEN";
        c.models.open_at(c.engine, player_, kBoxModels[i], seq, c.position, true);
    }
    boxes_ = true;
}

void DataSelect::box_names(Context& c, int player) {
    const auto& card = c.common.players[std::size_t(player)].card;
    for (int i = 0; i < 4; ++i) {
        const save::Record* rec = card ? &card->records[std::size_t(i)] : nullptr;
        if (rec && rec->used) text_set_string(c.engine, "CHRSELFONT08", player, i, rec->name, 1, false);
        else text_order_show(c.engine, (player << 16) | 0x300 | i); // NO DATA
    }
}

void DataSelect::clear_names(Context& c) {
    for (int i = 0; i < 4; ++i) c.models.close(c.engine, player_, kBoxModels[i], "SQ_CLOSE", true);
    for (int i = 0; i < 4; ++i) text_line_clear(c.engine, "CHRSELFONT08", player_, i);
    boxes_ = false;
}

bool DataSelect::show_boxes(Context& c) {
    if (step_ == 0) {
        open_boxes(c);
        c.models.open_at(c.engine, player_, "j_saveareacursorr", "SQ_OPEN", c.position, true);
        cursor_open_ = true;
        if (mode_) {
            called_ = false;
            c.message.show((player_ << 16) | 0x503);
            c.windows[0].show(1, player_);
        } else {
            // Loading: is the box under the cursor already called by another player?
            bool free = true;
            const auto& card = c.common.players[std::size_t(player_)].card;
            if (card && card->records[std::size_t(cursor_)].used) {
                const auto& rec = card->records[std::size_t(cursor_)];
                for (int p = 0; p < 4; ++p) {
                    const auto& other = c.common.records[std::size_t(p)];
                    if (p != player_ && other.used && other.unique == rec.unique && other.name == rec.name) free = false;
                }
            }
            called_ = !free;
            c.message.show((player_ << 16) | (free ? 0x513 : 0x515));
            c.windows[0].show(0, cursor_);
        }
        step_ = 1;
        window0_ = true;
    } else if (step_ == 1) {
        if (c.done(0) && c.models.sequences_done()) {
            box_names(c, player_);
            step_ = 2;
            return true;
        }
    } else {
        return true;
    }
    return false;
}

bool DataSelect::select(Context& c) {
    const bool input = c.common.players[std::size_t(player_)].active;
    const Pad in = input ? read_pad(c.engine, player_) : Pad{};
    auto& card = c.common.players[std::size_t(player_)].card;
    auto called_elsewhere = [&](const save::Record& rec) {
        for (int p = 0; p < 4; ++p) {
            const auto& other = c.common.records[std::size_t(p)];
            if (p != player_ && other.used && other.unique == rec.unique && other.name == rec.name) return true;
        }
        return false;
    };
    switch (step_) {
    case 0: {
        int target = cursor_;
        if (in.up) target = cursor_ == 2 ? 0 : cursor_ == 3 ? 1 : cursor_;
        else if (in.down) target = cursor_ == 0 ? 2 : cursor_ == 1 ? 3 : cursor_;
        else if (in.right) target = cursor_ == 0 ? 1 : cursor_ == 2 ? 3 : cursor_;
        else if (in.left) target = cursor_ == 1 ? 0 : cursor_ == 3 ? 2 : cursor_;
        if (target == cursor_) {
            if (in.trigger & dw4::pad_button::a) {
                if (mode_) {
                    c.common.pause_others(player_);
                    step_ = 2;
                    cancelled_ = false;
                } else if (card && card->records[std::size_t(cursor_)].used &&
                           !called_elsewhere(card->records[std::size_t(cursor_)])) {
                    // Loading marks the character as called by this player.
                    auto& mine = c.common.records[std::size_t(player_)];
                    mine.used = true;
                    mine.unique = card->records[std::size_t(cursor_)].unique;
                    mine.name = card->records[std::size_t(cursor_)].name;
                    step_ = 2;
                    cancelled_ = false;
                }
            } else if (in.trigger & dw4::pad_button::b) {
                step_ = 4;
                cancelled_ = true;
            }
        } else {
            cursor_ = target;
            if (!mode_) c.windows[0].change(0, cursor_);
            c.models.set(c.engine, player_, "j_saveareacursorr", "SQ_MOVE", false);
            step_ = 1;
        }
        if (!mode_ && card) {
            c.windows[0].change(0, cursor_);
            const auto& rec = card->records[std::size_t(cursor_)];
            const bool taken = rec.used && called_elsewhere(rec);
            if (taken && !called_) {
                called_ = true;
                c.message.show((player_ << 16) | 0x515);
            } else if (!taken && called_) {
                called_ = false;
                c.message.show((player_ << 16) | 0x513);
            }
        }
        break;
    }
    case 1:
        if ((mode_ || c.done(0)) && c.models.sequences_done()) {
            c.models.set(c.engine, player_, "j_saveareacursorr", kBoxCursor[cursor_], false);
            step_ = 0;
        }
        break;
    case 2:
        c.models.close(c.engine, player_, "j_saveareacursorr", kBoxPick[cursor_], true);
        cursor_open_ = false;
        step_ = 3;
        break;
    case 3:
    case 5:
        if (c.models.closes_done()) step_ = 6;
        break;
    case 4:
        c.models.close(c.engine, player_, "j_saveareacursorr", "SQ_CANCEL", true);
        cursor_open_ = false;
        step_ = 5;
        break;
    case 6: return true;
    default: break;
    }
    return false;
}

bool DataSelect::close_all(Context& c) {
    if (step_ == 0) {
        if (boxes_) clear_names(c);
        if (window0_) c.windows[0].hide(0);
        if (window1_) c.windows[1].hide(0);
        if (cursor_open_) c.models.close(c.engine, player_, "j_saveareacursorr", "SQ_CLOSE", true);
        step_ = 1;
    } else if (step_ == 1) {
        if (window0_) {
            if (!c.done(0)) return false;
            window0_ = false;
        }
        if (window1_) {
            if (!c.done(1)) return false;
            window1_ = false;
        }
        if (c.models.closes_done()) {
            step_ = 2;
            return true;
        }
    } else {
        return true;
    }
    return false;
}

bool DataSelect::show_detail(Context& c) {
    if (step_ == 0) {
        if (boxes_) clear_names(c);
        step_ = 1;
    } else if (step_ == 1) {
        if (c.models.closes_done()) {
            ++step_;
            c.windows[1].show(0, cursor_);
            window1_ = true;
        }
    } else if (step_ == 2) {
        if (c.done(1)) {
            step_ = 3;
            return true;
        }
    }
    return false;
}

bool DataSelect::back_to_boxes(Context& c) {
    if (step_ == 0) {
        c.windows[1].hide(0);
        step_ = 1;
        window1_ = false;
    } else if (step_ == 1) {
        if (c.done(1)) ++step_;
    } else if (step_ == 2) {
        open_boxes(c);
        step_ = 3;
    } else if (step_ == 3) {
        if (c.models.sequences_done()) {
            box_names(c, player_);
            ++step_;
        }
    } else if (step_ == 4) {
        return true;
    }
    return false;
}

bool DataSelect::hide_windows(Context& c) {
    if (step_ == 0) {
        c.windows[0].hide(0);
        c.windows[1].hide(0);
        ++step_;
        window0_ = window1_ = false;
    } else if (step_ == 1) {
        if (c.done(0) && c.done(1)) step_ = 2;
    } else {
        return true;
    }
    return false;
}

bool DataSelect::reopen(Context& c) {
    if (step_ == 0) {
        open_boxes(c);
        c.windows[0].show(1, player_);
        ++step_;
        window0_ = true;
    } else if (step_ == 1) {
        if (c.models.sequences_done() && c.done(0)) {
            step_ = 2;
            box_names(c, player_);
        }
    } else {
        return true;
    }
    return false;
}

bool DataSelect::reopen_cursor(Context& c) {
    if (step_ == 0) {
        if (!cursor_open_) {
            c.models.open_at(c.engine, player_, "j_saveareacursorr", kBoxReopen[cursor_], c.position, true);
            cursor_open_ = true;
        }
        ++step_;
    } else if (step_ == 1) {
        if (c.models.sequences_done()) {
            step_ = 2;
            c.message.show((player_ << 16) | (mode_ ? 0x503 : 0x513));
        }
    } else {
        return true;
    }
    return false;
}

bool DataSelect::close_boxes(Context& c) {
    if (step_ == 0) {
        if (boxes_) clear_names(c);
        if (cursor_open_) c.models.close(c.engine, player_, "j_saveareacursorr", "SQ_CLOSE", true);
        ++step_;
    } else if (step_ == 1) {
        if (c.models.closes_done()) {
            step_ = 2;
            return true;
        }
    } else {
        return true;
    }
    return false;
}

bool DataSelect::refresh_users(Context& c) {
    // Every panel on the same file shows the new box contents.
    const auto card = c.common.players[std::size_t(player_)].card;
    if (!card) return true;
    for (const int user : card->users) {
        for (int i = 0; i < 4; ++i) {
            const auto& rec = card->records[std::size_t(i)];
            if (shown(rec)) {
                c.models.set(c.engine, user, kBoxModels[i], kPartnerRows[partner_row(rec.digimon)].change, false);
                text_set_string(c.engine, "CHRSELFONT08", user, i, rec.name, 1, false);
            } else {
                c.models.set(c.engine, user, kBoxModels[i], "SQ_NOOPEN", false);
                text_order_show(c.engine, (user << 16) | 0x300 | i);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// NAMESEL (US layout)
namespace {
// 804E81CC rows 0..3: the characters a US name can use.
struct NameRow {
    int count;
    char16_t chars[26];
};
constexpr NameRow kNameRows[4] = {
    {26, {0xFF41, 0xFF42, 0xFF43, 0xFF44, 0xFF45, 0xFF46, 0xFF47, 0xFF48, 0xFF49, 0xFF4A, 0xFF4B, 0xFF4C, 0xFF4D,
          0xFF4E, 0xFF4F, 0xFF50, 0xFF51, 0xFF52, 0xFF53, 0xFF54, 0xFF55, 0xFF56, 0xFF57, 0xFF58, 0xFF59, 0xFF5A}},
    {26, {0xFF21, 0xFF22, 0xFF23, 0xFF24, 0xFF25, 0xFF26, 0xFF27, 0xFF28, 0xFF29, 0xFF2A, 0xFF2B, 0xFF2C, 0xFF2D,
          0xFF2E, 0xFF2F, 0xFF30, 0xFF31, 0xFF32, 0xFF33, 0xFF34, 0xFF35, 0xFF36, 0xFF37, 0xFF38, 0xFF39, 0xFF3A}},
    {10, {0xFF10, 0xFF11, 0xFF12, 0xFF13, 0xFF14, 0xFF15, 0xFF16, 0xFF17, 0xFF18, 0xFF19}},
    {7, {0xFF06, 0xFF01, 0xFF1F, 0xFF5E, 0x30FC, 0x30FB, 0xFF1A}},
};
constexpr int kNameRowStart = 0, kNameRowEnd = 4, kNameLength = 8;

std::string numbered(const char* prefix, int value) { return prefix + std::to_string(value); }
} // namespace

void NameSelect::open(bool clear) {
    if (clear) {
        name_.fill(0);
        state_.fill(0);
        pos_ = -1;
    }
    if (pos_ == -1) {
        saved_[0] = Slot{};
        pos_ = 0;
    }
    row_ = saved_[std::size_t(pos_)].row;
    scroll_ = saved_[std::size_t(pos_)].scroll;
    cursor_ = saved_[std::size_t(pos_)].cursor;
    state_[std::size_t(pos_)] = 1;
    done_ = confirmed_ = asking_ = answered_ = yes_ = arrows_ = false;
    phase_ = step_ = 0;
}

char16_t NameSelect::current() const {
    const auto& row = kNameRows[std::clamp(row_, 0, 3)];
    return row.chars[(scroll_ + cursor_) % row.count];
}

std::u16string NameSelect::name() const {
    std::u16string out;
    for (int i = 0; i < kNameLength && name_[std::size_t(i)]; ++i) out.push_back(name_[std::size_t(i)]);
    return out;
}

void NameSelect::draw_rows(Engine& engine, int player, bool clear) {
    for (int i = 0; i < 3; ++i) {
        if (clear) {
            text_line_clear(engine, "CHRSELFONT10", player, i);
            continue;
        }
        int row = row_ + i - 1;
        if (row < kNameRowStart) row = kNameRowEnd - 1;
        if (row >= kNameRowEnd) row = kNameRowStart;
        text_set_string(engine, "CHRSELFONT10", player, i, std::u16string(1, kNameRows[row].chars[0]), 1, false);
    }
}

void NameSelect::draw_chars(Engine& engine, int player, bool clear) {
    const auto& row = kNameRows[std::clamp(row_, 0, 3)];
    for (int i = 0; i < 5; ++i) {
        if (clear) text_line_clear(engine, "CHRSELFONT11", player, i);
        else text_set_string(engine, "CHRSELFONT11", player, i, std::u16string(1, row.chars[(scroll_ + i) % row.count]), 1, false);
    }
}

void NameSelect::draw_name(Engine& engine, int player, bool clear) {
    int i = 0;
    for (; !clear && i < kNameLength && name_[std::size_t(i)]; ++i) {
        const int state = state_[std::size_t(i)];
        if (state == 1 || state == 2)
            text_set_string(engine, "CHRSELFONT13", player, i, std::u16string(1, name_[std::size_t(i)]), 1, state == 1);
    }
    for (; i < kNameLength; ++i) text_line_clear(engine, "CHRSELFONT13", player, i);
}

bool NameSelect::update(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, bool input) {
    if (done_) return true;
    if (static const bool trace = std::getenv("DW4_TRACE_SAVE") != nullptr; trace) {
        static int last = -1;
        if (phase_ * 100 + step_ != last) {
            last = phase_ * 100 + step_;
            engine.log("namesel phase " + std::to_string(phase_) + " step " + std::to_string(step_) + " pos " + std::to_string(pos_));
        }
    }
    const Pad in = input ? read_pad(engine, player) : Pad{};
    const std::uint16_t trigger = std::uint16_t(in.trigger);
    auto step_to = [&](int step) { step_ = step; };
    auto phase_to = [&](int phase) {
        phase_ = phase;
        step_ = 0;
    };
    auto reel = [&](const std::string& seq, bool track) { models.set(engine, player, "j_namecursorus", seq, track); };
    auto store_current = [&] { name_[std::size_t(pos_)] = current(); };
    auto arrows = [&] {
        const bool wide = kNameRows[row_].count > 5;
        models.set(engine, player, "j_namecursorleft", wide ? "SQ_SELECT00" : "SQ_NOSELECT", false);
        models.set(engine, player, "j_namecursorright", wide ? "SQ_SELECT00" : "SQ_NOSELECT", false);
        arrows_ = wide;
    };
    switch (phase_) {
    case 0: // open the reel
        if (step_ == 0) {
            models.open_at(engine, player, "j_digifacewin", kPartnerRows[std::clamp(digimon_, 0, 15)].open, position, true);
            for (const char* model : {"j_namecursor01", "j_namecursor02", "j_namewin01", "j_namecursordown", "j_namecursorup",
                                      "j_namecursorleft", "j_namecursorright"})
                models.open_at(engine, player, model, "SQ_OPEN", position, true);
            arrows_ = true;
            store_current();
            models.open_at(engine, player, "j_namecursorus", numbered("SQ_NAMESL0", pos_), position, false);
            draw_rows(engine, player, false);
            draw_chars(engine, player, false);
            draw_name(engine, player, false);
            step_to(1);
        } else if (step_ == 1) {
            if (models.sequences_done()) step_to(2);
        } else if (step_ == 2) {
            if (pos_ >= kNameLength) pos_ = kNameLength - 1;
            models.set(engine, player, "j_namecursor02", numbered("SQ_SELECT0", cursor_), true);
            step_to(3);
        } else if (step_ == 3 && models.sequences_done()) {
            phase_to(1);
        }
        break;
    case 1: // edit
        if (step_ == 0) {
            const int count = kNameRows[row_].count;
            if (in.right || in.left) {
                if (in.right ? ++cursor_ < 5 : --cursor_ >= 0) {
                    models.set(engine, player, "j_namecursor02", numbered("SQ_SELECT0", cursor_), true);
                    reel(numbered("SQ_NAMECG0", pos_), true);
                } else {
                    cursor_ = in.right ? 4 : 0;
                    if (count > 5) {
                        scroll_ = in.right ? (scroll_ + 1) % count : (scroll_ + count - 1) % count;
                        if (arrows_) {
                            models.set(engine, player, in.right ? "j_namecursorright" : "j_namecursorleft", "SQ_ON00", true);
                            reel(numbered("SQ_NAMECG0", pos_), true);
                        }
                        draw_chars(engine, player, false);
                    }
                }
                store_current();
                draw_name(engine, player, false);
                step_to(1);
            } else if (in.up || in.down) {
                models.set(engine, player, in.up ? "j_namecursorup" : "j_namecursordown", "SQ_ON00", true);
                reel(numbered("SQ_NAMECG0", pos_), true);
                if (in.up) {
                    if (--row_ < kNameRowStart) row_ = kNameRowEnd - 1;
                } else if (++row_ >= kNameRowEnd) {
                    row_ = kNameRowStart;
                }
                arrows();
                scroll_ = 0;
                store_current();
                step_to(1);
                draw_name(engine, player, false);
                draw_rows(engine, player, false);
                draw_chars(engine, player, false);
            } else if (trigger & dw4::pad_button::a) {
                if (pos_ < kNameLength) {
                    saved_[std::size_t(pos_)] = {row_, scroll_, cursor_};
                    state_[std::size_t(pos_)] = 2;
                    store_current();
                    reel(numbered("SQ_NAMEON0", pos_), true);
                    ++pos_;
                    name_[std::size_t(pos_ + 1)] = 0;
                    state_[std::size_t(pos_)] = 1;
                    step_to(3);
                }
                draw_name(engine, player, false);
            } else if (trigger & dw4::pad_button::b) {
                name_[std::size_t(pos_)] = 0;
                --pos_;
                reel("SQ_NAMECANCEL", true);
                if (pos_ >= 0) {
                    row_ = saved_[std::size_t(pos_)].row;
                    scroll_ = saved_[std::size_t(pos_)].scroll;
                    cursor_ = saved_[std::size_t(pos_)].cursor;
                    store_current();
                    models.set(engine, player, "j_namecursor02", numbered("SQ_SELECT0", cursor_), true);
                    draw_chars(engine, player, false);
                    draw_rows(engine, player, false);
                    step_to(2);
                } else {
                    pos_ = -1;
                    phase_to(4); // leave the name entry
                }
                draw_name(engine, player, false);
            } else if ((trigger & dw4::pad_button::start) && pos_ > 0) {
                reel(numbered("SQ_NAMEON0", pos_), true);
                name_[std::size_t(pos_)] = 0;
                --pos_;
                draw_name(engine, player, false);
                step_to(4);
            }
        } else if (step_ == 1) {
            if (models.sequences_done()) step_to(0);
        } else if (step_ == 2) {
            if (models.sequences_done()) {
                reel(numbered("SQ_NAMESL0", pos_), false);
                state_[std::size_t(pos_)] = 1;
                draw_name(engine, player, false);
                step_to(0);
            }
        } else if (step_ == 3) {
            if (models.sequences_done()) {
                if (pos_ < kNameLength) {
                    reel(numbered("SQ_NAMESL0", pos_), false);
                    store_current();
                    draw_name(engine, player, false);
                } else {
                    pos_ = kNameLength - 1;
                    phase_to(2);
                }
                for (int i = pos_ + 1; i < kNameLength; ++i) name_[std::size_t(i)] = 0;
                if (phase_ == 1) step_to(0);
            }
        } else if (step_ == 4 && models.sequences_done()) {
            phase_to(2);
        }
        break;
    case 2:
        // 803572D8 waits for SQ_LINESL00 to leave; that sequence is a single
        // held action here, so the reel shows it and the question follows.
        reel("SQ_LINESL00", false);
        phase_to(3);
        break;
    case 3: // "Do you want to assign this name?" is asked by the panel
        if (step_ == 0) {
            asking_ = true;
            answered_ = false;
            step_to(1);
        } else if (step_ == 1) {
            if (answered_) {
                asking_ = false;
                confirmed_ = yes_;
                step_to(yes_ ? 2 : 4);
            }
        } else if (step_ == 2) {
            models.close(engine, player, "j_namecursorus", "SQ_LINEON00", true);
            step_to(3);
        } else if (step_ == 3) {
            if (models.closes_done()) phase_to(5);
        } else if (step_ == 4) {
            row_ = saved_[std::size_t(pos_)].row;
            scroll_ = saved_[std::size_t(pos_)].scroll;
            cursor_ = saved_[std::size_t(pos_)].cursor;
            state_[std::size_t(pos_)] = 1;
            reel(numbered("SQ_NAMESL0", pos_), false);
            models.set(engine, player, "j_namecursor02", numbered("SQ_SELECT0", cursor_), true);
            draw_chars(engine, player, false);
            draw_rows(engine, player, false);
            draw_name(engine, player, false);
            step_to(5);
        } else if (step_ == 5 && models.closes_done()) {
            phase_to(1);
        }
        break;
    case 4: // cancelled
        if (step_ == 0) {
            models.close(engine, player, "j_namecursorus", "SQ_CLOSE", true);
            step_to(1);
        } else if (models.closes_done()) {
            phase_to(5);
        }
        break;
    case 5: // close
        if (step_ == 0) {
            draw_rows(engine, player, true);
            draw_chars(engine, player, true);
            draw_name(engine, player, true);
            for (const char* model : {"j_digifacewin", "j_namewin01", "j_namecursor01", "j_namecursor02", "j_namecursordown",
                                      "j_namecursorup", "j_namecursorleft", "j_namecursorright"})
                models.close(engine, player, model, "SQ_CLOSE", true);
            step_to(1);
        } else if (models.closes_done()) {
            done_ = true;
        }
        break;
    default: break;
    }
    return done_;
}

// ---------------------------------------------------------------------------
// DIFFSELECT
void DiffSelect::open(int kind, int player) {
    cursor_ = 0;
    player_ = player;
    kind_ = kind;
    done_ = cancelled_ = false;
    phase_ = step_ = 0;
    count_ = kind == 1 ? 3 : 2;
}

bool DiffSelect::update(Engine& engine, CommonModelControl& models, MessageWindow& message, const Vec3f& position,
                        bool input) {
    if (done_) return true;
    const Pad in = input ? read_pad(engine, player_) : Pad{};
    const std::string window = kind_ == 1 ? "j_linewin03" : "j_linewin02";
    const int choices = (player_ << 16) | (kind_ == 1 ? 0x403 : 0x402);
    if (phase_ == 0) {
        if (step_ == 0) {
            models.open_at(engine, player_, window, "SQ_OPEN", position, true);
            step_ = 1;
        } else if (models.sequences_done()) {
            text_order_show(engine, choices);
            message.show((player_ << 16) | 0x404); // Select Difficulty
            phase_ = 1;
            step_ = 0;
        }
    } else if (phase_ == 1) {
        if (step_ == 1) {
            if (models.sequences_done()) step_ = 0;
        } else if (in.up) {
            if (--cursor_ >= 0) {
                models.set(engine, player_, window, kind_ == 1 && cursor_ ? "SQ_LINEUP01" : "SQ_LINEUP00", true);
                step_ = 1;
            } else {
                cursor_ = 0;
            }
        } else if (in.down) {
            if (++cursor_ <= count_ - 1) {
                models.set(engine, player_, window, kind_ == 1 && cursor_ == 2 ? "SQ_LINEDW02" : "SQ_LINEDW01", true);
                step_ = 1;
            } else {
                cursor_ = count_ - 1;
            }
        } else if (in.trigger & dw4::pad_button::a) {
            phase_ = 2;
            step_ = 0;
        } else if (in.trigger & dw4::pad_button::b) {
            cancelled_ = true;
            phase_ = 2;
            step_ = 0;
        }
    } else if (phase_ == 2) {
        if (step_ == 0) {
            text_order_clear(engine, choices);
            static const char* kPicks[3] = {"SQ_LINEON00", "SQ_LINEON01", "SQ_LINEON02"};
            models.close(engine, player_, window, cancelled_ ? "SQ_CANCEL" : kPicks[std::clamp(cursor_, 0, 2)], true);
            step_ = 1;
        } else if (models.closes_done()) {
            done_ = true;
        }
    }
    return done_;
}

// ---------------------------------------------------------------------------
// SAVECTRL
void SaveCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<CtrlRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        engine.messenger().add_row("SAVECTRL", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

bool SaveCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    (void)engine;
    (void)packet;
    auto& ram = static_cast<CtrlRam&>(base);
    if (command == "GENERATE") {
        if (ram.created) return true;
        ram.created = true;
        ram.state = 1;
        ram.sub = ram.step = ram.detail = 0;
        common_ = SaveCommon{};
        common_.active = true;
        return true;
    }
    return command == "GENERATE_PS" || command == "CLOSE_WIN" || command == "GETCOMMON";
}

void SaveCtrlManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.created) {
            send(engine, "SAVECTRL", std::nullopt, -1, "GENERATE");
            continue;
        }
        auto enter = [&](int state) {
            ram.state = state;
            ram.sub = ram.step = ram.detail = 0;
        };
        auto next_sub = [&] { ++ram.sub; ram.step = ram.detail = 0; };
        auto next_step = [&] { ++ram.step; ram.detail = 0; };
        auto& io = common_.io;
        switch (ram.state) {
        case 1: // memory card worker start-up
            if (ram.sub == 0) {
                io.generate(engine);
                next_sub();
            } else if (ram.sub == 1 && io.is_ready(engine)) {
                enter(2);
            }
            break;
        case 2:
            if (ram.sub == 0) {
                send(engine, "MODELCTRL", "j_chrselback", 0, "GENERATE");
                send(engine, "MODELCTRL", "j_chrselback", 0, "SQ_OPEN");
                next_sub();
            } else if (ram.sub == 1) {
                if (!query(engine, "MODELCTRL", "j_chrselback", 0, "ISSEQNAME", "SQ_OPEN")) next_sub();
            } else if (ram.sub == 2) {
                send(engine, "SAVEINTF_ALL", std::nullopt, -1, "GENERATE");
                next_sub();
            } else if (ram.sub == 3) {
                if (common_.state == 0) common_.update(engine);
                else if (common_.state == 3) next_sub();
            } else if (ram.sub == 4) {
                if (ram.step == 0) {
                    for (auto& p : common_.players) {
                        p.closed = false;
                        p.state = 2;
                        p.sub = p.detail = p.detail2 = 0;
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
                    send(engine, "SAVEINTF_ALL", std::nullopt, -1, "SETSELECT_DATA");
                    // 0x80378ECC..0x80378FB0: the players still in count for
                    // NINZU; the bit capsule of each one who left is
                    // forgotten (BKCHIPSTAGE[i] = null).
                    int joined = 0;
                    auto* sys = engine.get<GameSystem>();
                    for (std::size_t i = 0; i < common_.players.size(); ++i) {
                        if (common_.players[i].joined) ++joined;
                        else if (sys && i < sys->bit_chips.size()) sys->bit_chips[i].stage.clear();
                    }
                    if (sys) sys->ninzu = joined;
                    enter(3);
                }
            }
            break;
        case 3: // memory card worker end, then the lobby or the title
            if (ram.sub == 0) {
                io.remove(engine);
                next_sub();
            } else if (ram.sub == 1 && io.gone(engine)) {
                int joined = 0;
                for (const auto& p : common_.players) joined += p.joined ? 1 : 0;
                Packet stage;
                if (joined > 0) {
                    Packet point;
                    point.ints.push_back(2);
                    engine.messenger().send(engine, "PLAYERPOINT", std::nullopt, -1, "SETGENERATEPOS", point);
                    stage.strings.emplace_back(std::string("stagML12"));
                } else {
                    stage.strings.emplace_back(std::string("stagis10"));
                }
                engine.messenger().send(engine, "STAGECTL", std::nullopt, -1, "NEXTSTAGE", stage);
                enter(4);
            }
            break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// SAVEINTF_ALL
void SaveIntfManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<PanelRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        engine.messenger().add_row("SAVEINTF_ALL", ram->info_name, ram->index, ram.get());
        engine.messenger().add_row("SAVEINTF", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

bool SaveIntfManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<PanelRam&>(base);
    const int idx = std::clamp(ram.index, 0, 3);
    if (command == "GENERATE") {
        if (ram.created) return true;
        ram.created = true;
        ram.entry = true;
        ram.ready = false;         // CTRL +17
        ram.on_control = true;     // CTRL +19
        ram.state = ram.sub = ram.step = ram.detail = 0;
        ram.saved = false;
        ram.position = kPanelPositions[idx];
        ram.models.reset();
        ram.message = MessageWindow{};
        ram.windows = {RecordWindow(false), RecordWindow(true)};
        ram.slots = SlotSelect{};
        ram.boxes = DataSelect{};
        if (idx == 0) ram.entry = true; // CTRL +18 preset for controller 1
        else ram.entry = false;
        return true;
    }
    if (!ram.created) return true;
    if (command == "LINKCOMMON") return true;
    if (command == "ISENTRY") { packet.bools.push_back(ram.entry ? 1 : 0); return true; }
    if (command == "ISRDY") { packet.bools.push_back(ram.ready ? 1 : 0); return true; }
    if (command == "ONCTRL") { ram.on_control = true; return true; }
    if (command == "OFFCTRL") { ram.on_control = false; return true; }
    if (command == "SETSELECT_DATA") {
        auto* ctrl = engine.get<SaveCtrlManager>();
        auto* sys = engine.get<GameSystem>();
        // GAMERAM GAMEREMOVE: players who ended the game leave the party.
        if (ctrl && sys && !ctrl->common().players[std::size_t(idx)].joined)
            sys->players[std::size_t(idx)] = GameSystem::PlayerSlot{};
        return true;
    }
    return false;
}

void SaveIntfManager::update(Engine& engine) {
    auto* ctrl = engine.get<SaveCtrlManager>();
    if (!ctrl || !ctrl->common().active) return;
    for (auto& ram : rams_)
        if (ram->created) update_panel(engine, *ram, ctrl->common());
}

void SaveIntfManager::update_panel(Engine& engine, PanelRam& ram, SaveCommon& common) {
    const int idx = std::clamp(ram.index, 0, 3);
    auto& entry = common.players[std::size_t(idx)];
    ram.models.update(engine);
    ram.message.update(engine, idx);
    // The status windows advance only when polled (80353394), like the original.
    auto window_done = [&](int i) { return ram.windows[std::size_t(i)].update(engine, ram.models, idx, ram.position, common); };
    std::uint16_t trigger = 0;
    if (entry.active) trigger = std::uint16_t(engine.pads()[std::size_t(idx)].buttons.trigger);
    static const bool trace = std::getenv("DW4_TRACE_SAVE") != nullptr;
    if (trace) {
        const std::array<int, 8> now{ram.state, ram.sub, ram.step, ram.detail, entry.state, entry.sub, entry.detail, entry.detail2};
        if (now != ram.traced) {
            ram.traced = now;
            char line[160];
            std::snprintf(line, sizeof line, "save panel %d: ctrl %d/%d/%d/%d player %d/%d/%d/%d result %d", idx, now[0], now[1],
                          now[2], now[3], now[4], now[5], now[6], now[7], entry.result);
            engine.log(line);
        }
    }

    switch (entry.state) {
    case 0: update_ctrl(engine, ram, common, trigger); break;
    case 1: { // another panel uses the card: "Please wait . . ."
        bool frozen = false;
        if (entry.sub == 1) {
            if (--entry.counter <= 0) {
                entry.counter = 0;
                frozen = true;
            }
        } else if (entry.sub == 0) {
            ram.message.interrupt((idx << 16) | 0x51C);
            set_sub(entry, 1);
        }
        if (!frozen) update_ctrl(engine, ram, common, trigger);
        break;
    }
    case 2: // SAVECTRL closes the panels
        if (entry.sub == 0) {
            if (ram.state == 8) {
                ram.models.close(engine, idx, "j_chrselmesswin", "SQ_CLOSE", true);
                ram.message.close();
                ram.windows[0].hide(1);
                set_sub(entry, 2);
            } else {
                set_sub(entry, 1);
            }
            ram.models.close(engine, idx, "j_chrselbase", "SQ_CLOSE", true);
        } else if ((entry.sub == 1 || (entry.sub == 2 && window_done(0))) && ram.models.closes_done()) {
            entry.closed = true;
            set_sub(entry, 3);
        }
        break;
    case 3: card_flow(engine, ram, common); break;
    case 4: card_open_flow(engine, common, idx, ram.message, ram.slots, false); break;
    case 6: card_error_flow(engine, common, idx, ram.message, ram.slots, ram.models, ram.position, false); break;
    case 7: // release the card: every panel resumes
        for (auto& p : common.players) set_state(p, 8);
        break;
    case 8:
        ram.message.resume();
        set_state(entry, 0);
        entry.active = true;
        break;
    default: break;
    }
}

void SaveIntfManager::update_ctrl(Engine& engine, PanelRam& ram, SaveCommon& common, std::uint16_t trigger) {
    const int idx = std::clamp(ram.index, 0, 3);
    const int id = idx << 16;
    auto& entry = common.players[std::size_t(idx)];
    auto& models = ram.models;
    auto& message = ram.message;
    auto* sys = engine.get<GameSystem>();
    DataSelect::Context ctx{engine, models, message, ram.windows, common, ram.position, idx};
    auto enter = [&](int state) {
        ram.state = state;
        ram.sub = ram.step = ram.detail = 0;
    };
    auto set_ctrl_sub = [&](int sub) {
        ram.sub = sub;
        ram.step = ram.detail = 0;
    };
    auto next_ctrl_sub = [&] { set_ctrl_sub(ram.sub + 1); };
    auto next_step = [&] { ++ram.step; ram.detail = 0; };
    auto set_step = [&](int step) {
        ram.step = step;
        ram.detail = 0;
    };
    auto others_entered = [&] {
        for (int p = 1; p < 4; ++p)
            if (common.players[std::size_t(p)].entered) return true;
        return false;
    };
    auto after_save = [&] { enter(idx != 0 || !others_entered() ? 7 : 8); };
    auto window_done = [&](int i) { return ram.windows[std::size_t(i)].update(engine, models, idx, ram.position, common); };

    switch (ram.state) {
    case 0: // open the panel, take the character
        if (ram.sub == 1) {
            if (models.sequences_done()) next_ctrl_sub();
        } else if (ram.sub == 2) {
            if (sys && sys->players[std::size_t(idx)].joined) {
                // GAMERAM CURRENT2BASE, then the record (80364AEC).
                common.records[std::size_t(idx)] = sys->make_record(engine, idx);
                enter(1);
                entry.joined = false;
                entry.entered = true;
            } else if (idx != 0) {
                entry.entered = false;
                enter(9);
            } else if (sys) {
                // GAMESTART with the default partner (a direct stage start).
                sys->players[0].joined = true;
                sys->prepare_player(engine, 0);
            }
        } else {
            models.open_at(engine, idx, "j_chrselbase", "SQ_OPEN", ram.position, true);
            next_ctrl_sub();
        }
        break;
    case 1: // message window
        if (ram.sub == 1) {
            if (models.sequences_done()) {
                message.clear();
                enter(2);
            }
        } else if (ram.sub == 0) {
            models.open_at(engine, idx, "j_chrselmesswin", "SQ_OPEN", ram.position, true);
            message.open();
            next_ctrl_sub();
        }
        break;
    case 2: // Save. / Do not save.
        if (ram.sub == 1) {
            if (!message.busy()) {
                if (message.answer() == 1) next_ctrl_sub();
                else if (message.answer() == 0) enter(4);
            }
        } else if (ram.sub == 2) {
            if (ram.step == 1) {
                if (window_done(0)) after_save();
            } else if (ram.step == 0) {
                ram.windows[0].show(1, idx);
                next_step();
            }
        } else {
            message.question(id | 0x502, 0);
            next_ctrl_sub();
        }
        break;
    case 4: // memory card slot
        if (ram.sub == 0) {
            if (ram.step == 1) {
                if (ram.slots.open(engine, models, idx, ram.position, common.io)) {
                    message.show(id | 0x2024);
                    ram.slots.set_phase(1);
                    next_ctrl_sub();
                }
            } else if (ram.step == 0) {
                common.detach(idx);
                ram.slots.reset();
                ram.slots.set_phase(0);
                next_step();
            }
        } else if (ram.sub == 1) {
            if (ram.step == 0) {
                if (ram.slots.select(engine, models, idx, ram.position, entry.active, common)) {
                    if (ram.slots.skipped() || ram.slots.cancelled()) next_ctrl_sub();
                    else next_step();
                }
            } else if (ram.step == 1) {
                if (ram.detail == 0) {
                    message.show(id | 0x2004);
                    common.lock(idx, 4);
                    ++ram.detail;
                } else if (ram.detail == 1) {
                    const int result = entry.result;
                    if (result == 0) {
                        next_ctrl_sub();
                    } else if (result == 1 || result == 2 || result == 4) {
                        message.show(id | 0x2024);
                        ram.slots.reselect(engine, models, idx);
                        set_ctrl_sub(1);
                    }
                }
            }
        } else if (ram.sub == 2) {
            if (ram.step == 0) {
                ram.slots.set_phase(2);
                next_step();
            } else if (ram.step == 1 && ram.slots.close(engine, models, idx)) {
                if (ram.slots.skipped()) enter(4);
                else enter(ram.slots.cancelled() ? 2 : 5);
            }
        }
        break;
    case 5: // the file's boxes
        if (ram.sub == 0) {
            ram.boxes.open(idx, 1);
            next_ctrl_sub();
        } else if (ram.sub == 1) {
            if (ram.step == 0) {
                ram.boxes.reset();
                next_step();
            } else if (ram.step == 1 && ram.boxes.show_boxes(ctx)) {
                enter(6);
            }
        }
        break;
    case 6: // pick a box, write, "Are you done saving?"
        if (ram.sub == 0) {
            if (entry.card_missing) {
                set_ctrl_sub(100);
            } else if (ram.step == 0 || ram.step == 2) {
                ram.boxes.reset();
                next_step();
            } else if (ram.step == 1) {
                if (ram.boxes.select(ctx)) {
                    if (!ram.boxes.cancelled()) next_ctrl_sub();
                    else next_step();
                }
            } else if (ram.step == 3) {
                if (ram.boxes.close_all(ctx)) enter(4);
            }
        } else if (ram.sub == 1) {
            if (ram.step == 50) {
                ram.slots.set_phase(2);
                next_step();
            } else if (ram.step == 51) {
                if (ram.slots.close(engine, models, idx)) set_ctrl_sub(100);
            } else if (ram.step == 0) {
                common.lock(idx, 3);
                next_step();
            } else if (ram.step == 1) {
                const int result = entry.result;
                if (result == 1 || result == 2) set_step(50);
                else if (result == 3) {
                    ram.slots.unselect();
                    set_ctrl_sub(0);
                } else if (result == 0) {
                    ram.saved = true;
                    next_ctrl_sub();
                }
            }
        } else if (ram.sub == 2) {
            if (entry.card_missing) {
                set_ctrl_sub(100);
            } else if (ram.step == 0) {
                message.next(id | 0x50B);
                next_step();
            } else if (ram.step == 1) {
                if (!message.busy()) next_step();
            } else if (ram.step == 2) {
                message.question(id | 0x50D, 0);
                next_step();
            } else if (ram.step == 3) {
                if (!message.busy()) {
                    if (message.answer() == 1) set_step(4);
                    else if (message.answer() == 0) set_step(5);
                }
            } else if (ram.step == 4) { // Make another save.
                if (ram.detail == 0) {
                    ram.boxes.reset();
                    ++ram.detail;
                } else if (ram.detail == 1 && ram.boxes.reopen_cursor(ctx)) {
                    enter(6);
                }
            } else if (ram.step == 5) { // Done saving.
                if (ram.detail == 0) {
                    common.detach(idx);
                    ram.boxes.reset();
                    ++ram.detail;
                } else if (ram.detail == 1 && ram.boxes.close_boxes(ctx)) {
                    after_save();
                }
            }
        } else if (ram.sub == 100) { // the card left
            if (ram.step == 0) {
                message.cancel(engine, idx);
                ram.boxes.reset();
                next_step();
            } else if (ram.step == 1 && ram.boxes.close_all(ctx)) {
                enter(4);
            }
        }
        break;
    case 7: // Return to the lobby? / End the game.
        if (ram.sub == 1) {
            if (ram.step == 0) {
                if (!ram.saved) {
                    message.next(id | 0x527);
                    next_step();
                } else {
                    set_step(4);
                }
            } else if (ram.step == 1) {
                if (!message.busy()) next_step();
            } else if (ram.step == 2) {
                message.question(id | 0x529, 1);
                next_step();
            } else if (ram.step == 3) {
                if (!message.busy()) {
                    if (message.answer() == 1) set_step(5);
                    else if (message.answer() == 0) set_step(4);
                }
            } else if (ram.step == 4) { // quit
                if (ram.detail == 0) {
                    ram.windows[0].hide(1);
                    models.close(engine, idx, "j_chrselmesswin", "SQ_CLOSE", true);
                    message.close();
                    ++ram.detail;
                } else if (ram.detail == 1 && window_done(0) && models.closes_done()) {
                    enter(9);
                    entry.entered = false;
                    entry.joined = false;
                }
            } else if (ram.step == 5) { // back to saving
                if (ram.detail == 0) {
                    ram.windows[0].hide(0);
                    ++ram.detail;
                } else if (ram.detail == 1 && window_done(0)) {
                    enter(2);
                }
            }
        } else {
            if (ram.step == 0) {
                message.next(id | 0x50E);
                next_step();
            } else if (ram.step == 1) {
                if (!message.busy()) next_step();
            } else if (ram.step == 2) {
                message.question(id | 0x510, 0);
                next_step();
            } else if (ram.step == 3 && !message.busy()) {
                if (message.answer() == 1) next_ctrl_sub();
                else if (message.answer() == 0) enter(8);
            }
        }
        break;
    case 8: // ready; the host's START leaves
        if (ram.sub == 0) {
            entry.joined = true;
            if (idx != 0 || !common.all_confirmed()) {
                message.show(id | 0x530);
                next_ctrl_sub();
            } else {
                message.show(id | 0x51D);
                set_ctrl_sub(3);
            }
        } else if (ram.sub == 1) {
            bool cancel = false;
            if (ram.detail == 1) {
                if (!common.all_confirmed()) {
                    message.show(id | 0x530);
                    ram.detail = 0;
                } else {
                    cancel = trigger & dw4::pad_button::b;
                }
            } else if (ram.detail == 0) {
                if (idx != 0) {
                    if (common.all_confirmed()) {
                        message.show(id | 0x51F);
                        ++ram.detail;
                    } else {
                        cancel = trigger & dw4::pad_button::b;
                    }
                } else if (common.all_confirmed()) {
                    set_ctrl_sub(3);
                } else {
                    cancel = trigger & dw4::pad_button::b;
                }
            }
            if (cancel) {
                entry.joined = false;
                next_ctrl_sub();
            }
        } else if (ram.sub == 2) { // back out of the status window
            if (ram.step == 0) {
                ram.windows[0].hide(2);
                next_step();
            } else if (ram.step == 1 && window_done(0)) {
                enter(2);
            }
        } else if (ram.sub == 3) {
            if (ram.step == 0) {
                message.show(id | 0x51D);
                next_step();
            } else if (ram.step == 1) {
                if (trigger & dw4::pad_button::b) {
                    entry.joined = false;
                    set_ctrl_sub(2);
                } else if (common.all_confirmed()) {
                    if (trigger & dw4::pad_button::start) {
                        common.state = 3;
                        common.deactivate_all();
                        next_step();
                    }
                } else {
                    message.show(id | 0x530);
                    set_ctrl_sub(1);
                }
            }
        }
        break;
    case 9: // not playing on
        if (ram.sub == 0) {
            if (idx == 0) {
                message.show(id | 0x51F);
                common.state = 3;
                common.deactivate_all();
            }
            next_ctrl_sub();
        }
        break;
    default: break;
    }
}

void card_open_flow(Engine& engine, SaveCommon& common, int idx, MessageWindow& message, const SlotSelect& slots,
                    bool loading) {
    const int id = idx << 16;
    auto& entry = common.players[std::size_t(idx)];
    auto& io = common.io;
    if (loading && !io.available) return;
    const int slot = slots.chosen();
    const bool inserted = io.slots[std::size_t(std::clamp(slot, 0, 1))].inserted;
    auto finish = [&](int result) {
        set_state(entry, 7);
        entry.result = result;
    };
    auto error = [&] { set_state(entry, 6); };
    auto missing = [&] { // LABEL_539: the card left while asking
        message.cancel(engine, idx);
        finish(2);
    };
    switch (entry.sub) {
    case 0: {
        const int r = common.check(engine, idx, slot, 3);
        if (r == 1) {
            set_sub(entry, 5);
        } else if (r == 2) {
            // Loading never creates a file: a missing one is an error.
            const int code = io.result;
            if (code == 7 && (!loading || common.platform == 1)) set_sub(entry, 3);
            else if (code == 2) set_sub(entry, 5);
            else if (!loading && (code == 3 || code == 4)) set_sub(entry, 1);
            else error();
        }
        break;
    }
    case 1: // Do you want to create a new game file in this Memory Card?
        if (!inserted) {
            missing();
        } else if (const int r = common.ask(engine, idx, message, id | 0x2000, id | 0x2002, 0, 3); r == 1) {
            set_sub(entry, 2);
        } else if (r == 2) {
            finish(1);
        }
        break;
    case 2:
        if (entry.detail == 0) {
            next_detail(entry);
        } else if (entry.detail == 1) {
            const int r = common.create(engine, idx, message, slot, 3);
            if (r == 1) set_sub(entry, 5);
            else if (r == 2) {
                if (io.result == 7) set_sub(entry, 3);
                else {
                    error();
                    entry.result = 1;
                }
            }
        }
        break;
    case 3: // The Memory Card is corrupted and must be formatted.
        if (!inserted) {
            missing();
        } else if (const int r = common.ask(engine, idx, message, id | 0x2005, id | 0x2007, 1, 3); r == 1) {
            set_sub(entry, 4);
        } else if (r == 2) {
            finish(1);
        }
        break;
    case 4:
        if (entry.detail == 0) {
            message.show(id | 0x2008);
            next_detail(entry);
        } else if (entry.detail == 1) {
            const int r = common.format(engine, idx, slot, 3);
            if (r == 1) {
                if (loading) finish(4); // an empty card: pick again
                else set_sub(entry, 1);
            } else if (r == 2) {
                error();
                entry.result = 2;
            }
        }
        break;
    case 5: { // a file another player already opened
        std::shared_ptr<SaveCommon::CardData> found;
        for (const auto& card : common.cards)
            if (card->slot == slot) {
                found = card;
                break;
            }
        if (found) {
            found->users.push_back(idx);
            entry.card = found;
            finish(0);
        } else if (io.available) {
            next_sub(entry);
        }
        break;
    }
    case 6: {
        if (loading && entry.detail == 0) {
            next_detail(entry);
            break;
        }
        const int r = common.read(engine, idx, slot, 3);
        if (r == 1) finish(0);
        else if (r == 2) {
            if (io.result == 7) set_sub(entry, 3);
            else if (io.result == 8 && !loading) next_sub(entry);
            else {
                error();
                entry.result = 1;
            }
        }
        break;
    }
    case 7: // The Digimon World 4 file is corrupted.
        if (!inserted) {
            missing();
            break;
        }
        switch (entry.detail) {
        case 0: message.next(id | 0x202B); next_detail(entry); break;
        case 1:
        case 3:
            if (!message.busy()) next_detail(entry);
            break;
        case 2: message.next(id | 0x202C); next_detail(entry); break;
        case 4: message.question(id | 0x202D, 0); next_detail(entry); break;
        case 5:
            if (!message.busy()) {
                if (message.answer() == 1) finish(1);
                else if (message.answer() == 0) next_detail(entry);
            }
            break;
        case 6: { // a fresh file over the broken one
            auto card = std::make_shared<SaveCommon::CardData>();
            card->slot = slot;
            card->users.push_back(idx);
            common.cards.push_back(card);
            entry.card = card;
            finish(0);
            break;
        }
        default: break;
        }
        break;
    default: break;
    }
}

void card_error_flow(Engine& engine, SaveCommon& common, int idx, MessageWindow& message, SlotSelect& slots,
                     CommonModelControl& models, const Vec3f& position, bool loading) {
    auto& entry = common.players[std::size_t(idx)];
    auto& io = common.io;
    const int slot = slots.chosen();
    const bool inserted = io.slots[std::size_t(std::clamp(slot, 0, 1))].inserted;
    if (entry.sub != 0) return;
    if (entry.detail == 1) {
        if (!message.busy()) {
            set_state(entry, 7);
            entry.result = 1;
        }
        return;
    }
    if (entry.detail != 0) return;
    int text = 0x200E;
    if (io.formatting) {
        slots.mark(engine, models, idx, position, io.result == 12 ? slot : -1, io);
    } else if (inserted) {
        const int code = io.result;
        slots.mark(engine, models, idx, position, code == 12 ? slot : -1, io);
        switch (code) {
        case 4: text = loading ? 0x2028 : 0x200F; break; // no (Digimon World 4) game file
        case 5: text = 0x2010; break;
        case 6: text = 0x2011; break;
        case 7: text = 0x2012; break;
        case 8: text = 0x2013; break;
        case 9: text = 0x2014; break;
        case 10: text = 0x2015; break;
        case 12: text = 0x2016; break;
        default: text = loading ? 0x200D : 0x200E; break;
        }
    } else {
        slots.mark(engine, models, idx, position, slot, io);
        text = 0x2016;
    }
    message.next((idx << 16) | text);
    next_detail(entry);
}

// Player state 3 (80379790 case 3): write the character into the chosen box;
// the flow ends in state 7 with the outcome in `result`.
void SaveIntfManager::card_flow(Engine& engine, PanelRam& ram, SaveCommon& common) {
    const int idx = std::clamp(ram.index, 0, 3);
    const int id = idx << 16;
    auto& entry = common.players[std::size_t(idx)];
    auto& message = ram.message;
    auto& io = common.io;
    const int slot = ram.slots.chosen();
    const bool inserted = io.slots[std::size_t(std::clamp(slot, 0, 1))].inserted;
    DataSelect::Context ctx{engine, ram.models, message, ram.windows, common, ram.position, idx};
    auto finish = [&](int result) {
        set_state(entry, 7);
        entry.result = result;
    };
    auto error = [&] { set_state(entry, 6); };

    // State 3: write the character into the chosen box.
    switch (entry.sub) {
    case 0:
        if (entry.detail == 0) {
            ram.boxes.reset();
            next_detail(entry);
        } else if (entry.detail == 1 && ram.boxes.show_detail(ctx)) {
            next_sub(entry);
        }
        break;
    case 1: {
        const auto& card = entry.card;
        const bool used = card && card->records[std::size_t(ram.boxes.cursor())].used;
        if (entry.detail == 1) {
            if (entry.detail2 == 0) {
                ram.boxes.reset();
                ++entry.detail2;
            } else if (entry.detail2 == 1) {
                const bool done = message.answer() ? ram.boxes.back_to_boxes(ctx) : ram.boxes.hide_windows(ctx);
                if (done) next_detail(entry);
            }
        } else if (entry.detail == 2) {
            if (message.answer() == 1) {
                next_detail(entry);
            } else if (message.answer() == 0) {
                next_sub(entry);
                message.show(id | 0x200B); // Saving. Do not touch the Memory Card
            }
        } else if (entry.detail == 3) { // Select another box.
            if (entry.detail2 == 0) {
                ram.boxes.reset();
                ++entry.detail2;
            } else if (entry.detail2 == 1 && ram.boxes.reopen_cursor(ctx)) {
                finish(3);
            }
        } else if (entry.card_missing) {
            set_sub(entry, 100);
        } else if (entry.detail2 == 0) {
            message.next(id | (used ? 0x505 : 0x508));
            ++entry.detail2;
        } else if (entry.detail2 == 1 || entry.detail2 == 3) {
            if (!message.busy()) {
                if (entry.detail2 == 3) next_detail(entry);
                else ++entry.detail2;
            }
        } else if (entry.detail2 == 2) {
            message.question(id | (used ? 0x50A : 0x507), 0);
            ++entry.detail2;
        }
        break;
    }
    case 2:
        if (entry.detail == 0) {
            if (entry.detail2 == 0) {
                ram.slots.clear_flags();
                ram.slots.set_phase(0);
                ++entry.detail2;
            } else if (entry.detail2 == 1) {
                if (!inserted) ++entry.detail2;
                else if (ram.slots.open(engine, ram.models, idx, ram.position, io)) next_detail(entry);
            } else if (entry.detail2 == 2) {
                if (ram.slots.open(engine, ram.models, idx, ram.position, io)) {
                    io.result = 12;
                    error();
                }
            }
        } else if (entry.detail == 1) {
            const int r = common.check(engine, idx, slot, 3);
            if (r == 1) next_detail(entry);
            else if (r == 2) error();
        } else if (entry.detail == 2) {
            const int r = common.write(engine, idx, slot, ram.boxes.cursor(), 3);
            if (r == 1) next_sub(entry);
            else if (r == 2) error();
        }
        break;
    case 3: // after the write
        if (entry.detail2 == 0 && !inserted) entry.detail2 = 1;
        switch (entry.detail) {
        case 0:
            ram.slots.set_phase(2);
            next_detail(entry);
            break;
        case 1:
            if (ram.slots.close(engine, ram.models, idx)) next_detail(entry);
            break;
        case 2:
        case 4:
            ram.boxes.reset();
            next_detail(entry);
            break;
        case 3:
            if (ram.boxes.reopen(ctx)) next_detail(entry);
            break;
        case 5:
            if (ram.boxes.refresh_users(ctx)) {
                if (entry.detail2 == 0) finish(0);
                else next_sub(entry);
            }
            break;
        default: break;
        }
        break;
    case 4: // saved, but the card left right after
        if (entry.detail2 == 0) {
            message.next(id | 0x50B);
            ++entry.detail2;
        } else if (entry.detail2 == 1 && !message.busy()) {
            set_sub(entry, 100);
        }
        break;
    case 100:
        if (entry.detail == 0) {
            message.cancel(engine, idx);
            next_detail(entry);
            ram.boxes.reset();
        } else if (entry.detail == 1 && ram.boxes.close_all(ctx)) {
            finish(2);
        }
        break;
    default: break;
    }
}

} // namespace dw4::engine
