// Data Overflow's host side without game data (docs/SURVIVAL.md 13.2-13.3,
// round O1a lane A): the run request, the story's system generator across a
// run engine's construction and life, the test variables a run keeps out,
// the throwaway memory cards, the run engine's setup and the story switch.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "../src/game/overflow_run.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::engine;
namespace ov = dw4::game::overflow;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}
bool env_set(const char* name) {
    const char* value = std::getenv(name);
    return value && *value;
}

void requests() {
    std::string error;
    const auto plain = ov::parse_run_request({}, error);
    check(plain && plain->form == "p_agumon" && plain->field == "stagbs02" && plain->seed == 1 && plain->at < 0 && plain->frames == 0,
          "no tokens: Agumon on the Violet Ring, seed 1, at once");
    const auto full = ov::parse_run_request({"form=p_vmon", "field=STAGBS04", "seed=4000000000", "bot=kite", "at=4000", "frames=3600",
                                             "shots=10,200", "tier=hard", "offers=priority"},
                                            error);
    check(full.has_value(), "a full request parses: " + error);
    check(full->form == "p_vmon" && full->field == "stagbs04" && full->seed == 4000000000u && full->bot == "kite" && full->at == 4000 &&
              full->frames == 3600,
          "the host's keys");
    check(full->shots == std::vector<int>{10, 200}, "the shots");
    check(full->options.size() == 2 && full->options.at("tier") == "hard" && full->options.at("offers") == "priority",
          "the director's keys are kept");
    check(ov::describe(*full).find("form=p_vmon field=stagbs04 seed=4000000000 bot=kite at=4000 frames=3600") == 0, "describe");
    for (const auto& bad : std::vector<std::string>{"p_agumon", "form=p_gekomon", "seed=-1", "seed=x", "at=12a", "frames=0",
                                                    "shots=1,,2", "field=dng101b1", "=1"}) {
        error.clear();
        check(!ov::parse_run_request({bad}, error) && !error.empty(), "refused: " + bad);
    }
}

std::uint32_t draws(SystemRandom random, int count) {
    std::uint32_t mix = 0;
    for (int i = 0; i < count; ++i) mix = mix * 31u + random();
    return mix;
}

void story_generator() {
    auto& generator = fight::generator();
    generator.seed(SystemRandom::kBootSeed);
    for (int i = 0; i < 1234; ++i) (void)generator(); // the story is somewhere in its stream
    const SystemRandom story = generator;
    const std::uint32_t story_next = draws(story, 50);
    {
        ov::StoryRandomGuard guard;
        check(guard.saved() == story, "the guard keeps the story's state");
        {
            Engine run{dw4::MountedResources{}};
            check(!(generator == story), "an engine's construction reseeds the process-wide generator (engine.cpp)");
            ov::RunCards cards;
            ov::RunRequest request;
            request.seed = 77;
            ov::prepare_run_engine(run, request, cards.root());
            SystemRandom seeded;
            seeded.seed(77);
            check(generator == seeded, "the run plays its own seed");
            for (int i = 0; i < 500; ++i) (void)generator(); // the run draws
        }
        check(!(generator == story), "the run's draws are still in the generator before the guard ends");
    }
    check(generator == story, "the story's generator is back after the run");
    check(draws(generator, 50) == story_next, "the story's next draws are the ones it would have made");
    // restore() before the end, once.
    generator = story;
    {
        ov::StoryRandomGuard guard;
        (void)generator();
        guard.restore();
        check(generator == story, "restore() puts it back at once");
        (void)generator();
    }
    check(!(generator == story), "a restored guard does not restore again at its end");
}

void test_variables() {
    set_env("DW4_OVERFLOW_TEST_CHEATS", nullptr);
    set_env("DW4_LEVEL", "30");
    set_env("DW4_DEVICES", "0:1:0:0");
    set_env("DW4_BITS", nullptr);
    {
        ov::TestEnvironmentScope scope;
        check(!env_set("DW4_LEVEL") && !env_set("DW4_DEVICES"), "a run's new player takes no test variables");
        check(scope.removed().size() == 2, "the scope names what it took out");
    }
    check(env_set("DW4_LEVEL") && std::string(std::getenv("DW4_LEVEL")) == "30" && env_set("DW4_DEVICES"), "they come back afterwards");
    check(!env_set("DW4_BITS"), "an unset variable stays unset");
    set_env("DW4_OVERFLOW_TEST_CHEATS", "1");
    {
        ov::TestEnvironmentScope scope;
        check(env_set("DW4_LEVEL") && scope.removed().empty(), "DW4_OVERFLOW_TEST_CHEATS=1 keeps them");
    }
    set_env("DW4_OVERFLOW_TEST_CHEATS", nullptr);
    set_env("DW4_LEVEL", nullptr);
    set_env("DW4_DEVICES", nullptr);
    for (const auto name : ov::run_test_variables()) check(std::string(name).rfind("DW4_", 0) == 0, "variable names");
}

void cards() {
    std::filesystem::path root;
    {
        ov::RunCards cards;
        root = cards.root();
        check(std::filesystem::is_directory(root / "Card A") && std::filesystem::is_directory(root / "Card B"), "two empty card folders");
        check(std::filesystem::is_empty(root / "Card A"), "no save in them");
    }
    check(!std::filesystem::exists(root), "removed with the object");
}

void run_engine() {
    set_env("DW4_GOD", "1");
    set_env("DW4_OVERFLOW_TEST_CHEATS", nullptr);
    {
        Engine run{dw4::MountedResources{}};
        check(run.cheats().god, "the constructor takes DW4_GOD");
        run.cheats().exp_rate = 300;
        run.cheats().rand_seed = 99;
        run.cheats().rand_enemies = 1;
        auto* sys = run.get<GameSystem>();
        check(sys != nullptr, "the engine has its system");
        sys->players[1].joined = true;
        ov::RunCards cards;
        ov::RunRequest request;
        request.form = "p_vmon";
        ov::prepare_run_engine(run, request, cards.root());
        check(!run.cheats().god && run.cheats().exp_rate == 100 && !run.cheats().enemy_shuffle() && run.cheats().drop_seed == 0,
              "no cheats, rebalancing or randomizer in a run");
        check(run.cheats().fast_loading, "the run's engine loads fast");
        check(run.card_setting(0).mode == CardSetting::Mode::none && run.card_setting(1).mode == CardSetting::Mode::none,
              "both card slots empty");
        check(run.card_root() == cards.root(), "the throwaway folder");
        check(!run.port_events_enabled(), "no port events (achievements, the online client)");
        check(sys->direct_start && sys->players[0].digimon == "p_vmon" && sys->players[0].joined && !sys->players[1].joined &&
                  sys->ninzu == 1,
              "a direct start of the form, one player");
    }
    set_env("DW4_OVERFLOW_TEST_CHEATS", "1");
    {
        Engine run{dw4::MountedResources{}};
        ov::RunCards cards;
        ov::prepare_run_engine(run, ov::RunRequest{}, cards.root());
        check(run.cheats().god && run.cheats().fast_loading, "DW4_OVERFLOW_TEST_CHEATS=1 keeps the test cheats");
    }
    set_env("DW4_OVERFLOW_TEST_CHEATS", nullptr);
    set_env("DW4_GOD", nullptr);
}

void story_switch() {
    ov::StorySwitch sw;
    check(!sw.busy() && !sw.due() && sw.black() == 0.0f && sw.gain() == 1.0f, "idle: no black, the story's sound as it is");
    ov::RunRequest request;
    request.seed = 5;
    sw.request(request);
    check(sw.busy() && sw.fading_out() && !sw.due() && sw.black() == 0.0f, "a request fades the story out");
    for (int i = 0; i < ov::kFadeFrames - 1; ++i) sw.story_stepped();
    check(sw.fading_out() && sw.black() > 0.9f && sw.black() < 1.0f, "still fading one step before the end");
    sw.story_stepped();
    check(sw.due() && !sw.fading_out() && sw.black() == 1.0f, "due after the fade's steps");
    const auto taken = sw.take();
    check(taken.seed == 5 && !sw.busy(), "take() hands the request over");
    sw.returned();
    check(!sw.busy() && sw.black() == 1.0f, "back from the run: black, fading in");
    for (int i = 0; i < ov::kFadeFrames; ++i) sw.story_stepped();
    check(!sw.busy() && sw.black() == 0.0f && sw.gain() == 1.0f, "faded in");
    sw.request(request, true);
    check(sw.due(), "an immediate request is due at once (--overflow-run without at=)");
    (void)sw.take();
}

void names() {
    check(ov::form_display_name("p_agumon") == "Agumon" && ov::form_display_name("p_vmon") == "Veemon", "form names");
    check(ov::form_display_name("p_unknown") == "p_unknown", "an unknown model stays");
    check(std::string(ov::run_end_name(ov::RunEnd::window_closed)) == "window closed", "end names");
}
} // namespace

int main() {
    try {
        requests();
        story_generator();
        test_variables();
        cards();
        run_engine();
        story_switch();
        names();
    } catch (const std::exception& error) {
        std::cerr << "overflow_host_tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "overflow_host_tests: " << checks << " checks passed\n";
    return 0;
}
