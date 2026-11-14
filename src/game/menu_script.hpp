#pragma once

// The menu script (docs/CONSOLE_MENU.md 15): a test run's driver of the F1
// menus through the inputs a player has, on the PC, the emulator and the
// AYN Thor alike (injected key events do not reach SDL's joysticks on the
// Thor, so the controller is a virtual SDL gamepad the script attaches: by
// default the Thor's own, "Odin Controller" 2020:0111 with 17 buttons whose
// last two are the digital L2 / R2 outside the gamepad mapping, or the
// family DW4_MENU_PAD names). Its events take the real pad's way (host_event:
// the binding capture, the Back / Select rule; read_pad, menu_keys, the
// console menu). One command per line, run once per pass; '#' starts a
// comment:
//
//   wait N                      N passes
//   waitms N                    N milliseconds (the menus' timings are in real time)
//   pad BUTTON [hold N]         a, b, x, y, l1, r1, l2, r2, start, select, up, down, left, right,
//                               thor-l2, thor-r2 (joystick buttons 15 / 16); held N passes (2)
//   stick LX LY [hold N]        the left stick (-1..1, y up)
//   key NAME                    up down left right enter escape backspace pageup pagedown home end delete
//   back                        Android's Back key (SDLK_AC_BACK)
//   f1                          the F1 key
//   tap X Y [lower]             a finger's tap at X, Y (menu units); lower: the second screen
//   longpress X Y [lower]       held 0.8 s
//   drag X0 Y0 X1 Y1 MS [lower] down, moved over MS ms, up
//   type TEXT                   characters into the active text field
//   shot NAME                   NAME.bmp beside --screenshot's file (and NAME_screen2.bmp)
//   expect focus ROW | page PATH | value ROW TEXT | setting SECTION.KEY VALUE | log TEXT
//          | open | closed | dialog KIND
//
// A failed expect logs "menu script: FAILED line N: ..."; at the end the
// run quits (exit code 3 when an expect failed).
#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

class ConsoleMenu;
struct HostView;

struct MenuCommand {
    enum class Kind : std::uint8_t { wait, waitms, pad, stick, key, back, f1, tap, longpress, drag, type, shot, expect };
    Kind kind = Kind::wait;
    int line = 0;
    std::string name;              // pad: the button; key: its name; expect: what; shot / type: the text
    std::vector<std::string> args; // expect's arguments
    int count = 0;                 // wait / waitms / hold / drag ms
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool lower = false;
};

// The script's lines (empty lines and comments dropped); nullopt and
// `error` ("line N: ...") on a malformed line.
std::optional<std::vector<MenuCommand>> parse_menu_script(std::string_view text, std::string& error);

// The virtual pad of a family id ("thor", "xbox_one", ...): its USB ids,
// name and button count.
struct VirtualPadSpec {
    std::uint16_t vendor = 0, product = 0;
    const char* name = "";
    int buttons = 0;
};
std::optional<VirtualPadSpec> virtual_pad_spec(std::string_view family);

class MenuScript {
public:
    MenuScript() = default;
    ~MenuScript();
    MenuScript(const MenuScript&) = delete;
    MenuScript& operator=(const MenuScript&) = delete;
    bool load(const std::filesystem::path& file, std::string& error);
    // The virtual pad alone (DW4_MENU_PAD without a script).
    bool attach_pad(const std::string& family, std::string& error);
    bool active() const { return pad_ != 0 || !commands_.empty(); }
    bool running() const { return at_ < commands_.size(); }

    struct Host {
        HostView& view;
        ConsoleMenu& console;
        std::string shot;      // out: a screenshot for this pass
        bool finished = false; // out: the last command ran (the run quits)
    };
    // Once per pass before the events are read.
    void update(Host& host);
    int failures() const { return failures_; }

private:
    void fail(const MenuCommand& c, const std::string& why);
    bool expect(Host& host, const MenuCommand& c);
    void finger(Host& host, int action, float x, float y, bool lower);

    std::vector<MenuCommand> commands_;
    std::size_t at_ = 0;
    int failures_ = 0;
    SDL_JoystickID pad_ = 0;
    SDL_Joystick* joystick_ = nullptr;
    std::string family_;
    bool pad_logged_ = false;
    // A command in progress: passes or the clock until its next step.
    int phase_ = 0;
    int passes_ = 0;
    double started_ = 0;
    std::size_t log_mark_ = 0; // expect log: lines since the last expect
};

} // namespace dw4::game
