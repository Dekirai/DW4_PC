// The room story of seamless online co-op (docs/SEAMLESS.md 8; online_story.hpp).
#include "dw4/engine/online_story.hpp"

#include <algorithm>
#include <bitset>
#include <limits>
#include <set>
#include <string>
#include <stdexcept>

namespace dw4::engine::story {

namespace tables {
#include "online_story_sets.inc"
} // namespace tables

namespace {

struct Sets {
    std::bitset<kFlags> phase, persistent, copy;
    Sets() {
        for (const auto f : tables::kPhase) phase.set(std::size_t(f));
        for (const auto f : tables::kPersistent) persistent.set(std::size_t(f));
        for (int d = 0; d < kDifficulties; ++d) {
            for (const auto f : tables::kPhaseCopies[std::size_t(d)]) copy.set(std::size_t(f));
            for (const auto& g : tables::kWorking)
                for (std::int32_t j = 0; j < g.count; ++j) copy.set(std::size_t(g.copies[std::size_t(d)] + j));
            for (std::int32_t k = 0; k <= tables::kKeys; ++k) copy.set(std::size_t(tables::kKeyCopies[std::size_t(d)] + k));
        }
    }
};
const Sets& sets() {
    static const Sets s;
    return s;
}

bool flag_in(const std::bitset<kFlags>& set, std::int32_t flag) { return flag >= 0 && flag < kFlags && set.test(std::size_t(flag)); }

std::uint8_t get(const std::map<std::int32_t, std::uint8_t>& m, std::int32_t key) {
    const auto it = m.find(key);
    return it == m.end() ? std::uint8_t(0) : it->second;
}
std::int32_t get(const std::map<std::int32_t, std::int32_t>& m, std::int32_t key) {
    const auto it = m.find(key);
    return it == m.end() ? 0 : it->second;
}
// Sets a value, touching the key only when its value changes (an absent key reads 0).
template <class V>
bool put(std::map<std::int32_t, V>& m, std::int32_t key, V value) {
    const auto it = m.find(key);
    if (it == m.end()) {
        if (value == V{}) return false;
        m.emplace(key, value);
        return true;
    }
    if (it->second == value) return false;
    it->second = value;
    return true;
}

void put_u16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(std::uint8_t(v));
    out.push_back(std::uint8_t(v >> 8));
}

} // namespace

std::span<const std::int32_t> phase_flags() { return tables::kPhase; }
std::span<const std::int32_t> phase_copies(int difficulty) { return tables::kPhaseCopies.at(std::size_t(difficulty)); }
std::span<const WorkingGroup> working_groups() { return tables::kWorking; }
std::span<const std::int32_t> persistent_flags() { return tables::kPersistent; }
std::int32_t key_copy(int difficulty) { return tables::kKeyCopies.at(std::size_t(difficulty)); }
std::span<const std::int32_t> additive_counters() { return tables::kCounters; }
bool is_phase_flag(std::int32_t flag) { return flag_in(sets().phase, flag); }
bool is_persistent(std::int32_t flag) { return flag_in(sets().persistent, flag); }
bool is_key(std::int32_t key) { return key >= 0 && key < tables::kKeys; }
bool is_additive(std::int32_t counter) { return std::find(tables::kCounters.begin(), tables::kCounters.end(), counter) != tables::kCounters.end(); }
bool is_copy(std::int32_t flag) { return flag_in(sets().copy, flag); }

int phase(const RoomStory& story) {
    for (int i = kPhases; i >= 1; --i)
        if (story.flags[std::size_t(tables::kPhase[std::size_t(i - 1)])]) return i;
    return 0;
}

int phase(const StoryValues& values) {
    for (int i = kPhases; i >= 1; --i)
        if (get(values.flags, tables::kPhase[std::size_t(i - 1)])) return i;
    return 0;
}

StoryDelta pending(const RoomStory& confirmed, const StoryValues& engine) {
    StoryDelta d;
    if (const int pe = phase(engine); pe > phase(confirmed)) d.phase = std::uint8_t(pe);
    for (const auto& [f, v] : engine.flags)
        if (v && is_persistent(f) && !confirmed.flags[std::size_t(f)]) d.flags.push_back(f);
    for (const auto& [k, v] : engine.folder)
        if (v && is_key(k) && !confirmed.folder[std::size_t(k)]) d.folder.push_back(k);
    return d;
}

namespace {

void set_phase(RoomStory& story, int index) {
    for (int i = 1; i <= kPhases; ++i) story.flags[std::size_t(tables::kPhase[std::size_t(i - 1)])] = i == index ? 1 : 0;
}

} // namespace

void derive_copies(RoomStory& story, int difficulty) {
    if (difficulty < 0 || difficulty >= kDifficulties) return;
    const auto d = std::size_t(difficulty);
    for (std::size_t i = 0; i < tables::kPhase.size(); ++i)
        story.flags[std::size_t(tables::kPhaseCopies[d][i])] = story.flags[std::size_t(tables::kPhase[i])] ? 1 : 0;
    for (const auto& g : tables::kWorking)
        for (std::int32_t j = 0; j < g.count; ++j)
            story.flags[std::size_t(g.copies[d] + j)] = story.flags[std::size_t(g.first + j)] ? 1 : 0;
    for (std::int32_t k = 0; k < tables::kKeys; ++k)
        story.flags[std::size_t(tables::kKeyCopies[d] + k)] = story.folder[std::size_t(k)] ? 1 : 0;
}

void rebase(RoomStory& story, int difficulty) {
    if (difficulty < 0 || difficulty >= kDifficulties) return;
    const auto d = std::size_t(difficulty);
    for (std::size_t i = 0; i < tables::kPhase.size(); ++i)
        story.flags[std::size_t(tables::kPhase[i])] = story.flags[std::size_t(tables::kPhaseCopies[d][i])] ? 1 : 0;
    for (const auto& g : tables::kWorking)
        for (std::int32_t j = 0; j < g.count; ++j)
            story.flags[std::size_t(g.first + j)] = story.flags[std::size_t(g.copies[d] + j)] ? 1 : 0;
    // gameStart.lua clears folder 0-11 and sets 0-10 from the copies.
    for (std::int32_t k = 0; k < kFolder; ++k)
        story.folder[std::size_t(k)] = k < tables::kKeys && story.flags[std::size_t(tables::kKeyCopies[d] + k)] ? 1 : 0;
}

MergeResult merge(const RoomStory& room, const StoryDelta& delta, int difficulty, StoryLines& lines) {
    MergeResult r;
    r.story = room;
    auto& s = r.story;
    using Kind = StoryRefused::Kind;
    if (delta.phase != 0) {
        const int pr = phase(room);
        if (delta.phase > kPhases) {
            r.refused.push_back({Kind::phase, delta.phase, StoryRefusal::not_story});
        } else if (delta.phase > pr) {
            if (delta.phase <= pr + 1) {
                set_phase(s, delta.phase);
                r.changed = true;
            } else {
                r.refused.push_back({Kind::phase, delta.phase, StoryRefusal::phase_jump});
            }
        }
    }
    std::set<std::int32_t> seen;
    for (const auto f : delta.flags) {
        if (!seen.insert(f).second) continue;
        if (!is_persistent(f)) {
            r.refused.push_back({Kind::flag, f, StoryRefusal::not_story});
            continue;
        }
        if (!s.flags[std::size_t(f)]) {
            s.flags[std::size_t(f)] = 1;
            r.changed = true;
        }
    }
    seen.clear();
    for (const auto k : delta.folder) {
        if (!seen.insert(k).second) continue;
        if (!is_key(k)) {
            r.refused.push_back({Kind::folder, k, StoryRefusal::not_story});
            continue;
        }
        if (!s.folder[std::size_t(k)]) {
            s.folder[std::size_t(k)] = 1;
            r.changed = true;
        }
    }
    for (const auto& g : delta.counters) {
        if (!is_additive(g.counter)) {
            r.refused.push_back({Kind::counter, g.counter, StoryRefusal::not_story});
            continue;
        }
        if (g.growth < 0) {
            r.refused.push_back({Kind::counter, g.counter, StoryRefusal::limit});
            continue;
        }
        auto& taken = lines[LineKey{g.counter, g.inst, g.visit}];
        if (g.growth <= taken) continue;
        const std::int64_t sum = std::int64_t(s.counters[std::size_t(g.counter)]) + (g.growth - taken);
        s.counters[std::size_t(g.counter)] = std::int32_t(std::min<std::int64_t>(sum, std::numeric_limits<std::int32_t>::max()));
        taken = g.growth;
        r.changed = true;
    }
    if (r.changed) {
        derive_copies(s, difficulty);
        ++s.revision;
    }
    return r;
}

bool apply(std::map<std::int32_t, std::uint8_t>& flags, std::map<std::int32_t, std::uint8_t>& folder,
           std::map<std::int32_t, std::int32_t>& counters, const RoomStory& room, const StoryKeys& dropped) {
    bool removed = false;
    const std::set<std::int32_t> dflags(dropped.flags.begin(), dropped.flags.end());
    const std::set<std::int32_t> dfolder(dropped.folder.begin(), dropped.folder.end());
    const std::set<std::int32_t> dcounters(dropped.counters.begin(), dropped.counters.end());
    // The phase: up to the room's; a dropped one falls back to it.
    int pe = 0;
    for (int i = kPhases; i >= 1 && !pe; --i)
        if (get(flags, tables::kPhase[std::size_t(i - 1)])) pe = i;
    const int pr = phase(room);
    int target = std::max(pe, pr);
    if (pe > pr && dflags.contains(tables::kPhase[std::size_t(pe - 1)])) {
        target = pr;
        removed = true;
    }
    if (target != pe)
        for (int i = 1; i <= kPhases; ++i) put(flags, tables::kPhase[std::size_t(i - 1)], std::uint8_t(i == target ? 1 : 0));
    // Persistent flags and keys the room has; then the dropped ones.
    for (const auto f : tables::kPersistent)
        if (const auto v = room.flags[std::size_t(f)]; v && !get(flags, f)) put(flags, f, v);
    for (const auto f : dflags)
        if (is_persistent(f) && !room.flags[std::size_t(f)] && put(flags, f, std::uint8_t(0))) removed = true;
    for (std::int32_t k = 0; k < tables::kKeys; ++k)
        if (const auto v = room.folder[std::size_t(k)]; v && !get(folder, k)) put(folder, k, v);
    for (const auto k : dfolder)
        if (is_key(k) && !room.folder[std::size_t(k)] && put(folder, k, std::uint8_t(0))) removed = true;
    // Counters: up to the room's; a dropped one takes the room's value.
    for (const auto c : tables::kCounters) {
        const std::int32_t rv = room.counters[std::size_t(c)];
        const std::int32_t ev = get(counters, c);
        if (dcounters.contains(c)) {
            if (ev > rv) removed = true;
            put(counters, c, rv);
        } else if (rv > ev) {
            put(counters, c, rv);
        }
    }
    return removed;
}

bool holds_dropped(const StoryValues& values, const RoomStory& room, const StoryKeys& dropped) {
    if (dropped.empty()) return false;
    StoryValues copy = values;
    return apply(copy, room, dropped);
}

std::vector<std::uint8_t> encode_story(const RoomStory& story) {
    std::vector<std::uint8_t> out(story.flags.begin(), story.flags.end());
    out.insert(out.end(), story.folder.begin(), story.folder.end());
    for (const auto c : story.counters) {
        const auto u = std::uint32_t(c);
        for (int b = 0; b < 4; ++b) out.push_back(std::uint8_t(u >> (8 * b)));
    }
    return out;
}

RoomStory decode_story(std::span<const std::uint8_t> bytes, std::uint32_t revision) {
    if (bytes.size() != kStoryBytes) throw std::runtime_error("a story blob has " + std::to_string(bytes.size()) + " bytes, not 1100");
    RoomStory s;
    std::copy_n(bytes.begin(), kFlags, s.flags.begin());
    std::copy_n(bytes.begin() + kFlags, kFolder, s.folder.begin());
    for (std::size_t i = 0; i < s.counters.size(); ++i) {
        const auto* p = bytes.data() + kFlags + kFolder + 4 * i;
        s.counters[i] = std::int32_t(std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24);
    }
    s.revision = revision;
    return s;
}

std::vector<std::uint8_t> encode_keys(const StoryKeys& keys) {
    std::vector<std::uint8_t> out;
    if (keys.empty()) return out;
    if (keys.flags.size() > kFlags || keys.folder.size() > kFolder || keys.counters.size() > kCounters)
        throw std::runtime_error("a story key set is too large");
    put_u16(out, std::uint32_t(keys.flags.size()));
    for (const auto f : keys.flags) put_u16(out, std::uint32_t(f));
    out.push_back(std::uint8_t(keys.folder.size()));
    for (const auto k : keys.folder) out.push_back(std::uint8_t(k));
    out.push_back(std::uint8_t(keys.counters.size()));
    for (const auto c : keys.counters) out.push_back(std::uint8_t(c));
    return out;
}

StoryKeys decode_keys(std::span<const std::uint8_t> bytes) {
    StoryKeys keys;
    if (bytes.empty()) return keys;
    std::size_t at = 0;
    const auto need = [&](std::size_t n) {
        if (bytes.size() - at < n) throw std::runtime_error("a story key set is cut short");
    };
    need(2);
    const std::size_t nf = std::size_t(bytes[at]) | std::size_t(bytes[at + 1]) << 8;
    at += 2;
    if (nf > kFlags) throw std::runtime_error("a story key set holds too many flags");
    need(2 * nf);
    for (std::size_t i = 0; i < nf; ++i, at += 2) {
        const std::int32_t f = std::int32_t(bytes[at]) | std::int32_t(bytes[at + 1]) << 8;
        if (f >= kFlags) throw std::runtime_error("a story key set names flag " + std::to_string(f));
        keys.flags.push_back(f);
    }
    need(1);
    const std::size_t nk = bytes[at++];
    if (nk > kFolder) throw std::runtime_error("a story key set holds too many keys");
    need(nk);
    for (std::size_t i = 0; i < nk; ++i) {
        if (bytes[at] >= kFolder) throw std::runtime_error("a story key set names key " + std::to_string(bytes[at]));
        keys.folder.push_back(bytes[at++]);
    }
    need(1);
    const std::size_t nc = bytes[at++];
    if (nc > kCounters) throw std::runtime_error("a story key set holds too many counters");
    need(nc);
    for (std::size_t i = 0; i < nc; ++i) {
        if (bytes[at] >= kCounters) throw std::runtime_error("a story key set names counter " + std::to_string(bytes[at]));
        keys.counters.push_back(bytes[at++]);
    }
    if (at != bytes.size()) throw std::runtime_error("a story key set has trailing bytes");
    return keys;
}

} // namespace dw4::engine::story
