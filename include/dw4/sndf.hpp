#pragma once

// CRI's GameCube sound driver SNDF (803B8700..803C5D40) as DW4 drives its
// sound-effect banks, and the AX voice pool under it.
//
// The game side (the Cri sound object, 8031CC1C..8031DDB8) talks to the
// driver through 803C2AC4: SEQ_SET (command 1), SEQ_PLAY (2), the stop (3)
// and the SEQ volume (4) put messages into the driver's command ring, and
// GETSTATUS (7) reads its state at once. The driver runs once per AX frame
// in the AX frame callback (802A8F7C): its tick 803C325C (the voice records,
// then the queued messages, then the 16 tracks), the articulation service
// 80245048 (the voices' volume envelopes and SRC ratios) and
// MIXUpdateSettings 8025A420 (a changed level ramps across the next frame).
//
// A sequence number is the index of the Cri sound entry that plays the
// request (8031CCFC); a request starts a track (803C5188) whose notes start
// voice records (the drum note-on 803B9BFC) on AX voices (AXAcquireVoice
// 8023C9D8: priority lists, a voice of a lower priority is taken and its
// owner learns it through the drop callback 803C0B44).
//
// Driver is plain state: the sound manager keeps the one the game sees and
// renders audio from a mirror of it (Mirror: a copy ticked on the audio's own
// frames with the game's calls replayed), so what the game observes never
// depends on whether or when audio is rendered.
#include "dw4/sfp.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace dw4::sndf {

// One AX frame: 160 samples at the DSP's 54 MHz / 1686 = 32028.5 Hz.
inline constexpr int kFrameSamples = 160;
// The milliseconds a tick moves the tracks' waits on: 803C325C measures the
// time since its last tick on the CPU timer (OSGetTick, microseconds) and
// takes floor(0.5 + us / 1000.0) whole milliseconds (800A2650 / 800A3880),
// dropping the rest: 5 for every AX frame of 4995.5 us.
inline constexpr int kTickMs = 5;

// The AX mixer's levels (MIX, 80259334): 0.1 dB as a linear gain (-90.4 dB
// and below silent, +6 dB the ceiling); MIXInitChannel's stereo pan (the
// table 804C79AC at the pan and at 127 - pan); an SFP split's pan as the AX
// pan (8045D130, 803BA3F0).
float mix_level(int db10);
std::pair<float, float> mix_pan(int pan);
int sfp_pan(int pan);

// 8024583C: cents as a pitch ratio, from the table at 804C4A98 (cents
// 0..99, octaves, semitones up, semitones down) in single precision.
float cents_ratio(int cents);
// AXSetVoiceSrcRatio (8023F53C): the ratio as the PB's 16.16 SRC ratio,
// truncated, at most 4.0.
std::uint32_t src_ratio(float ratio);

// A bank as the driver reads it: the sfp data and its samples decoded once.
struct BankData {
    std::unique_ptr<sfp::Bank> bank;
    std::vector<std::vector<std::int16_t>> pcm;
    std::vector<std::uint32_t> loop_start;
};

// AX's voice pool (__AXAllocInit: 64 voices). A voice taken from the free
// list goes to the head of its priority's list; without a free voice
// AXAcquireVoice takes the oldest voice (the tail) of the lowest priority
// below the requested one, calling its owner's drop callback; priority 31
// is never taken. The CRI renderer (RNA) holds one voice per channel of
// every ADXT handle at priority 31 without a callback (802A79A4: the four
// mono SE handles and the stereo BGM and ambience handles from the Cri's
// init, a movie's stereo handle while it plays).
class AxPool {
public:
    static constexpr int kVoices = 64;
    // `reserved` voices held by the renderer (never taken from the SNDF).
    void set_reserved(int reserved) { reserved_ = reserved; }
    int reserved() const { return reserved_; }
    // A voice for `owner` (the SNDF voice's allocation counter), or -1;
    // `dropped` gets the owner of a voice taken from a lower priority (-1:
    // none).
    int acquire(int priority, std::uint32_t owner, std::int64_t& dropped);
    void release(int voice);                    // AXFreeVoice 8023C958
    void set_priority(int voice, int priority); // AXSetVoicePriority 8023CB4C
    int in_use() const;

private:
    struct Voice {
        int priority{};          // 0: free
        std::uint32_t owner{};
        std::uint64_t stamp{};   // list insertion order (larger: nearer the head)
    };
    std::array<Voice, kVoices> voices_{};
    int reserved_{};
    std::uint64_t stamp_{};
};

class Driver {
public:
    static constexpr int kVoices = 64, kTracks = 16, kSeqs = 256;
    // The banks in the driver's slot order (803C5188 searches slots 0..15
    // for the request's group; a slot is also the channel of its notes).
    using Banks = std::vector<std::shared_ptr<const BankData>>;

    // A voice record (284 bytes from SNDF +0x1918).
    struct Voice {
        bool used{};                 // +0x00 != 0xFF
        std::uint8_t key{};          // +0x01: 1 key on, 2 key off
        std::uint8_t channel{};      // +0x02: the bank slot
        std::uint8_t priority{};     // +0x08: the AX priority (split +0x1D + 11)
        std::uint8_t phase{};        // +0x14: volume envelope 0 attack, 1 decay, 2 release
        std::uint8_t seq{};          // +0x2C: the sequence (kept by a drop)
        std::uint32_t id{};          // +0x24 (6460 = 0x1918 + 0x24): the allocation counter, the drop callback's key
        int ax = -1;                 // +0x30: the AX voice
        // AXART volume envelope (+0xB4..: delta, target, attenuation; 0.1 dB in 16.16).
        std::int32_t env_delta{}, env_target{}, env{};
        std::int16_t decay{}, sustain{}, release{}; // split +0x0E / +0x10 / +0x12
        // The sample on the AX voice and its SRC.
        std::shared_ptr<const BankData> bank;
        int sample{};
        std::uint32_t ratio{};       // 16.16
        bool loop{};
        std::int64_t start{};        // AX frame the DSP starts it in
        std::int64_t end = std::numeric_limits<std::int64_t>::max(); // frame a one-shot runs out in
        // The note's gains (master, velocity curve, pan; the AUX A send),
        // its sequence's SEQ volume, and MIXUpdateSettings: the gain at the
        // previous frame's end and the one this frame ramps to.
        float left{}, right{}, aux_left{}, aux_right{};
        int seq_volume{};
        float gain_from{}, gain_to{};
        bool fresh{};                // started in this tick (no ramp in its first frame)
    };
    struct Track {
        std::uint8_t flags{};        // +0x00 (5784): 0x80 running
        std::uint8_t seq_flags{};    // +0x01: the sequence's first byte (0x80 a one-shot sound effect)
        std::uint8_t channel{};      // +0x02: the bank slot
        std::uint32_t id{};          // +0x08: request | sequence
        std::int32_t wait{};         // +0x0C: ms until the next event, 16.16 (tempo 1.0)
        std::shared_ptr<const BankData> bank;
        sfp::Sequence sequence;
    };
    struct Seq {
        std::uint8_t state{};        // +0x00 (24868): 1 set, 2 track running, 3 track ended (voices on)
        std::uint8_t volume{};       // +0x01: the SEQ volume (command 4)
        std::uint8_t voices{};       // +0x03: voice records of the sequence
        std::uint32_t request{};     // +0x0C: request & ~0x7F
        std::uint32_t id{};          // +0x10: request | sequence of a stop
    };

    // Game side (803C2AC4). `time` stamps the queued message: a tick takes
    // only messages of its time or before.
    bool seq_set(int seq, std::uint32_t request, std::int64_t time); // command 1
    bool seq_play(int seq, std::int64_t time);                        // command 2
    bool seq_stop(int seq, std::int64_t time);                        // command 3 (0xB035)
    void seq_volume(int seq, int volume, std::int64_t time);          // command 4 (0xB037)
    // Command 7: bit 1 a one-shot track of the sequence runs, bit 0 a voice
    // record (in use or not) belongs to it.
    int status(int seq) const;

    // The AX frame callback of frame `frame` at `time`: the driver tick and
    // the articulation service. `random` is the C library rand() state
    // (8009C1CC, the random detune), `ms` the whole milliseconds the tick
    // takes off the tracks' waits (kTickMs).
    void tick(std::int64_t frame, std::int64_t time, const Banks& banks, std::uint32_t& random, int ms = kTickMs);

    // A voice the game stops behind the driver's back (stage clear of a
    // bank's loops): freed like the driver frees it.
    void free_voice(int index) { release(index); }
    // Voice gains after the tick: linear, the volume envelope and the SEQ
    // volume included.
    static float gain(const Voice& v);

    const std::array<Voice, kVoices>& voices() const { return voices_; }
    const std::array<Track, kTracks>& tracks() const { return tracks_; }
    const std::array<Seq, kSeqs>& seqs() const { return seqs_; }
    AxPool& ax() { return ax_; }
    const AxPool& ax() const { return ax_; }
    // The voice's master volume for new notes (the SNDF master, 1..127).
    void set_master(float gain) { master_ = gain; }
    bool idle() const;

private:
    struct Message {
        std::int64_t time{};
        std::uint32_t value{};
    };
    void dispatch(std::uint32_t message, std::int64_t frame, const Banks& banks, std::uint32_t& random);
    void start_track(std::uint32_t message, std::int64_t frame, const Banks& banks, std::uint32_t& random);
    void run_tracks(std::int64_t frame, std::uint32_t& random);
    void note_on(int channel, const BankData& bank, const std::shared_ptr<const BankData>& owner, int key,
                 int velocity, int seq, std::int64_t frame, std::uint32_t& random);
    int allocate(int priority);
    void key_off(int index);
    void release(int index);
    void drop(std::uint32_t id);
    void update_volume(int index);

    std::array<Voice, kVoices> voices_{};
    std::array<Track, kTracks> tracks_{};
    std::array<Seq, kSeqs> seqs_{};
    std::deque<Message> messages_;
    AxPool ax_;
    std::uint32_t counter_{};   // +0x84C: voice allocations
    float master_ = 1.0f;
};

// The driver the audio hears (SoundManager::render): a copy of the game's
// driver that the audio ticks once per AX frame of its own timeline and that
// gets the game's calls between the same ticks. A call the game made after
// its tick of frame g (`after`) reaches the copy before the copy's tick of the
// audio frame that plays game frame g + 1, or at its next tick when the audio
// already ran past that frame (a host that filled its queue ahead): such a
// request just sounds later, nothing is played twice. While the audio keeps
// pace with the game the copy goes through exactly the game driver's states.
class Mirror {
public:
    struct Call {
        enum class Kind { set, play, stop, volume, free_loops, master, reserved } kind{};
        std::int64_t after{};   // the game driver's last tick when the call was made
        int seq{}, value{};
        std::uint32_t request{};
        std::int64_t time{};
        float gain{};
        std::vector<std::shared_ptr<const BankData>> banks; // free_loops: the loops of these banks go
    };
    bool live() const { return live_; }
    // The copy of `game` (its rand() state `random`), whose last tick
    // stands for audio frame `ticked`.
    void start(const Driver& game, std::uint32_t random, std::int64_t ticked);
    // Calls are kept only while live; a long run without ticks (a host that
    // stopped rendering) drops the copy, made afresh at the next start.
    void record(Call call);
    // The callbacks up to audio frame `frame`: audio frame f plays game frame
    // f - `delta`, its callback runs at frame_time(f - delta) (CPU ticks)
    // after the game's calls made before the game's tick of that frame.
    template <class FrameTime> void advance(std::int64_t frame, std::int64_t delta, const Driver::Banks& banks, FrameTime&& frame_time) {
        while (ticked_ < frame) {
            ++ticked_;
            const std::int64_t game = ticked_ - delta;
            replay(game);
            driver_.tick(ticked_, frame_time(game), banks, random_);
        }
    }
    const Driver& driver() const { return driver_; }
    std::int64_t ticked() const { return ticked_; }
    std::uint32_t random() const { return random_; }
    std::size_t pending() const { return calls_.size(); }

private:
    void replay(std::int64_t game);
    Driver driver_;
    std::uint32_t random_{};
    std::int64_t ticked_{};
    bool live_{};
    std::vector<Call> calls_;
};

} // namespace dw4::sndf
