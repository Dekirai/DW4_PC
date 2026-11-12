// Round I1b (docs/SEAMLESS.md 19.7.4): Engine::pending_departure_choice, the
// departure census's read-only accessor (the census itself is in
// include/dw4/engine/online_departure.hpp). The gate form of the arrival
// prompt holds the own player's confirming press out of the pad stream while
// this names a destination outside the hub stages; nothing here changes the
// simulation.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>

namespace dw4::engine {
namespace {

// The case records of a GETSELECT switch: the destination of `value`'s case
// (its STAGECTL NEXTSTAGE and the PLAYERPOINT SETGENERATEPOS before it), as
// DemoManager::update would run them. False when the case names no stage.
bool case_destination(const std::vector<DemoRecord>& records, std::size_t switch_at, int value, std::string& stage, int& point) {
    std::size_t k = switch_at + 1;
    for (; k < records.size(); ++k) {
        if (records[k].type == 5) return false;
        if (records[k].type == 4 && records[k].case_value == value) break;
    }
    if (k >= records.size()) return false;
    bool found = false;
    point = 0;
    for (++k; k < records.size(); ++k) {
        const auto& r = records[k];
        if (r.type == 4 || r.type == 5) break;
        if (r.category == "PLAYERPOINT" && r.command == "SETGENERATEPOS" && r.argument) {
            Packet at;
            parse_packet_arguments(*r.argument, at);
            if (!at.ints.empty()) point = int(at.ints[0]);
        } else if (r.category == "STAGECTL" && r.command == "NEXTSTAGE" && r.argument && !r.argument->empty()) {
            Packet to;
            parse_packet_arguments(*r.argument, to);
            stage = to.has_string(0) ? std::string(to.string(0)) : *r.argument;
            found = true;
            break; // (the departure: what follows is the next stage's)
        }
    }
    return found;
}

} // namespace

std::optional<DepartureChoice> Engine::pending_departure_choice(int own_slot) const {
    if (!hub_stage(stage_name_) || !pending_stage_.empty() || loading() || rebooting_) return std::nullopt;
    const auto* selects = get<SelectCtrlManager>();
    if (!selects) return std::nullopt;
    for (const auto& menu : selects->open_selects()) {
        if (!menu.taking_input) continue;
        if (pad_reader_slot(menu.index, menu.trigger) != own_slot) continue;
        if (menu.cursor < 0 || menu.cursor >= menu.items) continue;
        // (A line the menu refuses answers A with CURSOR_NG: no departure.)
        if (!menu.enabled.empty() && std::size_t(menu.cursor) < menu.enabled.size() && !menu.enabled[std::size_t(menu.cursor)]) continue;
        // The demo work that waits on this menu: its GETSELECT switch at or
        // after the work's cursor.
        for (const auto& work : demo_.works()) {
            const auto& records = work->records;
            for (std::size_t k = work->cursor; k < records.size(); ++k) {
                const auto& r = records[k];
                if (r.type != 3 || r.category != "SELECTCTRL" || r.command != "GETSELECT" || !r.name || *r.name != menu.name ||
                    r.index != menu.index)
                    continue;
                DepartureChoice choice;
                if (!case_destination(records, k, menu.cursor, choice.stage, choice.point)) return std::nullopt;
                if (hub_stage(choice.stage)) return std::nullopt; // (a move between hub stages: home anyway)
                choice.menu = menu.name;
                choice.cursor = menu.cursor;
                choice.items = menu.items;
                choice.reader = own_slot;
                choice.purpose = menu.name == "i_zzstchgcur" ? DepartureChoice::Purpose::gate
                                 : menu.name == "i_zzstchgwn" ? DepartureChoice::Purpose::return_mat
                                                              : DepartureChoice::Purpose::other;
                // (The lobby's point next to the mat, for *Back to the lobby*: online_departure.hpp.)
                if (stage_name_ == "stagml11")
                    choice.back_point = choice.purpose == DepartureChoice::Purpose::gate         ? 1
                                        : choice.purpose == DepartureChoice::Purpose::return_mat ? 3
                                                                                                 : -1;
                return choice;
            }
        }
    }
    return std::nullopt;
}

} // namespace dw4::engine
