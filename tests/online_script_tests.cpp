// The online test driver's script language (src/game/online_script.hpp): steps,
// "until" groups joined by "or", "fail" conditions, log lines, unknown names;
// and every scenario script of tools/online_test loads (its actions and
// conditions are known to the driver).
#include "online_script.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using dw4::game::OnlineScript;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path write_script(const std::string& name, const std::string& text) {
    const auto path = std::filesystem::temp_directory_path() / ("dw4_online_script_" + name + ".script");
    std::ofstream(path) << text;
    return path;
}

void parse_steps() {
    const auto path = write_script("steps", "# a comment line\n"
                                            "campaign until stage=stagml11,control\n"
                                            "\n"
                                            "idle until epoch>=1700000000.5   # trailing comment\n"
                                            "wander 440 20 40 until present=2,apart>200 or sealed>=600,present<=1 fail stagechange,asked\n"
                                            "log a said no until or fail\n"
                                            "mat matz02 yes 2 until onmat:matz02>=2\n"
                                            "hold B+X+START 240 fail stagechange\n"
                                            "errand talk:n_digilfm#1 until flag:409=1 # a model's #N is no comment\n");
    OnlineScript script;
    std::string error;
    check(script.load(path, error), "the script loads: " + error);
    const auto& steps = script.steps();
    check(steps.size() == 7, "seven steps (comments and empty lines skipped)");
    check(steps[0].action == "campaign" && steps[0].line == 2, "line numbers count every line");
    check(steps[0].until.size() == 1 && steps[0].until[0].size() == 2, "one group of two conditions");
    check(steps[0].until[0][0].key == "stage" && steps[0].until[0][0].op == "=" && steps[0].until[0][0].text == "stagml11", "stage=NAME");
    check(steps[0].until[0][1].key == "control" && steps[0].until[0][1].op.empty(), "a condition without a value");
    check(steps[1].until[0][0].op == ">=" && steps[1].until[0][0].value == 1700000000.5, "epoch>=S with a fraction");
    const auto& w = steps[2];
    check(w.args.size() == 3 && w.args[0] == "440" && w.args[2] == "40", "action arguments before until");
    check(w.until.size() == 2, "two groups joined by or");
    check(w.until[0][1].key == "apart" && w.until[0][1].op == ">" && w.until[0][1].value == 200, "apart>D");
    check(w.until[1][1].key == "present" && w.until[1][1].op == "<=" && w.until[1][1].value == 1, "present<=N");
    check(w.fail.size() == 2 && w.fail[0].key == "stagechange" && w.fail[1].key == "asked", "fail conditions after until");
    check(steps[3].action == "log" && steps[3].args.size() == 1 && steps[3].args[0] == "a said no until or fail" && steps[3].until.empty(),
          "a log line keeps its text, keywords included");
    check(steps[4].args.size() == 3 && steps[4].until[0][0].key == "onmat:matz02" && steps[4].until[0][0].value == 2, "onmat:NAME>=N");
    check(steps[5].until.empty() && steps[5].fail.size() == 1, "fail without until");
    check(steps[6].action == "errand" && steps[6].args.size() == 1 && steps[6].args[0] == "talk:n_digilfm#1",
          "'#' inside a word is part of it (a model's index)");
    check(steps[6].until.size() == 1 && steps[6].until[0].size() == 1 && steps[6].until[0][0].key == "flag:409", "a comment after a blank");
}

void unknown_names() {
    OnlineScript a, b, c;
    std::string error;
    check(!a.load(write_script("bad_action", "idle\njump until frames>=2\n"), error) && error.find("unknown action jump") != std::string::npos,
          "an unknown action fails the load: " + error);
    check(!a.active(), "a failed load leaves no steps");
    error.clear();
    check(!b.load(write_script("bad_condition", "idle until frame>=2\n"), error) && error.find("unknown condition frame") != std::string::npos,
          "an unknown condition fails the load: " + error);
    error.clear();
    check(!c.load(write_script("empty", "# nothing\n\n"), error) && error.find("no steps") != std::string::npos, "an empty script fails");
    check(OnlineScript::known_condition("onme:matz00") && !OnlineScript::known_condition("onmat:"), "mat conditions need a mat name");
    check(OnlineScript::known_condition("roomflag:505") && !OnlineScript::known_condition("roomflag:") &&
              !OnlineScript::known_condition("roomflag:5x"),
          "a room story flag condition needs a flag number");
    check(OnlineScript::known_condition("members"), "the members condition (round I1b: INSTANCE_INFO's members)");
    // Round 60: "shot NAME" (a file name of A-Z a-z 0-9 _) and the ghosts condition.
    OnlineScript d, e, f;
    error.clear();
    check(d.load(write_script("shot", "shot a_lobby_1\nidle until ghosts>=2 or ghosts<=0\n"), error) && d.steps().size() == 2 &&
              d.steps()[0].args == std::vector<std::string>{"a_lobby_1"},
          "a shot step and the ghosts condition load: " + error);
    error.clear();
    check(!e.load(write_script("shot_unnamed", "shot\n"), error) && error.find("shot needs a name") != std::string::npos, "a shot needs a name");
    error.clear();
    check(!f.load(write_script("shot_path", "shot ../x\n"), error), "a shot name is no path");
}

void scenario_scripts() {
    const std::filesystem::path root = std::filesystem::path(DW4_SOURCE_DIR) / "tools" / "online_test" / "scenarios";
    int scripts = 0;
    for (const auto& dir : std::filesystem::directory_iterator(root)) {
        if (!dir.is_directory()) continue;
        check(std::filesystem::exists(dir.path() / "scenario.json"), dir.path().string() + " has a scenario.json");
        check(std::filesystem::exists(dir.path() / "expected.txt"), dir.path().string() + " has an expected.txt");
        for (const auto& file : std::filesystem::directory_iterator(dir.path())) {
            if (file.path().extension() != ".script") continue;
            OnlineScript script;
            std::string error;
            check(script.load(file.path(), error), "scenario script " + file.path().string() + ": " + error);
            ++scripts;
        }
    }
    check(scripts >= 20, "the scenario scripts were found (" + std::to_string(scripts) + ")");
}
} // namespace

int main() {
    try {
        parse_steps();
        unknown_names();
        scenario_scripts();
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
    std::cout << "online script tests: " << checks << " checks passed\n";
    return 0;
}
