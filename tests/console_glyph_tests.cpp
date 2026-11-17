// The console F1 menu's button pictures and the Back / Select rule
// (src/game/console_glyphs; docs/CONSOLE_MENU.md 7): console_face for every
// family, both menu_buttons modes and both platforms, Face::back_key's name,
// back_toggles_menu, and the menu script's parser and virtual pads
// (src/game/menu_script).
#include "../src/game/console_glyphs.hpp"
#include "../src/game/console_menu.hpp"
#include "../src/game/menu_script.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

Face face(ConsoleGlyph g, PadFamily family, const Settings& settings, MenuPlatform platform = MenuPlatform::android) {
    return console_face(g, family, settings, platform).face;
}

void glyphs() {
    Settings layout;
    // The Thor (Nintendo layout by label): A confirms, B goes back, L1 / R1, its START arrow, Back closes on Android.
    check(face(ConsoleGlyph::ok, PadFamily::thor, layout) == Face::letter_a, "Thor OK = A");
    check(face(ConsoleGlyph::back, PadFamily::thor, layout) == Face::letter_b, "Thor BACK = B");
    check(face(ConsoleGlyph::del, PadFamily::thor, layout) == Face::letter_x, "Thor DEL = X (the binding space's west button by label)");
    check(face(ConsoleGlyph::prev, PadFamily::thor, layout) == Face::l1 && face(ConsoleGlyph::next, PadFamily::thor, layout) == Face::r1, "Thor L1 / R1");
    check(face(ConsoleGlyph::l2, PadFamily::thor, layout) == Face::l2, "Thor L2");
    check(face(ConsoleGlyph::start, PadFamily::thor, layout) == Face::start_arrow, "Thor START: the arrow");
    check(face(ConsoleGlyph::close, PadFamily::thor, layout) == Face::back_key, "Android, Thor: CLOSE is the Back key");
    check(face(ConsoleGlyph::close, PadFamily::thor, layout, MenuPlatform::pc) == Face::select_square, "PC, Thor: CLOSE is its SELECT");
    check(std::string(face_name(Face::back_key)) == "Back", "back_key's name");
    check(face(ConsoleGlyph::look, PadFamily::thor, layout) == Face::letter_y, "Thor LOOK = Y (the north button by label)");
    check(face(ConsoleGlyph::look, PadFamily::xbox_one, layout) == Face::letter_y, "Xbox LOOK = Y");
    check(console_faces(ConsoleGlyph::look, PadFamily::keyboard, layout, MenuPlatform::pc).empty(), "keyboard: no LOOK");
    // Xbox: A / B / X, LB / RB, LT, Menu; CLOSE the View button.
    check(face(ConsoleGlyph::ok, PadFamily::xbox_one, layout) == Face::letter_a && face(ConsoleGlyph::back, PadFamily::xbox_one, layout) == Face::letter_b,
          "Xbox A / B");
    check(face(ConsoleGlyph::prev, PadFamily::xbox_one, layout) == Face::lb && face(ConsoleGlyph::l2, PadFamily::xbox_one, layout) == Face::lt, "Xbox LB, LT");
    check(face(ConsoleGlyph::close, PadFamily::xbox_one, layout) == Face::view, "Xbox CLOSE: View");
    // PlayStation: Cross / Circle / Square, L1, Options.
    check(face(ConsoleGlyph::ok, PadFamily::ps5, layout) == Face::cross && face(ConsoleGlyph::del, PadFamily::ps5, layout) == Face::square, "DualSense");
    check(face(ConsoleGlyph::start, PadFamily::ps5, layout) == Face::options, "DualSense START: Options");
    // menu_buttons=bindings: the game's A / B / X and L / R; no L2 / R2.
    Settings bindings;
    bindings.menu_buttons = MenuButtons::bindings;
    check(console_faces(ConsoleGlyph::l2, PadFamily::xbox_one, bindings, MenuPlatform::android).empty(), "bindings: no L2");
    check(face(ConsoleGlyph::ok, PadFamily::xbox_one, bindings) == Face::letter_a, "bindings: the game's A");
    // The keyboard: key caps.
    check(console_face(ConsoleGlyph::ok, PadFamily::keyboard, layout, MenuPlatform::pc).key == "Return", "keyboard OK: Enter");
    check(console_face(ConsoleGlyph::prev, PadFamily::keyboard, layout, MenuPlatform::pc).key == "PageUp", "keyboard PREV: PgUp");
    // Pairs.
    const auto lr = console_faces(ConsoleGlyph::lr, PadFamily::thor, layout, MenuPlatform::android);
    check(lr.size() == 2 && lr[0].face == Face::dpad_left && lr[1].face == Face::dpad_right, "LR: the D-pad's left and right");
    // A GameCube pad on its original layout: its own A and B.
    check(face(ConsoleGlyph::ok, PadFamily::gamecube, layout) == Face::letter_a, "GameCube A");
}

void back_rule() {
    check(!back_toggles_menu(MenuPlatform::android, PadFamily::thor), "Android + Thor: Select stays a game button");
    check(back_toggles_menu(MenuPlatform::android, PadFamily::xbox_one), "Android, another pad: as before");
    check(back_toggles_menu(MenuPlatform::pc, PadFamily::thor), "the PC: as before");
}

void script_parser() {
    std::string error;
    const auto parsed = parse_menu_script("# a comment\nwait 3\npad a hold 4\nstick 0.5 -1\nkey enter\nback\nf1\ntap 10 20 lower\n"
                                          "longpress 1 2\ndrag 1 2 3 4 300\ntype hello world\nshot pic\nexpect focus general.volume\n"
                                          "expect value saves.state.1 Save|Load\nexpect setting audio.volume 95\nexpect log menu: open (the Back key)\n"
                                          "expect closed\nexpect dialog confirm\nwaitms 250\n",
                                          error);
    check(parsed.has_value() && parsed->size() == 18, "a script parses: " + error);
    const auto& c = *parsed;
    check(c[1].kind == MenuCommand::Kind::pad && c[1].name == "a" && c[1].count == 4, "pad a hold 4");
    {
        std::string held_error;
        const auto held = parse_menu_script("pad a down\npad a up\n", held_error);
        check(held && held->size() == 2 && (*held)[0].count == -1 && (*held)[1].count == -2, "pad a down / up");
    }
    check(c[2].kind == MenuCommand::Kind::stick && c[2].x0 == 0.5f && c[2].y0 == -1.0f && c[2].count == 2, "stick");
    check(c[6].kind == MenuCommand::Kind::tap && c[6].lower && c[6].x0 == 10 && c[6].y0 == 20, "tap ... lower");
    check(c[8].kind == MenuCommand::Kind::drag && c[8].count == 300 && c[8].x1 == 3, "drag");
    check(c[9].name == "hello world", "type keeps spaces");
    check(c[12].args.size() == 2 && c[12].args[1] == "Save|Load", "expect value ROW TEXT");
    check(c[14].args.size() == 1 && c[14].args[0] == "menu: open (the Back key)", "expect log keeps the text");
    for (const char* bad : {"pad q\n", "wait\n", "tap 1\n", "drag 1 2 3 4\n", "expect focus\n", "jump 3\n", "pad a hold\n", "key f13\n"}) {
        error.clear();
        check(!parse_menu_script(bad, error) && error.rfind("line 1: ", 0) == 0, std::string("rejected: ") + bad);
    }
    check(virtual_pad_spec("thor") && virtual_pad_spec("thor")->buttons == 17 && virtual_pad_spec("thor")->vendor == kVendorAyn,
          "the Thor's virtual pad: 17 buttons, 2020:0111");
    check(!virtual_pad_spec("nes"), "an unknown family");
}
} // namespace

int main() {
    try {
        glyphs();
        back_rule();
        script_parser();
    } catch (const std::exception& error) {
        std::cerr << "console glyph tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "console glyph tests passed (" << checks << " checks)\n";
    return 0;
}
