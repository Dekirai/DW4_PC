#pragma once

// Data Overflow (docs/SURVIVAL.md): a run's engine as the host makes it and
// keeps it apart from the story (13.1-13.3, round O1a lane A). Nothing here
// needs a window: the run loop and the switch are in overflow_host.hpp, the
// director's side plugs in through RunDriver.
//
// A run lives in a second Engine on a copy of the mounted resources. The
// story engine is not stepped while it lives and continues bit for bit
// afterwards, because:
//   - every Engine constructor reseeds the process-wide system generator
//     (fight::generator, engine.cpp): StoryRandomGuard keeps the story's
//     state and puts it back once the run's engine is gone;
//   - the engine-side test variables (DW4_LEVEL, DW4_DEVICES, ...) act on
//     every new player: TestEnvironmentScope takes them out of the process
//     environment for the run's life (DW4_OVERFLOW_TEST_CHEATS=1 keeps
//     them), and prepare_run_engine sets the run's cheats itself;
//   - the run's memory cards are an empty throwaway folder with both slots
//     empty (RunCards): the mode never writes the story's saves;
//   - the run's engine never collects port events, so the achievements and
//     the online client never see it.

#include "dw4/engine/engine.hpp"
#include "dw4/engine/random.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game::overflow {

// What a run is asked to be: the F1 dev start (F1 > Gameplay > Data
// Overflow (test)) or --overflow-run's KEY=VALUE tokens:
//   form=p_agumon     the Digimon (a shinka MODEL_NAME; O1a plays p_agumon and p_vmon)
//   field=stagbs02    the arena (the Violet Ring)
//   seed=N            the run's seed (the system generator's and the director's)
//   bot=NAME          a test bot drives player 1 (the director's; the host's stand-in knows "circle")
//   at=F              the run starts after story frame F (absent: before the story's first step)
//   frames=N          test runs: Retire after N run steps (absent: --smoke-test's count)
//   shots=A,B,...     test runs with pictures: screenshots at these run steps
// Every other key (tier, offers, ...) is kept in `options` for the director.
struct RunRequest {
    std::string form = "p_agumon";
    std::string field = "stagbs02";
    std::uint32_t seed = 1;
    std::string bot;
    int at = -1;
    int frames = 0;
    std::vector<int> shots;
    std::map<std::string, std::string, std::less<>> options;
};
// Empty and `error` for a token without '=' or a value that does not parse.
std::optional<RunRequest> parse_run_request(const std::vector<std::string>& tokens, std::string& error);
// "form=p_agumon field=stagbs02 seed=1 ..." (the log line).
std::string describe(const RunRequest& request);

// Why a run ended: the director's run_end (defeated, cleared, time_up),
// Retire (the pause menu, the F1 tab, a test run's frame limit), the window
// closing (the run counts as retired, then the program quits), or a run
// that could not start or went where it must not (failed).
enum class RunEnd { retired, defeated, cleared, time_up, window_closed, failed };
const char* run_end_name(RunEnd end);

// What the host knows of a run for its own lines (Discord, the log).
struct RunStatus {
    std::string form_name; // "Agumon"
    int level = 1;
    double seconds = 0;    // the run clock
};

// The system generator of the story (13.2): saved when made, put back when
// destroyed (or by restore()). It must outlive the run's engine.
class StoryRandomGuard {
public:
    StoryRandomGuard();
    ~StoryRandomGuard();
    StoryRandomGuard(const StoryRandomGuard&) = delete;
    StoryRandomGuard& operator=(const StoryRandomGuard&) = delete;
    void restore();
    const engine::SystemRandom& saved() const { return saved_; }

private:
    engine::SystemRandom saved_;
    bool restored_ = false;
};

// The engine-side test variables a new player takes (GameSystem::
// create_player: DW4_DEVICES, DW4_BITS, DW4_XDATA, DW4_DISKS,
// DW4_FLAG_FOLDER, DW4_FORMS, DW4_LEVEL; save_game.cpp: DW4_CARD_RECORD),
// out of the process environment while the object lives, unless
// DW4_OVERFLOW_TEST_CHEATS=1. The engine constructor's cheat variables
// (DW4_GOD, DW4_ONE_HIT, DW4_INFINITE_MP) are undone by prepare_run_engine.
// Everything else a test run sets is applied by game_main to the story
// engine only (DW4_PLAYERS, DW4_DIFFICULTY, DW4_ENTRY, DW4_COUNTERS, the
// online test switches, DW4_GAMEPLAY, DW4_RANDOMIZER); DW4_FIXED_RANDOM
// fixes the one process-wide generator for every engine (a research tool).
class TestEnvironmentScope {
public:
    TestEnvironmentScope();
    ~TestEnvironmentScope();
    TestEnvironmentScope(const TestEnvironmentScope&) = delete;
    TestEnvironmentScope& operator=(const TestEnvironmentScope&) = delete;
    // The variables it took out (name, value).
    const std::vector<std::pair<std::string, std::string>>& removed() const { return removed_; }

private:
    std::vector<std::pair<std::string, std::string>> removed_;
};
const std::vector<std::string_view>& run_test_variables();
// DW4_OVERFLOW_TEST_CHEATS=1: a run keeps the test variables and cheats.
bool run_test_cheats();

// The run's memory cards: a new empty folder (Card A, Card B) below the
// temporary folder, removed with the object.
class RunCards {
public:
    RunCards();
    ~RunCards();
    RunCards(const RunCards&) = delete;
    RunCards& operator=(const RunCards&) = delete;
    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

// The fades around a run, in steps of the story (out, in) and presents of
// the run (in, out).
inline constexpr int kFadeFrames = 30;

// The story loop's side of the switch.
class StorySwitch {
public:
    // A run is asked for: the story fades out first (immediate: it runs at
    // the next pass, before any further story step).
    void request(RunRequest request, bool immediate = false);
    // Fading out or due.
    bool busy() const { return phase_ == Phase::fading_out || phase_ == Phase::due; }
    // The story's live pads are neutral (the input scripts still play).
    bool fading_out() const { return phase_ == Phase::fading_out; }
    // After every story step: the fades count down.
    void story_stepped();
    bool due() const { return phase_ == Phase::due; }
    RunRequest take();
    // The run is over: the story fades back in.
    void returned();
    // The black over the story's picture (0..1) and the story's sound gain.
    float black() const;
    float gain() const { return 1.0f - black(); }

private:
    enum class Phase { idle, fading_out, due, fading_in } phase_ = Phase::idle;
    int count_ = 0;
    std::optional<RunRequest> pending_;
};

// The run's engine before its first stage request (13.3 step 2): the
// throwaway cards with both slots empty, the cheats off except fast loading
// (DW4_OVERFLOW_TEST_CHEATS=1: the test cheats stay), no port events, a
// direct start of the form as the only player, and the system generator
// reseeded with the run's seed.
void prepare_run_engine(engine::Engine& run, const RunRequest& request, const std::filesystem::path& cards);

// Where the run's 2D overlay may draw (window pixels, the 4:3 screen inside).
struct OverlayFrame {
    int width = 0, height = 0;                  // the window
    int screen_x = 0, screen_y = 0;             // the 4:3 screen (the game's 2D layer)
    int screen_width = 0, screen_height = 0;
    bool menu_open = false;                     // the F1 menu is over the run (it is paused)
    std::uint64_t steps = 0;                    // run steps so far
};

// The director's side of a run as the host sees it (round O1a lane C
// implements it on the engine's director; make_run_driver gives the host's
// stand-in until then). The host calls, per pass of its run loop:
//   before_step / step / after_step for each step it runs (none while the
//   F1 menu is open), decorate after Engine::render, draw_overlay inside
//   the ImGui frame, status for Discord and the log.
class RunDriver {
public:
    virtual ~RunDriver() = default;
    // The new run engine after prepare_run_engine, before its first stage
    // request: the rules (Engine::set_overflow_rules with the run's rule set,
    // dw4/engine/overflow.hpp; the host's request is then the only one H3
    // lets through besides OverflowRules::field_change), the director
    // (OverflowRules::director: the host destroys the engine before the
    // driver, so a director the driver owns outlives every call), the
    // loadout.
    virtual void attach(engine::Engine& run, const RunRequest& request) = 0;
    // The pads of the next step: `pads` holds this machine's player 1 as
    // the story composes it (slots 2-4 disconnected); a test bot or the
    // run's own menus may replace them.
    virtual void before_step(engine::Engine& run, std::array<engine::PadSample, 4>& pads) = 0;
    // After each step: the run's end once it is over (its results closed);
    // nothing while it goes on.
    virtual std::optional<RunEnd> after_step(engine::Engine& run) = 0;
    // The host ends the run (F1 Retire, the window closing, a test run's
    // frame limit): after_step reports `why` from the next step on, or at
    // once when the host asks again with no step between (window closing).
    virtual void retire(engine::Engine& run, RunEnd why) = 0;
    // Whether a retire() is answered (the host stops stepping then).
    virtual std::optional<RunEnd> ended() const = 0;
    // The picture's draw list after Engine::render (2D items, as TitleEntry).
    virtual void decorate(engine::Engine&, std::vector<engine::DrawItem>&) {}
    // Inside the ImGui frame after the picture: the run's overlay.
    virtual void draw_overlay(engine::Engine&, const OverlayFrame&) {}
    // Inside the F1 menu's Data Overflow tab (after the host's Retire).
    virtual void draw_menu(engine::Engine&) {}
    virtual RunStatus status(const engine::Engine& run) const = 0;
};
std::unique_ptr<RunDriver> make_run_driver(const RunRequest& request);

// The shinka form names of the presence data for a model ("p_agumon" ->
// "Agumon"); the model itself when unknown.
std::string form_display_name(std::string_view model);

} // namespace dw4::game::overflow
