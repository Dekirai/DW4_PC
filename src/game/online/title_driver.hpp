#pragma once

// Round I1a (docs/SEAMLESS.md 19.5.1): Play goes straight into the own lobby.
// The world session starts at the title as every online session does; this
// driver then plays the title's own Game Start and the box's Call Digimon
// behind one loading screen ("Entering the server..."): START at "press
// start", Game Start, the character select's Call Digimon, card slot A (the
// online character's virtual card), the character's box, and START. The
// original code runs unchanged; the driver only presses a button when the
// screen that takes it waits for input (read from the engine's managers
// before each step, never from a frame count), with the cursor where it
// should be, one press down and at least one step up, and the same press
// again only when the screen did not move for half a second (a press the F1
// menu's neutral pads swallowed). So the drive takes as long as the title
// takes on this PC and never lands a press on another screen.
//
// Hand-overs (the loading screen lifts):
//   - the original difficulty dialog (19.8.7: the box's own clears offer it)
//     is the player's while it is open; the drive goes on after it (a test
//     run's --online-difficulty answers it instead);
//   - a card without the online character (a new account): the driver stops
//     at the character select's Create Digimon, the original new-character
//     flow;
//   - a memory card message, or no lobby after `give_up_steps`: the player
//     goes on from where the title is.
// The driver never runs offline and never with an online test script (the
// scripts' campaign bot drives the title as before); it only adds pad
// presses to the own slot, so nothing of the engine changes.

#include "dw4/engine/save_menu.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace dw4::engine {
class Engine;
}

namespace dw4::game::online {

// What the title shows before a step (read_title).
struct TitleView {
    std::string stage;         // lower case
    bool ready = false;        // presenting and not loading
    bool game_started = false; // GameSystem::game_started (TITLE2 GAMESTART)
    int title_state = -1, title_sub = 0, title_step = 0, title_detail = 0, title_cursor = 0; // TITLE2
    int ctrl_state = -1;       // LOADCTRL2 (2: the character select)
    int entry_state = 0;       // the panel's card flow (SaveCommon::Player::state; 0: the panel takes its input)
    bool entry_active = true;
    std::optional<engine::LoadIntf2Manager::PanelView> panel; // the own slot's panel
};

TitleView read_title(const engine::Engine& engine, int slot = 0);

class TitleDriver {
public:
    enum class Phase : std::uint8_t {
        off,         // never started, or stopped
        driving,     // pressing through the title behind the loading screen
        player,      // the difficulty dialog is the player's (the loading screen lifted)
        done,        // the game started: the lobby (or the first stage after the title) plays
        handed_over, // the player goes on (a new character, a card message)
        gave_up,     // no lobby after give_up_steps
    };
    struct Config {
        std::optional<int> box;        // the card box (0-3) of the online character; none: a new character
        std::optional<int> difficulty; // the difficulty dialog's answer (development test runs); none: the player's
        int give_up_steps = 5400;      // 90 s of steps
    };

    void start(const Config& config);
    void stop() { phase_ = Phase::off; }
    // The buttons of the own slot for the next step (0: none); call once per
    // step with the view before it.
    std::uint32_t step(const TitleView& view);

    Phase phase() const { return phase_; }
    bool active() const { return phase_ == Phase::driving || phase_ == Phase::player; }
    // The loading screen shows (the live pads are the driver's).
    bool covering() const { return phase_ == Phase::driving; }
    // Why the drive ended (or what the player is asked), for the log.
    const std::string& note() const { return note_; }
    int steps() const { return steps_; }
    int presses() const { return presses_; }
    // The phase changed with the last step() (the host logs and lifts the loading screen then).
    bool changed() { return std::exchange(changed_, false); }

    // The button the screen of `view` waits for (0: wait), and whether it is a
    // hand-over; public for the tests.
    struct Want {
        enum class Kind : std::uint8_t { wait, press, player, hand_over } kind = Kind::wait;
        std::uint32_t button = 0;
        const char* why = "";
    };
    Want decide(const TitleView& view) const;

private:
    using Key = std::array<int, 16>;
    static Key key_of(const TitleView& view);
    void finish(Phase phase, std::string why);

    Config config_;
    Phase phase_ = Phase::off;
    std::string note_;
    int steps_ = 0, presses_ = 0;
    int last_press_ = -1000;
    bool released_ = true; // the last step pressed nothing
    bool changed_ = false;
    Key last_key_{};
};

const char* title_phase_name(TitleDriver::Phase phase);

} // namespace dw4::game::online
