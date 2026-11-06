#include "character.hpp"

#include "api.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

namespace dw4::game::online {

// ---------------------------------------------------------------------------
// base64
// ---------------------------------------------------------------------------
namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int sextet(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::uint32_t be32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) << 24 | std::uint32_t(bytes[at + 1]) << 16 | std::uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
}
void put_be32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[at + std::size_t(i)] = std::uint8_t(v >> (24 - 8 * i));
}

// The record layout (save_data.cpp, 80364AEC): ISUSE u8 at 0, UNIQUE u32 at
// 4, PLAYERNAME at 40 (0xFFFF, then up to eight UTF-16 units).
constexpr std::size_t kUsed = 0, kUnique = 4, kName = 40;

// The first data copy of a GCI whose checksum and version word hold (the
// offset in the GCI bytes), as the game's READ_MEDIA checks it.
std::optional<std::size_t> valid_copy(std::span<const std::uint8_t> gci) {
    if (gci.size() < save::kGciEntrySize + save::kFileSize) return std::nullopt;
    for (const auto offset : save::kCopyOffsets) {
        const auto copy = gci.subspan(save::kGciEntrySize + offset, save::kCopySize);
        if (be32(copy, 4) == save::kCopyVersion && be32(copy, 0) == save::checksum(copy)) return save::kGciEntrySize + offset;
    }
    return std::nullopt;
}
} // namespace

std::string base64_encode(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t v = std::uint32_t(bytes[i]) << 16 | std::uint32_t(bytes[i + 1]) << 8 | bytes[i + 2];
        for (int k = 3; k >= 0; --k) out.push_back(kAlphabet[(v >> (6 * k)) & 63]);
    }
    if (const std::size_t rest = bytes.size() - i; rest) {
        std::uint32_t v = std::uint32_t(bytes[i]) << 16;
        if (rest == 2) v |= std::uint32_t(bytes[i + 1]) << 8;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(rest == 2 ? kAlphabet[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t v = 0;
    int bits = 0, padding = 0;
    for (const char c : text) {
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding) return std::nullopt; // data after the padding
        const int s = sextet(c);
        if (s < 0) return std::nullopt;
        v = (v << 6) | std::uint32_t(s);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(std::uint8_t(v >> bits));
        }
    }
    if (padding > 2 || (text.size() % 4) != 0) return std::nullopt;
    return out;
}

// ---------------------------------------------------------------------------
// REST
// ---------------------------------------------------------------------------
namespace character_api {

HttpRequest get_request() {
    HttpRequest r;
    r.path = "/online-character";
    return r;
}

HttpRequest put_request(const CharacterRecord& record, int revision, std::int64_t play_seconds, const Context& context) {
    HttpRequest r;
    r.method = "PUT";
    r.path = "/online-character";
    r.content_type = "application/json";
    if (revision > 0) r.headers.emplace_back("If-Match", "\"" + std::to_string(revision) + "\"");
    else r.headers.emplace_back("If-None-Match", "*");
    std::string ctx = "{\"players\":" + std::to_string(std::clamp(context.players, 1, 10)) + ",\"randomizer\":" +
                      (context.randomizer ? "true" : "false") + ",\"cheats\":" + (context.cheats ? "true" : "false") +
                      ",\"rebalanced\":" + (context.rebalanced ? "true" : "false") + ",\"saveStateLoaded\":false,\"difficulty\":" +
                      std::to_string(std::clamp(context.difficulty, 0, 2)) + ",\"protocol\":" + std::to_string(context.protocol) +
                      ",\"clientVersion\":" + json::quote(context.client_version);
    if (!context.room_code.empty()) ctx += ",\"roomCode\":" + json::quote(context.room_code);
    if (context.room_players > 0) ctx += ",\"roomPlayers\":" + std::to_string(std::clamp(context.room_players, 1, 10));
    ctx += "}";
    r.body = "{\"record\":\"" + base64_encode(record) + "\",\"playSeconds\":" + std::to_string(std::max<std::int64_t>(0, play_seconds)) +
             ",\"context\":" + ctx + "}";
    return r;
}

HttpRequest reset_request(const std::string& current_password) {
    HttpRequest r;
    r.method = "POST";
    r.path = "/online-character/reset";
    r.content_type = "application/json";
    r.body = "{\"currentPassword\":" + json::quote(current_password) + "}";
    return r;
}

std::optional<Character> parse_character(const HttpResponse& response) {
    if (response.status != 200) return std::nullopt;
    const auto v = json::parse(response.body);
    if (!v) return std::nullopt;
    Character c;
    const auto revision = (*v)["revision"].i64();
    const auto record = (*v)["record"].str();
    if (!revision || *revision < 1 || !record) return std::nullopt;
    const auto bytes = base64_decode(*record);
    if (!bytes || bytes->size() != save::kRecordSize) return std::nullopt;
    c.revision = int(*revision);
    std::copy(bytes->begin(), bytes->end(), c.record.begin());
    c.play_seconds = std::max<std::int64_t>(0, (*v)["playSeconds"].i64().value_or(0));
    c.flagged = (*v)["flagged"].flag().value_or(false);
    return c;
}

std::optional<int> parse_saved(const HttpResponse& response) {
    if (response.status != 200 && response.status != 201) return std::nullopt;
    const auto v = json::parse(response.body);
    if (!v) return std::nullopt;
    const auto revision = (*v)["revision"].i64();
    if (!revision || *revision < 1) return std::nullopt;
    return int(*revision);
}

std::string problem_code(const HttpResponse& response) {
    if (const auto v = json::parse(response.body)) return (*v)["code"].str().value_or("");
    return {};
}

std::optional<int> current_revision(const HttpResponse& response) {
    if (const auto v = json::parse(response.body))
        if (const auto r = (*v)["currentRevision"].i64(); r && *r >= 0) return int(*r);
    return std::nullopt;
}

} // namespace character_api

// ---------------------------------------------------------------------------
// The virtual card
// ---------------------------------------------------------------------------
std::filesystem::path online_card_folder(const std::filesystem::path& online_folder, const std::string& account) {
    std::string name;
    for (const char c : account) {
        const char lower = c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        name += (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '-' || lower == '_' ? lower : '_';
    }
    if (name.empty()) name = "_";
    return online_folder / name;
}

save::CardStore OnlineCard::store() const { return save::CardStore::single_file(file(), folder_ / "online_card_b.dat"); }

std::optional<std::vector<std::uint8_t>> OnlineCard::read() const { return save::read_whole_file(file()); }

bool OnlineCard::write(std::span<const std::uint8_t> gci, std::string* error) const {
    // (No .bak beside the cache: the backups below keep what a conflict replaces.)
    return save::write_file_atomic(file(), gci, false, error);
}

std::optional<std::filesystem::path> OnlineCard::backup() const {
    const auto bytes = read();
    if (!bytes || !save::valid_save_gci(*bytes)) return std::nullopt;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
    std::error_code ec;
    std::filesystem::create_directories(backups(), ec);
    auto target = backups() / ("online_card-" + std::string(stamp) + ".dat");
    for (int n = 2; std::filesystem::exists(target, ec) && n < 100; ++n)
        target = backups() / ("online_card-" + std::string(stamp) + "-" + std::to_string(n) + ".dat");
    if (!save::write_file_atomic(target, *bytes, false)) return std::nullopt;
    return target;
}

void OnlineCard::clear() const {
    backup();
    std::error_code ec;
    std::filesystem::remove(file(), ec);
}

CardCacheState OnlineCard::state() const {
    CardCacheState s;
    std::ifstream in(state_file());
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        try {
            if (key == "revision") s.revision = std::max(0, std::stoi(value));
            else if (key == "play_seconds") s.play_seconds = std::max<std::int64_t>(0, std::stoll(value));
            else if (key == "pending") s.pending = value == "1";
            else if (key == "box") s.box = std::clamp(std::stoi(value), 0, int(save::kRecordsPerCopy) - 1);
            else if (key == "unique") s.unique = std::uint32_t(std::stoul(value));
        } catch (const std::exception&) {
            return CardCacheState{};
        }
    }
    return s;
}

bool OnlineCard::set_state(const CardCacheState& s) const {
    std::ostringstream out;
    out << "# The online character's card cache (docs/SEAMLESS.md 9); the server is the truth.\n"
        << "revision=" << s.revision << "\nplay_seconds=" << s.play_seconds << "\npending=" << (s.pending ? 1 : 0) << "\nbox=" << s.box
        << "\nunique=" << s.unique << "\n";
    const auto text = out.str();
    return save::write_file_atomic(state_file(), std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), false);
}

std::vector<std::uint8_t> card_with_record(const std::vector<std::uint8_t>* base, const CharacterRecord& record, int box, bool keep_others,
                                           std::uint32_t modified) {
    const auto copy_at = base ? valid_copy(*base) : std::nullopt;
    std::vector<std::uint8_t> gci(save::kGciEntrySize + save::kFileSize, 0);
    if (copy_at) {
        // The directory entry and the 8 KB header (comment, banner, icon) stay.
        std::copy_n(base->begin(), save::kGciEntrySize + save::kHeaderSize, gci.begin());
    } else {
        save::GciEntry entry;
        entry.modified = modified;
        const auto head = save::encode_gci_entry(entry);
        std::copy(head.begin(), head.end(), gci.begin());
    }
    if (modified && copy_at) put_be32(gci, 0x28, modified);
    std::vector<std::uint8_t> copy(save::kCopySize, 0);
    if (copy_at && keep_others) std::copy_n(base->begin() + std::ptrdiff_t(*copy_at), save::kCopySize, copy.begin());
    put_be32(copy, 4, save::kCopyVersion);
    const std::size_t at = 8 + std::size_t(std::clamp(box, 0, int(save::kRecordsPerCopy) - 1)) * save::kRecordStride;
    // (The game's write clears the whole 8 KB box buffer before the record, 803674C0.)
    std::fill_n(copy.begin() + std::ptrdiff_t(at), std::min(save::kRecordStride, save::kCopySize - at), std::uint8_t(0));
    std::copy(record.begin(), record.end(), copy.begin() + std::ptrdiff_t(at));
    put_be32(copy, 0, save::checksum(copy));
    for (const auto offset : save::kCopyOffsets)
        std::copy(copy.begin(), copy.end(), gci.begin() + std::ptrdiff_t(save::kGciEntrySize + offset));
    return gci;
}

std::optional<CharacterRecord> record_of_box(std::span<const std::uint8_t> gci, int box) {
    if (box < 0 || box >= int(save::kRecordsPerCopy)) return std::nullopt;
    const auto at = valid_copy(gci);
    if (!at) return std::nullopt;
    CharacterRecord r{};
    const auto from = *at + 8 + std::size_t(box) * save::kRecordStride;
    std::copy_n(gci.begin() + std::ptrdiff_t(from), save::kRecordSize, r.begin());
    return r;
}

std::uint32_t record_unique(const CharacterRecord& r) { return be32(r, kUnique); }
bool record_used(const CharacterRecord& r) { return r[kUsed] != 0; }
std::string record_name(const CharacterRecord& r) {
    std::u16string name;
    for (std::size_t i = 0; i < 8; ++i) {
        const char16_t c = char16_t(r[kName + 2 + i * 2] << 8 | r[kName + 3 + i * 2]);
        if (c == 0) break;
        name.push_back(c);
    }
    return save::name_from_card(name);
}

// ---------------------------------------------------------------------------
// The uploads
// ---------------------------------------------------------------------------
namespace {
std::uint32_t card_time_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &now) != 0) return 0;
#else
    if (!localtime_r(&now, &local)) return 0;
#endif
    return save::card_time(local);
}
} // namespace

CharacterSync::CharacterSync(Config config) : config_(std::move(config)) {}

const char* CharacterSync::why_name(Why why) {
    switch (why) {
    case Why::save_room: return "save room";
    case Why::stage_change: return "stage change";
    case Why::leave: return "leaving the room";
    case Why::recovered: return "an unsent save";
    }
    return "?";
}

void CharacterSync::trace(const std::string& line) const {
    if (config_.trace) std::cout << "online-character: " << line << std::endl;
}

void CharacterSync::notice(std::string text) {
    trace("notice " + text);
    notices_.push_back(std::move(text));
}

std::vector<std::string> CharacterSync::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

std::int64_t CharacterSync::play_seconds() const { return std::int64_t(std::floor(base_play_ + session_play_)); }

save::StoryBytes record_story_bytes(const engine::RoomStory& story) {
    save::StoryBytes out{};
    std::copy(story.flags.begin(), story.flags.end(), out.begin());
    std::copy(story.folder.begin(), story.folder.end(), out.begin() + std::ptrdiff_t(story.flags.size()));
    std::size_t at = story.flags.size() + story.folder.size();
    for (const auto c : story.counters) {
        const auto u = std::uint32_t(c);
        for (int b = 3; b >= 0; --b) out[at++] = std::uint8_t(u >> (8 * b));
    }
    return out;
}

void CharacterSync::put_own_story(CharacterRecord& record) const {
    std::copy(own_story_.begin(), own_story_.end(), record.begin() + std::ptrdiff_t(save::kStoryOffset));
}

bool CharacterSync::enter(const HttpResponse& response, std::string& error) {
    namespace api = character_api;
    const auto& card = config_.card;
    const auto state = card.state();
    // The own story first (round G-core, 8.8): the server's record holds it (composed from the account's own story), no
    // character means none.
    std::optional<api::Character> server;
    if (response.status == 200) {
        server = api::parse_character(response);
        if (!server) {
            error = "The server's online character could not be read.";
            return false;
        }
        std::copy_n(server->record.begin() + std::ptrdiff_t(save::kStoryOffset), save::kStoryBytes, own_story_.begin());
    } else {
        own_story_.fill(0);
    }
    auto cached = card.read();
    // A cache written before round G-core may hold a borrowed story (the save room wrote the engine's room story): every
    // used box gets the own story before anything reads, uploads or backs it up.
    if (cached && (response.status == 200 || response.status == 404) && save::valid_save_gci(*cached)) {
        auto own = save::with_story(*cached, own_story_);
        if (own != *cached) {
            if (card.write(own)) trace("the card cache's boxes got the account's own story");
            cached = std::move(own);
        }
    }
    const auto cached_record = cached ? record_of_box(*cached, state.box) : std::nullopt;
    const bool unsent = state.pending && cached_record && record_used(*cached_record);
    if (response.status == 200) {
        revision_ = server->revision;
        unique_ = record_unique(server->record);
        base_play_ = double(server->play_seconds);
        if (unsent && state.revision == server->revision && record_unique(*cached_record) == unique_) {
            // A save of the last session that never reached the server (a
            // crash or a lost connection): it goes up first.
            box_ = state.box;
            base_play_ = double(std::max(server->play_seconds, state.play_seconds));
            entered_ = true;
            trace("enter revision " + std::to_string(revision_) + ": an unsent save of box " + std::to_string(box_) + " goes up");
            notice("Uploading the online character's last save, which did not reach the server before.");
            request(Save{*cached_record, Why::recovered}, false);
            return true;
        }
        if (unsent) {
            if (const auto kept = card.backup())
                notice("Your online character was saved on another PC since this PC's last save; this PC's unsent save was "
                       "kept in " + kept->generic_string() + ".");
        }
        const auto gci = card_with_record(cached ? &*cached : nullptr, server->record, 0, false, card_time_now());
        std::string problem;
        if (!card.write(gci, &problem)) {
            error = "The online card could not be written: " + problem;
            return false;
        }
        box_ = 0;
        foreign_boxes_.clear();
        confirmed_ = server->record;
        card.set_state({revision_, server->play_seconds, false, 0, unique_});
        entered_ = true;
        trace("enter revision " + std::to_string(revision_) + " unique " + std::to_string(unique_) + " name " + record_name(server->record) +
              " play " + std::to_string(server->play_seconds));
        return true;
    }
    if (response.status == 404) {
        revision_ = 0;
        unique_ = 0;
        base_play_ = 0;
        if (unsent && state.revision == 0) {
            // A new character whose first upload never arrived.
            box_ = state.box;
            base_play_ = double(state.play_seconds);
            entered_ = true;
            trace("enter without a character: the unsent new character of box " + std::to_string(box_) + " goes up");
            notice("Uploading your new online character, which did not reach the server before.");
            request(Save{*cached_record, Why::recovered}, false);
            return true;
        }
        if (cached && cached_record && record_used(*cached_record))
            if (const auto kept = card.backup())
                trace("the server has no online character; the old card cache was kept in " + kept->generic_string());
        std::error_code ec;
        std::filesystem::remove(card.file(), ec);
        card.set_state({});
        box_ = 0;
        entered_ = true;
        trace("enter without a character (a new game)");
        return true;
    }
    error = "Loading the online character failed: " + (response.status == 0 ? response.error : "HTTP " + std::to_string(response.status));
    return false;
}

void CharacterSync::card_written(int box) {
    if (!entered_ || box < 0 || box >= int(save::kRecordsPerCopy)) return;
    const auto gci = config_.card.read();
    const auto record = gci ? record_of_box(*gci, box) : std::nullopt;
    if (!record || !record_used(*record)) {
        trace("card_written box " + std::to_string(box) + ": no character in the card file");
        return;
    }
    if (foreign_ || (unique_ && record_unique(*record) != unique_)) {
        // (Called from this box later, it is still another character, even with the online character's UNIQUE.)
        foreign_boxes_.insert(box);
        notice("Box " + std::to_string(box + 1) + " holds another character than your online character, so it was not uploaded. " +
               std::string(box == box_ ? "Your online character was in that box: go offline and press Play again to have it on the card. "
                                       : "") +
               "Reset the online character (Online > Account) to start a new one.");
        skip_told_ = true;
        return;
    }
    foreign_boxes_.erase(box);
    box_ = box;
    request(Save{*record, Why::save_room}, false);
}

void CharacterSync::new_game(std::uint32_t unique) {
    title_choice_ = TitleChoice::made_new;
    // (Without an online character yet the new one becomes it.)
    if (!entered_ || (revision_ == 0 && unique_ == 0)) return;
    trace("new character (unique " + std::to_string(unique) + ") while the online character (unique " + std::to_string(unique_) +
          ") exists: not uploaded if the game starts with it");
    // (Told at once, at the status window: B back to the character select and Call Digimon still plays the online one.)
    notice("You made a NEW character, but your account already has an online character. The new one is not saved online. To play your "
           "online character, go back (B) to the character select and call it from the card; to start over, reset it in Online > "
           "Account first.");
}

void CharacterSync::character_called(int box, std::uint32_t unique) {
    title_choice_ = TitleChoice::called;
    called_box_ = box;
    trace("called box " + std::to_string(box) + " (unique " + std::to_string(unique) + ")");
}

void CharacterSync::game_started() {
    const bool has_online = entered_ && !(revision_ == 0 && unique_ == 0);
    const bool made_new = title_choice_ == TitleChoice::made_new;
    const bool foreign_box = title_choice_ == TitleChoice::called && foreign_boxes_.contains(called_box_);
    title_choice_ = TitleChoice::none;
    if (has_online && (made_new || foreign_box)) {
        foreign_ = true;
        skip_told_ = true;
        trace(made_new ? std::string("the game starts with a new character: its saves are not uploaded")
                       : "the game starts with box " + std::to_string(called_box_) + ", another character: its saves are not uploaded");
        if (foreign_box)
            notice("Box " + std::to_string(called_box_ + 1) + " holds another character than your online character: this game is not "
                   "saved online.");
        return;
    }
    foreign_ = false;
    skip_told_ = false;
}

void CharacterSync::replaced() {
    if (!entered_ || auth_blocked_) return;
    auth_blocked_ = true;
    last_problem_ = "this account went online on another PC";
    trace("replaced by another connection of the account: saves stay pending in the card cache");
    notice("This account went online on another PC, so this PC stopped saving the online character. Its progress since the last "
           "save stays on this PC and goes up the next time you play online here if the character was not saved elsewhere meanwhile.");
}

void CharacterSync::skipped(Why why) {
    trace(std::string("auto save (") + why_name(why) + ") skipped: another character than the online one");
    if (skip_told_) return;
    skip_told_ = true;
    notice("This game is not your online character, so it is not saved online. Load your online character at the title, or reset it in "
           "Online > Account to start a new one.");
}

void CharacterSync::auto_save(const CharacterRecord& record, Why why) {
    if (!entered_ || !record_used(record)) return;
    if (foreign_ || (unique_ && record_unique(record) != unique_)) {
        skipped(why);
        return;
    }
    request(Save{record, why}, true);
}

void CharacterSync::request(const Save& given, bool write_cache) {
    // A guest's cached or uploaded records carry the account's own story (round G-core, 8.8): the engine's records hold the
    // room owner's story in someone else's room, and the "unchanged" check below compares like with like. The room owner's
    // engine story is his own: his records keep it (a save that only moved his story on is not "unchanged").
    Save save = given;
    if (config_.guest) put_own_story(save.record);
    if (blocked_) {
        // (The cache holds the server's character again, and the next room entry rebuilds it: the session's later progress
        // goes into the backup the conflict made, which keeps the newest.)
        std::string where;
        if (blocked_backup_) {
            const auto before = save::read_whole_file(*blocked_backup_);
            const auto gci = card_with_record(before && save::valid_save_gci(*before) ? &*before : nullptr, save.record, box_, true, card_time_now());
            if (save::write_file_atomic(*blocked_backup_, gci, false)) where = ", kept in " + blocked_backup_->generic_string();
        }
        trace(std::string("save (") + why_name(save.why) + ") not uploaded: blocked (" + last_problem_ + ")" + where);
        return;
    }
    if (confirmed_ && *confirmed_ == save.record && in_flight_ == 0 && !waiting_) {
        trace(std::string("save (") + why_name(save.why) + ") unchanged since revision " + std::to_string(revision_));
        return;
    }
    // The cache first: a crash before the answer leaves a pending save that
    // the next room entry uploads.
    const auto& card = config_.card;
    if (write_cache) {
        const auto before = card.read();
        const auto gci = card_with_record(before ? &*before : nullptr, save.record, box_, true, card_time_now());
        std::string problem;
        if (!card.write(gci, &problem)) trace("the card cache could not be written: " + problem);
    }
    card.set_state({revision_, play_seconds(), true, box_, unique_ ? unique_ : record_unique(save.record)});
    if (auth_blocked_) {
        // (Pending in the cache: the next room entry, after a new login, uploads it.)
        trace(std::string("save (") + why_name(save.why) + ") kept in the card cache, not uploaded (" + last_problem_ + ")");
        return;
    }
    // One upload at a time, and none before a retry is due (a rate limit):
    // the newest save waits and replaces an older waiting one.
    if (in_flight_ || (retry_at_ >= 0 && now_ < retry_at_)) {
        waiting_ = save;
        return;
    }
    waiting_.reset();
    send(save);
}

void CharacterSync::send(const Save& save) {
    flying_ = save;
    sent_play_ = session_play_;
    retry_at_ = -1;
    in_flight_ = config_.submit(character_api::put_request(save.record, revision_, play_seconds(), config_.context));
    trace(std::string("upload (") + why_name(save.why) + ") if-match " + std::to_string(revision_) + " play " + std::to_string(play_seconds()) +
          " box " + std::to_string(box_));
}

void CharacterSync::reload() {
    reload_ = config_.submit(character_api::get_request());
}

void CharacterSync::after_reload(const HttpResponse& response) {
    const auto& card = config_.card;
    if (const auto server = character_api::parse_character(response)) {
        revision_ = server->revision;
        unique_ = record_unique(server->record);
        base_play_ = double(server->play_seconds);
        session_play_ = 0;
        std::copy_n(server->record.begin() + std::ptrdiff_t(save::kStoryOffset), save::kStoryBytes, own_story_.begin());
        const auto before = card.read();
        card.write(card_with_record(before ? &*before : nullptr, server->record, 0, false, card_time_now()));
        box_ = 0;
        confirmed_ = server->record;
        card.set_state({revision_, server->play_seconds, false, 0, unique_});
        trace("reloaded revision " + std::to_string(revision_));
    } else if (response.status == 404) {
        revision_ = 0;
        unique_ = 0;
        own_story_.fill(0);
        std::error_code ec;
        std::filesystem::remove(card.file(), ec);
        card.set_state({});
        trace("reloaded: the server has no online character now");
    } else {
        trace("reload failed: HTTP " + std::to_string(response.status));
    }
}

bool CharacterSync::on_response(std::uint64_t id, const HttpResponse& response) {
    namespace api = character_api;
    if (id == 0) return false;
    if (id == reload_) {
        reload_ = 0;
        after_reload(response);
        return true;
    }
    if (id != in_flight_) return false;
    in_flight_ = 0;
    const auto& card = config_.card;
    if (const auto revision = api::parse_saved(response)) {
        revision_ = *revision;
        unique_ = record_unique(flying_.record);
        // The play time the upload reported is the character's from now on.
        base_play_ += sent_play_;
        session_play_ -= sent_play_;
        confirmed_ = flying_.record;
        ++uploads_;
        last_problem_.clear();
        races_ = 0;
        card.set_state({revision_, play_seconds(), waiting_.has_value(), box_, unique_});
        trace(std::string("saved (") + why_name(flying_.why) + ") revision " + std::to_string(revision_));
        if (flying_.why == Why::save_room) notice("Online character saved.");
        if (waiting_) {
            const Save next = *waiting_;
            waiting_.reset();
            if (*confirmed_ == next.record) card.set_state({revision_, play_seconds(), false, box_, unique_});
            else if (!auth_blocked_) send(next);
            else card.set_state({revision_, play_seconds(), true, box_, unique_}); // (pending: the next room entry)
        }
        return true;
    }
    const auto code = api::problem_code(response);
    const std::string text = online::api::problem_text(response);
    // A 409 that names the very revision this save was based on is no save
    // from elsewhere: the server's write lost a race (a database deadlock
    // with another account's write). The same save goes again shortly.
    if (response.status == 409 && api::current_revision(response) == revision_ && races_ < 3) {
        ++races_;
        if (!waiting_) waiting_ = flying_;
        retry_at_ = now_ + 2.0;
        trace("409 at the same revision " + std::to_string(revision_) + " (a lost race on the server): retry in 2 s");
        return true;
    }
    if (response.status == 409 || (response.status == 404 && revision_ > 0)) {
        // Saved from another PC (or reset) meanwhile: never overwrite it.
        blocked_ = true;
        waiting_.reset();
        last_problem_ = "the online character changed on the server";
        const auto kept = card.backup();
        blocked_backup_ = kept;
        trace("conflict " + code + " current " + std::to_string(api::current_revision(response).value_or(-1)) + ": reloading");
        notice("Your online character was saved on another PC meanwhile (or reset). This PC's progress was NOT uploaded" +
               (kept ? " (kept in " + kept->generic_string() + ")" : std::string()) +
               "; the card now holds the server's character. Go offline and press Play again to go on with it.");
        reload();
        return true;
    }
    if (response.status == 429 || response.status == 0 || response.status >= 500) {
        // Busy or unreachable: the same save again later (a newer one wins).
        if (!waiting_) waiting_ = flying_;
        retry_at_ = now_ + (response.status == 429 ? 20.0 : 10.0);
        if (last_problem_.empty())
            notice("The online character could not be saved yet (" + text + "); trying again.");
        last_problem_ = text;
        trace("upload failed (" + std::to_string(response.status) + " " + text + "), retry in " + (response.status == 429 ? "20" : "10") + " s");
        return true;
    }
    if (response.status == 401 || response.status == 403 || response.status == 426) {
        // The login ended (logged out everywhere, a new password on another
        // PC) or the version is too old: the saves go on into the card cache
        // as pending (this one is there already), and the next room entry
        // uploads the newest when the server still has this revision.
        auth_blocked_ = true;
        waiting_.reset();
        last_problem_ = text;
        notice("The online character cannot be uploaded now (" + text + "). It is kept on this PC and goes up when you play online "
               "again.");
        trace("refused (" + std::to_string(response.status) + " " + code + "): saves stay pending in the card cache");
        return true;
    }
    // 422 (the record failed the server's checks) and the rest: this save is
    // refused; the next one may pass.
    last_problem_ = text;
    notice("The server refused the online character's save: " + text);
    trace("refused (" + std::to_string(response.status) + " " + code + "): " + text);
    if (waiting_) {
        const Save next = *waiting_;
        waiting_.reset();
        send(next);
    } else {
        card.set_state({revision_, play_seconds(), false, box_, unique_});
    }
    return true;
}

void CharacterSync::tick(double now) {
    now_ = now;
    if (retry_at_ >= 0 && now >= retry_at_ && in_flight_ == 0 && waiting_ && !blocked_ && !auth_blocked_) {
        const Save next = *waiting_;
        waiting_.reset();
        send(next);
    }
}

std::string CharacterSync::status_text() const {
    if (!entered_) return "Online character: not loaded";
    if (blocked_) return "Online character: not saving (" + last_problem_ + ")";
    if (foreign_ || skip_told_)
        return "Online character: not saving (this game is another character; load your online character at the title, or reset it in "
               "Online > Account to start a new one)";
    if (auth_blocked_) return "Online character: kept on this PC, uploaded when you play online again (" + last_problem_ + ")";
    if (in_flight_ || waiting_) return "Online character: saving...";
    if (revision_ == 0) return "Online character: none yet (a new game makes it)";
    return "Online character: saved (revision " + std::to_string(revision_) + ")";
}

} // namespace dw4::game::online
