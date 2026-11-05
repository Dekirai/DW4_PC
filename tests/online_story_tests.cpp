// The room story of seamless online co-op (docs/SEAMLESS.md 8): the compiled
// story sets against data/online/story_sets.json (the census), the golden
// vectors of data/online/story_golden.json (merge, pending set, rebase,
// application; the C# server's StoryGoldenTests read the same file) and the
// story and key blobs of BARRIER_OK / STORY_ACK.
#include "dw4/engine/online_story.hpp"
#include "online/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DW4_SOURCE_DIR
#define DW4_SOURCE_DIR "."
#endif

namespace {
using namespace dw4::engine;
namespace json = dw4::game::online::json;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
const std::filesystem::path kSource = DW4_SOURCE_DIR;

json::Value load(const char* relative) {
    std::ifstream file(kSource / relative, std::ios::binary);
    check(bool(file), std::string(relative) + " is readable");
    std::stringstream text;
    text << file.rdbuf();
    auto value = json::parse(text.str());
    check(value.has_value(), std::string(relative) + " parses");
    return *value;
}

std::int64_t num(const json::Value& v) {
    const auto n = v.i64();
    check(n.has_value(), "a number");
    return *n;
}
std::vector<std::int32_t> ints(const json::Value& v) {
    std::vector<std::int32_t> out;
    for (const auto& item : v.items) out.push_back(std::int32_t(num(item)));
    return out;
}

RoomStory room_of(const json::Value& v) {
    RoomStory r;
    r.revision = std::uint32_t(num(v["revision"]));
    for (const auto f : ints(v["flags"])) r.flags[std::size_t(f)] = 1;
    for (const auto k : ints(v["folder"])) r.folder[std::size_t(k)] = 1;
    for (const auto& pair : v["counters"].items) r.counters[std::size_t(num(pair.items.at(0)))] = std::int32_t(num(pair.items.at(1)));
    return r;
}
std::string describe(const RoomStory& r) {
    std::string s = "rev " + std::to_string(r.revision) + " flags";
    for (int i = 0; i < 1024; ++i)
        if (r.flags[std::size_t(i)]) s += " " + std::to_string(i);
    s += " folder";
    for (int i = 0; i < 12; ++i)
        if (r.folder[std::size_t(i)]) s += " " + std::to_string(i);
    s += " counters";
    for (int i = 0; i < 16; ++i)
        if (r.counters[std::size_t(i)]) s += " " + std::to_string(i) + "=" + std::to_string(r.counters[std::size_t(i)]);
    return s;
}
template <class V>
std::map<std::int32_t, V> pairs(const json::Value& v) {
    std::map<std::int32_t, V> out;
    for (const auto& pair : v.items) out[std::int32_t(num(pair.items.at(0)))] = V(num(pair.items.at(1)));
    return out;
}
StoryValues values_of(const json::Value& v) {
    StoryValues e;
    e.flags = pairs<std::uint8_t>(v["flags"]);
    e.folder = pairs<std::uint8_t>(v["folder"]);
    e.counters = pairs<std::int32_t>(v["counters"]);
    return e;
}
StoryKeys keys_of(const json::Value& v) { return StoryKeys{ints(v["flags"]), ints(v["folder"]), ints(v["counters"])}; }

// ---------------------------------------------------------------------------
void tables_against_the_census() {
    const auto sets = load("data/online/story_sets.json");
    check(sets["format"].str() == std::optional<std::string>("dw4-story-sets/1"), "the census file's format");
    check(ints(sets["phase"]) == std::vector<std::int32_t>(story::phase_flags().begin(), story::phase_flags().end()), "phase flags match the census");
    for (int d = 0; d < 3; ++d) {
        const auto copies = story::phase_copies(d);
        check(ints(sets["phaseCopies"].items.at(std::size_t(d))) == std::vector<std::int32_t>(copies.begin(), copies.end()),
              "phase copies match the census");
        check(num(sets["keyCopies"].items.at(std::size_t(d))) == story::key_copy(d), "key copies match the census");
    }
    const auto groups = story::working_groups();
    check(sets["working"].items.size() == groups.size(), "working groups match the census");
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto& g = sets["working"].items[i];
        check(num(g["first"]) == groups[i].first && num(g["count"]) == groups[i].count, "a working group matches the census");
        for (int d = 0; d < 3; ++d) check(num(g["copies"].items.at(std::size_t(d))) == groups[i].copies[std::size_t(d)], "a working copy matches");
    }
    check(ints(sets["persistent"]) == std::vector<std::int32_t>(story::persistent_flags().begin(), story::persistent_flags().end()),
          "persistent flags match the census");
    check(num(sets["keys"]) == story::kKeys, "key count matches the census");
    check(ints(sets["counters"]) == std::vector<std::int32_t>(story::additive_counters().begin(), story::additive_counters().end()),
          "additive counters match the census");
    // Never-merged values are none of the merged ones; copies are never persistent.
    for (const auto f : ints(sets["never"]["flags"]))
        check(!story::is_persistent(f) && !story::is_phase_flag(f), "never-merged flag " + std::to_string(f) + " is not merged");
    for (int f = 0; f < 1024; ++f)
        if (story::is_copy(f)) check(!story::is_persistent(f), "copy " + std::to_string(f) + " is not taken from members");
    check(story::is_persistent(19) && story::is_persistent(392) && !story::is_persistent(23) && !story::is_persistent(1), "spot checks");
    check(story::is_key(10) && !story::is_key(11) && story::is_additive(7) && !story::is_additive(2), "keys and counters");
}

void golden_vectors() {
    const auto golden = load("data/online/story_golden.json");
    check(golden["format"].str() == std::optional<std::string>("dw4-story-golden/1"), "the golden file's format");
    int cases = 0;
    for (const auto& c : golden["merge"].items) {
        const auto name = c["name"].str().value_or("?");
        StoryDelta delta;
        delta.phase = std::uint8_t(num(c["delta"]["phase"]));
        delta.flags = ints(c["delta"]["flags"]);
        delta.folder = ints(c["delta"]["folder"]);
        for (const auto& g : c["delta"]["counters"].items)
            delta.counters.push_back({std::int32_t(num(g.items.at(0))), std::uint32_t(num(g.items.at(1))), std::uint32_t(num(g.items.at(2))),
                                      std::int32_t(num(g.items.at(3)))});
        story::StoryLines lines;
        for (const auto& l : c["lines"].items)
            lines[{std::int32_t(num(l.items.at(0))), std::uint32_t(num(l.items.at(1))), std::uint32_t(num(l.items.at(2)))}] = std::int32_t(num(l.items.at(3)));
        const auto result = story::merge(room_of(c["room"]), delta, int(num(c["difficulty"])), lines);
        const auto want = room_of(c["expect"]["room"]);
        check(result.story == want, "merge '" + name + "': " + describe(result.story) + " != " + describe(want));
        check(result.changed == c["expect"]["changed"].flag().value_or(false), "merge '" + name + "': changed");
        std::vector<StoryRefused> refused;
        for (const auto& r : c["expect"]["refused"].items)
            refused.push_back({StoryRefused::Kind(num(r.items.at(0))), std::int32_t(num(r.items.at(1))), StoryRefusal(num(r.items.at(2)))});
        check(result.refused == refused, "merge '" + name + "': refused keys");
        story::StoryLines want_lines;
        for (const auto& l : c["expect"]["lines"].items)
            want_lines[{std::int32_t(num(l.items.at(0))), std::uint32_t(num(l.items.at(1))), std::uint32_t(num(l.items.at(2)))}] = std::int32_t(num(l.items.at(3)));
        check(lines == want_lines, "merge '" + name + "': lines");
        ++cases;
    }
    for (const auto& c : golden["pending"].items) {
        const auto name = c["name"].str().value_or("?");
        const auto d = story::pending(room_of(c["confirmed"]), values_of(c["engine"]));
        check(d.phase == num(c["expect"]["phase"]), "pending '" + name + "': phase");
        check(d.flags == ints(c["expect"]["flags"]), "pending '" + name + "': flags");
        check(d.folder == ints(c["expect"]["folder"]), "pending '" + name + "': keys");
        check(d.counters.empty(), "pending '" + name + "': no counters");
        ++cases;
    }
    for (const auto& c : golden["rebase"].items) {
        const auto name = c["name"].str().value_or("?");
        auto r = room_of(c["room"]);
        story::rebase(r, int(num(c["difficulty"])));
        const auto want = room_of(c["expect"]);
        check(r == want, "rebase '" + name + "': " + describe(r) + " != " + describe(want));
        ++cases;
    }
    for (const auto& c : golden["apply"].items) {
        const auto name = c["name"].str().value_or("?");
        auto e = values_of(c["engine"]);
        const auto room = room_of(c["room"]);
        const auto dropped = keys_of(c["dropped"]);
        const std::string target = c["target"].str().value_or("");
        const bool would = story::holds_dropped(e, room, dropped, target);
        const bool removed = story::apply(e, room, dropped, target);
        check(e == values_of(c["expect"]["engine"]), "apply '" + name + "': engine values");
        check(removed == c["expect"]["removed"].flag().value_or(false) && would == removed, "apply '" + name + "': removed");
        // A second application changes nothing (the barrier merge is idempotent).
        auto again = e;
        story::apply(again, room, StoryKeys{}, target);
        check(again == e, "apply '" + name + "': idempotent");
        ++cases;
    }
    check(cases >= 20, "the golden file holds the cases");
    std::cout << cases << " golden story cases\n";
}

void blobs() {
    RoomStory s;
    s.flags[1] = 1;
    s.flags[1023] = 1;
    s.folder[10] = 1;
    s.counters[1] = 3956000;
    s.counters[15] = -2;
    const auto bytes = story::encode_story(s);
    check(bytes.size() == story::kStoryBytes && bytes.size() == 1100, "a story blob is 1,100 bytes");
    check(bytes[1] == 1 && bytes[1023] == 1 && bytes[1024 + 10] == 1, "flags then keys");
    check(bytes[1036 + 4] == 0x20 && bytes[1036 + 5] == 0x5D && bytes[1036 + 6] == 0x3C && bytes[1036 + 7] == 0x00, "counters little endian");
    check(story::decode_story(bytes, 9) == RoomStory{s.flags, s.folder, s.counters, 9}, "the story round trip");
    bool threw = false;
    try {
        (void)story::decode_story(std::span(bytes).first(1099), 0);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "a short story blob throws");
    const StoryKeys keys{{4, 392, 1023}, {5}, {7}};
    const auto kb = story::encode_keys(keys);
    const std::vector<std::uint8_t> want{3, 0, 4, 0, 0x88, 1, 0xFF, 3, 1, 5, 1, 7};
    check(kb == want, "the dropped blob's bytes");
    check(story::decode_keys(kb) == keys, "the dropped blob round trip");
    check(story::encode_keys({}).empty() && story::decode_keys({}).empty(), "an empty set is an empty blob");
    for (const auto& bad : {std::vector<std::uint8_t>{1, 0, 0}, std::vector<std::uint8_t>{1, 0, 0, 4, 0, 0}, std::vector<std::uint8_t>{0, 0, 1, 12, 0},
                            std::vector<std::uint8_t>{0, 0, 0, 1, 16}, std::vector<std::uint8_t>{0, 0, 0, 0, 9}}) {
        threw = false;
        try {
            (void)story::decode_keys(bad);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "a damaged dropped blob throws");
    }
}

} // namespace

int main() {
    try {
        tables_against_the_census();
        golden_vectors();
        blobs();
        std::cout << checks << " story checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Story test failure: " << error.what() << '\n';
        return 1;
    }
}
