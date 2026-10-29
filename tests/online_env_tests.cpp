// The online environment check: every "DW4_..." name in src and include is
// on exactly one list (simulation variables lock the online session start,
// presentation and harness variables do not), and the check finds a set
// simulation variable.
#include "online/env_check.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef DW4_SOURCE_DIR
#error "DW4_SOURCE_DIR must name the repository root"
#endif

namespace {
using namespace dw4::game::online;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

std::set<std::string> names_in_sources() {
    std::set<std::string> names;
    const std::regex literal("\"(DW4_[A-Z0-9_]+)\"");
    for (const char* folder : {"src", "include"}) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::path(DW4_SOURCE_DIR) / folder)) {
            const auto ext = entry.path().extension().string();
            if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".hpp" && ext != ".h")) continue;
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            const std::string body = text.str();
            for (std::sregex_iterator it(body.begin(), body.end(), literal), end; it != end; ++it) names.insert((*it)[1].str());
        }
    }
    return names;
}

void every_name_classified() {
    std::set<std::string> forbidden, allowed;
    for (const auto name : online_forbidden_env()) check(forbidden.insert(std::string(name)).second, "listed twice: " + std::string(name));
    for (const auto name : online_allowed_env()) check(allowed.insert(std::string(name)).second, "listed twice: " + std::string(name));
    for (const auto& name : forbidden) check(!allowed.contains(name), name + " is on both lists");
    const auto names = names_in_sources();
    check(names.size() > 100, "the scan found the sources");
    std::string missing;
    for (const auto& name : names)
        if (!forbidden.contains(name) && !allowed.contains(name)) missing += " " + name;
    check(missing.empty(), "variables on neither list (add them to src/game/online/env_check.cpp):" + missing);
    // The variables the design names as simulation variables.
    for (const char* name : {"DW4_GOD", "DW4_ONE_HIT", "DW4_INFINITE_MP", "DW4_FIXED_RANDOM", "DW4_CLOCK_FILE", "DW4_DISC_CLOCK",
                             "DW4_AX_PHASE", "DW4_LEVEL", "DW4_BITS", "DW4_DEVICES", "DW4_DISKS", "DW4_FORMS", "DW4_XDATA",
                             "DW4_FLAG_FOLDER", "DW4_CARD_RECORD"})
        check(forbidden.contains(name), std::string(name) + " locks the online start");
    check(allowed.contains("DW4_TRACE_FINGERPRINT") && allowed.contains("DW4_NO_ERROR_BOX") && allowed.contains("DW4_RENDER"),
          "the hidden test runs' switches stay allowed");
}

std::map<std::string, std::string> fake_env;
const char* fake_lookup(const char* name) {
    const auto found = fake_env.find(name);
    return found == fake_env.end() ? nullptr : found->second.c_str();
}

void lookup() {
    fake_env = {{"DW4_TRACE_FINGERPRINT", "60"}, {"DW4_NO_ERROR_BOX", "1"}};
    check(!first_forbidden_env(fake_lookup), "harness switches do not lock");
    fake_env["DW4_GOD"] = "";
    check(!first_forbidden_env(fake_lookup), "an empty value counts as unset");
    fake_env["DW4_ONE_HIT"] = "1";
    const auto found = first_forbidden_env(fake_lookup);
    check(found && *found == "DW4_ONE_HIT", "the set variable is named");
    check(env_refusal(*found).find("DW4_ONE_HIT") != std::string::npos, "the refusal names it");
}
} // namespace

int main() {
    try {
        every_name_classified();
        lookup();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
