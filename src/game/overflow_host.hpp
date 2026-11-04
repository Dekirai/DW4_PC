#pragma once

// Data Overflow's host side (docs/SURVIVAL.md 13.3, round O1a lane A): the
// switch from the story to a run and back, and the run's own loop. The run
// loop calls the host's shared parts (host_view.hpp) with the run's engine,
// as game_main's loop calls them with the story's, plus its own pacing, the
// run's overlay (RunDriver) and an F1 menu with the settings tabs only.
//
// The story loop (game_main.cpp) owns a StorySwitch:
//   1. request(): the dev start (F1 > Gameplay > Data Overflow (test)) or
//      --overflow-run's at=F; the story keeps stepping with neutral live pads
//      while its picture and sound fade out over kFadeFrames steps;
//   2. due() at the top of a pass: run_from_story() builds the run's engine
//      (overflow_run.hpp), runs it until it ends and destroys it; the story
//      engine is not stepped meanwhile and nothing of it is touched;
//   3. back: the story's generator is in place again, the renderer cache,
//      smooth motion, the name tags and the pacing start over, the pad waits
//      for released buttons, the picture and the sound fade back in.

#include "host_view.hpp"
#include "overflow_run.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace dw4::game {
class DiscordPresence;
}

namespace dw4::game::overflow {

// What the run loop takes from the story loop besides the HostView.
struct RunHost {
    DiscordPresence* discord = nullptr; // the Rich Presence (none in test runs without DW4_DISCORD_PIPE)
    std::string screenshot;             // test runs: --screenshot's file; a run's shots go beside it
    int test_frames = 0;                // test runs: --smoke-test's count, the run's limit without frames=
};

struct RunOutcome {
    RunEnd end = RunEnd::failed;
    std::uint64_t steps = 0;    // the run's steps
    std::string form;
    int level = 0;
    std::string error;          // why it failed
    std::uint64_t run_hash = 0; // the run engine's net fingerprint at its end (two runs of a request compare it)
};

// A run from the story (13.3 steps 2-4): returns once the run is over and
// its engine is gone. The story engine is only read (its mounted resources
// and mods); the picture state, the renderer's cache, smooth motion and the
// name tags start over before and after, the host gain is 1 afterwards.
RunOutcome run_from_story(HostView& view, const engine::Engine& story, const RunRequest& request, const RunHost& host);

// The black over the story's picture while a StorySwitch fades (the ImGui
// foreground; nothing at 0).
void draw_fade(float black);

// Why the story cannot hand over to a run now (empty: it can): online
// sessions (rule 4), a reboot, a stage that is still loading, two or more
// joined players (the simulator takes one Digimon).
std::string start_refusal(const engine::Engine& story, bool online_session);

// The F1 Gameplay tab's "Data Overflow (test)" lines (O1a's dev start): a
// form, a seed and Start; the request once Start was pressed.
struct DevStartState {
    int form = 0; // 0 Agumon, 1 Veemon
    int seed = 0; // 0: a new one from the clock
};
std::optional<RunRequest> draw_dev_start(DevStartState& state, const std::string& refusal);

} // namespace dw4::game::overflow
