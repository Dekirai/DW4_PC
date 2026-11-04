#include "overflow_run.hpp"

#include "presence_data.hpp"

#include "dw4/engine/managers.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#define DW4_GETPID _getpid
#else
#include <unistd.h>
#define DW4_GETPID getpid
#endif

namespace dw4::game::overflow {

namespace {
bool parse_int(std::string_view text, long long& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const std::string copy(text);
    out = std::strtoll(copy.c_str(), &end, 10);
    return end && *end == '\0';
}

void set_variable(const std::string& name, const char* value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value ? value : "");
#else
    if (value) setenv(name.c_str(), value, 1);
    else unsetenv(name.c_str());
#endif
}
} // namespace

std::optional<RunRequest> parse_run_request(const std::vector<std::string>& tokens, std::string& error) {
    RunRequest request;
    for (const auto& token : tokens) {
        const auto eq = token.find('=');
        if (eq == std::string::npos || eq == 0) {
            error = "--overflow-run takes KEY=VALUE tokens, not '" + token + "'";
            return std::nullopt;
        }
        const std::string key = token.substr(0, eq), value = token.substr(eq + 1);
        long long number = 0;
        if (key == "form") {
            if (!form_by_model(value)) {
                error = "--overflow-run form: '" + value + "' is no player form (p_agumon, p_vmon, ...)";
                return std::nullopt;
            }
            request.form = value;
        } else if (key == "field") {
            std::string field = value;
            for (auto& c : field) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (field.size() < 5 || field.rfind("stag", 0) != 0) {
                error = "--overflow-run field: '" + value + "' is no stage name";
                return std::nullopt;
            }
            request.field = field;
        } else if (key == "seed") {
            if (!parse_int(value, number) || number < 0 || number > 0xFFFFFFFFll) {
                error = "--overflow-run seed: '" + value + "' is no number 0..4294967295";
                return std::nullopt;
            }
            request.seed = std::uint32_t(number);
        } else if (key == "bot") {
            request.bot = value;
        } else if (key == "at") {
            if (!parse_int(value, number) || number < 0 || number > 100000000) {
                error = "--overflow-run at: '" + value + "' is no frame";
                return std::nullopt;
            }
            request.at = int(number);
        } else if (key == "frames") {
            if (!parse_int(value, number) || number < 1 || number > 100000000) {
                error = "--overflow-run frames: '" + value + "' is no step count";
                return std::nullopt;
            }
            request.frames = int(number);
        } else if (key == "shots") {
            for (std::size_t begin = 0; begin <= value.size();) {
                const std::size_t end = std::min(value.find(',', begin), value.size());
                if (!parse_int(std::string_view(value).substr(begin, end - begin), number) || number < 0) {
                    error = "--overflow-run shots: '" + value + "' is no list of steps";
                    return std::nullopt;
                }
                request.shots.push_back(int(number));
                begin = end + 1;
            }
        } else {
            request.options[key] = value;
        }
    }
    return request;
}

std::string describe(const RunRequest& request) {
    std::ostringstream out;
    out << "form=" << request.form << " field=" << request.field << " seed=" << request.seed;
    if (!request.bot.empty()) out << " bot=" << request.bot;
    if (request.at >= 0) out << " at=" << request.at;
    if (request.frames) out << " frames=" << request.frames;
    for (const auto& [key, value] : request.options) out << ' ' << key << '=' << value;
    return out.str();
}

const char* run_end_name(RunEnd end) {
    switch (end) {
    case RunEnd::retired: return "retired";
    case RunEnd::defeated: return "defeated";
    case RunEnd::cleared: return "cleared";
    case RunEnd::time_up: return "time up";
    case RunEnd::window_closed: return "window closed";
    case RunEnd::failed: return "failed";
    }
    return "?";
}

StoryRandomGuard::StoryRandomGuard() : saved_(engine::fight::generator()) {}

StoryRandomGuard::~StoryRandomGuard() { restore(); }

void StoryRandomGuard::restore() {
    if (restored_) return;
    engine::fight::generator() = saved_;
    restored_ = true;
}

const std::vector<std::string_view>& run_test_variables() {
    static const std::vector<std::string_view> names = {"DW4_DEVICES", "DW4_BITS",  "DW4_XDATA",      "DW4_DISKS",
                                                        "DW4_FLAG_FOLDER", "DW4_FORMS", "DW4_LEVEL", "DW4_CARD_RECORD"};
    return names;
}

bool run_test_cheats() {
    const char* value = std::getenv("DW4_OVERFLOW_TEST_CHEATS");
    return value && *value && std::string_view(value) != "0";
}

TestEnvironmentScope::TestEnvironmentScope() {
    if (run_test_cheats()) return;
    for (const auto name : run_test_variables()) {
        const std::string key(name);
        if (const char* value = std::getenv(key.c_str()); value && *value) {
            removed_.emplace_back(key, value);
            set_variable(key, nullptr);
        }
    }
}

TestEnvironmentScope::~TestEnvironmentScope() {
    for (const auto& [name, value] : removed_) set_variable(name, value.c_str());
}

RunCards::RunCards() {
    std::error_code error;
    const auto base = std::filesystem::temp_directory_path(error);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = (error ? std::filesystem::path(".") : base) /
            ("dw4_overflow_cards_" + std::to_string(DW4_GETPID()) + "_" + std::to_string(stamp));
    std::filesystem::create_directories(root_ / "Card A", error);
    std::filesystem::create_directories(root_ / "Card B", error);
}

RunCards::~RunCards() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
}

void prepare_run_engine(engine::Engine& run, const RunRequest& request, const std::filesystem::path& cards) {
    run.set_card_root(cards);
    engine::CardSetting none;
    none.mode = engine::CardSetting::Mode::none;
    run.set_card_setting(0, none);
    run.set_card_setting(1, none);
    if (!run_test_cheats()) run.cheats() = engine::Cheats{};
    run.cheats().fast_loading = true; // the run's engine always loads fast (SURVIVAL.md 12)
    run.set_port_events(false);
    if (auto* sys = run.get<engine::GameSystem>()) {
        sys->direct_start = true;
        sys->players[0].digimon = request.form;
        sys->players[0].joined = true;
        for (std::size_t slot = 1; slot < sys->players.size(); ++slot) sys->players[slot].joined = false;
        sys->ninzu = 1;
    }
    // The constructor seeded the boot seed; a run plays its own stream.
    engine::fight::generator().seed(request.seed);
}

void StorySwitch::request(RunRequest request, bool immediate) {
    pending_ = std::move(request);
    phase_ = immediate ? Phase::due : Phase::fading_out;
    count_ = immediate ? 0 : kFadeFrames;
}

void StorySwitch::story_stepped() {
    if (phase_ == Phase::fading_out && --count_ <= 0) phase_ = Phase::due;
    else if (phase_ == Phase::fading_in && --count_ <= 0) phase_ = Phase::idle;
}

RunRequest StorySwitch::take() {
    RunRequest request = pending_.value_or(RunRequest{});
    pending_.reset();
    phase_ = Phase::idle;
    return request;
}

void StorySwitch::returned() {
    phase_ = Phase::fading_in;
    count_ = kFadeFrames;
}

float StorySwitch::black() const {
    switch (phase_) {
    case Phase::fading_out: return 1.0f - float(count_) / float(kFadeFrames);
    case Phase::due: return 1.0f;
    case Phase::fading_in: return float(count_) / float(kFadeFrames);
    case Phase::idle: break;
    }
    return 0.0f;
}

std::string form_display_name(std::string_view model) {
    if (const auto* form = form_by_model(model)) return form->name;
    return std::string(model);
}

} // namespace dw4::game::overflow
