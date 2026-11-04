// The online character without a server (docs/SEAMLESS.md 9): base64, the
// request bodies and answers of /online-character, the virtual card (the
// record in a box of both data copies, the cache state) and the upload state
// machine with a scripted HTTP worker: create, If-Match, the newest save
// winning while one is in flight, unchanged saves skipped, 409 (stop, keep the
// local cache, reload the server's character), a record of another
// character, retries after 429 and network errors, the recovery of an unsent
// save at the next room entry.
#include "online/achievement_api.hpp"
#include "online/character.hpp"
#include "online/cloud_save.hpp"
#include "online/json.hpp"

#include "dw4/card_store.hpp"
#include "dw4/save_data.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game::online;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

CharacterRecord make_record(std::uint32_t unique, int level, const char* name = "Tamer") {
    dw4::save::Record r;
    r.blank = false;
    r.used = true;
    r.unique = unique;
    r.digimon = "p_dorumon";
    r.name = dw4::save::name_to_card(name);
    r.level = level;
    r.hp = r.max_hp = 100 + level;
    r.devices.fill(-1);
    r.disks.fill(-1);
    r.bank.fill(-1);
    r.power_socket.fill(-1);
    r.power_pocket.fill(-1);
    r.main_slot = r.sub_slot = -1;
    r.main_pocket.fill(-1);
    r.base_level.fill(1);
    CharacterRecord out{};
    const auto bytes = dw4::save::encode(r);
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return out;
}

HttpResponse answer(int status, std::string body) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

std::string character_json(int revision, const CharacterRecord& record, long long play) {
    return "{\"revision\":" + std::to_string(revision) + ",\"record\":\"" + base64_encode(record) + "\",\"playSeconds\":" + std::to_string(play) +
           ",\"flagged\":false,\"summary\":{}}";
}

struct FakeHttp {
    std::deque<std::pair<std::uint64_t, HttpRequest>> sent;
    std::uint64_t next = 1;
    std::uint64_t submit(HttpRequest request) {
        sent.emplace_back(next, std::move(request));
        return next++;
    }
    std::pair<std::uint64_t, HttpRequest> take() {
        check(!sent.empty(), "a request was sent");
        auto r = std::move(sent.front());
        sent.pop_front();
        return r;
    }
};

std::string header(const HttpRequest& r, const std::string& name) {
    for (const auto& [k, v] : r.headers)
        if (k == name) return v;
    return {};
}

CharacterRecord body_record(const HttpRequest& r) {
    const auto v = json::parse(r.body);
    check(v.has_value(), "the PUT body is JSON");
    const auto bytes = base64_decode((*v)["record"].str().value_or(""));
    check(bytes && bytes->size() == dw4::save::kRecordSize, "the PUT carries a 3316-byte record");
    CharacterRecord out{};
    std::copy(bytes->begin(), bytes->end(), out.begin());
    return out;
}

std::filesystem::path fresh_folder(const char* name) {
    const auto folder = std::filesystem::temp_directory_path() /
                        ("dw4_online_character_" + std::string(name) + "_" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

void base64_round_trip() {
    for (std::size_t n = 0; n < 40; ++n) {
        std::vector<std::uint8_t> bytes(n);
        for (std::size_t i = 0; i < n; ++i) bytes[i] = std::uint8_t(i * 37 + 11);
        const auto text = base64_encode(bytes);
        check(text.size() == (n + 2) / 3 * 4, "base64 length");
        check(base64_decode(text) == bytes, "base64 round trip " + std::to_string(n));
    }
    check(base64_encode(std::vector<std::uint8_t>{'M', 'a', 'n'}) == "TWFu", "RFC 4648 vector");
    check(base64_encode(std::vector<std::uint8_t>{'M', 'a'}) == "TWE=", "one pad");
    check(!base64_decode("TWE") && !base64_decode("TW=u") && !base64_decode("T!E="), "malformed base64");
}

void requests() {
    const auto record = make_record(77, 5);
    character_api::Context ctx;
    ctx.room_code = "ABC123";
    ctx.players = 3;
    ctx.room_players = 7;
    ctx.difficulty = 1;
    ctx.protocol = 44;
    ctx.client_version = "0.56.0";
    const auto create = character_api::put_request(record, 0, 120, ctx);
    check(create.method == "PUT" && create.path == "/online-character", "PUT /online-character");
    check(header(create, "If-None-Match") == "*" && header(create, "If-Match").empty(), "a create sends If-None-Match: *");
    const auto update = character_api::put_request(record, 7, 130, ctx);
    check(header(update, "If-Match") == "\"7\"", "an update sends If-Match with the revision");
    const auto v = json::parse(update.body);
    check(v && (*v)["playSeconds"].i64() == 130, "play seconds");
    check((*v)["context"]["roomCode"].str() == "ABC123" && (*v)["context"]["players"].i64() == 3 && (*v)["context"]["difficulty"].i64() == 1 &&
              (*v)["context"]["cheats"].flag() == false && (*v)["context"]["saveStateLoaded"].flag() == false &&
              (*v)["context"]["protocol"].i64() == 44 && (*v)["context"]["roomPlayers"].i64() == 7,
          "the ranked play context");
    {
        auto alone = ctx;
        alone.room_players = 0;
        const auto a = json::parse(character_api::put_request(record, 7, 130, alone).body);
        check(a && !(*a)["context"].find("roomPlayers"), "no roomPlayers when the room's size is unknown");
    }
    {
        // The engine's real flags, no constant (SEAMLESS.md 9.1.10).
        auto cheated = ctx;
        cheated.cheats = true;
        const auto a = json::parse(character_api::put_request(record, 7, 130, cheated).body);
        check(a && (*a)["context"]["cheats"].flag() == true && (*a)["context"]["rebalanced"].flag() == false, "cheats on in the context");
        auto rebalanced = ctx;
        rebalanced.rebalanced = true;
        const auto b = json::parse(character_api::put_request(record, 7, 130, rebalanced).body);
        check(b && (*b)["context"]["cheats"].flag() == false && (*b)["context"]["rebalanced"].flag() == true, "rebalancing in the context");
        dw4::game::online::achievements::ReportContext report;
        report.cheats = true;
        const auto r = json::parse(dw4::game::online::achievements::context_json(report));
        check(r && (*r)["cheats"].flag() == true && (*r)["rebalanced"].flag() == false, "cheats on in the achievement context");
        report.cheats = false;
        report.rebalanced = true;
        const auto q = json::parse(dw4::game::online::achievements::context_json(report));
        check(q && (*q)["cheats"].flag() == false && (*q)["rebalanced"].flag() == true, "rebalancing in the achievement context");
    }
    check(body_record(update) == record, "the record travels as base64");

    const auto c = character_api::parse_character(answer(200, character_json(4, record, 999)));
    check(c && c->revision == 4 && c->record == record && c->play_seconds == 999, "GET answer");
    check(!character_api::parse_character(answer(404, "{\"code\":\"no_online_character\"}")), "404 is no character");
    check(!character_api::parse_character(answer(200, "{\"revision\":4,\"record\":\"AAAA\"}")), "a short record is refused");
    check(character_api::parse_saved(answer(201, "{\"revision\":1,\"summary\":{}}")) == 1, "PUT 201");
    const auto conflict = answer(409, "{\"type\":\"urn:dw4online:problem:revision_conflict\",\"title\":\"changed\",\"status\":409,"
                                      "\"code\":\"revision_conflict\",\"currentRevision\":9}");
    check(character_api::problem_code(conflict) == "revision_conflict" && character_api::current_revision(conflict) == 9, "409 answer");
    const auto reset = character_api::reset_request("pw \"x\"");
    check(reset.method == "POST" && reset.path == "/online-character/reset" && json::parse(reset.body).has_value(), "reset request");
}

void card_layout() {
    const auto a = make_record(1, 3, "Alpha"), b = make_record(2, 9, "Beta");
    auto gci = card_with_record(nullptr, a, 0, false, 12345);
    check(gci.size() == dw4::save::kGciEntrySize + dw4::save::kFileSize, "a whole GCI");
    check(dw4::save::valid_save_gci(gci), "both copies pass the game's check");
    check(record_of_box(gci, 0) == a, "box 0 holds the record");
    check(record_of_box(gci, 1) && !record_used(*record_of_box(gci, 1)), "the other boxes are empty");
    const auto records = dw4::save::read_file(std::span<const std::uint8_t>(gci).subspan(dw4::save::kGciEntrySize));
    check(records && (*records)[0].unique == 1 && (*records)[0].level == 3 && dw4::save::name_from_card((*records)[0].name) == "Alpha",
          "the game's reader sees the character");
    check(record_unique(a) == 1 && record_name(a) == "Alpha" && record_used(a), "record fields");
    // A second box keeps the first (the save room's other boxes), unless asked otherwise.
    const auto both = card_with_record(&gci, b, 2, true, 0);
    check(record_of_box(both, 0) == a && record_of_box(both, 2) == b, "keep the other boxes");
    check(std::equal(gci.begin(), gci.begin() + 0x40 + 0x2000, both.begin()), "entry and header stay");
    const auto only = card_with_record(&both, b, 0, false, 0);
    check(record_of_box(only, 0) == b && !record_used(*record_of_box(only, 2)), "a room entry's card holds only the online character");
    // A damaged copy 1 falls back to copy 2.
    auto damaged = both;
    damaged[dw4::save::kGciEntrySize + 0x2000 + 100] ^= 0xFF;
    check(record_of_box(damaged, 2) == b, "the second copy");
}

void cache_state() {
    const OnlineCard card(fresh_folder("state"));
    check(card.state() == CardCacheState{}, "no state file: defaults");
    const CardCacheState s{12, 3600, true, 2, 4242};
    check(card.set_state(s) && card.state() == s, "state round trip");
    const auto store = card.store();
    check(store.kind() == dw4::save::CardStore::Kind::single_file && store.save_path(0) == card.file(), "slot A is the cache file");
    check(!store.slot_exists(1), "card B is absent");
    check(online_card_folder("x", "Kim.Lee") == std::filesystem::path("x") / "kim_lee", "account folder");
    std::filesystem::remove_all(card.folder());
}

CharacterSync make_sync(const OnlineCard& card, FakeHttp& http) {
    CharacterSync::Config config;
    config.card = card;
    config.context.protocol = 44;
    config.submit = [&http](HttpRequest r) { return http.submit(std::move(r)); };
    return CharacterSync(config);
}

void new_character() {
    const OnlineCard card(fresh_folder("new"));
    FakeHttp http;
    auto sync = make_sync(card, http);
    std::string error;
    check(sync.enter(answer(404, "{\"code\":\"no_online_character\"}"), error), "404 enters with an empty card");
    check(!std::filesystem::exists(card.file()), "no card file: the new game flow");
    const auto r1 = make_record(500, 1);
    sync.add_play_time(30.4);
    sync.auto_save(r1, CharacterSync::Why::stage_change);
    check(card.state().pending && record_of_box(*card.read(), 0) == r1, "the save is in the cache before the upload");
    auto [id, put] = http.take();
    check(header(put, "If-None-Match") == "*", "the first save creates the character");
    check(json::parse(put.body).value()["playSeconds"].i64() == 30, "the session's play time");
    // A newer save while the first is in flight waits; the newest wins.
    const auto r2 = make_record(500, 2), r3 = make_record(500, 3);
    sync.auto_save(r2, CharacterSync::Why::stage_change);
    sync.auto_save(r3, CharacterSync::Why::stage_change);
    check(http.sent.empty(), "one upload at a time");
    check(sync.on_response(id, answer(201, "{\"revision\":1}")), "the answer is the sync's");
    check(sync.revision() == 1 && sync.unique() == 500, "revision 1");
    auto [id2, put2] = http.take();
    check(header(put2, "If-Match") == "\"1\"" && body_record(put2) == r3, "the newest waiting save follows with If-Match");
    check(sync.on_response(id2, answer(200, "{\"revision\":2}")), "second answer");
    check(sync.revision() == 2 && !card.state().pending && card.state().revision == 2, "confirmed: nothing pending");
    // An unchanged save is skipped.
    sync.auto_save(r3, CharacterSync::Why::leave);
    check(http.sent.empty(), "identical to the confirmed record: no upload");
    // Another character is never uploaded.
    sync.auto_save(make_record(999, 50), CharacterSync::Why::stage_change);
    check(http.sent.empty(), "another UNIQUE is not the online character");
    check(sync.idle() && sync.uploads() == 2, "two uploads");
    std::filesystem::remove_all(card.folder());
}

void save_room_and_conflict() {
    const OnlineCard card(fresh_folder("conflict"));
    FakeHttp http;
    auto sync = make_sync(card, http);
    const auto server = make_record(42, 10, "Kim");
    std::string error;
    check(sync.enter(answer(200, character_json(5, server, 7200)), error), "enter with the server's character");
    check(record_of_box(*card.read(), 0) == server && card.state().revision == 5 && !card.state().pending, "the card holds revision 5");
    // The save room writes box 1 (the game's own write), card_written uploads it.
    const auto saved = make_record(42, 11, "Kim");
    const auto before = card.read();
    card.write(card_with_record(&*before, saved, 1, true, 0));
    sync.card_written(1);
    auto [id, put] = http.take();
    check(header(put, "If-Match") == "\"5\"" && body_record(put) == saved, "the written box goes up with If-Match 5");
    check(json::parse(put.body).value()["playSeconds"].i64() == 7200, "the server's play time carries on");
    // Another PC saved meanwhile: 409. Nothing is overwritten, the server's character is reloaded.
    sync.on_response(id, answer(409, "{\"code\":\"revision_conflict\",\"currentRevision\":6}"));
    check(sync.blocked(), "uploads stop for this room session");
    auto notices = sync.take_notices();
    check(!notices.empty() && notices.back().find("another PC") != std::string::npos, "the player is told");
    check(std::filesystem::exists(card.backups()) && !std::filesystem::is_empty(card.backups()), "the local cache was kept in backups");
    auto [reload_id, get] = http.take();
    check(get.method == "GET" && get.path == "/online-character", "the server's character is loaded again");
    const auto other = make_record(42, 14, "Kim");
    sync.on_response(reload_id, answer(200, character_json(6, other, 9000)));
    check(record_of_box(*card.read(), 0) == other && card.state().revision == 6 && !card.state().pending, "the card holds revision 6");
    sync.auto_save(make_record(42, 12, "Kim"), CharacterSync::Why::stage_change);
    check(http.sent.empty() && sync.blocked(), "no silent overwrite later either");
    std::filesystem::remove_all(card.folder());
}

void retries_and_recovery() {
    const OnlineCard card(fresh_folder("retry"));
    {
        FakeHttp http;
        auto sync = make_sync(card, http);
        std::string error;
        sync.enter(answer(200, character_json(3, make_record(8, 4), 100)), error);
        sync.tick(0);
        const auto r = make_record(8, 5);
        sync.auto_save(r, CharacterSync::Why::stage_change);
        auto [id, put] = http.take();
        sync.on_response(id, answer(429, "{\"code\":\"rate_limited\"}"));
        check(!sync.blocked() && http.sent.empty(), "a rate limit waits");
        sync.tick(5);
        check(http.sent.empty(), "not before the retry time");
        sync.tick(25);
        auto [id2, again] = http.take();
        check(body_record(again) == r && header(again, "If-Match") == "\"3\"", "the same save again");
        sync.on_response(id2, answer(0, ""));
        sync.tick(40);
        auto [id3, third] = http.take();
        check(body_record(third) == r, "after a network error too");
        // A 409 naming the save's own base revision is a lost race on the server (a deadlock), not another PC.
        sync.on_response(id3, answer(409, "{\"code\":\"revision_conflict\",\"currentRevision\":3}"));
        check(!sync.blocked() && http.sent.empty(), "a lost server race waits briefly");
        sync.tick(43);
        auto [id4, fourth] = http.take();
        (void)id4;
        check(body_record(fourth) == r && header(fourth, "If-Match") == "\"3\"", "and goes again");
        // The PC crashes now: the cache holds the pending save.
    }
    check(card.state().pending && card.state().revision == 3, "pending save of revision 3 in the cache");
    {
        // Next room entry: the server still has revision 3 -> the unsent save goes up first.
        FakeHttp http;
        auto sync = make_sync(card, http);
        std::string error;
        check(sync.enter(answer(200, character_json(3, make_record(8, 4), 100)), error), "enter");
        auto [id, put] = http.take();
        check(header(put, "If-Match") == "\"3\"" && body_record(put) == make_record(8, 5), "the unsent save is recovered");
        sync.on_response(id, answer(200, "{\"revision\":4}"));
        check(!card.state().pending && card.state().revision == 4, "recovered");
    }
    {
        // A pending save whose base revision the server no longer has: the server wins, the cache is kept aside.
        card.set_state({4, 100, true, 0, 8});
        FakeHttp http;
        auto sync = make_sync(card, http);
        std::string error;
        check(sync.enter(answer(200, character_json(9, make_record(8, 20), 500)), error), "enter");
        check(http.sent.empty(), "nothing is uploaded over the newer revision");
        check(record_of_box(*card.read(), 0) == make_record(8, 20) && !card.state().pending, "the server's revision 9 is on the card");
        check(!sync.take_notices().empty(), "the player is told");
    }
    {
        // A failed GET does not enter the room.
        FakeHttp http;
        auto sync = make_sync(card, http);
        std::string error;
        check(!sync.enter(answer(500, ""), error) && !error.empty(), "no guess at the character");
    }
    std::filesystem::remove_all(card.folder());
}

// Round 56 review: "Create Digimon" (the character select's first option)
// while the account has an online character. The new character's saves were
// skipped silently (another UNIQUE) or, with the UNIQUE the frame count gave
// the old one too, overwrote it. Now the new game is never uploaded and the
// player is told at once; a Game Start with the card's character saves again.
void new_game_beside_the_online_character() {
    const OnlineCard card(fresh_folder("foreign"));
    FakeHttp http;
    auto sync = make_sync(card, http);
    const auto online = make_record(3420661002u, 30, "Kim");
    std::string error;
    check(sync.enter(answer(200, character_json(4, online, 9000)), error), "enter with the online character");
    // The character select makes a new character with the same UNIQUE (the same frame of a fresh engine).
    sync.new_game(3420661002u);
    sync.game_started();
    auto notices = sync.take_notices();
    check(notices.size() == 1 && notices[0].find("NEW character") != std::string::npos, "the player is told at once");
    check(sync.status_text().find("not saving") != std::string::npos, "the status line says it");
    sync.auto_save(make_record(3420661002u, 1, "Kim"), CharacterSync::Why::stage_change);
    sync.auto_save(make_record(3420661002u, 2, "Kim"), CharacterSync::Why::leave);
    check(http.sent.empty() && !card.state().pending && card.state().revision == 4, "nothing goes up or into the cache");
    check(sync.take_notices().empty(), "told once");
    // Back at the title, the card's character: its saves go up again.
    sync.game_started();
    check(sync.status_text().find("saved (revision 4)") != std::string::npos, "the status line is back");
    sync.auto_save(make_record(3420661002u, 31, "Kim"), CharacterSync::Why::stage_change);
    auto [id, put] = http.take();
    check(header(put, "If-Match") == "\"4\"" && body_record(put) == make_record(3420661002u, 31, "Kim"), "the online character saves");
    sync.on_response(id, answer(200, "{\"revision\":5}"));
    // A save of another UNIQUE (a second card character): skipped, and the player is told the first time.
    sync.auto_save(make_record(77, 9, "Lee"), CharacterSync::Why::stage_change);
    notices = sync.take_notices();
    check(http.sent.empty() && notices.size() == 1 && notices[0].find("not your online character") != std::string::npos,
          "another character's first skipped save is told");
    std::filesystem::remove_all(card.folder());
}

// Round 59 review: Create Digimon reports the new character at the status
// window; B back out of it, the name entry and the partner selection, then
// Call Digimon from the card and START: the online character was never saved
// for the whole session. The Game Start decides from the title's last choice
// now. A box the save room filled with another character stays another
// character when it is called later, even with the online character's
// UNIQUE.
void the_title_decides_at_the_game_start() {
    const OnlineCard card(fresh_folder("title"));
    FakeHttp http;
    auto sync = make_sync(card, http);
    const auto online = make_record(3420661002u, 30, "Kim");
    std::string error;
    check(sync.enter(answer(200, character_json(4, online, 9000)), error), "enter with the online character");
    // K3 (the same UNIQUE) and K3b (another one): a new character, taken back, the card's character called.
    int revision = 4;
    for (const std::uint32_t made : {3420661002u, 4037944336u}) {
        sync.new_game(made);
        check(!sync.take_notices().empty(), "told at the status window");
        sync.character_called(0, 3420661002u);
        sync.game_started();
        check(!sync.foreign() && sync.status_text().find("not saving") == std::string::npos, "the called online character saves");
        sync.auto_save(make_record(3420661002u, 27 + revision, "Kim"), CharacterSync::Why::stage_change);
        auto [id, put] = http.take();
        check(header(put, "If-Match") == "\"" + std::to_string(revision) + "\"", "the stage change goes up");
        sync.on_response(id, answer(200, "{\"revision\":" + std::to_string(++revision) + "}"));
    }
    // A new character that does start: not uploaded; the save room puts it into box 1.
    sync.new_game(77);
    sync.game_started();
    check(sync.foreign(), "a new character's game");
    const auto stranger = make_record(3420661002u, 2, "Kim"); // (a UNIQUE collision)
    const auto before = card.read();
    card.write(card_with_record(&*before, stranger, 1, true, 0));
    sync.card_written(1);
    check(http.sent.empty(), "not uploaded");
    (void)sync.take_notices();
    // Called from box 1 later, it is still not the online character.
    sync.character_called(1, 3420661002u);
    sync.game_started();
    check(sync.foreign(), "box 1 holds another character");
    sync.auto_save(stranger, CharacterSync::Why::stage_change);
    check(http.sent.empty(), "its saves are not uploaded");
    // Box 0 again: the online character.
    sync.character_called(0, 3420661002u);
    sync.game_started();
    check(!sync.foreign(), "box 0 is the online character");
    std::filesystem::remove_all(card.folder());
}

// Round 59 review: another PC of the account entered the room and the relay
// replaced this connection; this PC's leaving save made the other PC's first
// upload a 409. The saves stay pending in the card cache now.
void replaced_by_another_pc() {
    const OnlineCard card(fresh_folder("replaced"));
    FakeHttp http;
    auto sync = make_sync(card, http);
    std::string error;
    sync.enter(answer(200, character_json(2, make_record(9, 5), 600)), error);
    sync.replaced();
    check(sync.blocked() && sync.status_text().find("another PC") != std::string::npos, "the status line says why");
    check(!sync.take_notices().empty(), "the player is told");
    sync.auto_save(make_record(9, 6), CharacterSync::Why::leave);
    check(http.sent.empty(), "no upload");
    check(card.state().pending && card.state().revision == 2 && record_of_box(*card.read(), 0) == make_record(9, 6),
          "the leaving save is pending in the cache, based on revision 2");
    std::filesystem::remove_all(card.folder());
}

// Round 59 review: after a 409 the session's later saves were kept nowhere
// (the next room entry rebuilt the cache without a backup). They go into the
// backup the conflict made.
void saves_after_a_conflict_go_into_its_backup() {
    const OnlineCard card(fresh_folder("blocked"));
    FakeHttp http;
    auto sync = make_sync(card, http);
    std::string error;
    sync.enter(answer(200, character_json(3, make_record(5, 10), 600)), error);
    sync.auto_save(make_record(5, 11), CharacterSync::Why::stage_change);
    auto [id, put] = http.take();
    sync.on_response(id, answer(409, "{\"code\":\"revision_conflict\",\"currentRevision\":4}"));
    auto [reload_id, get] = http.take();
    sync.on_response(reload_id, answer(200, character_json(4, make_record(5, 12), 700)));
    sync.auto_save(make_record(5, 13), CharacterSync::Why::stage_change);
    sync.auto_save(make_record(5, 14), CharacterSync::Why::leave);
    check(http.sent.empty(), "nothing goes up");
    std::filesystem::path kept;
    for (const auto& e : std::filesystem::directory_iterator(card.backups())) kept = e.path();
    const auto bytes = dw4::save::read_whole_file(kept);
    check(bytes && record_of_box(*bytes, 0) == make_record(5, 14), "the backup holds the session's newest save");
    check(record_of_box(*card.read(), 0) == make_record(5, 12), "the cache holds the server's character");
    std::filesystem::remove_all(card.folder());
}

// Round 56 review: a refused login (Log out everywhere, a password change on
// another PC) stopped every later save of the session. The saves now go on
// into the card cache as pending, and the next room entry uploads the newest.
void refused_login_keeps_saving_into_the_cache() {
    const OnlineCard card(fresh_folder("auth"));
    {
        FakeHttp http;
        auto sync = make_sync(card, http);
        std::string error;
        sync.enter(answer(200, character_json(7, make_record(21, 10), 600)), error);
        sync.auto_save(make_record(21, 11), CharacterSync::Why::stage_change);
        auto [id, put] = http.take();
        sync.on_response(id, answer(401, "{\"code\":\"unauthorized\",\"title\":\"Not logged in.\"}"));
        check(sync.blocked() && http.sent.empty(), "no upload with a refused login");
        sync.auto_save(make_record(21, 12), CharacterSync::Why::stage_change);
        sync.auto_save(make_record(21, 13), CharacterSync::Why::leave);
        sync.tick(100);
        check(http.sent.empty(), "nothing is sent later either");
        check(card.state().pending && card.state().revision == 7 && record_of_box(*card.read(), 0) == make_record(21, 13),
              "the newest save is pending in the cache, based on revision 7");
        check(sync.status_text().find("next room entry") != std::string::npos, "the status line says where it goes");
    }
    {
        FakeHttp http;
        auto sync = make_sync(card, http);
        std::string error;
        check(sync.enter(answer(200, character_json(7, make_record(21, 10), 600)), error), "the next room entry");
        auto [id, put] = http.take();
        check(header(put, "If-Match") == "\"7\"" && body_record(put) == make_record(21, 13), "the newest save goes up first");
    }
    std::filesystem::remove_all(card.folder());
}

void sha256_vectors() {
    check(sha256_hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of nothing");
    const std::string abc = "abc";
    check(sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size())) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 of abc");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(two.data()), two.size())) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 of two blocks");
    std::vector<std::uint8_t> million(1000000, 'a');
    check(sha256_hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of a million a");
}

void cloud_backup() {
    using namespace dw4::game::online::cloud;
    const auto folder = fresh_folder("cloud");
    const auto store = dw4::save::CardStore::single_file(folder / "savedata.dat", folder / "savedata_b.dat");
    const auto local = card_with_record(nullptr, make_record(11, 30, "Local"), 0, false, 1);
    check(store.write(0, local), "a local save");
    FakeHttp http;
    CloudBackup::Config config;
    config.submit = [&http](HttpRequest r) { return http.submit(std::move(r)); };
    config.device = "test-pc";
    config.client_version = "0.56.0";
    CloudBackup cloud(config);
    cloud.refresh();
    auto [meta_id, meta] = http.take();
    check(meta.path == "/cloud-save/meta", "meta request");
    cloud.on_response(meta_id, answer(404, "{\"code\":\"no_cloud_save\"}"));
    check(cloud.known() && !cloud.overview(), "no backup yet");
    cloud.upload(*store.read(0));
    auto [up_id, up] = http.take();
    check(up.method == "PUT" && header(up, "If-None-Match") == "*" && header(up, "X-Sha256") == sha256_hex(local) &&
              header(up, "X-Device-Name") == "test-pc" && up.body.size() == local.size(),
          "the first upload");
    cloud.on_response(up_id, answer(201, "{\"revision\":1,\"format\":\"gci\",\"sizeBytes\":90176,\"sha256\":\"" + sha256_hex(local) + "\"}"));
    auto [meta2_id, meta2] = http.take();
    (void)meta2;
    const auto newer = card_with_record(nullptr, make_record(11, 35, "Cloud"), 0, false, 2);
    const std::string overview = "{\"latest\":{\"revision\":3,\"format\":\"gci\",\"sizeBytes\":90176,\"sha256\":\"" +
                                 sha256_hex(newer) +
                                 "\",\"deviceName\":\"other-pc\",\"uploadedAt\":\"2026-10-30T10:00:00Z\"},"
                                 "\"boxes\":[{\"box\":0,\"used\":true,\"playerName\":\"Cloud\",\"digimon\":\"p_dorumon\",\"level\":35}],"
                                 "\"history\":[{\"revision\":2,\"format\":\"gci\",\"sha256\":\"00\"},{\"revision\":1,\"sha256\":\"" +
                                 sha256_hex(local) + "\"}]}";
    cloud.on_response(meta2_id, answer(200, overview));
    check(cloud.overview() && cloud.overview()->latest.revision == 3 && cloud.overview()->boxes.size() == 1 &&
              cloud.overview()->history.size() == 2 && cloud.overview()->boxes[0].level == 35,
          "the overview");
    // An upload over revision 3 that meets a newer one: 409, nothing overwritten.
    cloud.upload(*store.read(0));
    auto [up2_id, up2] = http.take();
    check(header(up2, "If-Match") == "\"3\"", "the upload carries the revision the player saw");
    cloud.on_response(up2_id, answer(409, "{\"code\":\"revision_conflict\",\"currentRevision\":4}"));
    check(cloud.message_is_error() && cloud.message().find("another PC") != std::string::npos, "409 is shown");
    // Download: a wrong body is refused, the right one replaces the local save after a backup.
    cloud.download(0, store, folder / "backups");
    auto [down_id, down] = http.take();
    check(down.path == "/cloud-save", "download the latest");
    cloud.on_response(down_id, answer(200, std::string(local.begin(), local.end())));
    check(cloud.message_is_error() && store.read(0) == local, "a checksum mismatch leaves the local save alone");
    cloud.download(0, store, folder / "backups");
    auto [down2_id, down2] = http.take();
    (void)down2;
    cloud.on_response(down2_id, answer(200, std::string(newer.begin(), newer.end())));
    check(!cloud.message_is_error() && store.read(0) == newer, "the download is the local save now");
    check(std::filesystem::exists(folder / "backups") && !std::filesystem::is_empty(folder / "backups"), "the old save was backed up");
    cloud.download(1, store, folder / "backups");
    auto [down3_id, down3] = http.take();
    check(down3.path == "/cloud-save?revision=1", "an older revision");
    cloud.on_response(down3_id, answer(200, std::string(local.begin(), local.end())));
    check(store.read(0) == local, "revision 1 back");
    cloud.restore(2);
    auto [rest_id, rest] = http.take();
    check(rest.method == "POST" && rest.path == "/cloud-save/restore/2", "restore request");
    (void)rest_id;
    std::filesystem::remove_all(folder);
}

} // namespace

int main() {
    try {
        base64_round_trip();
        requests();
        card_layout();
        cache_state();
        new_character();
        save_room_and_conflict();
        retries_and_recovery();
        new_game_beside_the_online_character();
        the_title_decides_at_the_game_start();
        replaced_by_another_pc();
        saves_after_a_conflict_go_into_its_backup();
        refused_login_keeps_saving_into_the_cache();
        sha256_vectors();
        cloud_backup();
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
    std::cout << "online character: " << checks << " checks passed\n";
    return 0;
}
