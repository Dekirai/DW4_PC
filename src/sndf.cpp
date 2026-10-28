#include "dw4/sndf.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace dw4::sndf {
namespace {

// 804C4A98: 2^(c/1200) for cents 0..99, the octaves 2^0..2^11, 2^(s/12)
// for semitones 0..11 and 2^(-s/12) for 0..127 (six decimals, as floats).
constexpr std::uint32_t kCentsTable[252] = {
    0x3F800000u, 0x3F8012F1u, 0x3F8025E1u, 0x3F8038D2u, 0x3F804BCBu, 0x3F805EC4u, 0x3F8071C5u, 0x3F8084C7u,
    0x3F8097C8u, 0x3F80AAC9u, 0x3F80BDD3u, 0x3F80D0DDu, 0x3F80E3EFu, 0x3F80F6F9u, 0x3F810A13u, 0x3F811D26u,
    0x3F813040u, 0x3F81435Bu, 0x3F815675u, 0x3F816998u, 0x3F817CBBu, 0x3F818FE7u, 0x3F81A312u, 0x3F81B63Du,
    0x3F81C969u, 0x3F81DC9Cu, 0x3F81EFD8u, 0x3F82030Cu, 0x3F821648u, 0x3F822984u, 0x3F823CC9u, 0x3F82500Du,
    0x3F826352u, 0x3F827696u, 0x3F8289E3u, 0x3F829D39u, 0x3F82B085u, 0x3F82C3DBu, 0x3F82D730u, 0x3F82EA8Eu,
    0x3F82FDEBu, 0x3F831149u, 0x3F8324AFu, 0x3F833815u, 0x3F834B7Bu, 0x3F835EEAu, 0x3F837258u, 0x3F8385C6u,
    0x3F83993Du, 0x3F83ACB4u, 0x3F83C02Bu, 0x3F83D3AAu, 0x3F83E729u, 0x3F83FAA9u, 0x3F840E30u, 0x3F8421B8u,
    0x3F843548u, 0x3F8448CFu, 0x3F845C68u, 0x3F846FF8u, 0x3F848390u, 0x3F849729u, 0x3F84AAC9u, 0x3F84BE62u,
    0x3F84D20Bu, 0x3F84E5ACu, 0x3F84F955u, 0x3F850CFEu, 0x3F8520B0u, 0x3F853461u, 0x3F854813u, 0x3F855BCDu,
    0x3F856F87u, 0x3F858341u, 0x3F859703u, 0x3F85AAC5u, 0x3F85BE88u, 0x3F85D252u, 0x3F85E61Du, 0x3F85F9E8u,
    0x3F860DBBu, 0x3F86218Eu, 0x3F863569u, 0x3F864945u, 0x3F865D20u, 0x3F8670FCu, 0x3F8684E0u, 0x3F8698C4u,
    0x3F86ACB0u, 0x3F86C09Cu, 0x3F86D489u, 0x3F86E87Du, 0x3F86FC72u, 0x3F871066u, 0x3F872464u, 0x3F873861u,
    0x3F874C5Eu, 0x3F876063u, 0x3F877469u, 0x3F87886Eu, 0x3F800000u, 0x40000000u, 0x40800000u, 0x41000000u,
    0x41800000u, 0x42000000u, 0x42800000u, 0x43000000u, 0x43800000u, 0x44000000u, 0x44800000u, 0x45000000u,
    0x3F800000u, 0x3F879C7Cu, 0x3F8FACD6u, 0x3F9837EFu, 0x3FA14517u, 0x3FAADC0Au, 0x3FB504F7u, 0x3FBFC886u,
    0x3FCB2FF5u, 0x3FD744FEu, 0x3FE411EDu, 0x3FF1A1C2u, 0x3F800000u, 0x3F71A1BAu, 0x3F6411F5u, 0x3F5744F6u,
    0x3F4B2FFDu, 0x3F3FC88Eu, 0x3F3504F7u, 0x3F2ADC0Au, 0x3F214520u, 0x3F1837F8u, 0x3F0FACD6u, 0x3F079C84u,
    0x3F000000u, 0x3EF1A1BAu, 0x3EE411E4u, 0x3ED744F6u, 0x3ECB2FECu, 0x3EBFC88Eu, 0x3EB504E6u, 0x3EAADC0Au,
    0x3EA1450Fu, 0x3E9837F8u, 0x3E8FACE6u, 0x3E879C84u, 0x3E800000u, 0x3E71A1DCu, 0x3E641206u, 0x3E5744F6u,
    0x3E4B2FECu, 0x3E3FC86Du, 0x3E350508u, 0x3E2ADC0Au, 0x3E21450Fu, 0x3E1837F8u, 0x3E0FACE6u, 0x3E079C84u,
    0x3E000000u, 0x3DF1A198u, 0x3DE411C3u, 0x3DD744F6u, 0x3DCB302Fu, 0x3DBFC86Du, 0x3DB504C5u, 0x3DAADBC6u,
    0x3DA1450Fu, 0x3D9837B5u, 0x3D8FACE6u, 0x3D879C41u, 0x3D800000u, 0x3D71A198u, 0x3D6411C3u, 0x3D5744F6u,
    0x3D4B2FA9u, 0x3D3FC86Du, 0x3D3504C5u, 0x3D2ADC4Du, 0x3D214595u, 0x3D18383Bu, 0x3D0FACE6u, 0x3D079C41u,
    0x3D000000u, 0x3CF1A198u, 0x3CE412CFu, 0x3CD744F6u, 0x3CCB2FA9u, 0x3CBFC86Du, 0x3CB504C5u, 0x3CAADC4Du,
    0x3CA14489u, 0x3C98372Eu, 0x3C8FABDAu, 0x3C879C41u, 0x3C800000u, 0x3C71A198u, 0x3C6410B6u, 0x3C5744F6u,
    0x3C4B31C2u, 0x3C3FCA86u, 0x3C3506DDu, 0x3C2ADA34u, 0x3C214489u, 0x3C183947u, 0x3C0FABDAu, 0x3C079C41u,
    0x3C000219u, 0x3BF1A198u, 0x3BE410B6u, 0x3BD74928u, 0x3BCB31C2u, 0x3BBFCA86u, 0x3BB502ACu, 0x3BAADA34u,
    0x3BA148BBu, 0x3B983516u, 0x3B8FB00Cu, 0x3B87A073u, 0x3B7FFBCEu, 0x3B71A198u, 0x3B6410B6u, 0x3B574928u,
    0x3B4B295Fu, 0x3B3FC222u, 0x3B3502ACu, 0x3B2ADA34u, 0x3B2148BBu, 0x3B183D79u, 0x3B0FA7A8u, 0x3B07980Fu,
    0x3AFFFBCEu, 0x3AF1B25Fu, 0x3AE410B6u, 0x3AD73861u, 0x3ACB295Fu, 0x3ABFC222u, 0x3AB502ACu, 0x3AAAEAFBu,
    0x3AA137F4u, 0x3A982CB2u, 0x3A8FA7A8u, 0x3A87A8D6u, 0x3A800EAEu, 0x3A71B25Fu, 0x3A6410B6u, 0x3A573861u,
    0x3A4B295Fu, 0x3A3FE3B0u, 0x3A35243Au, 0x3A2AEAFBu,
};
float table(int index) { return std::bit_cast<float>(kCentsTable[std::clamp(index, 0, 251)]); }

// The AX mixer (MIXInitChannel 80259334, stereo output): the left side is
// attenuated by the pan table at the pan, the right side by the table at
// 127 - pan (0.1 dB, 804C79AC), so a centred voice plays at -3 dB on each
// side; -90.4 dB and below is silence, +6 dB the ceiling.
constexpr std::int16_t kMixPan[128] = {
    0,    0,    -1,   -1,   -1,   -2,   -2,   -2,   -3,   -3,   -4,   -4,   -4,   -5,   -5,   -5,
    -6,   -6,   -7,   -7,   -7,   -8,   -8,   -9,   -9,   -10,  -10,  -10,  -11,  -11,  -12,  -12,
    -13,  -13,  -14,  -14,  -14,  -15,  -15,  -16,  -16,  -17,  -17,  -18,  -18,  -19,  -20,  -20,
    -21,  -21,  -22,  -22,  -23,  -23,  -24,  -25,  -25,  -26,  -26,  -27,  -28,  -28,  -29,  -30,
    -30,  -31,  -32,  -33,  -33,  -34,  -35,  -36,  -36,  -37,  -38,  -39,  -40,  -40,  -41,  -42,
    -43,  -44,  -45,  -46,  -47,  -48,  -49,  -50,  -51,  -52,  -54,  -55,  -56,  -57,  -59,  -60,
    -61,  -63,  -64,  -66,  -67,  -69,  -71,  -72,  -74,  -76,  -78,  -80,  -83,  -85,  -87,  -90,
    -93,  -96,  -99,  -102, -106, -110, -115, -120, -126, -133, -140, -150, -163, -180, -210, -904};
// 8045D130: an SFP split's pan as the AX pan (803BA3F0).
constexpr std::uint8_t kSfpPan[128] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   2,   2,   2,   2,   3,
    3,   4,   4,   5,   5,   6,   7,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  19,  20,  21,  23,
    24,  26,  28,  29,  31,  33,  35,  37,  39,  41,  43,  46,  48,  50,  52,  54,  56,  58,  60,  62,  64,  66,
    68,  70,  72,  74,  76,  78,  80,  82,  84,  87,  89,  91,  93,  94,  96,  98,  100, 101, 103, 104, 106, 107,
    108, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 119, 120, 121, 121, 122, 123, 123, 124, 124, 124, 125,
    125, 125, 126, 126, 126, 126, 126, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127};
} // namespace

float mix_level(int db10) { return db10 <= -904 ? 0.0f : std::pow(10.0f, float(std::min(db10, 60)) / 200.0f); }
std::pair<float, float> mix_pan(int pan) {
    pan = std::clamp(pan, 0, 127);
    return {mix_level(kMixPan[pan]), mix_level(kMixPan[127 - pan])};
}
int sfp_pan(int pan) { return kSfpPan[std::clamp(pan, 0, 127)]; }

float cents_ratio(int cents) {
    if (cents > 0) return table(cents % 100) * (table(cents / 1200 + 100) * table(cents % 1200 / 100 + 112));
    if (cents == 0) return 1.0f;
    int rest = cents % 100, semitones = cents / 100;
    if (rest != 0) {
        rest += 100;
        --semitones;
    }
    return table(-semitones + 124) * table(rest);
}

std::uint32_t src_ratio(float ratio) {
    const float fixed = 65536.0f * ratio;
    const std::uint32_t value = fixed <= 0.0f ? 0u : fixed >= 4294967040.0f ? 0xFFFFFFFFu : std::uint32_t(fixed);
    return std::min<std::uint32_t>(value, 0x40000u);
}

// ---------------------------------------------------------------------------
int AxPool::acquire(int priority, std::uint32_t owner, std::int64_t& dropped) {
    dropped = -1;
    // The free list: the voices the renderer does not hold.
    int free = -1;
    int used = 0;
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[std::size_t(i)].priority) ++used;
        else if (free < 0) free = i;
    }
    if (used >= kVoices - reserved_) free = -1;
    if (free < 0) {
        // The oldest voice (the list's tail) of the lowest priority below
        // the request (AX priorities 1..31; the renderer's 31 never).
        for (int p = 1; p < priority && free < 0; ++p) {
            std::uint64_t oldest = ~std::uint64_t{};
            for (int i = 0; i < kVoices; ++i) {
                const auto& v = voices_[std::size_t(i)];
                if (v.priority == p && v.stamp < oldest) {
                    oldest = v.stamp;
                    free = i;
                }
            }
        }
        if (free < 0) return -1;
        dropped = std::int64_t(voices_[std::size_t(free)].owner);
    }
    voices_[std::size_t(free)] = {priority, owner, ++stamp_};
    return free;
}

void AxPool::release(int voice) {
    if (voice >= 0 && voice < kVoices) voices_[std::size_t(voice)] = {};
}

void AxPool::set_priority(int voice, int priority) {
    if (voice < 0 || voice >= kVoices || !voices_[std::size_t(voice)].priority) return;
    voices_[std::size_t(voice)].priority = priority;
    voices_[std::size_t(voice)].stamp = ++stamp_; // to the head of the new list
}

int AxPool::in_use() const {
    int n = reserved_;
    for (const auto& v : voices_) n += v.priority ? 1 : 0;
    return n;
}

// ---------------------------------------------------------------------------
// Command 1 (803C2B08 case 1): a sequence still running gets 0xB038 (its
// tracks stop, its voices are keyed off and leave it); the request (low
// seven bits cleared) and state 1 are written at once.
bool Driver::seq_set(int seq, std::uint32_t request, std::int64_t time) {
    if (seq < 1 || seq >= kSeqs) return false;
    auto& s = seqs_[std::size_t(seq)];
    if (s.state > 1) {
        s.id = s.request | std::uint32_t(seq);
        messages_.push_back({time, 0xB0380000u | std::uint32_t(seq)});
    }
    s.request = request & ~0x7Fu;
    s.state = 1;
    return true;
}

// Command 2: the message request | sequence (only a request id with its
// top bit set, 0xA9..).
bool Driver::seq_play(int seq, std::int64_t time) {
    if (seq < 1 || seq >= kSeqs) return false;
    const auto& s = seqs_[std::size_t(seq)];
    if (!s.request || !s.state) return false;
    const std::uint32_t message = s.request | std::uint32_t(seq);
    if (!(message & 0x80000000u)) return false;
    messages_.push_back({time, message});
    return true;
}

// Command 3: 0xB035 while the sequence runs (state 2 or 3).
bool Driver::seq_stop(int seq, std::int64_t time) {
    if (seq < 1 || seq >= kSeqs || seqs_[std::size_t(seq)].state <= 1) return false;
    messages_.push_back({time, 0xB0350000u | std::uint32_t(seq)});
    return true;
}

// Command 4: 0xB037vvss.
void Driver::seq_volume(int seq, int volume, std::int64_t time) {
    if (seq < 1 || seq >= kSeqs) return;
    messages_.push_back({time, 0xB0370000u | (std::uint32_t(volume) << 8 & 0x7F00u) | std::uint32_t(seq)});
}

// Command 7 (803C319C): bit 1 when a track with the sequence's id runs a
// one-shot sequence (track +1 bit 7), bit 0 when any voice record names the
// sequence, in use or not (a dropped record keeps it).
int Driver::status(int seq) const {
    if (seq < 1 || seq >= kSeqs) return -1;
    int result = 0;
    const std::uint32_t id = seqs_[std::size_t(seq)].request | std::uint32_t(seq);
    for (const auto& t : tracks_)
        if (id && t.id == id && (t.seq_flags & 0x80)) {
            result |= 2;
            break;
        }
    for (const auto& v : voices_)
        if (v.seq == seq) return result | 1;
    return result;
}

float Driver::gain(const Voice& v) {
    const int attenuation = v.env >> 16;
    if (attenuation <= -904 || v.seq_volume <= 0) return 0.0f;
    return float(v.seq_volume) / 127.0f * mix_level(attenuation);
}

bool Driver::idle() const {
    if (!messages_.empty() || !ring_.empty()) return false;
    for (const auto& v : voices_)
        if (v.used) return false;
    for (const auto& t : tracks_)
        if (t.flags) return false;
    return true;
}

// The AX frame callback (802A8F7C).
void Driver::tick(std::int64_t frame, std::int64_t time, const Banks& banks, std::uint32_t& random, int ms) {
    // 803C325C: the tracks' waits (16.16 ms) move on by the tempo (+0x04,
    // 1.0 here) times the whole milliseconds since the last tick.
    for (auto& t : tracks_)
        if (t.flags && !(t.flags & 1)) t.wait -= std::int32_t(ms) << 16;
    // The voice records: an AX voice that ran out (its PB stopped in the
    // last frame, VPB +0x146 as AX synced it) is freed; a key-on voice whose
    // attack reached 0 dB goes to its decay and sustain level; a key-off
    // voice whose release reached -96 dB is freed (803BBC74).
    for (int i = 0; i < kVoices; ++i) {
        auto& v = voices_[std::size_t(i)];
        if (!v.used) continue;
        if (!v.loop && frame > v.end) {
            release(i);
            continue;
        }
        if (v.key == 1) {
            if (v.phase == 0 && v.env == 0) {
                v.env_delta = std::int32_t(v.decay) * 65536;
                v.env_target = std::int32_t(v.sustain) * 65536;
                v.phase = 1;
            }
        } else if (v.key == 2 && v.phase == 2 && v.env == -960 * 65536) {
            release(i);
        }
    }
    // The command ring: up to 64 messages a tick (SNDF +0x85E) go into the
    // MIDI ring behind the events the last tick's pass left there (DW4's
    // requests all name a sequence and its commands are 0xB0..: none starts
    // at once, 803C3590..803C3750); then the ring is drained, also of what
    // the drain adds (the passes of the requests it starts).
    for (int n = 0; n < 64 && !messages_.empty() && messages_.front().time <= time; ++n) {
        ring_push({false, messages_.front().value});
        messages_.pop_front();
    }
    while (!ring_.empty()) {
        const RingEntry entry = std::move(ring_.front());
        ring_.pop_front();
        if (!entry.note) dispatch(entry.message, banks);
        else if (entry.bank) note_on(entry.channel, *entry.bank, entry.bank, entry.key, entry.velocity, entry.seq, frame, random);
    }
    // The tick's own pass (803C3820): its events wait in the ring for the
    // next tick (a note after a delta sounds one AX frame after the tick
    // its wait ran out in; of the game's 599 sequences only 0xA9010A00 of
    // _common/sbiallse.igb has one).
    run_tracks();
    // AXARTServiceSound (80245544): the volume envelope steps (80245994) and
    // the MIX input; MIXUpdateSettings ramps the frame from the last level.
    for (auto& v : voices_) {
        if (!v.used) continue;
        v.gain_from = v.gain_to;
        if (v.env != v.env_target) {
            v.env += v.env_delta;
            if (v.env_delta > 0 && v.env > v.env_target) v.env = v.env_target;
            if (v.env_delta < 0 && v.env < v.env_target) v.env = v.env_target;
        }
        v.gain_to = gain(v);
        // A voice keeps its level of the last frame and ramps to the new one
        // (MIXUpdateSettings); a voice that starts in this frame starts at
        // its level. (By the DOL the note-on initialises the MIX channel
        // (802450BC -> MIXInitChannel) before the SEQ volume 127 that follows
        // SEQ_PLAY reaches it, which would fade every voice in over its first
        // frame from the SEQ volume 0 its sequence kept from its last stop;
        // Dolphin's onsets show no fade-in: 0.0 dB in the first 5 ms over 77
        // isolated sound effects of dng102b1.)
        if (v.fresh) v.gain_from = v.gain_to;
        v.fresh = false;
    }
}

// 803BBDD4 for the messages DW4 sends: 0xB038 / 0xB035 (cases 56 / 53),
// 0xB037 (case 55) and a request (803C5188).
void Driver::dispatch(std::uint32_t message, const Banks& banks) {
    const int seq = int(message & 0xFF);
    if ((message >> 24) == 0xB0) {
        const std::uint32_t command = (message >> 16) & 0x7F;
        auto& s = seqs_[std::size_t(seq)];
        if (command == 0x37) {
            s.volume = std::uint8_t((message >> 8) & 0x7F);
            if (s.state > 1)
                for (int i = 0; i < kVoices; ++i)
                    if (voices_[std::size_t(i)].seq == seq) update_volume(i);
            return;
        }
        if (command != 0x35 && command != 0x38) return;
        if (command == 0x35) s.id = s.request | std::uint32_t(seq);
        for (auto& t : tracks_)
            if (s.id && t.id == s.id && (t.seq_flags & 0x80)) {
                t.flags = 0;
                t.id = 0;
            }
        s.id = 0;
        for (int i = 0; i < kVoices; ++i) {
            auto& v = voices_[std::size_t(i)];
            if (v.seq != seq) continue;
            key_off(i);
            if (command == 0x38) {
                seqs_[std::size_t(v.seq)].voices = 0;
                v.seq = 0;
            }
        }
        return;
    }
    if ((message >> 24) >= 0xA8 && (message >> 24) < 0xAD && seq) start_track(message, banks);
}

// 803C5188: the request's group in the first bank slot that has it, the
// track (a one-shot sequence with flag 0x10 takes the running track of its
// group, else a free track, else the first one-shot track), state 2, and at
// once a pass over all tracks (803C49F4).
void Driver::start_track(std::uint32_t message, const Banks& banks) {
    int slot = -1;
    for (std::size_t i = 0; i < banks.size() && i < 16; ++i)
        if (banks[i] && banks[i]->bank && banks[i]->bank->has_group(message & 0xFFFFFF00u)) {
            slot = int(i);
            break;
        }
    if (slot < 0) return;
    const auto& bank = banks[std::size_t(slot)];
    const auto data = bank->bank->sequence(message & 0xFFFFFF00u);
    if (data.empty()) return;
    const std::uint8_t flags = data[0];
    int track = -1;
    if (flags & 0x80) {
        if ((flags & 0x10) == 0x10)
            for (int i = 0; i < kTracks; ++i)
                if (tracks_[std::size_t(i)].flags && (tracks_[std::size_t(i)].seq_flags & 0x0F) == (flags & 0x0F)) {
                    track = i;
                    break;
                }
    } else {
        for (int i = 0; i < kTracks; ++i)
            if (tracks_[std::size_t(i)].flags && tracks_[std::size_t(i)].channel == slot &&
                !(tracks_[std::size_t(i)].seq_flags & 0x80)) {
                track = i;
                break;
            }
    }
    if (track < 0)
        for (int i = 0; i < kTracks; ++i)
            if (!tracks_[std::size_t(i)].flags) {
                track = i;
                break;
            }
    if (track < 0)
        for (int i = 0; i < kTracks; ++i)
            if (tracks_[std::size_t(i)].seq_flags & 0x80) {
                track = i;
                break;
            }
    if (track < 0) return; // (the original writes past its 16 tracks)
    auto& t = tracks_[std::size_t(track)];
    t.channel = std::uint8_t(slot);
    t.id = message;
    const int seq = int(message & 0xFF);
    if (seq) seqs_[std::size_t(seq)].state = 2;
    t.wait = 0;
    t.seq_flags = flags;
    t.flags = 0x80;
    t.bank = bank;
    t.sequence = sfp::Sequence(data);
    run_tracks();
}

// 803C1C5C: an event into the MIDI ring unless it holds 128.
void Driver::ring_push(RingEntry entry) {
    if (ring_.size() < kRing) ring_.push_back(std::move(entry));
}

// 803C49F4: every running track whose wait is over (its whole milliseconds,
// wait >> 16, at most 0) plays its events up to the first one with a delta,
// which adds the delta to the wait and ends the track's pass, also a delta of
// 0 (a later pass goes on); a note goes into the MIDI ring (803C1C5C) for the
// drum note-on of the track's channel when the tick drains it (the channel's
// program: the bank's drum program, 803C06C4), the end stops the track at
// once (its sequence: state 3).
void Driver::run_tracks() {
    for (auto& t : tracks_) {
        if (!t.flags || (t.wait >> 16) > 0) continue;
        while (t.flags) {
            const auto event = t.sequence.next();
            if (event.kind == sfp::Event::Kind::end) {
                const int seq = int(t.id & 0xFF);
                if (seq) seqs_[std::size_t(seq)].state = 3;
                t.flags = 0;
                t.id = 0;
                break;
            }
            if (event.kind == sfp::Event::Kind::note && event.velocity && t.bank)
                ring_push({true, 0, t.channel, event.key, event.velocity, std::uint8_t(t.id & 0xFF), t.bank});
            if (event.has_wait) {
                t.wait += std::int32_t(event.wait_ms) << 16;
                break;
            }
        }
    }
}

// The drum note-on (803B9BFC) as the game's banks use it (one layer, type 2
// programs: the split is the key's): the split's sample and AX priority
// (+0x1D + 11), its tuning plus a random detune of +-split[0x14] 64ths of a
// semitone from the C library's rand() (8009C1CC), no voice above +2400
// cents, the voice
// (803C0CC0), the volume (803B936C: master, velocity curve, SEQ volume),
// pan and AUX A send, the volume envelope (an attack starts at -96 dB) and
// the SRC ratio of the pitch (AXART on a 32000 Hz sound: 8024583C).
// Not modelled, because none of the 86 shipped banks (600 splits, 599
// sequences) and none of the game's SNDF commands use them: the exclusive
// groups / key cut of split +0x1B (803B9700, modes 0x80 / 0x81 / 0x82 / 0x84 /
// 0x88; +0x1B is 0 in every split), velocity ranges of several layers (every
// program has one layer for velocities 0..127), the key - 60 of a type 0
// program (every program is type 2), the channel and sequence transposes
// (SNDF +0xAA6 / +0xAA7, sequence +8 / +9: commands 5 / 6 / 8 are never
// sent), the pitch bend (803C03E4) and the priority 10 -> 11 special case
// (803B9DEC: every split's +0x1D is 9).
void Driver::note_on(int channel, const BankData& bank, const std::shared_ptr<const BankData>& owner, int key,
                     int velocity, int seq, std::int64_t frame, std::uint32_t& random) {
    const auto* split = bank.bank->program_type() == 2 ? bank.bank->split_at(key) : bank.bank->split(key);
    if (!split || !split->sample_word || split->sample >= bank.pcm.size()) return;
    const int priority = (split->priority + 11) & 0xFF;
    // The pitch (803B9EC0..803B9F20): fctiwz(100 * semitones + 1.5625 *
    // fine), the fine tuning (+0x0B) and the random detune in 1/64
    // semitones (r2 -0x6D60 = 100.0, -0x6D58 = 1.5625).
    int fine = split->cents;
    if (split->detune) {
        random = random * 1103515245u + 12345u;
        const int r = int((random >> 16) & 0x7FFF);
        fine += std::int16_t(r % (2 * split->detune) - split->detune);
    }
    const int pitch = int(100.0 * double(split->semitones) + 1.5625 * double(fine));
    if (pitch > 2400) return;
    const int index = allocate(priority);
    if (index < 0) return;
    auto& v = voices_[std::size_t(index)];
    v.used = true;
    v.key = 1;
    v.channel = std::uint8_t(channel);
    v.priority = std::uint8_t(priority);
    v.seq = std::uint8_t(seq);
    if (seq) ++seqs_[std::size_t(seq)].voices;
    v.bank = owner;
    v.sample = split->sample;
    v.loop = bank.bank->sample(split->sample).loop;
    v.ratio = src_ratio(cents_ratio(pitch));
    v.start = frame;
    v.end = std::numeric_limits<std::int64_t>::max();
    const std::size_t length = bank.pcm[split->sample].size();
    if (!v.loop && v.ratio) {
        // The DSP runs out of samples after length / ratio of its own.
        const std::uint64_t samples = (std::uint64_t(length) * 65536u + v.ratio - 1) / v.ratio;
        v.end = frame + std::int64_t(samples ? (samples - 1) / kFrameSamples : 0);
    }
    const float level = master_ * float(bank.bank->level(velocity)) / 255.0f;
    const auto [left, right] = mix_pan(sfp_pan(split->pan));
    v.left = level * left;
    v.right = level * right;
    // The AUX A send (803BA5B4): the split's level 1..127 as 10 * 20
    // log10(level / 127), truncated (0.1 dB), after the fader with the main
    // bus's pan (every split of the game); a split taking the channel's
    // sends (+0x15, or a type 0 program) is never shipped and stays dry.
    v.aux_left = v.aux_right = 0.0f;
    if (!split->channel_aux && bank.bank->program_type() != 0 && split->aux_a) {
        const double ratio = double(float(split->aux_a) / 127.0f);
        const float aux = mix_level(int(10.0 * (20.0 * std::log10(ratio))));
        v.aux_left = v.left * aux;
        v.aux_right = v.right * aux;
    }
    v.decay = split->decay;
    v.sustain = split->sustain;
    v.release = split->release;
    v.phase = 0;
    if (split->attack) {
        v.env_delta = std::int32_t(split->attack) * 65536;
        v.env_target = 0;
        v.env = -960 * 65536;
    } else {
        v.env_delta = v.env_target = v.env = 0;
    }
    v.seq_volume = seq ? seqs_[std::size_t(seq)].volume : 127;
    v.gain_to = v.gain_from = gain(v);
    v.fresh = true;
}

// 803C0CC0: an AX voice of the priority (the drop callback 803C0B44 with the
// allocation counter), then the first free record; a voice without a record
// goes back.
int Driver::allocate(int priority) {
    std::int64_t dropped = -1;
    const int ax = ax_.acquire(priority, counter_, dropped);
    if (dropped >= 0) drop(std::uint32_t(dropped));
    int index = -1;
    if (ax >= 0)
        for (int i = 0; i < kVoices; ++i)
            if (!voices_[std::size_t(i)].used) {
                index = i;
                voices_[std::size_t(i)] = Voice{};
                voices_[std::size_t(i)].ax = ax;
                voices_[std::size_t(i)].id = counter_;
                break;
            }
    ++counter_;
    if (ax >= 0 && index < 0) ax_.release(ax);
    return index;
}

// 803BBA80: key off: the release (split +0x12 per frame, 0: at once) to
// -96 dB, the AX priority one lower.
void Driver::key_off(int index) {
    auto& v = voices_[std::size_t(index)];
    if (!v.used) return;
    v.key = 2;
    v.env_delta = v.release ? std::int32_t(v.release) * 65536 : -960 * 65536;
    v.env_target = -960 * 65536;
    v.phase = 2;
    if (v.priority >= 2 && !(v.priority & 0x80)) {
        --v.priority;
        ax_.set_priority(v.ax, v.priority);
    }
}

// 803BBC74: the voice stops and goes; its sequence loses it (state 3 with no
// voice left: state 1).
void Driver::release(int index) {
    auto& v = voices_[std::size_t(index)];
    if (!v.used) return;
    ax_.release(v.ax);
    if (v.seq) {
        auto& s = seqs_[std::size_t(v.seq)];
        if (s.voices) --s.voices;
        if (!s.voices && s.state == 3) s.state = 1;
    }
    v = Voice{};
}

// 803C0B44: the AX voice was taken: the record goes without touching its
// sequence (the sequence byte and the voice count stay).
void Driver::drop(std::uint32_t id) {
    for (auto& v : voices_)
        if (v.used && v.id == id) {
            const std::uint8_t seq = v.seq;
            v = Voice{};
            v.seq = seq;
            return;
        }
}

// 803B8F28: the voice's volume with its sequence's SEQ volume now.
void Driver::update_volume(int index) {
    auto& v = voices_[std::size_t(index)];
    if (v.used && v.seq) v.seq_volume = seqs_[std::size_t(v.seq)].volume;
}

// ---------------------------------------------------------------------------
void Driver::shift_frames(std::int64_t delta) {
    for (auto& v : voices_) {
        if (!v.used) continue;
        v.start += delta;
        if (v.end != std::numeric_limits<std::int64_t>::max()) v.end += delta;
    }
}

void Mirror::start(const Driver& game, std::uint32_t random, std::int64_t game_ticked, std::int64_t delta) {
    driver_ = game;
    driver_.shift_frames(delta);
    random_ = random;
    ticked_ = game_ticked + delta;
    calls_.clear();
    live_ = true;
}

void Mirror::record(Call call) {
    if (!live_) return;
    calls_.push_back(std::move(call));
    if (calls_.size() > 65536) {
        calls_.clear();
        live_ = false;
    }
}

void Mirror::replay(std::int64_t game) {
    std::size_t used = 0;
    for (; used < calls_.size() && calls_[used].after < game; ++used) {
        const auto& call = calls_[used];
        switch (call.kind) {
        case Call::Kind::set: driver_.seq_set(call.seq, call.request, call.time); break;
        case Call::Kind::play: driver_.seq_play(call.seq, call.time); break;
        case Call::Kind::stop: driver_.seq_stop(call.seq, call.time); break;
        case Call::Kind::volume: driver_.seq_volume(call.seq, call.value, call.time); break;
        case Call::Kind::master: driver_.set_master(call.gain); break;
        case Call::Kind::reserved: driver_.ax().set_reserved(call.value); break;
        case Call::Kind::free_loops:
            for (int v = 0; v < Driver::kVoices; ++v) {
                const auto& voice = driver_.voices()[std::size_t(v)];
                if (voice.used && voice.loop &&
                    std::any_of(call.banks.begin(), call.banks.end(), [&](const auto& b) { return b.get() == voice.bank.get(); }))
                    driver_.free_voice(v);
            }
            break;
        }
    }
    calls_.erase(calls_.begin(), calls_.begin() + std::ptrdiff_t(used));
}

} // namespace dw4::sndf
