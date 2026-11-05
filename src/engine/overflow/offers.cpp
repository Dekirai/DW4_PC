// Data Overflow (docs/SURVIVAL.md 4.2, 4.6): the design tables and the
// level-up offers (pure functions, unit tested: tests/overflow_director_tests).
#include "dw4/engine/overflow_director.hpp"

#include <algorithm>
#include <cstring>

namespace dw4::engine::overflow {
namespace {
#include "tables.inc"

// The offer weights of 4.6 (initial): Forge 25 %, Train 25 % (over its
// groups), Plug 20 % (over its stats). (Gear's 30 % comes with O1b.)
constexpr double kForgeWeight = 25.0, kTrainWeight = 25.0, kPlugWeight = 20.0;
} // namespace

const std::vector<FamilyRow>& family_table() {
    static const std::vector<FamilyRow> table(std::begin(kFamilies), std::end(kFamilies));
    return table;
}

const FamilyRow* family_row(std::string_view family) {
    for (const auto& row : family_table())
        if (family == row.family) return &row;
    return nullptr;
}

std::vector<PoolEntry> phase_pool(int phase, float own, float earlier) {
    std::vector<PoolEntry> pool;
    for (const auto& row : family_table()) {
        if (row.phase > phase) continue;
        pool.push_back({&row, (row.phase == phase ? own : earlier) * row.weight});
    }
    return pool;
}

const std::array<PhaseMusic, 4>& phase_music() { return kMusic; }

int train_cap(int steps) {
    static constexpr int kCaps[5] = {300, 750, 1250, 1750, 2500};
    return kCaps[std::clamp(steps, 0, 4)];
}

int plug_points(int level) { return 10 + 2 * std::max(1, level); }

const std::array<int, 11>& plug_stats() { return kPlugStats; }

const char* plug_name(int upcnt_index) { return upcnt_index >= 0 && upcnt_index < 11 ? kPlugNames[upcnt_index] : "?"; }

std::vector<Offer> eligible_offers(const OfferState& state) {
    std::vector<Offer> out;
    if (state.forge < std::min(5, state.forge_families)) out.push_back({OfferKind::forge, state.forge + 1});
    for (int g = 0; g < 3; ++g)
        if (state.groups[std::size_t(g)] && state.train[std::size_t(g)] < 4 && state.level + kTrainLead >= state.unlock_level[std::size_t(g)])
            out.push_back({OfferKind::train, kBlastSkill + g});
    for (const int stat : kPlugStats) out.push_back({OfferKind::plug, stat});
    return out;
}

std::uint64_t next_random(std::uint64_t& state) {
    // splitmix64 (the randomizer's pattern)
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double next_unit(std::uint64_t& state) { return double(next_random(state) >> 11) * (1.0 / 9007199254740992.0); }

std::vector<Offer> draw_offers(const OfferState& state, std::uint64_t& random) {
    auto pool = eligible_offers(state);
    // (A resistance plug weighs a quarter of a stat plug: UPCNT 7-10.)
    auto plug_share = [](int stat) { return stat >= 7 ? 0.25 : 1.0; };
    int trains = 0;
    double plugs = 0;
    for (const auto& offer : pool) {
        trains += offer.kind == OfferKind::train ? 1 : 0;
        if (offer.kind == OfferKind::plug) plugs += plug_share(offer.value);
    }
    auto weight = [&](const Offer& offer) {
        switch (offer.kind) {
        case OfferKind::forge: return kForgeWeight;
        case OfferKind::train: return kTrainWeight / double(std::max(1, trains));
        case OfferKind::plug: return kPlugWeight * plug_share(offer.value) / std::max(0.25, plugs);
        }
        return 0.0;
    };
    // Three lines drawn by weight without repeats: a Plug takes a line by its
    // weight, and since every plug stat is always possible the window is
    // never empty (the plugs fill it once Forge and Train are spent).
    std::vector<Offer> out;
    // The sure first Forge (kForgeSure): the first line of the first offers
    // until it is taken; the other two lines by weight.
    if (state.forge == 1 && state.drawn < state.forge_sure)
        if (const auto forge = std::find(pool.begin(), pool.end(), Offer{OfferKind::forge, 2}); forge != pool.end()) {
            out.push_back(*forge);
            pool.erase(forge);
        }
    while (out.size() < 3 && !pool.empty()) {
        double total = 0;
        for (const auto& offer : pool) total += weight(offer);
        double pick = next_unit(random) * total;
        std::size_t chosen = pool.size() - 1;
        for (std::size_t i = 0; i < pool.size(); ++i) {
            pick -= weight(pool[i]);
            if (pick < 0) {
                chosen = i;
                break;
            }
        }
        out.push_back(pool[chosen]);
        pool.erase(pool.begin() + std::ptrdiff_t(chosen));
    }
    return out;
}

int offer_priority(const Offer& offer) {
    switch (offer.kind) {
    case OfferKind::forge: return 0;
    case OfferKind::train: return 1 + (offer.value - kBlastSkill);
    case OfferKind::plug:
        for (std::size_t i = 0; i < kPlugStats.size(); ++i)
            if (kPlugStats[i] == offer.value) return 10 + int(i);
        return 30;
    }
    return 40;
}

} // namespace dw4::engine::overflow
