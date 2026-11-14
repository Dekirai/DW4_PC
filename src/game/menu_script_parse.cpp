// The menu script's parser and virtual pads (menu_script.hpp), without SDL's
// devices or a window: tests/console_glyph_tests.cpp checks them.
#include "menu_script.hpp"

#include "pad_family.hpp"

#include <cstdlib>
#include <sstream>

namespace dw4::game {

namespace {
std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    std::istringstream in{std::string(line)};
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

bool number(const std::string& text, float& out) {
    char* end = nullptr;
    out = std::strtof(text.c_str(), &end);
    return end && *end == 0 && !text.empty();
}

const char* const kPadButtons[] = {"a", "b", "x", "y", "l1", "r1", "l2", "r2", "start", "select", "up", "down", "left", "right", "thor-l2", "thor-r2"};
const char* const kKeys[] = {"up", "down", "left", "right", "enter", "escape", "backspace", "pageup", "pagedown", "home", "end", "delete"};

template <typename List>
bool listed(const List& list, const std::string& name) {
    for (const char* entry : list)
        if (name == entry) return true;
    return false;
}

} // namespace

std::optional<std::vector<MenuCommand>> parse_menu_script(std::string_view text, std::string& error) {
    std::vector<MenuCommand> out;
    int number_of_line = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto end = text.find('\n', at);
        std::string line(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        at = end == std::string_view::npos ? text.size() + 1 : end + 1;
        ++number_of_line;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        auto w = words(line);
        if (w.empty()) continue;
        MenuCommand c;
        c.line = number_of_line;
        const std::string& cmd = w[0];
        const auto bad = [&](const std::string& why) {
            error = "line " + std::to_string(number_of_line) + ": " + why;
            return std::nullopt;
        };
        const auto hold = [&](std::size_t from, int fallback) -> bool {
            c.count = fallback;
            if (w.size() == from) return true;
            if (w.size() == from + 2 && w[from] == "hold") {
                float n = 0;
                if (!number(w[from + 1], n) || n < 1) return false;
                c.count = int(n);
                return true;
            }
            return false;
        };
        if (cmd == "wait" || cmd == "waitms") {
            float n = 0;
            if (w.size() != 2 || !number(w[1], n) || n < 0) return bad(cmd + " needs a count");
            c.kind = cmd == "wait" ? MenuCommand::Kind::wait : MenuCommand::Kind::waitms;
            c.count = int(n);
        } else if (cmd == "pad") {
            if (w.size() < 2 || !listed(kPadButtons, w[1])) return bad("pad needs a button");
            c.kind = MenuCommand::Kind::pad;
            c.name = w[1];
            if (!hold(2, 2)) return bad("pad BUTTON [hold N]");
        } else if (cmd == "stick") {
            if (w.size() < 3 || !number(w[1], c.x0) || !number(w[2], c.y0)) return bad("stick LX LY [hold N]");
            c.kind = MenuCommand::Kind::stick;
            if (!hold(3, 2)) return bad("stick LX LY [hold N]");
        } else if (cmd == "key") {
            if (w.size() != 2 || !listed(kKeys, w[1])) return bad("key needs a key name");
            c.kind = MenuCommand::Kind::key;
            c.name = w[1];
        } else if (cmd == "screen2") {
            if (w.size() != 1) return bad("screen2 takes nothing");
            c.kind = MenuCommand::Kind::screen2;
        } else if (cmd == "back" || cmd == "f1") {
            if (w.size() != 1) return bad(cmd + " takes nothing");
            c.kind = cmd == "back" ? MenuCommand::Kind::back : MenuCommand::Kind::f1;
        } else if (cmd == "tap" || cmd == "longpress") {
            if (w.size() < 3 || w.size() > 4 || !number(w[1], c.x0) || !number(w[2], c.y0) || (w.size() == 4 && w[3] != "lower"))
                return bad(cmd + " X Y [lower]");
            c.kind = cmd == "tap" ? MenuCommand::Kind::tap : MenuCommand::Kind::longpress;
            c.lower = w.size() == 4;
        } else if (cmd == "drag") {
            float ms = 0;
            if (w.size() < 6 || w.size() > 7 || !number(w[1], c.x0) || !number(w[2], c.y0) || !number(w[3], c.x1) || !number(w[4], c.y1) ||
                !number(w[5], ms) || ms < 0 || (w.size() == 7 && w[6] != "lower"))
                return bad("drag X0 Y0 X1 Y1 MS [lower]");
            c.kind = MenuCommand::Kind::drag;
            c.count = int(ms);
            c.lower = w.size() == 7;
        } else if (cmd == "type") {
            const auto from = line.find("type") + 4;
            c.kind = MenuCommand::Kind::type;
            c.name = line.substr(std::min(line.size(), from + 1));
            if (c.name.empty()) return bad("type needs text");
        } else if (cmd == "shot") {
            if (w.size() != 2) return bad("shot NAME");
            c.kind = MenuCommand::Kind::shot;
            c.name = w[1];
        } else if (cmd == "expect") {
            if (w.size() < 2) return bad("expect what?");
            c.kind = MenuCommand::Kind::expect;
            c.name = w[1];
            c.args.assign(w.begin() + 2, w.end());
            const std::size_t n = c.args.size();
            const bool ok = ((c.name == "focus" || c.name == "page" || c.name == "dialog") && n == 1) || (c.name == "value" && n >= 1) ||
                            (c.name == "setting" && n == 2) || (c.name == "log" && n >= 1) || ((c.name == "open" || c.name == "closed") && n == 0);
            if (!ok) return bad("expect focus ROW | page PATH | value ROW TEXT | setting SECTION.KEY VALUE | log TEXT | open | closed | dialog KIND");
            if (c.name == "log") c.args = {line.substr(line.find("log") + 4)};
            if (c.name == "value" && n >= 2) {
                // (The text: everything after the row's id.)
                const auto id_at = line.find(c.args[0], line.find("value") + 5);
                c.args = {c.args[0], line.substr(id_at + c.args[0].size() + 1)};
            }
        } else {
            return bad("unknown command " + cmd);
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::optional<VirtualPadSpec> virtual_pad_spec(std::string_view family) {
    // (The ids classify() reads as the family; the Thor's 17 buttons: the 15
    // of the gamepad mapping and its digital L2 / R2.)
    if (family == "thor") return VirtualPadSpec{kVendorAyn, kProductOdinController, "Odin Controller", 17};
    if (family == "xbox_one") return VirtualPadSpec{kVendorMicrosoft, 0x02ea, "Xbox One Controller", 15};
    if (family == "xbox360") return VirtualPadSpec{kVendorMicrosoft, 0x028e, "Xbox 360 Controller", 15};
    if (family == "ps5" || family == "dualsense") return VirtualPadSpec{kVendorSony, 0x0ce6, "DualSense Wireless Controller", 15};
    if (family == "ps4" || family == "dualshock4") return VirtualPadSpec{kVendorSony, 0x09cc, "Wireless Controller", 15};
    if (family == "switch_pro") return VirtualPadSpec{kVendorNintendo, kProductSwitchPro, "Pro Controller", 15};
    if (family == "generic") return VirtualPadSpec{0x1234, 0x5678, "Virtual Gamepad", 15};
    return std::nullopt;
}

} // namespace dw4::game
