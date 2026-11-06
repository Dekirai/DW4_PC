// Data Overflow (docs/SURVIVAL.md 7, round O1b lane E): the run's gear.
//
// DW4's own rows, equipped into the run player's equipment window by DW4's
// rules (recalc_player turns them into stats as in the story):
//   - the Brave Core (armor1 0-2, MAINSLOT): alpha, then beta at run level
//     22 and gamma at 37 (its LVPOINT read as the run level, 7.1), then
//     +30 / +60 / +120 / +240 value (DW4 adds it to its DEF); beta opens the
//     core pocket;
//   - one Board (armor2: Armor 21, Fire 9, Ice 12, Bolt 15, Dark 18,
//     SUBSLOT): its alpha row only, then +30 / +60 / +120 / +240 / +480
//     value, which DW4 adds to the column the board names;
//   - chips in the two pockets (abChip, POWERPOCKET / MAINPOCKET): Wisdom,
//     Spirit and Speed Chip (both pockets), the form's own PWR Medal and the
//     Drain Ram (the weapon pocket). Wisdom / Spirit / Speed and the medal
//     power up with ¶ Data at a twentieth of DW4's NEXTX (alpha to
//     alpha+240, beta+..., 7.5) and step to beta and gamma by offers; the
//     Drain Ram steps beta..epsilon by offers only (DW4: "Cannot power-up").
// The Drain Ram's HPDRAIN is DW4's (lua_host.cpp; the run caps it at 2 % of
// MaxHP a second as 4.8 asks only once O2 measures it: docs/ROUND_O1B_E_NOTES.md).
#include "dw4/engine/overflow_director.hpp"

#include "director_util.hpp"

#include <algorithm>

namespace dw4::engine::overflow {

namespace {
// 7.1-7.3 (round O1b's part). The PWR Medal's row is the fire one; the
// form's element adds 15 a step (fire 45, ice 60, thunder 75, dark 90).
const std::vector<GearRow> kGear = {
    {"brave_core", "Brave Core", GearSlot::core, 0, false, false, false, true},
    {"armor_board", "Armor Board", GearSlot::board, 21, false, false, false, true},
    {"fire_board", "Fire Board", GearSlot::board, 9, false, false, false, true},
    {"ice_board", "Ice Board", GearSlot::board, 12, false, false, false, true},
    {"bolt_board", "Bolt Board", GearSlot::board, 15, false, false, false, true},
    {"dark_board", "Dark Board", GearSlot::board, 18, false, false, false, true},
    {"wisdom_chip", "Wisdom Chip", GearSlot::chip, 0, true, true, true, true},
    {"spirit_chip", "Spirit Chip", GearSlot::chip, 15, true, true, true, true},
    {"speed_chip", "Speed Chip", GearSlot::chip, 30, true, true, true, true},
    {"pwr_medal", "PWR Medal", GearSlot::chip, 45, true, false, true, true},
    {"drain_ram", "Drain Ram", GearSlot::chip, 105, true, false, false, false},
};
constexpr int kCoreLevels[3] = {1, 22, 37};          // the core's LVPOINT as run levels (7.1)
constexpr int kCoreValues[4] = {30, 60, 120, 240};   // after gamma
constexpr int kBoardValues[5] = {30, 60, 120, 240, 480};
constexpr int kBand = 5;                             // a chip's rows a grade: alpha .. alpha+240, beta .. beta+360, gamma ..
constexpr int kRamSteps = 4;                         // the Drain Ram's beta .. epsilon

// The pocket list of a slot (0 the weapon pocket, 1 the core pocket).
std::vector<int>& pocket(GameSystem::PlayerSlot& player, int which) { return which == 0 ? player.power_pocket : player.main_pocket; }
} // namespace

const std::vector<GearRow>& gear_table() { return kGear; }

int gear_index(std::string_view key) {
    for (std::size_t i = 0; i < kGear.size(); ++i)
        if (key == kGear[i].key) return int(i);
    return -1;
}

int OverflowDirector::gear_row(int gear) const {
    if (gear < 0 || gear >= int(kGear.size())) return -1;
    const auto& row = kGear[std::size_t(gear)];
    return std::string_view(row.key) == "pwr_medal" ? row.first + 15 * element_ : row.first;
}

bool OverflowDirector::gear_open(int gear) const {
    if (gear < 0 || gear >= int(kGear.size())) return false;
    const auto& row = kGear[std::size_t(gear)];
    return row.start || std::find(config_.gear_open.begin(), config_.gear_open.end(), row.key) != config_.gear_open.end();
}

// Equip `record` of a gear into its slot (a new device; the old one in that
// slot stays in the list, unequipped, as DW4's window leaves it).
bool OverflowDirector::equip_gear(Engine& engine, int gear, int record) {
    auto* sys = engine.get<GameSystem>();
    if (!sys || gear < 0) return false;
    auto& player = sys->players[0];
    const auto& row = kGear[std::size_t(gear)];
    ItemDevice device;
    device.type = row.slot == GearSlot::core ? 1 : row.slot == GearSlot::board ? 2 : 3;
    device.record = record;
    device.flag = true;
    if (row.slot == GearSlot::core) {
        if (const auto* old = player.device(player.main_slot[0])) device.value = old->value;
        player.devices.push_back(device);
        player.main_slot[0] = int(player.devices.size()) - 1;
        core_gear_ = gear;
    } else if (row.slot == GearSlot::board) {
        if (const auto* old = player.device(player.sub_slot[0])) device.value = old->value;
        player.devices.push_back(device);
        player.sub_slot[0] = int(player.devices.size()) - 1;
        board_gear_ = gear;
    } else {
        int which = -1;
        for (int p = 0; p < 2 && which < 0; ++p)
            if (pocket_gear_[std::size_t(p)] == gear) which = p; // (a step of a chip in its pocket)
        if (which < 0) {
            const bool core_open = core_gear_ >= 0 && player.device(player.main_slot[0]) && player.device(player.main_slot[0])->record > kGear[std::size_t(core_gear_)].first;
            if (row.weapon_pocket && pocket_gear_[0] < 0) which = 0;
            else if (row.core_pocket && pocket_gear_[1] < 0 && core_open) which = 1;
        }
        if (which < 0) return false;
        player.devices.push_back(device);
        pocket(player, which)[0] = int(player.devices.size()) - 1;
        pocket_gear_[std::size_t(which)] = gear;
    }
    sys->recalc_player(engine, 0);
    sys->store_species(0);
    return true;
}

// The starting loadout (8.3): one armour (a Core or a Board) and one chip, at alpha.
void OverflowDirector::apply_loadout(Engine& engine) {
    if (const int armor = gear_index(config_.loadout_armor); armor >= 0 && gear_open(armor) && kGear[std::size_t(armor)].slot != GearSlot::chip)
        equip_gear(engine, armor, gear_row(armor));
    if (const int chip = gear_index(config_.loadout_chip); chip >= 0 && gear_open(chip) && kGear[std::size_t(chip)].slot == GearSlot::chip) {
        // (A chip only the core pocket takes needs a Core as the armour: its pocket opens for the loadout, 8.3.)
        if (!equip_gear(engine, chip, gear_row(chip)) && core_gear_ >= 0 && kGear[std::size_t(chip)].core_pocket) {
            auto* sys = engine.get<GameSystem>();
            if (sys) {
                auto& player = sys->players[0];
                ItemDevice device{3, gear_row(chip), true, 0, 0};
                player.devices.push_back(device);
                player.main_pocket[0] = int(player.devices.size()) - 1;
                pocket_gear_[1] = chip;
                sys->recalc_player(engine, 0);
                sys->store_species(0);
            }
        }
    }
    if (!config_.loadout_armor.empty() || !config_.loadout_chip.empty())
        event_log_.push_back({clock_, "loadout " + (config_.loadout_armor.empty() ? std::string("-") : config_.loadout_armor) + " " +
                                          (config_.loadout_chip.empty() ? std::string("-") : config_.loadout_chip)});
}

// The gear lines a level-up window may show (4.6), from the build.
void OverflowDirector::gear_offers(Engine& engine) {
    offer_state_.gear.clear();
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    auto& player = sys->players[0];
    const int level = player.stats.level;
    for (int g = 0; g < int(kGear.size()); ++g) {
        if (!gear_open(g)) continue;
        const auto& row = kGear[std::size_t(g)];
        if (row.slot == GearSlot::core) {
            if (core_gear_ < 0) {
                offer_state_.gear.push_back({OfferKind::core, g});
            } else if (core_gear_ == g) {
                const auto* core = player.device(player.main_slot[0]);
                const int grade = core ? core->record - row.first : 0;
                if ((grade < 2 && level >= kCoreLevels[grade + 1]) || (grade == 2 && core_steps_ < 4)) offer_state_.gear.push_back({OfferKind::core, g});
            }
        } else if (row.slot == GearSlot::board) {
            if (board_gear_ < 0 || (board_gear_ == g && board_steps_ < 5)) offer_state_.gear.push_back({OfferKind::board, g});
        } else {
            int in = -1;
            for (int p = 0; p < 2; ++p)
                if (pocket_gear_[std::size_t(p)] == g) in = p;
            if (in >= 0) {
                // Its next grade by an offer: beta, gamma (the Rams: beta .. epsilon).
                const auto* chip = player.device(pocket(player, in)[0]);
                const int offset = chip ? chip->record - gear_row(g) : 0;
                const bool more = row.x_data ? offset < 2 * kBand : offset < kRamSteps;
                if (more) offer_state_.gear.push_back({OfferKind::chip, g});
            } else {
                const bool core_open = core_gear_ >= 0 && player.device(player.main_slot[0]) &&
                                       player.device(player.main_slot[0])->record > kGear[std::size_t(core_gear_)].first;
                if ((row.weapon_pocket && pocket_gear_[0] < 0) || (row.core_pocket && pocket_gear_[1] < 0 && core_open))
                    offer_state_.gear.push_back({OfferKind::chip, g});
            }
        }
    }
}

void OverflowDirector::take_gear(Engine& engine, const Offer& offer) {
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    auto& player = sys->players[0];
    const int g = offer.value;
    if (g < 0 || g >= int(kGear.size())) return;
    const auto& row = kGear[std::size_t(g)];
    if (row.slot == GearSlot::core) {
        if (core_gear_ != g) {
            equip_gear(engine, g, gear_row(g));
        } else if (auto* core = player.main_slot[0] >= 0 ? &player.devices[std::size_t(player.main_slot[0])] : nullptr) {
            if (core->record - row.first < 2) {
                equip_gear(engine, g, core->record + 1); // beta / gamma (the value goes with it)
            } else {
                core->value = std::min(2047, core->value + kCoreValues[std::clamp(core_steps_, 0, 3)]);
                ++core_steps_;
                sys->recalc_player(engine, 0);
                sys->store_species(0);
            }
        }
    } else if (row.slot == GearSlot::board) {
        if (board_gear_ != g) {
            equip_gear(engine, g, gear_row(g));
        } else if (player.sub_slot[0] >= 0) {
            auto& board = player.devices[std::size_t(player.sub_slot[0])];
            board.value = std::min(2047, board.value + kBoardValues[std::clamp(board_steps_, 0, 4)]);
            ++board_steps_;
            sys->recalc_player(engine, 0);
            sys->store_species(0);
        }
    } else {
        int in = -1;
        for (int p = 0; p < 2; ++p)
            if (pocket_gear_[std::size_t(p)] == g) in = p;
        if (in < 0) {
            equip_gear(engine, g, gear_row(g));
        } else if (auto& index = pocket(player, in)[0]; index >= 0) {
            auto& chip = player.devices[std::size_t(index)];
            const int offset = chip.record - gear_row(g);
            // The next grade's first row (beta: alpha + 5, gamma: alpha + 10); a Ram's next row.
            chip.record = row.x_data ? gear_row(g) + (offset / kBand + 1) * kBand : chip.record + 1;
            sys->recalc_player(engine, 0);
            sys->store_species(0);
        }
    }
    if (auto* db = engine.get<DatabaseManager>()) {
        // (What it is now, for the run log and the notice.)
        (void)db;
    }
}

// ¶ Data powers the chips in the pockets up (7.5): the weapon pocket first,
// a step whenever the run's data reaches the chip's NEXTX / 20.
void OverflowDirector::power_up_chips(Engine& engine) {
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    if (!sys || !db || x_data_ <= 0) return;
    auto& player = sys->players[0];
    for (int p = 0; p < 2; ++p) {
        const int g = pocket_gear_[std::size_t(p)];
        if (g < 0 || !kGear[std::size_t(g)].x_data) continue;
        const int index = pocket(player, p)[0];
        if (index < 0) continue;
        auto& chip = player.devices[std::size_t(index)];
        const int next = db->number("abChip", chip.record, "NEXTX").value_or(-1);
        if (next <= 0) continue; // (the grade's last row: beta and gamma come by offers)
        const int cost = std::max(1, next / kXDataDivisor);
        if (x_data_ < cost) continue;
        x_data_ -= cost;
        ++chip.record;
        ++chip_steps_;
        sys->recalc_player(engine, 0);
        sys->store_species(0);
        const std::string name = detail::plain_name(db->text("abChip", chip.record, "NAME"));
        notice(name);
        event_log_.push_back({clock_, "chip " + name});
        post_effect(engine, "MODELCTRL", player.digimon, 0, "i_getitempnl01", "SQ_TECUP");
        return; // (one step a call)
    }
}

std::string OverflowDirector::gear_text(const Engine& engine, const Offer& offer) const {
    const int g = offer.value;
    if (g < 0 || g >= int(kGear.size())) return "?";
    auto* sys = const_cast<Engine&>(engine).get<GameSystem>();
    auto* db = const_cast<Engine&>(engine).get<DatabaseManager>();
    const auto& row = kGear[std::size_t(g)];
    static const char* const kTables[3] = {"armor1", "armor2", "abChip"};
    const char* table = kTables[int(row.slot)];
    // What taking it gives: the next row's own name ("Brave Core β"), or "+60" for a value step.
    int record = gear_row(g);
    std::string suffix;
    if (sys) {
        const auto& player = sys->players[0];
        if (row.slot == GearSlot::core && core_gear_ == g) {
            const auto* core = player.device(player.main_slot[0]);
            if (core && core->record - row.first < 2) record = core->record + 1;
            else if (core) return (db ? detail::plain_name(db->text(table, core->record, "NAME")) : std::string(row.name)) + " +" + std::to_string(kCoreValues[std::clamp(core_steps_, 0, 3)]);
        } else if (row.slot == GearSlot::board && board_gear_ == g) {
            return (db ? detail::plain_name(db->text(table, record, "NAME")) : std::string(row.name)) + " +" + std::to_string(kBoardValues[std::clamp(board_steps_, 0, 4)]);
        } else if (row.slot == GearSlot::chip) {
            for (int p = 0; p < 2; ++p)
                if (pocket_gear_[std::size_t(p)] == g) {
                    const auto* chip = player.device(p == 0 ? player.power_pocket[0] : player.main_pocket[0]);
                    if (chip) {
                        const int offset = chip->record - record;
                        record = row.x_data ? record + (offset / kBand + 1) * kBand : chip->record + 1;
                    }
                }
        }
    }
    return db ? detail::plain_name(db->text(table, record, "NAME")) + suffix : std::string(row.name);
}

std::string OverflowDirector::gear_detail(const Engine& engine, const Offer& offer) const {
    (void)engine;
    const int g = offer.value;
    if (g < 0 || g >= int(kGear.size())) return {};
    const auto& row = kGear[std::size_t(g)];
    switch (row.slot) {
    case GearSlot::core: return core_gear_ == g ? "Core: the next grade (DEF, MaxHP, Speed, Fire RES)" : "Core: DEF, MaxHP, Speed, Fire RES";
    case GearSlot::board: return board_gear_ == g ? "Board: its value grows" : "Board: " + std::string(row.name);
    case GearSlot::chip: {
        for (int p = 0; p < 2; ++p)
            if (pocket_gear_[std::size_t(p)] == g) return "Chip: the next grade";
        return std::string("Chip: into the ") + (row.weapon_pocket && pocket_gear_[0] < 0 ? "weapon" : "core") + " pocket";
    }
    }
    return {};
}

} // namespace dw4::engine::overflow
