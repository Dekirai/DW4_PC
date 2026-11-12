#include "online_run.hpp"

#include "online/host_priority.hpp"
#include "presence_data.hpp"

#include "online/room_settings.hpp"
#include "online/token_store.hpp"
#include "online/transport.hpp"

#include "dw4/engine/managers.hpp"

#include "dw4/engine/online_story.hpp"

#include <zstd.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <tuple>

namespace dw4::game {

OnlineRoom world_from_entry(const online::api::WorldEntry& entry, const std::string& account, const std::string& token, int relay_port) {
    OnlineRoom room;
    room.account = account;
    room.token = token;
    room.ticket = entry.ticket;
    room.relay_port = entry.relay_port ? entry.relay_port : relay_port;
    room.settings = entry.prefs.settings;
    room.settings.difficulty = 0; // (the Game Start's dialog chooses it)
    room.box = entry.box;
    room.role = entry.role;
    room.default_visibility = entry.prefs.default_visibility;
    return room;
}

std::optional<OnlineRoom> online_enter_world(online::HttpWorker& http, const OnlineOptions& options, std::uint64_t data_hash,
                                             const std::string& client_version, const std::filesystem::path& token_folder, std::string& error) {
    namespace api = online::api;
    OnlineRoom room;
    room.endpoint = http.endpoint();
    const auto server = room.endpoint.host + ":" + std::to_string(room.endpoint.port);
    auto status_response = http.wait(http.submit(api::status_request()));
    const auto status = api::parse_status(status_response);
    if (!status) {
        error = "The server does not answer (" + api::problem_text(status_response) + ").";
        return std::nullopt;
    }
    if (status->maintenance) {
        error = "The server is in maintenance." + (status->motd.empty() ? std::string() : " " + status->motd);
        return std::nullopt;
    }
    if (status->minimum_protocol > int(kOnlineProtocol)) {
        error = "Update required: the server needs a newer version of the game.";
        return std::nullopt;
    }
    // The remembered token first, else a password login.
    std::string account = options.user, token;
    if (const auto remembered = online::load_token(token_folder, server, options.user)) {
        http.set_token(*remembered);
        online::HttpRequest me;
        me.path = "/me";
        const auto answer = http.wait(http.submit(me));
        if (answer.ok()) token = *remembered;
        // Only a refused token is forgotten; a network error keeps it for the next start.
        else if (answer.status == 401 || answer.status == 403) online::forget_token(token_folder, options.user);
        else {
            error = "The server did not answer the login check: " + api::problem_text(answer);
            return std::nullopt;
        }
    }
    if (token.empty()) {
        const char* password = std::getenv("DW4_ONLINE_PASSWORD");
        if (!password || !*password) {
            error = "No remembered login for " + options.user + " (set DW4_ONLINE_PASSWORD to log in).";
            return std::nullopt;
        }
        const auto response = http.wait(http.submit(api::login_request(options.user, password, client_version, int(kOnlineProtocol))));
        const auto login = api::parse_login(response);
        if (!login) {
            error = "Login failed: " + api::problem_text(response);
            return std::nullopt;
        }
        token = login->token;
        if (!login->account.empty()) account = login->account;
        http.set_token(token);
        if (!online::save_token(token_folder, server, options.user, token)) std::cout << "online: the login could not be remembered\n";
    }
    // Play (19.5.1): the world session's ticket, the own settings, the box.
    const auto response = http.wait(http.submit(api::world_enter_request(options.box, int(kOnlineProtocol), data_hash, client_version)));
    const auto entry = api::parse_world_entry(response);
    if (!entry) {
        error = "Entering the server failed: " + api::problem_text(response);
        return std::nullopt;
    }
    room = world_from_entry(*entry, account, token, status->relay_port);
    room.endpoint = http.endpoint();
    if (options.camera) room.settings.camera_mode = *options.camera;
    if (options.light) room.settings.light_mode = *options.light;
    if (options.fast) room.settings.fast_loading = true;
#ifdef DW4_ONLINE_DEV
    room.difficulty = options.difficulty; // (W12's test runs; a release build ignores --online-difficulty)
#endif
    return room;
}

OnlineRun::OnlineRun(std::unique_ptr<online::HttpWorker> http, OnlineRoom room, online::EngineHooks hooks, std::uint64_t data_hash,
                     std::uint32_t rate)
    : http_(std::move(http)), room_(std::move(room)), data_hash_(data_hash), engine_(hooks.engine), link_(std::move(hooks), room_.settings) {
    online::SessionConfig config;
    config.ticket = room_.ticket;
    config.protocol = kOnlineProtocol;
    config.data_hash = data_hash;
    config.settings_hash = 0; // (protocol 49: the hello's settings hash is sent as 0, 19.17.3)
    config.own_name = room_.account;
    config.box = std::uint8_t(std::clamp(room_.box, 1, 4));
    // The toasts' stage names (19.5.6: "Kim is in Ancient Ruins B2": the
    // place, without its region).
    config.stage_name = [](std::string_view stage) {
        if (const auto* area = find_area(stage)) return std::string(area->place);
        return std::string(stage);
    };
    // Round I1b's go along (19.30.6): a dungeon or boss stage, a stage with
    // its floor ("Ancient Ruins B2": the dungeon key's suffix, b2 -> B2, f1 ->
    // F1), a dungeon's name ("Ancient Ruins").
    config.dungeon_stage = [](std::string_view stage) {
        const auto* area = find_area(stage);
        return area && (std::string_view(area->kind) == "dungeon" || std::string_view(area->kind) == "boss");
    };
    config.floor_name = [](std::string_view stage) {
        const auto* area = find_area(stage);
        if (!area) return std::string(stage);
        std::string name = area->place;
        if (area->dungeon && std::string_view(area->kind) == "dungeon") {
            const std::string_view key = area->dungeon, folder = area->folder;
            if (folder.size() >= key.size() + 2 && (folder[key.size()] == 'b' || folder[key.size()] == 'f') &&
                folder[key.size() + 1] >= '0' && folder[key.size() + 1] <= '9')
                name += std::string(" ") + char(folder[key.size()] - 'a' + 'A') + folder[key.size() + 1];
        }
        return name;
    };
    config.dungeon_name = [](std::string_view stage) {
        if (const auto* area = find_area(stage)) return std::string(area->place);
        return std::string(stage);
    };
#ifdef DW4_ONLINE_DEV
    if (room_.difficulty) link_.force_difficulty(room_.difficulty);
    // DW4_ONLINE_AUTOCHOICE=first|new|cursor|back (development builds, 19.24):
    // the arrival prompt answered without a person (round I1b): first = the
    // first choosable row by 19.7.3's order, new = New instance (also a
    // departure without a plan asks for one, plan id 0xFFFF), cursor = the
    // preselection, back = B. DW4_ONLINE_AUTOCHOICE_DELAY_MS waits before the
    // answer (a picture of the prompt); DW4_ONLINE_TEST_GATE_FORM=0 turns the
    // gate form off (every arrival gets the loading-screen form, W11b).
    if (const char* choice = std::getenv("DW4_ONLINE_AUTOCHOICE")) {
        // (A list "back,first": one answer per prompt, the last one stays.)
        const auto parse = [](std::string_view c) {
            return c == "first"    ? online::AutoChoice::first
                   : c == "new"    ? online::AutoChoice::new_instance
                   : c == "cursor" ? online::AutoChoice::cursor
                   : c == "back"   ? online::AutoChoice::back
                                   : online::AutoChoice::none;
        };
        std::string_view list(choice);
        while (!list.empty()) {
            const auto comma = list.find(',');
            config.auto_sequence.push_back(parse(list.substr(0, comma)));
            list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
        }
        if (config.auto_sequence.size() == 1) {
            config.auto_choice = config.auto_sequence.front();
            config.auto_sequence.clear();
        }
        if (std::string_view(choice) == "new") config.plan_new_instance = true;
    }
    if (const char* delay = std::getenv("DW4_ONLINE_AUTOCHOICE_DELAY_MS"); delay && std::atoi(delay) > 0)
        config.auto_choice_delay = std::atoi(delay) / 1000.0;
    if (const char* gate = std::getenv("DW4_ONLINE_TEST_GATE_FORM"); gate && std::string(gate) == "0") config.gate_form = false;
    if (const char* cut = std::getenv("DW4_ONLINE_TEST_CUT_AT_PARK"); cut && std::atof(cut) > 0) test_cut_park_ = std::atof(cut);
    // DW4_ONLINE_TEST_IGNORE_HOME=1 (round I1b's W10c): a removed member's client
    // that never answers its JOIN_REQ home.
    if (const char* ignore = std::getenv("DW4_ONLINE_TEST_IGNORE_HOME"); ignore && std::string(ignore) == "1") config.test_ignore_home = true;
#endif
    // Round I1a: the own settings go with every BARRIER (19.8.7).
    config.own_settings = online::to_wire(room_.settings);
    config.rate = rate;
    // Round J: checkpoints travel zstd-compressed (level 3; docs/SEAMLESS.md 6.13.2).
    config.compress = [](std::span<const std::uint8_t> raw) {
        std::vector<std::uint8_t> out(ZSTD_compressBound(raw.size()));
        const std::size_t n = ZSTD_compress(out.data(), out.size(), raw.data(), raw.size(), 3);
        if (ZSTD_isError(n)) throw std::runtime_error(std::string("checkpoint compression: ") + ZSTD_getErrorName(n));
        out.resize(n);
        return out;
    };
    config.decompress = [](std::span<const std::uint8_t> packed) {
        const unsigned long long size = ZSTD_getFrameContentSize(packed.data(), packed.size());
        if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size > (64ull << 20))
            throw std::runtime_error("checkpoint: a damaged or oversized frame");
        std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
        const std::size_t n = ZSTD_decompress(out.data(), out.size(), packed.data(), packed.size());
        if (ZSTD_isError(n) || n != out.size()) throw std::runtime_error("checkpoint: a damaged frame");
        return out;
    };
    config.trace = std::getenv("DW4_ONLINE_TRACE") != nullptr;
    // DW4_ONLINE_RATE=N (test runs): this machine replays at most N steps per
    // second and reports that rate (a slower machine, SEAMLESS.md 6.6).
    if (const char* cap = std::getenv("DW4_ONLINE_RATE"); cap && std::atoi(cap) > 0) {
        config.max_replay_rate = std::uint32_t(std::atoi(cap));
        config.rate = std::min(config.rate, config.max_replay_rate);
    }
    session_ = std::make_unique<online::Session>(link_, config);
}

namespace {
// The relay port to connect to: the room's, or (development builds, the
// latency scenarios of tools/online_test) DW4_ONLINE_RELAY_PORT, a latency /
// jitter / loss proxy in front of the loopback relay.
int relay_port_for(int port) {
#ifdef DW4_ONLINE_DEV
    if (const char* text = std::getenv("DW4_ONLINE_RELAY_PORT"); text && std::atoi(text) > 0 && std::atoi(text) < 65536) return std::atoi(text);
#endif
    return port;
}
} // namespace

bool OnlineRun::connect(double now, std::string& error) {
    auto transport = online::connect_tcp(room_.endpoint.host, relay_port_for(room_.relay_port), 5000, error);
    if (!transport) return false;
    session_->attach(std::move(transport), now);
    // The relay's answer: by then it replaced any older connection of this account (a session another PC still holds in
    // the room ends without uploading). A refusal ends here; a slow relay is waited for in the main loop as before.
    if (!session_->await_hello(10.0) && session_->closed()) {
        error = session_->close_reason();
        return false;
    }
    return true;
}

void OnlineRun::test_cut_at_park(double now) {
#ifdef DW4_ONLINE_DEV
    // DW4_ONLINE_TEST_CUT_AT_PARK=SECONDS (development builds, scenario W7e): the
    // link is cut once, the first time this member parks at a barrier with
    // another member in its stage group, and the reconnect waits that long (a
    // proxy's cut is timed by the clock and misses the short park). Checked
    // right after the live step that parked and before the next poll, which
    // may bring BARRIER_OK on loopback within the same frame.
    if (test_cut_park_ > 0 && !test_cut_done_ && session_->mode() == online::Mode::parked && std::popcount(unsigned(session_->present())) >= 2) {
        test_cut_done_ = true;
        std::cout << "online: test: the link is cut at the barrier for " << test_cut_park_ << " s" << std::endl;
        log_.line("test: the link is cut at the barrier for %.0f s (DW4_ONLINE_TEST_CUT_AT_PARK)", test_cut_park_);
        session_->test_cut(now);
        next_reconnect_ = now + test_cut_park_;
    }
#else
    (void)now;
#endif
}

void OnlineRun::pass(double now) {
    test_cut_at_park(now);
    session_->poll(now);
    while (auto done = http_->poll()) dispatch(done->first, done->second, now);
    if (character_) {
        // Round I1a's review: the own story moves on at home and as a host; its
        // confirmed revision (STORY_ACK with the own key) is what the card, its
        // backups and the uploads carry from now on (before: the story of the
        // session's entry, while the records got it put in at every save).
        if (const auto& own = session_->confirmed_story(); own && own_story_fed_ != own->revision) {
            own_story_fed_ = own->revision;
            character_->set_own_story(online::record_story_bytes(*own));
        }
        character_->tick(now);
    }
    // The own story changed (the session's own STORY_ACK above, a 409's reload,
    // round G-core's review): the engine's card store writes the newer one from
    // now on.
    if (character_ && card_story_changed && handed_story_ && *handed_story_ != character_->own_story()) card_story_changed();
    // The Game Start's hold (round G-core): the story may have come with this pass.
    if (story_hold_since_ >= 0 && !left_ && !session_->closed()) {
        if (link_.game_start()) begin_game(now);
        if (story_hold_since_ >= 0 && now - story_hold_since_ > 15.0) {
            std::cout << "online: the own story did not arrive within 15 s; going offline\n";
            log_.line("the own story did not arrive within 15 s after the Game Start: going offline");
            leave(8.0, "Your own story did not arrive. Press Play again.");
            return;
        }
    }
    if (left_ || session_->closed() || !session_->wants_transport()) return;
    // Reconnect: a new one-time ticket, then the relay again (6.10).
    if (ticket_request_ == 0 && now >= next_reconnect_)
        ticket_request_ = http_->submit(online::api::world_ticket_request(int(kOnlineProtocol), data_hash_, client_version_));
}

void OnlineRun::open_log(const std::filesystem::path& path, const std::string& client_version) {
    // A second game from the same folder (two games on one PC for one room)
    // has the file open already: it writes online-2.log (online-3.log, ...).
    bool opened = log_.open(path);
    for (int n = 2; !opened && n <= 4; ++n)
        opened = log_.open(path.parent_path() / (path.stem().string() + "-" + std::to_string(n) + path.extension().string()));
    if (!opened) {
        std::cout << "online: the online log " << path.string() << " cannot be written\n";
        return;
    }
    client_version_ = client_version;
    log_.line("---- online session: game %s, protocol %u, server %s:%d (%s), relay port %d, account %s, world session (box %d, camera %d, "
              "light %d, fast loading %d, randomizer %u)",
              client_version.c_str(), unsigned(kOnlineProtocol), room_.endpoint.host.c_str(), room_.endpoint.port,
              room_.endpoint.development ? "development" : "release", room_.relay_port, room_.account.c_str(), room_.box,
              room_.settings.camera_mode, room_.settings.light_mode, room_.settings.fast_loading ? 1 : 0, room_.settings.rand_seed);
    session_->set_log(&log_);
    status_request_ = http_->submit(online::api::status_request());
    // (A game in the background, such as the second of two on one PC, keeps its pace: online/host_priority.hpp.)
    log_.line("background throttling of this process: %s", online::keep_full_speed_in_background() ? "off" : "left to the system");
}

namespace {
const char* window_state(const OnlineRun::HostFacts& f) {
    if (!f.window) return "none";
    if (f.minimized) return "minimized";
    if (f.hidden) return "hidden";
    if (f.occluded) return "covered (occluded)";
    return f.focused ? "in front (focused)" : "in the background (not focused)";
}
} // namespace

void OnlineRun::host_pass(double now, const HostFacts& f) {
    auto& h = host_window_;
    if (h.since < 0) h.since = now;
    if (h.last >= 0) h.longest = std::max(h.longest, now - h.last);
    h.last = now;
    ++h.passes;
    if (f.presented) ++h.presents;
    const auto settings_of = [](const HostFacts& x) {
        return std::tuple(x.window, x.vsync, x.swap_interval, x.fps_limit, std::lround(x.refresh_hz * 100.0));
    };
    const auto window_of = [](const HostFacts& x) { return std::tuple(x.focused, x.minimized, x.occluded, x.hidden); };
    if (!host_logged_ || settings_of(*host_logged_) != settings_of(f)) {
        if (f.window)
            log_.line("host: vsync %s (swap interval %d), display %.3f Hz, frame limit %s", f.vsync ? "on" : "off", f.swap_interval, f.refresh_hz,
                      f.fps_limit > 0 ? (std::to_string(f.fps_limit) + " fps").c_str() : "off");
        else
            log_.line("host: no window (a run without pictures)");
    }
    if (f.window && (!host_logged_ || window_of(*host_logged_) != window_of(f))) log_.line("window: %s", window_state(f));
    host_logged_ = f;
    if (now - h.since >= 10.0) {
        // (A game at 60 pictures a second with vsync on a display of 60.000 Hz
        // sends its pads on a grid that beats against the relay's 59.94 Hz.)
        const double span = now - h.since;
        log_.line("host: %.2f passes/s, %.2f pictures/s, longest pass %.0f ms, window %s", double(h.passes) / span, double(h.presents) / span,
                  h.longest * 1000.0, window_state(f));
        h = HostWindow{now, now, 0, 0, 0};
    }
}

void OnlineRun::dispatch(std::uint64_t id, const online::HttpResponse& response, double now) {
    if (character_ && character_->on_response(id, response)) return;
    if (id == status_request_ && status_request_ != 0) {
        status_request_ = 0;
        if (const auto status = online::api::parse_status(response)) {
            log_.line("server: version %s, protocol %d (minimum %d), relay port %d%s", status->current_version.empty() ? "?" : status->current_version.c_str(),
                      status->current_protocol, status->minimum_protocol, status->relay_port,
                      status->current_protocol != 0 && status->current_protocol != int(kOnlineProtocol)
                          ? " - the server is set for another protocol than this game: update the server or the game"
                          : "");
        } else {
            log_.line("server: no status (%s)", online::api::problem_text(response).c_str());
        }
        return;
    }
    if (id != ticket_request_) return;
    ticket_request_ = 0;
    next_reconnect_ = now + 2.0;
    const auto ticket = online::api::parse_ticket(response);
    if (!ticket || left_ || session_->closed() || !session_->wants_transport()) return;
    std::string error;
    if (auto transport = online::connect_tcp(room_.endpoint.host, relay_port_for(room_.relay_port), 2000, error)) {
        log_.line("reconnected to the relay");
        session_->attach(std::move(transport), now, *ticket);
    } else {
        log_.line("reconnect failed: %s", error.c_str());
    }
}

// ---------------------------------------------------------------------------
// The online character (docs/SEAMLESS.md 9)
// ---------------------------------------------------------------------------
online::character_api::Context OnlineRun::character_context() const {
    online::character_api::Context c;
    c.room_code = session_->wid() ? "wid-" + std::to_string(session_->wid()) : std::string();
    // (The same numbers as the online achievements' reports: the own stage
    // instance, and the room from the relay's ROSTER or else the presence.)
    c.players = std::clamp(std::popcount(unsigned(session_->present())), 1, 4);
    c.room_players = std::clamp(std::max(session_->instance_players(), int(session_->remote().size()) + 1), 1, 10);
    c.randomizer = link_.settings().randomizer();
    // The engine's real cheat flags, not a constant (SEAMLESS.md 9.1.10;
    // online they stay off, Engine::step).
    if (engine_) {
        const auto& cheats = engine_().cheats();
        c.cheats = cheats.god || cheats.infinite_mp || cheats.one_hit;
        c.rebalanced = cheats.exp_rate != 100 || cheats.damage_dealt != 100 || cheats.damage_taken != 100;
    }
    c.difficulty = link_.settings().difficulty;
    c.protocol = int(kOnlineProtocol);
    c.client_version = client_version_;
    return c;
}

bool OnlineRun::load_character(const std::filesystem::path& online_folder, const std::string& client_version, std::string& error) {
    client_version_ = client_version;
    online::CharacterSync::Config config;
    config.card = online::OnlineCard(online::online_card_folder(online_folder, room_.account));
    config.context = character_context();
    config.submit = [http = http_.get()](online::HttpRequest request) { return http->submit(std::move(request)); };
    config.trace = std::getenv("DW4_ONLINE_TRACE") != nullptr;
    // (Round I1a: every member's records carry the own story; an engine in an
    // instance holds the host's, round G-core's net, 19.8.9. The own story is
    // the session's own confirmed one, pass() keeps it current.)
    config.guest = true;
    character_ = std::make_unique<online::CharacterSync>(std::move(config));
    const auto response = http_->wait(http_->submit(online::character_api::get_request()));
    if (!character_->enter(response, error)) {
        if (response.status != 0 && response.status != 200 && response.status != 404) error += " (" + online::api::problem_text(response) + ")";
        character_.reset();
        return false;
    }
    return true;
}

save::CardStore OnlineRun::character_card() const {
    if (!character_) return save::CardStore{};
    auto store = character_->card().store();
    // The card keeps the own story (round G-core, 8.8; round I1a, 19.8.9): the save room writes the engine's records,
    // which hold the host's story in an instance, and the file, its backups and an import of it must not. (The own
    // story follows the session's own confirmed story: pass() hands a new store to the engine when it moved on.)
    store.set_story_override(character_->own_story());
    handed_story_ = character_->own_story();
    return store;
}

std::optional<engine::RoomStory> OnlineRun::story_for_start(double now) {
#ifdef DW4_ONLINE_DEV
    // G7 (round G-core): the room story counts only N seconds after the session's first Game Start, so the Game Start
    // finds none and holds (the hold must outlast the room's await_hello, whatever the title's length).
    if (story_ignore_until_ == 0)
        if (const char* delay = std::getenv("DW4_ONLINE_TEST_STORY_DELAY"); delay && std::atof(delay) > 0) story_ignore_until_ = now + std::atof(delay);
    if (now < story_ignore_until_) return std::nullopt;
#else
    (void)now;
#endif
    return session_->confirmed_story();
}

void OnlineRun::begin_game(double now) {
    const auto story = story_for_start(now);
    if (!story) {
        if (story_hold_since_ < 0) {
            // (The title is a solo instance: no tick waits for this member, as in a loading hitch. The session sends no
            // STORY from a stagIS* stage, so nothing of the character's own story leaves meanwhile.)
            story_hold_since_ = now;
            session_->hold_for_story(true);
            std::cout << "online: the Game Start waits for the room story\n";
            log_.line("the Game Start waits for the room story");
        }
        return;
    }
    const bool rebuilt = engine_().common_rebuilt && !engine_().title_game_start;
    link_.set_room_story(story);
    if (!link_.after_live_step()) return;
    if (story_hold_since_ >= 0) {
        std::cout << "online: the room story came " << int((now - story_hold_since_) * 1000) << " ms after the Game Start\n";
        story_hold_since_ = -1;
        session_->hold_for_story(false);
    }
    if (rebuilt) {
        std::cout << "online: a character call outside the title: the room story replaced the common story again (revision " << story->revision << ")\n";
        return;
    }
    std::cout << "online: the game starts with the own story (revision " << story->revision << ", difficulty " << link_.settings().difficulty
              << ")\n";
    // (Round I1a: the Game Start's difficulty is the own settings' from here; every BARRIER carries it.)
    session_->set_own_settings(online::to_wire(link_.settings()));
    if (character_) character_->game_started();
}

std::vector<std::string> OnlineRun::take_notices() {
    auto out = std::move(notices_);
    notices_.clear();
    return out;
}

std::string OnlineRun::story_line() const {
    if (!session_->in_instance() || session_->is_host()) return "Story: yours";
    const auto host = session_->member_name(session_->host());
    const auto& story = session_->visit_story();
    if (!story) return "Story: " + host + "'s (loading)";
    const int phase = engine::story::phase(*story);
    int keys = 0;
    for (int k = 0; k < engine::story::kKeys; ++k) keys += story->folder[std::size_t(k)] ? 1 : 0;
    return "Story: " + host + "'s, " + (phase >= engine::story::kPhases ? std::string("game cleared") : "phase " + std::to_string(phase)) +
           ", " + std::to_string(keys) + (keys == 1 ? " key item" : " key items");
}

void OnlineRun::save_character(online::CharacterSync::Why why) {
    if (!character_) return;
    character_->set_context(character_context());
    if (const auto record = link_.character_record(session_->own_slot())) character_->auto_save(*record, why);
}

void OnlineRun::after_live_step(double now) {
    test_cut_at_park(now);
    // The title starts a game: the confirmed room story replaces the
    // character's FLAG / FLAGFOLDER / COUNTER before gameStart.lua runs
    // (GameEngineLink applies it with the room's modes on this very step).
    // Round G-core: never without a confirmed story; without one the next
    // live step waits until it comes (begin_game, pass), so the character's
    // own story never plays in the room nor reaches a pending set.
    // (Every Game Start of the session: after "Continue? No" the title
    // starts the next one with the room story and modes again.)
    if (link_.game_start() && story_hold_since_ < 0) begin_game(now);
    // The call shop is closed online (round G-core): its refusal branch ran.
    // (Only a rise: a join, co-arrival or resync restores into a fresh engine
    // whose count starts at 0 again, and that is no new refusal.)
    if (const int refused = engine_().callshop_refused; refused != callshop_seen_) {
        if (refused > callshop_seen_) notices_.push_back("The call shop is not available online: everyone plays their own character.");
        callshop_seen_ = refused;
    }
    if (!character_) return;
    auto& engine = engine_();
    // Play time without loads, as the server compares it with real time.
    if (link_.game_started() && engine.presenting() && !engine.loading()) character_->add_play_time(engine::kFieldSeconds);
    // The barrier tick (the step that executed NEXTSTAGE, SEAMLESS.md 6.7):
    // the state the member carries into the next stage is saved, so a crash
    // loses at most the stage it happens in.
    const bool pending = link_.pending_stage().has_value();
    if (pending && !pending_before_) save_character(online::CharacterSync::Why::stage_change);
    pending_before_ = pending;
    character_->tick(now);
}

void OnlineRun::port_events(const std::vector<engine::PortEvent>& events) {
    if (!character_) return;
    for (const auto& e : events) {
        if (e.kind == engine::PortEvent::Kind::card_written && e.slot == session_->own_slot() && e.value == 0 && e.extra >= 0) {
            character_->set_context(character_context());
            character_->card_written(e.extra);
        }
        // A character the character select made new or called from the card
        // (the title is the member's own solo instance: its slot is this
        // machine's); the Game Start decides from the last one.
        if (e.kind == engine::PortEvent::Kind::new_character && e.slot == session_->own_slot()) character_->new_game(std::uint32_t(e.value));
        if (e.kind == engine::PortEvent::Kind::character_called && e.slot == session_->own_slot())
            character_->character_called(e.extra, std::uint32_t(e.value));
    }
}

void OnlineRun::replaced() {
    if (character_) character_->replaced();
}

void OnlineRun::save_on_leave(double seconds) {
    if (!character_ || saved_on_leave_) return;
    saved_on_leave_ = true;
    save_character(online::CharacterSync::Why::leave);
    const auto clock = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const double until = clock() + seconds;
    while (!character_->idle() && clock() < until) {
        while (auto done = http_->poll()) dispatch(done->first, done->second, clock());
        character_->tick(clock());
        if (!character_->idle()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!character_->idle())
        std::cout << "online: the online character's last save did not reach the server; the next room entry uploads it\n";
}

void OnlineRun::leave(double save_seconds, const std::string& reason) {
    if (left_) return;
    save_on_leave(save_seconds);
    left_ = true;
    {
        // (The latency of the pads that waited for the network: solo ticks run in the step of their sample.)
        const auto& t = session_->stats().total();
        const auto& n = session_->stats().delayed_total();
        log_.line("going offline: %llu own pads ran (%llu with others), %llu not sealed as sent, latency with others avg %.0f ms p95 %.0f ms "
                  "max %.0f ms, input delay %d, reconnects %d, resyncs %d",
                  static_cast<unsigned long long>(t.samples), static_cast<unsigned long long>(n.samples), static_cast<unsigned long long>(t.dropped),
                  n.exec_avg() * 1000.0, n.exec_quantile(0.95) * 1000.0, n.exec_max * 1000.0, session_->delay(), session_->net().reconnects,
                  session_->net().resyncs);
    }
    session_->leave(reason.empty() ? std::string("left") : reason);
    http_->wait(http_->submit(online::api::world_leave_request()));
}

} // namespace dw4::game
