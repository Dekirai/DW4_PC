#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <ostream>

namespace dw4::engine {

// The system's random number generator, beSystem +0x30: one
// igMersenneTwisterRandomNumber (vtable 804BB42C) for the whole game. Every
// random draw goes through its vtable +60 (8021EB40 -> genrand 8021B338):
// FIGHTCALC, the AIM maps, item drops, generators, shops, poison cures,
// particle headings, random WAITCHECKs and the scripts' SYSgetRandom. It is
// MT19937 with the original 1998 seeding: 8021B2F4 seeds 0x141E (5150)
// through vtable +5C, Knuth's 69069 generator (8021B2A0), when the object is
// made at boot; nothing seeds it again.
class SystemRandom {
public:
    static constexpr std::uint32_t kBootSeed = 0x141E;

    SystemRandom() { seed(kBootSeed); }

    // 8021B2A0: each state word takes the high halves of two successive
    // values of seed = 69069 * seed + 1.
    void seed(std::uint32_t value) {
        for (auto& word : state_) {
            word = value & 0xFFFF0000u;
            value = 69069u * value + 1u;
            word |= (value & 0xFFFF0000u) >> 16;
            value = 69069u * value + 1u;
        }
        index_ = kWords;
    }

    // 8021B338: refill all 624 words when used up (mag01 80566DB4), then
    // temper the next word.
    std::uint32_t operator()() {
        if (index_ >= kWords) {
            auto twist = [this](int at, int next, int far) {
                const std::uint32_t y = (state_[std::size_t(at)] & 0x80000000u) | (state_[std::size_t(next)] & 0x7FFFFFFFu);
                state_[std::size_t(at)] = state_[std::size_t(far)] ^ (y >> 1) ^ ((y & 1u) ? 0x9908B0DFu : 0u);
            };
            int k = 0;
            for (; k < kWords - kShift; ++k) twist(k, k + 1, k + kShift);
            for (; k < kWords - 1; ++k) twist(k, k + 1, k + kShift - kWords);
            twist(kWords - 1, 0, kShift - 1);
            index_ = 0;
        }
        std::uint32_t y = state_[std::size_t(index_++)];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9D2C5680u;
        y ^= (y << 15) & 0xEFC60000u;
        y ^= y >> 18;
        return y;
    }

    bool operator==(const SystemRandom&) const = default;

    // Save states: the index, then the 624 words.
    friend std::ostream& operator<<(std::ostream& out, const SystemRandom& random) {
        out << random.index_;
        for (const auto word : random.state_) out << ' ' << word;
        return out;
    }
    friend std::istream& operator>>(std::istream& in, SystemRandom& random) {
        SystemRandom read;
        in >> read.index_;
        for (auto& word : read.state_) in >> word;
        if (in && read.index_ >= 0 && read.index_ <= kWords) random = read;
        return in;
    }

    // The tick state (tick_state.hpp): the index and the words, binary.
    template <class Archive> void tick_io(Archive& a) {
        a.io(index_);
        a.io(state_);
        if (a.reading() && (index_ < 0 || index_ > kWords)) index_ = kWords;
    }

private:
    static constexpr int kWords = 624;
    static constexpr int kShift = 397;
    std::array<std::uint32_t, kWords> state_{};
    int index_ = kWords;
};

} // namespace dw4::engine
