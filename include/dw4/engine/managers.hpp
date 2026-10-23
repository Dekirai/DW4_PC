#pragma once

#include "dw4/adx.hpp"
#include "dw4/axfx.hpp"
#include "dw4/card_timing.hpp"
#include "dw4/disc_timing.hpp"
#include "dw4/engine/collision.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/engine/random.hpp"
#include "dw4/motion.hpp"
#include "dw4/sfp.hpp"

#include <array>
#include <random>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::save {
struct Record;
}

namespace dw4::engine {

struct CardIo; // save_menu.hpp

void register_default_managers(Engine& engine);

// Game text: bytes with the high bit set pair into Shift JIS codes that the
// 802FB59C table maps to the fonts' UTF-16 codes; other bytes are ASCII.
std::u16string decode_game_text(std::string_view text);
std::string game_text_utf8(std::string_view text);

// DEVICEFOLDER entry (pack 803B0D4C / unpack 803B0D94): kind (0 weapon,
// 1 armor1, 2 armor2, 3 abChip), record, a flag, rank 0..5 and value 0..2047.
struct ItemDevice {
    int type{}, record{};
    bool flag{};
    int rank{}, value{};
    std::uint32_t pack() const {
        return std::uint32_t(record & 0x3FF) | (flag ? 0x400u : 0u) | (std::uint32_t(type & 0xF) << 12) |
               (std::uint32_t(rank & 0xF) << 16) | (std::uint32_t(value & 0x7FF) << 20);
    }
    static ItemDevice unpack(std::uint32_t word) {
        return {int((word >> 12) & 0xF), int(word & 0x3FF), (word & 0x400u) != 0, int((word >> 16) & 0xF),
                int((word >> 20) & 0x7FF)};
    }
};
// Drop table id (GameSystem::item_tables order) + record -> rolled equipment.
ItemDevice roll_item_device(int table, int record, int level);
// 803B07E0: the name color of a device by its value (grey for chips and the
// placeholder records).
Vec3f device_color(const ItemDevice& device);
// The database table of a device kind: weapon, armor1, armor2, abChip.
const char* device_table(const ItemDevice& device);
// 802EDC54: a floating number (0 damage, 1 EXP, 2 heal, 3 player damage,
// 4 bits) rising from a world position.
void show_number(Engine& engine, int kind, int value, const Vec3f& position);

// Original scaled node/timer clock (8030FA2C / 8031DE44 family).
struct NodeTimer {
    float scale = 1.0f;
    std::int64_t bias{};
    std::int64_t end{};
    std::int64_t saved_world = -1;
    float cached_frames{};
    std::int64_t previous{};
};
std::uint64_t scaled_clock(std::int64_t world, float scale);
void timer_start(NodeTimer& timer, std::int64_t world, std::int64_t end = 0);
std::int64_t timer_time(const NodeTimer& timer, std::int64_t world);
float timer_frames(NodeTimer& timer, std::int64_t world);
bool timer_expired(const NodeTimer& timer, std::int64_t world);
std::int64_t frames_to_time(float frames);

// ---------------------------------------------------------------------------
class SceneManager final : public Manager {
public:
    SceneManager() : Manager("igSceneInfo") {}
    bool accepts(const ObjectView& info) const override { return info.is_a("igSceneInfo"); }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    const std::vector<ObjectView>& scenes() const { return scenes_; }
    // Camera paths (igGraphPath lists) keyed by scene name.
    ObjectView find_scene(std::string_view name) const;

private:
    std::vector<ObjectView> scenes_;
};

class LayerManager final : public Manager {
public:
    LayerManager() : Manager("beLayerCtl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beLayerInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    void order(Engine& engine, Ram& ram);
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<std::string> groups_;
    std::unique_ptr<Node> layer_switch_;
    std::vector<std::unique_ptr<Node>> group_nodes_;
    bool ordered_{};
};

class BkColorManager final : public Manager {
public:
    BkColorManager() : Manager("beBkColor") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beBkColorInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    Vec4f color() const { return color_; }

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    Vec4f color_{0, 0, 0, 1};
};

class CameraManager final : public Manager {
public:
    CameraManager() : Manager("beCameraCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beCameraCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    // The current viewer's own update (beCameraMode vfunc +0x78, 802F4268):
    // the viewer module (vtable 804F1B40, +0x74 = 8040DA1C -> 8040F90C)
    // sits in the same serial scheduler list (+0x0C) as the managers but
    // ahead of them: it adds itself in its +0x5C (8040D488, 8028A398 at
    // 0x8040D7A8) when the application adds its modules early (8040F1C8..
    // 8040F1F8 -> 8028A268), while the demo (802F7C58) and the messenger
    // (80304BB0) join only with the plugins. So it advances its clock and
    // sets the camera matrix the frame is drawn and steered with before the
    // demo or the camera update can change the viewer. Engine::step (and
    // Engine::restore) call it before the messenger, the demo and the
    // managers.
    void begin_frame(Engine& engine);
    // The camera of this frame (begin_frame's), or the live one before it ran.
    void frame_camera(Engine& engine, FrameCamera& camera);

    struct CameraPath {
        std::string name;
        std::vector<Node*> nodes; // root .. igCamera
        Node* camera{};
    };

private:
    // beCameraBoxData ([7] of a box camera info): the pivot that follows the
    // players' centre, the rotation ("target") and distance ("eye")
    // transforms, [0] the live ones and [k] those of camera box k, and the
    // boxes' bounds ([0] unused).
    struct BoxData {
        Node* pivot{};
        std::vector<Node*> eyes, targets;
        std::vector<std::pair<Vec3f, Vec3f>> boxes;
    };
    // The "SHK" data of SHAKE (802F3B2C): a digit pattern of directions,
    // each held for `step` frames, for `total` frames.
    struct Shake {
        std::string pattern;
        float amplitude{}, step{}, total{};
        std::size_t index{};
        bool fresh = true;
        Vec3f eye{}, target{};
        // The camera RAM's named timers: "SHAKEONE" steps the pattern (SHAKE
        // removes it, so the first update steps), "SHAKEALL" ends the shake
        // (802F3474 / 802F34D4 check, 802F34C8 / 802F3DF4 start).
        std::optional<NodeTimer> one;
        NodeTimer all;
    };
    struct CameraRam : Ram {
        std::optional<BoxData> box;
        std::optional<Shake> shake;
        std::vector<CameraPath> paths;
        // beCameraDemoData ([7]): a demo camera info's own path and length.
        std::optional<CameraPath> demo_data;
        std::int64_t demo_data_duration{};
        // RAM +18: 0 until the box or demo update has run once after
        // SETBOXCAMERA / SETDEMOCAMERA (802F37AC clears it).
        int state{};
        // Port fallback for a SETDEMOCAMERA name without a demo camera info
        // (the original's ISDEMOCAMERA wait would never end): the path of
        // this camera holding a node of that name, -1 for none.
        int named_path = -1;
        std::int64_t named_start{}, named_duration{};
        bool named_active{};
        // The RAM's CAMHOKAN timer, the box camera's blend step (802F2BC0).
        std::optional<NodeTimer> hokan;
    };
    // The manager's one viewer (+28, 802E1478) once the box or the demo
    // update made it the current viewer (8040F870): the camera path it
    // evaluates and its time, which 802F4268 advances by the frame step and
    // holds at the duration (+68), so a finished demo keeps its end pose.
    struct Viewer {
        const CameraRam* ram{};
        const CameraPath* path{};
        std::int64_t time{}, duration{}, last{};
        bool finished{};
    };
    std::vector<std::unique_ptr<CameraRam>> rams_;
    std::optional<Viewer> viewer_;
    // Box camera state (802F2BC0, manager +2C..+D4).
    Vec3f centre_{-1.0f, -1.0f, -1.0f};
    bool centre_moved_{};
    float blend_{};
    int box_{}, previous_box_{};
    Mat4 saved_eye_, saved_target_;
    std::optional<FrameCamera> frame_;
    void update_box(Engine& engine, CameraRam& ram);
    void set_viewer(const CameraRam& ram, const CameraPath& path, std::int64_t duration);
    void compute_camera(Engine& engine, FrameCamera& camera);
    bool named_demo_running(const Engine& engine) const;
};

class StageCtlManager final : public Manager {
public:
    StageCtlManager() : Manager("beNDMWStageCtl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWStageCtlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    // 80347BB4: the soft reset (RAM +18 state, _resetTrgTime).
    int reset_state_{};
    std::int64_t reset_time_{};
};

class SwitchCtrlManager final : public Manager {
public:
    SwitchCtrlManager() : Manager("beSwitchCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beSwitchCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
};

class LogoManager final : public Manager {
public:
    LogoManager() : Manager("beNDMWLogo") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWLogoInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    void update(Engine& engine) override;

private:
    // CTRL data of 803B18BC: state / sub / step / substep ([4]..[7]), the
    // card slot being checked (+44), the per-slot error flags (+72 / +76),
    // whether a slot can hold the save (+56), check again from slot A (+48),
    // the answer of the question (+60), the slot and kind of the reported
    // problem (+64 / +68) and the card worker (+40).
    struct LogoRam : Ram {
        int state{}, sub{}, step{}, substep{};
        int slot{};
        std::array<std::uint32_t, 2> flags{};
        bool usable{}, restart{};
        int choice{}, problem_slot = -1, problem = 2;
        std::shared_ptr<CardIo> io;
    };
    std::vector<std::unique_ptr<LogoRam>> rams_;
};

// Sound, sound effects and movies (beAsmSound2 command handler 80314F14).
// The BGM (type 1) and ambience (type 2) streams are the CRI ADX files the
// stage's sound list names as "[archive.entry]"; the host's audio service
// pulls them mixed through render(). Sound effects (types 3/4) are requests
// into the sfp banks (the common allse.sfp and the stage's beStaticInfo
// banks) or, without a request id, ADX files on the four SE stream channels.
// A movie (MOVIE PLAY, Sofdec) plays its ADX sound on the movie channel and
// hands its MPEG-1 video to the host, which shows it over the screen.
class SoundManager final : public Manager {
public:
    SoundManager() : Manager("beSound") {}
    bool accepts(const ObjectView& info) const override;
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    // The CRI driver part (stream ramps and fades, SE sequences) also runs
    // while the stage is not active; the cues and the movie are RAM work.
    void update_inactive(Engine& engine) override { update_driver(engine); }
    // Interleaved stereo 16-bit PCM at kOutputRate of the running streams.
    void render(std::int16_t* out, std::size_t frames);
    static constexpr int kOutputRate = 48000;
    // An ACTION2 action started on a model (ACTION_BROADCAST, 803130D4): the
    // model's SE set (beSeInfo) sends it MODELCTRL SETSOUND for each sound of
    // the action, which the model queues as a sound subcommand.
    void action_started(Engine& engine, const std::string& model, int index, std::string_view action);
    // SOUND PLAY of a model's sound subcommand (803105DC): the play id (0:
    // nothing started) and the sound's type (beSoundData [4]; only a type 4
    // sound keeps its subcommand, for the end call).
    std::uint64_t play_model_sound(Engine& engine, const std::string& name, int& type);
    // SOUND VOLUMECTL UP / DOWN with a play id (80315340 -> 802F64F0): the
    // BGM entry holding that id, else the ambience's, else every sound effect
    // entry holding it fades from `from` to `to` over `frames`.
    void fade_id(std::uint64_t id, float from, float to, float frames);
    // The movie playing: its video stream, picture rate and count; the
    // picture shown is the time since its first picture at that rate.
    struct Movie {
        std::string name, script; // beMovieInfoData [2] file, [5] end script
        std::vector<std::uint8_t> video;
        double frame_rate = 30000.0 / 1001.0;
        std::size_t pictures{};
        std::int64_t start{};       // world time picture 0 is shown (picture_at)
        std::int64_t sound_start{}; // world time the sound's ADXT leaves its pause
        bool sound_on{};            // the sound plays (render mixes it)
        std::uint32_t serial{}; // changes with every PLAY
        // Picture shown at world time `now`. Fields and pictures are both
        // NTSC rates (two fields per picture at 29.97); the small bias keeps
        // the nanosecond rounding of field times from landing a field early.
        std::size_t picture_at(std::int64_t now) const {
            const double seconds = double(now - start) / 1e9;
            return seconds > 0.0 ? static_cast<std::size_t>(seconds * frame_rate + 1e-6) : 0;
        }
    };
    const Movie* movie() const { return movie_ ? &*movie_ : nullptr; }
    // Save states: the streams that play on into the next stage, and back.
    std::shared_ptr<const void> persistent() const;
    void restore(Engine& engine, const std::shared_ptr<const void>& state);
    static void serialize_persistent(class StateArchive& archive, std::shared_ptr<const void>& state);

    // A Cri sound entry's volume fade (disc_timing.hpp).
    using Fade = CriFade;

    // A stage loader read on the simulated drive (the loader's own CRI file
    // stream, CriStreams::Loader, after the stream handles' reads): the whole
    // AFS entry (or `count` sectors of it from sector `first`) in requests of
    // up to 64 sectors; loading() holds until its last chunk is booked.
    // Returns the sectors read (0: nothing to read).
    std::uint32_t read_entry(Engine& engine, unsigned archive, unsigned entry, std::uint32_t first = 0,
                             std::uint32_t count = 0xFFFFFFFFu);
    bool loading() const { return cri_.loader.active; }
    // A loader read of `sectors` at a disc offset (the boot's catalog and
    // AFS directory reads), and where an archive starts on the disc.
    void read_disc(std::uint64_t offset, std::uint32_t sectors) { cri_.load(offset, sectors); }
    std::uint64_t archive_offset(Engine& engine, unsigned archive) const { return disc_extent(engine, archive, ~0u).first; }

private:
    void update_driver(Engine& engine);
    // beSoundData: [4] type (1 BGM, 2 ambience, 3/4 sound effects), [5] file
    // list index, [6] SE request id (-1: an ADX file), [8] volume in 0.1 dB,
    // [9] priority of an ADX sound effect on the SE stream channels.
    struct SoundData {
        int type{}, file{}, volume{};
        std::uint32_t request{0xffffffffu};
        int priority{};
    };
    // One of the Cri object's ADX sound entries (802F66C8): channel 0 BGM
    // (Cri +0x20), 1 ambience (+0x24), 2-5 the four SE stream handles (the
    // first entries of the SE list +0x28), each on the CRI slot of its index.
    struct Stream {
        std::string name;                // entry +8: set when a sound starts, cleared when it stops
        std::string file;                // "[archive.entry]" it was opened from
        std::uint64_t serial{};          // entry +24: nonzero while the entry holds a sound
        int priority{};                  // entry +34: beSoundData [9] of the sound started last
        int volume{};                    // fade object +12: the sound's volume, 0.1 dB (beSoundData [8])
        int out_volume{-999};            // ADXT_SetOutVol (0.1 dB): volume + base, then the fade's steps
        Fade fade;
        // Audio only (never consulted by the game): the decoder the host mixes.
        std::uint64_t frames_read{};     // source frames decoded so far
        std::unique_ptr<adx::Decoder> decoder;
        double phase = 1.0;
        std::array<float, 2> previous{}, next{};
    };
    // A loaded sfp bank with its samples decoded once.
    struct SeBank {
        std::string name; // lower-case file name
        std::unique_ptr<sfp::Bank> bank;
        std::vector<std::vector<std::int16_t>> pcm;
        std::vector<std::uint32_t> loop_start;
        bool common{};
    };
    // A running request sequence (803C49F4, one of 16 tracks). `id` is the
    // play id (the serial) of the Cri sound entry that started it.
    struct SeTrack {
        std::shared_ptr<SeBank> bank;
        sfp::Sequence sequence;
        std::string name;
        std::uint64_t id{};
        float gain = 1.0f;
        double wait_ms{};
    };
    // A playing split sample (of the entry with play id `id`).
    struct SeVoice {
        std::shared_ptr<SeBank> bank;
        int sample{};
        std::string name;
        std::uint64_t id{};
        std::size_t position{};
        double phase{};
        double pitch = 1.0; // random detune
        float left = 1.0f, right = 1.0f;
        float level = 1.0f; // the entry's SEQ volume / 127 (a VOLUMECTL fade)
        // The level the AX mixer ramps across the running AX frame: a change
        // of `level` takes effect at the next frame (begin_ax_frame).
        float level_from = 1.0f, level_to = 1.0f;
        float aux_left{}, aux_right{}; // the AUX A send (803BA5B4)
        bool loop{};
    };
    // A fade of an sfp sound entry (VOLUMECTL with its play id): 802F53B8
    // steps it once per field with 8031DDB8, the level as the SEQ volume
    // 0..127 (SNDF command 4, 8031CF94), and stops the entry once a fade to
    // 0.0 reaches 12 or less.
    struct SeqFade {
        std::uint64_t id{};
        Fade fade;
        int volume = 127;
    };
    // SOUND PLAY <name> (80314FC8): the play id (the Cri serial of the
    // started sound, 0 if nothing started).
    std::uint64_t play(Engine& engine, const std::string& name);
    // 802F5828: a BGM or ambience start on its channel (0 while the channel
    // still runs: it fades out first).
    std::uint64_t start_bgm_env(Engine& engine, const std::string& name, const SoundData& data);
    // 802F5A04: a sound effect, an sfp request or an ADX file on an SE stream channel.
    std::uint64_t start_effect(Engine& engine, const std::string& name, const SoundData& data);
    // The sfp request (8031CCFC SEQ_PLAY); false: nothing started.
    bool play_request(Engine& engine, const std::string& name, const SoundData& data, std::uint64_t id);
    // The sfp entry with that play id stops (its tracks and voices; 0: all).
    void stop_effects(std::uint64_t id);
    // 802F53B8 for the sfp entries: their fades step once per field.
    void step_seq_fades();
    // 802F5828 / 802F5A04 after the entry is chosen: pause, SetOutVol,
    // ADXT_StartAfs, the PLAYatPAUSE flag, the serial (0: not started).
    std::uint64_t start_stream(Engine& engine, int channel, const std::string& name, const SoundData& data);
    bool open_stream(Engine& engine, Stream& stream, const std::string& name, const SoundData& data);
    // 802F56B0 for an ADX sound effect: the first free SE stream entry, else
    // the oldest whose priority does not exceed the sound's (stopped); -1: none.
    int choose_se_channel(int priority);
    // 8031D0F0: 1 while the channel's stream runs (ADXT status 1-4), 0 when
    // it is stopped or has played out.
    int busy(int channel) const;
    // 802F6FB0: ISREADY of a name ("ALL": every running ADX entry).
    bool ready(std::string_view name) const;
    // 802F61F8 / 802F6334: the BGM entry of that name, else the ambience's,
    // else every SE stream entry of that name.
    template <class F> void for_named(std::string_view name, F&& f);
    // 8031D294 (ADXT_Pause) on a channel's handle.
    void pause_channel(Engine& engine, int channel, bool paused);
    // 802F7244: the SNDF master volume, set once (803C2734).
    void set_sndf_volume(int volume);
    // The channel's CRI handle (slot = channel) and the simulated disc reads
    // that decide when a stream is ready (disc_timing.hpp).
    CriStreams::File stream_file(Engine& engine, unsigned archive, unsigned entry, const adx::Header& header) const;
    std::pair<std::uint64_t, std::uint32_t> disc_extent(Engine& engine, unsigned archive, unsigned entry) const;
    void trace_cri(Engine& engine, int inflight_before, std::int64_t done_before, const std::array<bool, CriStreams::kSlots>& playing_before);
    void play_movie(Engine& engine, const Ram& ram, const std::string& name);
    void end_movie(Engine& engine);
    // One stream resampled into the mix at the rate the AX voice plays it
    // (`sofdec`: the movie's handle, RNA mode 1).
    void mix_stream(Stream& stream, float gain, bool sofdec, float* mix, std::size_t frames);
    void add_bank(Engine& engine, const ObjectView& data, bool common);
    void run_track(SeTrack& track);
    // The effect voices into `mix` and, while the AUX A bus is on, their
    // AUX A sends into `aux` (both interleaved stereo); `at` is the first
    // sample's place in the running AX frame (the level ramp).
    void mix_voices(float* mix, std::size_t frames, std::size_t at, float* aux = nullptr);
    // _common/sbiallse.igb's banks (allse.sfp is the SNDF's bank 0), loaded
    // once at boot by the original (802F902C).
    void load_common_banks(Engine& engine);
    // 802F6F10: the effect of the stage's first beCriFxInfo (Cri +0x2C) if
    // it has that name (its _reverb: bank << 8 | preset), else effects off
    // (0x7F), sent as the SNDF commands 0xA470hh00 / 0xA400ll00 (803C1A94).
    void select_effect(Engine& engine, std::string_view name);
    // 803BF698: the AUX effects of a bank's effect preset (0x7F: off).
    void sndf_effect(Engine& engine, int bank, int preset);
    // render() in AX frames: the frame interrupt (the AUX A callback, the
    // return the DSP adds in the frame) and the frame's end (its sends).
    void begin_ax_frame();
    void end_ax_frame();
    // 802F5F98: the entry stops (serial, name and fade cleared, ADXT_Stop).
    void stop(int channel);
    std::vector<std::unique_ptr<Ram>> rams_;
    std::map<std::string, SoundData, std::less<>> sounds_;
    std::vector<std::string> files_;
    static constexpr std::size_t kSeStreams = 4;  // 802F66C8: four mono ADXT handles
    std::array<Stream, 2 + kSeStreams> streams_; // BGM, ambience, SE streams
    CriStreams cri_;
    std::uint64_t serial_{};        // Cri +0x18: serial of the last sound started
    bool pause_next_{};             // Cri +0x3C: PLAYatPAUSE, the next sound starts paused
    // RAM +0x28 (803151B0): a sound effect PLAY of a name started less than
    // 49 999 998 ns (world time) ago is ignored; name -> end of that time.
    std::map<std::string, std::int64_t, std::less<>> se_recent_;
    std::vector<std::shared_ptr<SeBank>> banks_;
    bool common_loaded_{};
    std::vector<SeTrack> tracks_;
    std::vector<SeVoice> voices_;
    std::vector<SeqFade> seq_fades_;
    // beSeInfoData: per model name, action -> sound after [4] frames.
    struct SeCue {
        std::string action, sound;
        int delay{};
    };
    std::map<std::string, std::vector<SeCue>, std::less<>> se_sets_;
    std::uint32_t random_ = 0x12345678u;
    std::string current_bgm_;       // RAM +0x24 (set by a started BGM PLAY; BGMSTOP and a new stage's RAM clear it)
    int se_volume_{};               // Cri +0x38: SNDF master 1..127 (beSoundInfo [10] / SNDFVOLUME), 0 unset
    int base_volume_{};             // Cri +0x34: beSoundInfo [9] _baseVolume, 0.1 dB
    std::optional<Movie> movie_;
    Stream movie_stream_;
    std::uint32_t movie_serial_{};
    // Sofdec's start after MOVIE PLAY (play_movie): the sound's unpause 29
    // and the first picture 37 NTSC fields later.
    static constexpr std::int64_t kMovieSoundNs = 483816667;   // 29 * 1001/60 ms
    static constexpr std::int64_t kMoviePictureNs = 617283333; // 37 * 1001/60 ms
    // The stage's beCriFxInfo in acceptance order (Cri +0x2C, 802F51C4):
    // name and _reverb (slot 6).
    std::vector<std::pair<std::string, int>> fx_infos_;
    bool fx_due_{};                        // the SOUND RAM's first update (80314C00 state 0) selects REVERB
    // The SNDF's effect state: AUX A / B effect types (+0x980 / +0x981,
    // 0xFF none), the bank and preset set up (+0x982 / +0x983), the bank of
    // the next preset command (+0x984).
    std::uint8_t fx_type_a_{0xff}, fx_type_b_{0xff}, fx_bank_{0xff}, fx_preset_{0xff}, fx_bank_select_{};
    axfx::ReverbStd reverb_a_;             // 80537748, AXRegisterAuxACallback 8023D07C
    bool aux_a_callback_{};
    axfx::AuxBus aux_a_;
    // The AX frame render() is in: one frame is 240 output samples (160 at
    // the DSP's 32 kHz); its AUX A sends and the return the DSP adds in it
    // (48 kHz, interleaved), the last returned sample.
    static constexpr std::size_t kAxFrameOutput = 240;
    std::size_t ax_fill_{};
    bool aux_emit_{};
    std::array<float, 2 * kAxFrameOutput> aux_sends_{}, aux_return_{};
    std::array<float, 2> aux_last_{};
};

// beTextureCtrl (80324B30 / 80324C54): binds named pack textures to the
// placeholder igTextureBindAttr of listed attribute sets.
class TextureCtrlManager final : public Manager {
public:
    TextureCtrlManager() : Manager("beTextureCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beTextureCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    void update(Engine& engine) override;
    void bind_all(Engine& engine);

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<ObjectView> pack_data_;   // beTextureCtrlData
    std::vector<ObjectView> ctrl_attrs_;  // beTextureCtrlAttr
    std::vector<std::unique_ptr<Attribute>> owned_;
    bool pending_{};
};

// beParticleCtrl2 (manager vtable 804DF2B0): particle effects answering as
// MODELCTRL rows. GENERATE (80312554) places an instance on a model with an
// offset, an X/Y/Z rotation in radians (Y above 2 pi: random) and optionally
// follows it; the update (80312268) starts the instance's ParticleTimer and
// switches it off once the world time minus the timer's _timeBias reaches the
// info's duration ([10]); the bias only moves when the traversal visits the
// timer.
class ParticleCtrlManager final : public Manager {
public:
    ParticleCtrlManager() : Manager("beParticleCtrl2") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beParticleCtrl2Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct ParticleRam : Ram {
        bool running{};
        int state{};
        std::int64_t start{}; // the start's world time (only for an instance without a timer)
        Node* transform{};  // ram [11]
        Node* timer{};      // ram [12] ParticleTimer
        std::string owner;  // followed model (lock)
        int owner_index = -1;
        Vec3f offset{};
    };
    void stop(ParticleRam& ram);
    std::vector<std::unique_ptr<ParticleRam>> rams_;
};

// beWaterPlain (update 803178CC): rippling water. Players and enemies in the
// surface's height bands press the grid point under them down (803176F0),
// beWaterMove drops do so at their times, and a wave-equation step
// (8031716C) turns the heights into the surface mesh's vertex heights and
// normals.
class WaterPlainManager final : public Manager {
public:
    WaterPlainManager() : Manager("beWaterPlain") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beWaterPlainInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    void update(Engine& engine) override;

private:
    struct Drop {
        std::int64_t time{};
        float strength{};
        std::vector<Vec3f> points; // relative to the origin
    };
    struct Water {
        ObjectView vertices;             // [21] the surface's vertex array
        std::vector<float> weight;       // [22] per grid point (0: no water)
        std::vector<int> map;            // [23] grid point -> vertex
        int width{}, height{};           // [24]/[25]
        Vec3f origin{}, max{}, min{};    // [26]..[28]
        float cell = 4.0f;               // [31]
        float c_sum{}, c_cur{};          // [12]/[13]
        float range1_top{}, range1_bottom{}, range1_amount{}; // [15]..[17]
        float range2_top{}, range2_bottom{}, range2_amount{}; // [18]..[20]
        float limit_max{}, limit_min{}, boundary{};           // [33]..[35]
        std::array<std::vector<float>, 2> buffers;           // [32]
        int current{};
        std::int64_t period{}, move_start{};                  // [37] beWaterMove
        std::size_t move_index{};
        std::vector<Drop> drops;
        int state{};                     // [6] 0 first update, 1 active, 2 out of view
        std::int64_t last_update{};      // [36] world time of the last update
        GeometryData* geometry{};
        bool searched{};
    };
    bool inside(const Water& w, const Vec3f& p) const;
    void push(Water& w, const Vec3f& p, float strength);
    void step(Water& w);
    bool visible(Engine& engine, const Water& w) const;
    void flatten(Water& w);
    void restart_move(Engine& engine, Water& w);
    void surface_update(Engine& engine, Water& w);
    std::vector<Water> waters_;
};

// LIGHTCTRL (beLightCtrl, command handler 803248AC): SETCOLOR name/V<ambient>/
// V<diffuse>/V<specular> recolours the lights of one of the info's light sets
// ([7]); the stage's ROOTATTR light states light the scene with them.
class LightCtrlManager final : public Manager {
public:
    LightCtrlManager() : Manager("beLightCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beLightCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::map<std::string, std::vector<ObjectView>, std::less<>> sets_; // light set name -> igLightAttr
};

// beShadow01 (manager vtable 804DF760): projected shadows. The stage's
// beShadow01Info with an igProjectiveShadowShader ([9], 80314690) sets the
// light; infos with [8] add casters (model switches or beCopyModelCtrl2
// shadow proxies) to the shader's caster list and infos with [7] add
// receivers as the shader's children (803144D0). The light stays 200 units
// above the players' centre (8031476C, 8031C1FC) and looks along its
// igLightAttr's direction with the shader's _upVector (801FE5E4).
class ShadowManager final : public Manager {
public:
    ShadowManager() : Manager("beShadow01") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beShadow01Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    void update(Engine& engine) override;
    // Fills the frame's light and caster draw items (casters traverse with
    // the given context's time and actor poses).
    void render(Engine& engine, const TraversalContext& context, ShadowFrame& frame);
    // The same caster and receiver traversals without draw items
    // (Engine::traverse_scene).
    void traverse(const TraversalContext& context);

private:
    std::vector<Node*> casters_, receivers_;
    bool shader_{};
    float factor_ = 0.3f;               // [13] _shadowFactor
    int width_ = 128, height_ = 128;    // [14]/[15] _shadowTexW/_shadowTexH
    // [48]..[51] _top/_bottom/_right/_left: the light box
    float top_ = 100.0f, bottom_ = -100.0f, right_ = 100.0f, left_ = -100.0f;
    float near_ = 0.1f, far_ = 1000.0f; // [27]/[28]
    Vec3f up_{1, 0, 0};                 // [29] _upVector
    Vec3f direction_{0, -1, 0};         // the light's direction
    std::optional<Vec3f> light_;
    bool active() const { return shader_ && light_.has_value() && !casters_.empty() && !receivers_.empty(); }
    Mat4 light_view() const; // only while active()
    Mat4 light_projection() const;
    void traverse(const TraversalContext& context, std::vector<DrawItem>* casters, std::vector<DrawItem>* receivers);
};

// bePoint01 (80312A70): PLAYERPOINT generation groups and point matrices.
class PlayerPointManager final : public Manager {
public:
    PlayerPointManager() : Manager("bePoint01") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "bePoint01Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    std::optional<Mat4> point_matrix(int group, int player) const;
    std::int32_t point_no() const { return point_no_; }
    void set_point_no(std::int32_t value) { point_no_ = value; }

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<ObjectView> infos_;
    std::int32_t point_no_{}; // manager-wide _pointNo, survives stage loads
};

// beHitLandModel / beHitLandDeliv: the collision geometry with its original
// HITLANDMODEL queries and the models' HITLANDDELIVER responses.
class HitLandManager final : public Manager {
public:
    HitLandManager() : Manager("beHitLandModel") {}
    bool accepts(const ObjectView& info) const override {
        return info.type() == "beHitLandModelInfo" || info.type() == "beHitLandDelivInfo";
    }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    CollisionWorld& world() { return world_; }
    // The wall kinds a model's HITLANDDELIVER cylinder collides with (test bot).
    std::uint32_t delivery_flags(std::string_view name, int index) const {
        for (const auto& ram : deliveries_)
            if (ram->info_name == name && ram->index == index) return ram->flags;
        return 7;
    }
    // The collision group of a named HITLANDMODEL child, switched without a
    // script command (test bot: routes leave out the players' limit circle).
    std::optional<bool> child_hit(std::string_view name) const {
        for (const auto& child : children_)
            if (child->info_name == name) return world_.group_enabled(child->group);
        return std::nullopt;
    }
    void set_child_hit(std::string_view name, bool on) {
        for (const auto& child : children_)
            if (child->info_name == name) world_.set_group_enabled(child->group, on);
    }
    // The half-width of that cylinder (test bot).
    float delivery_width(std::string_view name, int index) const {
        for (const auto& ram : deliveries_)
            if (ram->info_name == name && ram->index == index) return ram->width;
        return 2.5f;
    }

private:
    struct HitRam : Ram {
        std::uint32_t group{};
    };
    // HITLANDDELIVER (beHitLandDelivInfoRam, callback 802FDA0C): one model
    // instance's collision cylinder on its ONE node.
    struct DelivRam : Ram {
        Node* node{};                    // [11] (+2C)
        std::uint32_t flags{};           // [12] query flags (+30)
        float width{}, up{}, down{};     // [13] half-width, [14]/[15] ranges (+34..+3C)
        Vec3f position, previous, land;  // [16] (1,0,0) after a wall hit, [17], [18] (+40/+4C/+58)
        bool land_valid{};               // +64
    };
    bool deliver(Engine& engine, DelivRam& ram, std::string_view command, Packet& packet);
    bool query(Engine& engine, std::string_view command, Packet& packet);
    std::vector<std::unique_ptr<DelivRam>> deliveries_;
    // A kind 0x400 child: its dynamic collision, the model node it follows and
    // its bit in the check switch's mask.
    struct Follower {
        std::uint32_t id{};
        ObjectView source;   // beCopyTransform [12], a model instance's node
        ObjectView check;    // the info's _checkSwitch [8]
        std::uint32_t bit{}; // the child's index in it
        std::string info;    // owning HITLANDMODEL info
        bool resolved{};
    };
    std::vector<Follower> followers_;
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<std::unique_ptr<HitRam>> children_;
    CollisionWorld world_;
};

// beChangePosTransform (802F4494, change_pos.cpp): a transform at the
// players' centre that holds while a player is 40 or more away; the HitLand
// limit circle under it keeps the players together.
class ChangePosManager final : public Manager {
public:
    ChangePosManager() : Manager("beChangePosTransform") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beChangePosTransformInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    void update(Engine& engine) override;

private:
    struct ChangeRam : Ram {
        Node* transform{};
        int state{};
        Vec3f last;
    };
    std::vector<std::unique_ptr<ChangeRam>> rams_;
};

// Global font images from bectext.igb (beFontImage + igCharMetricsList).
struct Glyph {
    float advance{}, x_offset{}, y_offset{}, width{}, height{};
    float u0{}, v0{}, u1{}, v1{};
};
struct FontImage {
    std::string name;
    float line_height{};
    std::unique_ptr<Attribute> binding;
    std::map<std::uint32_t, Glyph> glyphs;
};

// The glyph quads of one text line node: 80116960 fills them on one pen,
// 802FBF98 breaks the lines and measures them, 802FC2D0 aligns them
// (align 1 RIGHT, 2 CENTER) inside max_width.
GeometryData layout_text(const FontImage& font, std::u16string_view text, int align, float max_width);

// TEXTCTL (beFontInfo, commands 802FA900): text lines built from pack unicode
// strings. Every beFontInfoData entry is one line: layer name [2], line node
// [4] (named fontNNN), category [5] in the FONT layer switch, line index [6]
// in that category's first child switch, max width [7], line height [8],
// alignment [9] and style [10]. Commands address lines by layer and name.
class TextCtlManager final : public Manager {
public:
    TextCtlManager() : Manager("beFont") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beFontInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    struct Line {
        const IgbDocument* document{};
        std::uint32_t info{};
        std::string layer, name;
        int category{}, index{};
        Node* node{};
        float max_width{};
        float spacing{};
        int align{};              // beFontInfoData [9], enum 804D2048: 0 left, 1 right, 2 center
        std::string style;
        std::vector<ObjectView>* strings{};
    };
    void load_fonts(Engine& engine);
    void set_text(Engine& engine, Line& line, const std::u16string& value);
    void switch_line(Engine& engine, const Line& line, bool on);
    void refresh_categories(Engine& engine);
    Node* root_switch(Engine& engine);
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<Line> lines_;
    std::map<std::pair<const IgbDocument*, std::uint32_t>, std::vector<ObjectView>> strings_;
    std::map<std::string, std::unique_ptr<FontImage>> fonts_;
    std::shared_ptr<const IgbDocument> font_document_;
    std::vector<std::shared_ptr<GeometryData>> geometry_;
};

// SELECTCTRL (beSelectCtrl, 80313CD4 / 803132C0): cursor menus and prompts.
class SelectCtrlManager final : public Manager {
public:
    SelectCtrlManager() : Manager("beSelectCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beSelectCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    // The running cursor menus (read only, for test drivers): the cursor
    // moves and A/B are read only while `taking_input` (state 1, sub 0).
    struct OpenSelect {
        std::string name;
        int index{}, cursor{}, items{};
        bool vertical{}, cancel{}, taking_input{};
    };
    std::vector<OpenSelect> open_selects() const;

private:
    struct SelectRam : Ram {
        bool running{};
        int state{}, sub{}, counter{}, busy{};
        int traced_state = -1, traced_sub = -1; // DW4_TRACE_SELECT
        int cursor{}, items = 1;
        bool vertical = true, cancel = true;
        Vec3f position{};
        std::vector<std::uint8_t> enabled;
        std::string sequence;
    };
    std::vector<std::unique_ptr<SelectRam>> rams_;
};

// COMMDLCTRL helper (8035BE8C..8035C73C): opens, re-sequences and closes menu
// models and tracks two wait lists. A sequence wait ends once the model left
// the requested sequence; a close wait ends once the model is removed.
class CommonModelControl {
public:
    void open(Engine& engine, int index, const std::string& name, const std::string& seq, bool track);  // 8035BE8C
    void open_at(Engine& engine, int index, const std::string& name, const std::string& seq, const Vec3f& position,
                 bool track);                                                                            // 8035C0D8
    bool in_sequence(Engine& engine, int index, const std::string& name, const std::string& seq);        // 8035C490
    void set(Engine& engine, int index, const std::string& name, const std::string& seq, bool track);   // 8035C2DC
    void close(Engine& engine, int index, const std::string& name, const std::string& seq, bool track); // 8035C51C
    bool is_live(Engine& engine, int index, const std::string& name);                                   // 8035C6D0
    bool sequences_done() const { return sequence_waits_.empty(); }                                     // 8035C504
    bool closes_done() const { return close_waits_.empty(); }                                           // 8035C724
    void update(Engine& engine);                                                                         // 8035C73C
    void reset() { sequence_waits_.clear(); close_waits_.clear(); }

private:
    struct Wait {
        int index{};
        std::string name, seq;
    };
    std::vector<Wait> sequence_waits_, close_waits_;
};

// TITLE2 (beNDMWTitle2, update 80369284, commands 8036B1C0): press start,
// Game Start / Option menu, attract timeout and hand-off to LOADCTRL2.
class Title2Manager final : public Manager {
public:
    Title2Manager() : Manager("beNDMWTitle2") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWTitle2Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    int state() const { return rams_.empty() ? -1 : rams_.front()->state; }

private:
    struct Option {
        int kind{};                  // +20: 0 = window B (camera/light only)
        int row{};                   // +24
        bool confirmed{};            // +28
        int camera_mode{}, light_mode = 1; // +32 / +36 committed values
        bool keep{};                 // +40
        int edit_camera{}, edit_light{}, edit_language{}; // +44 / +48 / +52
    };
    struct TitleRam : Ram {
        bool created{};              // data list at RAM +28
        bool run{};                  // RAM +18, set by RUN
        int cursor{}, choice{};      // CTRL +18 / +1C
        int state{}, sub{}, step{}, detail{}; // CTRL +20..+2C
        CommonModelControl models;
        Option option;
    };
    void update_ram(Engine& engine, TitleRam& ram);
    void update_option(Engine& engine, TitleRam& ram, bool up, bool down, bool left, bool right, std::uint32_t trigger);
    std::vector<std::unique_ptr<TitleRam>> rams_;
};

// MESWIN helper (80362B44 / 80363064..803631A8): the panel message window.
// Messages are TEXTORDER ids; prompts drive j_chrselmesswin's sequences.
class MessageWindow {
public:
    void open();                          // 80363064
    void close();                         // 8036308C
    void show(int id);                    // 803630C0
    void question(int id, int cursor);    // 803630D0: two-choice prompt
    void next(int id);                    // 803630F4: wait for A
    void clear();                         // 8036317C
    void cancel(Engine& engine, int player); // 80363114: drop the prompt
    void interrupt(int id);               // 8036318C: show id over the prompt
    void resume();                        // 803631A8: back to the parked message
    bool busy() const { return prompt_; } // 8036316C
    int answer() const { return cursor_; }
    void update(Engine& engine, int player);

private:
    bool open_{}, closing_{}, answered_{}, prompt_{}, pending_{}, clear_request_{};
    bool interrupted_{}, shown_{};
    int mode_{}, step_{}, cursor_{}, id_{}, interrupt_id_{}, interrupt_count_{};
    std::string sequence_;
};

// DATABASE (beDBManagerInfo): named beDataBase tables with typed columns;
// string columns are names [12] with row-major values [13].
class DatabaseManager final : public Manager {
public:
    DatabaseManager() : Manager("beDBManager") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beDBManagerInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); tables_.clear(); }
    std::string text(std::string_view table, int row, std::string_view column) const;
    // 8031F3CC / 8031F608: integer cell of a char, short, int or bool column.
    std::optional<std::int32_t> number(std::string_view table, int row, std::string_view column) const;
    // The table exists and has that char, short, int or bool column (8031F54C finds it).
    bool has_column(std::string_view table, std::string_view column) const;
    // First row whose string column equals value, or -1.
    int find_row(std::string_view table, std::string_view column, std::string_view value) const;
    // 8031F760: the row count, from the string cells per string column.
    int rows(std::string_view table) const;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::map<std::string, ObjectView, std::less<>> tables_;
};

// NUMBERCTRL (beNumberCtrl): HUD digits drawn by texture columns.
class NumberCtrlManager final : public Manager {
public:
    NumberCtrlManager() : Manager("beNumberCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNumberCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct NumberRam : Ram {
        Node* geometry{};
        float step{};
        int maximum = 9999;
        bool blank_leading{};
        std::vector<std::vector<std::pair<int, int>>> places;
        int shown{}, target{}, rate{};
        bool drawn{};
    };
    void draw(Engine& engine, NumberRam& ram, int value);
    std::vector<std::unique_ptr<NumberRam>> rams_;
};

// METERCTRL (beMeterCtrl): HUD meters whose clips play at the meter's time.
class MeterCtrlManager final : public Manager {
public:
    MeterCtrlManager() : Manager("beMeterCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beMeterCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct MeterRam : Ram {
        Node* node{};
        float maximum = 100, target{}, shown{};
        std::int64_t low_time{}, high_time{};
        bool continuous = true, dirty = true;
        bool integer{}; // OneData [14] (+0x3E): the shown value is truncated every step
        std::string model_name;
        int model_index{};
    };
    std::vector<std::unique_ptr<MeterRam>> rams_;
};

// GENERATER (beGenerater, update 802FCF68, commands 802FD5C4, spawn 802FC9FC):
// one RAM per beGeneraterData. START creates the work (enemy name and max HP
// from DATABASE enemy[_geneEnemyId]); the start mode (direct / timer / range)
// releases the first wave, the fill mode (wipeout / always / timer) spawns the
// rest; when every enemy of the last wave is gone the generator runs _callLua.
class GeneraterManager final : public Manager {
public:
    GeneraterManager() : Manager("beGenerater") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beGeneraterInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

    // Randomizer: a drop's record after the seeded permutation of its table.
    static int randomized_record(Engine& engine, const std::string& table, int record);
    // Each generator's data name and whether its waves were ever cleared (test bot).
    std::vector<std::pair<std::string, bool>> cleared() const {
        std::vector<std::pair<std::string, bool>> out;
        for (const auto& ram : rams_) out.emplace_back(ram->data ? std::string(ram->data.str(2)) : std::string(), ram->done);
        return out;
    }
    // Each generator's state and how often its waves were cleared (a
    // restarted generator clears again; test bot). `stopped`: a running wave
    // held by STOP/STOPREVERS (work +9), which update (802FD174..802FD194) neither
    // fills nor finishes; `filled`: all of it spawned (state 3); `alive`:
    // its enemies still generated.
    struct Status {
        std::string name;
        bool started{}, done{};
        int clears{};
        bool stopped{}, filled{};
        int alive{};
    };
    std::vector<Status> status() const {
        std::vector<Status> out;
        for (const auto& ram : rams_)
            if (ram->data)
                out.push_back({std::string(ram->data.str(2)), ram->started, ram->done, ram->clears, ram->work && ram->system_freeze,
                               ram->work && ram->state == 3, int(ram->enemies.size())});
        return out;
    }
    // The enemies each started, unfinished generator has out (model name,
    // RAM numbers, work +14), by generator name (test bot).
    struct Wave {
        std::string name, enemy;
        std::vector<int> members;
    };
    std::vector<Wave> waves() const {
        std::vector<Wave> out;
        for (const auto& ram : rams_)
            if (ram->data && ram->started && !ram->done && ram->work) out.push_back({std::string(ram->data.str(2)), ram->enemy_name, ram->enemies});
        return out;
    }
    // Started "radar" generators still waiting for a player within reach of
    // their base (test bot).
    struct Radar {
        std::string name;
        Vec3f base;
        float range_sq{};
    };
    std::vector<Radar> radar_waiting() const {
        std::vector<Radar> out;
        for (const auto& ram : rams_)
            if (ram->data && ram->started && !ram->done && ram->state == 0 && ram->data.str(9).starts_with('r'))
                out.push_back({std::string(ram->data.str(2)), ram->data.vec3(3).value_or(Vec3f{}), ram->data.f32(10)});
        return out;
    }

private:
    struct GeneRam : Ram {
        ObjectView data;           // beGeneraterData
        bool started{};            // RAM +14
        bool done{};               // RAM +18: finished, START is ignored
        int state{};               // RAM +1C
        bool work{};               // RAM +2C allocated
        bool freeze{}, system_freeze{}; // work +8 / +9
        int player_count = 1;      // work +C
        int remaining{};           // work +10 (_nokoriKazu)
        std::vector<int> enemies;  // work +14 (_enemyRamList): generated RAM numbers
        int point{};               // work +18 (_genePoint)
        std::string enemy_name;    // work +1C
        int enemy_record{};        // work +20
        int enemy_max_hp{};        // work +24
        // The RAM's named timers (data list +0x28): "BOOT_TIMER" of a timed
        // start, "GENE_TIMER" between timed fills (8031DE44 start, 8031E238
        // check, 8031E8BC remove). No command drops them.
        std::optional<NodeTimer> boot_timer, gene_timer;
        int clears{};              // waves cleared so far (test bot)
    };
    int spawn(Engine& engine, GeneRam& ram, int count);
    void refresh(Engine& engine, GeneRam& ram, bool last_pass = false);
    void drop(Engine& engine, GeneRam& ram, const Vec3f& position, bool last);
    std::vector<std::unique_ptr<GeneRam>> rams_;
};

// MATCTRL (beMatCtrl, update 803030A8, commands 80303B14): floor mats. Each
// beMatCtrlData [8] is a named igAABox; whenever the set of players standing on
// a mat changes, "<name>.lua" runs with that mat as the script context.
class MatCtrlManager final : public Manager {
public:
    MatCtrlManager() : Manager("beMatCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beMatCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    // MATisAllPlayer / MATgetPlayerNo / MATisRideOffPlayer for the running mat script.
    bool script_call(Engine& engine, const std::string& method, std::span<const Lua4Value> args, std::vector<Lua4Value>& out);
    // The centre of a mat by name ("z02" or its script name "matz02"; test bot).
    std::optional<Vec3f> mat_centre(std::string_view name) const;
    // Every mat's name and bounds (test bot).
    struct MatBox {
        std::string name;
        Vec3f min, max;
    };
    std::vector<MatBox> mat_boxes() const;

private:
    struct Mat {
        std::string name;
        Vec3f min{}, max{};
        bool enabled = true;
        std::uint32_t current{}, previous{};
    };
    struct MatRam : Ram {
        bool running = true;  // RAM +20: PAUSE / RESTART
        bool initialized{};   // RAM +0x18 == 1 after the first running update (803030A8)
        Node* visuals{};      // info [7] switch
        std::vector<Mat> mats;
    };
    std::vector<std::unique_ptr<MatRam>> rams_;
    Mat* context_{};
    std::optional<bool> pending_run_; // MATsetRun for the running mat (80302DA8)
    // Manager +0x20..+0x23: the players that answered this update's PLAYER
    // GETMATRIX (generated, i.e. alive; 0x80303668..0x803036C4), cleared by
    // a RAM's first update (0x80303530). MATisAllPlayer and MATgetPlayerNo
    // only count these.
    std::uint32_t live_{};
};

// SAVEUTILITY (beSaveUtil, commands 80323D20, update 80323538): the memory
// card front end. GENERATE starts it (beSvStartApi: ready in the second
// update), REMOVE ends it (beSvEndApi: not ready from the third update).
// One request runs at a time (manager +32) as a beSv*Api object stepped
// once per update (CardRequest, card_timing.hpp: mount, file work and the
// card's access times); its finish fills the deliver record (beSvDeliver)
// with the beSv*Api codes: 2 done, 3 error, 4 no file, 5 I/O error, 7
// broken or unformatted card, 8 both data copies broken, 9 not enough
// space, 10 no free file entry, 11 wrong device, 12 no card. While ready,
// beSvConnectCheck probes both slots every update (deliver slot states).
// The cards are GCI folders (Engine::card_root()/Card A|B) with what
// Engine::card_setting puts in the slots.
class SaveUtilManager final : public Manager {
public:
    SaveUtilManager() : Manager("beSaveUtil") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beSaveUtilInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

    // beSvDeliver: request, result and the free space check.
    struct Deliver {
        int slot{};                        // +16
        std::vector<std::uint8_t>* data{}; // +8: game data after the copy header (40952 bytes)
        int result = 1;                    // +24
        int operation{};                   // +28
        std::uint32_t free_bytes{};        // +32: GETFREESIZE bytes free
        std::uint32_t free_files{};        // +36: ... directory entries free
        bool enough_space{}, free_entry{}; // +42 / +43
        bool done = true;                  // +44
        // Per card slot (lists [3], [15], [12], [14]): card state (1 usable,
        // -1 wrong device, 0 none); beSvConnectCheck never fills the free and
        // needed bytes or the room flag (0).
        struct Slot {
            int state{};
            std::uint32_t free_bytes{}, need_bytes{};
            bool enough{};
        };
        std::array<Slot, 2> slots{};
        bool ready = true; // +40
    };

private:
    struct UtilRam : Ram {
        int state{}, sub{}, step{}; // +24 / +28 / +32 (80323538)
        bool ready{};               // +44: ISRDY
        int version = 4;
        Deliver deliver;
    };
    // The slot as a request starting now sees it.
    CardSlot card_slot(Engine& engine, int slot, int version) const;
    void finish(Engine& engine, UtilRam& ram);
    std::vector<std::uint8_t> header_; // 8 KB file header from beSvPlatDataGC
    std::vector<std::unique_ptr<UtilRam>> rams_;
    std::unique_ptr<CardRequest> request_;  // +32: the request in flight
    std::array<CardSlot, 2> cards_{};       // the slots it works on
    std::array<CardChannel, 2> channels_{}; // the CARD library's control blocks
    std::array<bool, 2> formatted_{};       // an unformatted card a FORMAT fixed
    std::uint64_t request_frame_{};         // DW4_TRACE_SAVE
};

// Accepts info types whose runtime is not reconstructed yet, reporting them once.
class PassiveManager final : public Manager {
public:
    PassiveManager() : Manager("passive") {}
    bool accepts(const ObjectView& info) const override;
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
};

// ACTION2 (beAction2) and MODELCTRL (beModelCtrl).
class ActionManager;
class ModelCtrlManager final : public Manager {
public:
    ModelCtrlManager() : Manager("beModelCtrl") {}
    bool accepts(const ObjectView& info) const override;
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    // Each model class (beNDMWMdlPlayer, ...Enemy, ...Object, 80346F74) is a
    // manager of its own that accepts the infos of its KIND, so each is
    // scheduled by its first info.
    int schedule_slot(const ObjectView& info) const override;
    void update_slot(Engine& engine, int slot) override;
    std::string slot_name(int slot) const override;
    void install_actor_hook(Engine& engine, TraversalContext& context);

    struct Sequence {
        std::string name;
        std::vector<ObjectView> commands;
    };
    // Authored HIT record (first "HIT" data): target categories and local boxes.
    struct HitBody {
        bool player_bullet{}, enemy_bullet{}, player_talk{};
        std::vector<std::pair<Vec3f, Vec3f>> boxes;
    };
    // "AIH" data (beModelCtrlInfoDataAIH): the homing of bullets (80308A2C).
    struct Homing {
        bool pitch{}, yaw{};         // [4] follow the height, [5] steer
        float delay{}, interval{};   // [6]/[7] frames before the first and between steering steps
        float range2{};              // [8] squared reach
        float pitch_rate{}, yaw_rate{}; // [9]/[10] radians per step
        int steps{};                 // [11] steering steps
    };
    struct ModelRam : Ram {
        std::string category;
        Node* node{};
        std::vector<Sequence>* sequences{};
        bool generated{};
        std::string seq_name;
        const Sequence* seq{};
        std::size_t cursor{};
        bool label_passed{};
        std::uint32_t sequence_serial{}; // counts sequence starts (port: run_commands trace)
        std::optional<std::string> wait_name;
        std::int32_t hp = 100, max_hp = 100;
        Mat4 base_matrix{};
        // Motion / control work (beModelCtrlInfoWork subset).
        dw4::MotionChannels channels;
        int ctrl_mode{}, ctrl_phase{};
        std::array<float, 4> pdmove{};
        bool pdmove_set{};
        bool fall_check = true;      // ctrl work +DA (onFall / noFall)
        bool wall_check = true;      // ctrl work +D8 (MUTEKI ONWALL / OFFWALL)
        bool floor_check = true;     // ctrl work +D9 (MUTEKI ONFLOOR / OFFFLOOR)
        // ctrl work +CC: what the model rides (beModelCtrlInfoRideOn, set on
        // landing on a named HitLand info) or a floor effect of the stage
        // scripts (RideOn2-4: SETCONVEYOR / SETQUICKSAND / SETSLIPPEDFLOOR).
        struct Ride {
            enum class Kind { platform, conveyor, quicksand, slipped } kind{};
            std::int32_t type{};       // +C: the HitLand child; 0x66 / 0x67 / 0x68 for the floor effects
            std::string name;          // +8: the HitLand info, a MODELCTRL info name
            const Node* node{};        // +10: the ridden model's node (platform)
            Mat4 own;                  // +10 of the floor effects: their own transform
            Mat4 last;                 // +14: the ridden matrix of the last frame
            Vec3f point{};             // +54: conveyor step, quicksand centre, slipped floor last position
            Vec3f velocity{};          // +60: slipped floor velocity
            float speed_far{}, speed_near{}, radius_near{}, radius_far{}; // quicksand +60 / +64 / +68 / +6C
            float rate{}, rest{};      // slipped floor +6C / +70
            int state{};               // slipped floor +74
        };
        std::optional<Ride> ride;
        std::array<float, 3> applied{}, desired{};
        // Info work +0x90: the Y turn rate (radians/frame). GENERATE sets it
        // to 7 for players (8034D78C) and 0 for every other kind (fresh work).
        float turn_rate_y = 7.0f;
        // Info work +8C: the X (pitch) turn rate; only the AIH homing sets it
        // (0x80308D30). Info work +0x54: the Y of the placement offset an
        // effect-form GENERATE stored (0x8030D5E0); the homing aims that high
        // above the target. Both start at 0 with the fresh info work of every
        // GENERATE (the KIND +0x98 hooks).
        float turn_rate_x{};
        float aim_height{};
        bool is_player{};
        bool control_enabled = true; // ONCTRL / OFCTRL
        bool key_input{};
        bool on_ground = true;
        // KIND_ITEM work (beNDMWMdlItem): drop table id and record (+EC/+10),
        // the rolled equipment (+F0); the name label: its owner player, which
        // is also its ITNAMEFONT line (+FC, -1 = no label: +100 clear), the
        // blink phase (+108, set while the full colour shows) and the named
        // RAM timer "COLOR".
        int item_table = -1, item_record{};
        ItemDevice item_device;
        int item_label = -1;
        bool item_lit{};
        std::optional<NodeTimer> item_color_timer;
        std::string label;
        std::map<std::string, std::string, std::less<>> labels; // SETLABEL per sequence
        const HitBody* hit{};
        // beModelCtrlSCHitBox queue (MODEL_HITBOX): kind, delay, absolute end.
        struct Hitbox {
            std::int32_t kind{};
            float delay{}, end{}, elapsed{};
            Vec3f offset{}, size{};
            bool started{};
        };
        std::vector<Hitbox> hitboxes;
        // Effect subcommands (SETEFFECT, 80310B50 / start 80310340).
        struct Effect {
            std::string model, sequence;
            Vec3f offset{}, rotation{};
            float delay{}, elapsed{};
            bool started{}, sequence_dead{}, lock{};
            int ram = -1;
        };
        std::vector<Effect> effects;
        // Sound subcommands (SETSOUND 8030DE1C -> 80310FAC: the sound name,
        // the delay as a float, elapsed 0). A started type 4 sound keeps its
        // entry with the play id until the end call (803106C0).
        struct Sound {
            std::string name;
            float delay{}, elapsed{};
            bool started{};
            std::uint64_t id{};
        };
        std::vector<Sound> sounds;
        // An effect placed on its source model (GENERATE effect form: string0/int0).
        std::string attach_name;
        int attach_index = -1;
        Vec3f attach_offset{}, attach_rotation{}; // offset: ctrl work +BC..+C4 with the lock
        // Ctrl work +AC: the source's node a locked effect form took from the
        // GETMATRIX reply (8030D4C8..8030D510); placed on it every update.
        const Node* attach_node{};
        // Ctrl work +A8 _isPosLock: set by a locked effect form (8030D530)
        // and by MOVERL (8030ACFC / 8030AE40), cleared by the next MOVE /
        // MOVEEX / MOVERL (8030A72C). While set, 8030CB2C places the model
        // (attach node, else trace, else bone) instead of running 8030A448.
        bool pos_lock{};
        // Ctrl work +78: a translation channel ran (8030A448) or a ride moved
        // the model (8030CFCC); the walls and floors run when set (8030D00C).
        // Only 8030A448 clears it, so it keeps its value while locked.
        bool translated{};
        // The model its bullets shoot for (string2/int1, effect work +0xDC/+0xE0);
        // a chained effect inherits its source's owner (80310340).
        std::string owner_name;
        int owner_index = -1;
        std::uint32_t hitbox_generation{}; // bumped when the queue is cleared
        float muteki{};              // MUTEKI invulnerability frames
        // The RAM's named timer "KEIKATIMER": made by the first MOVE / MOVEEX /
        // MOVERL (8030A6BC) or MUTEKI (8030C1F0); its cached frame delta
        // (8031E4C4) steps the channels, the turn and MUTEKI.
        std::optional<dw4::MotionTimer> keika;
        std::int32_t record = -1;    // GENERATE record number (enemy DATABASE row)
        std::optional<NodeTimer> attack_delay; // player: the named RAM timer "ATDELAY"
        int combo_phase{};           // player work +128
        bool combo_released{};       // player work +12C
        bool panel_open{};           // player work +124: an NDMWPANEL panel is up
        bool frozen{};               // ONFREEZE: control, AI and motion paused
        float lod_high_saved = -1.0f; // ctrl work +A4 _svLODHighRange: igLod range[2] kept by SETLODHIGH (-1: none)
        bool previous_live{};        // enemy AI: generated last frame
        std::string lua_bhit;        // LUACALL BHit script prefix (ctrl work +30)
        std::string lua_whit, lua_fhit; // LUACALL WHit / FHit prefixes (+34 / +38)
        std::array<int, 8> spin{};   // player work +114: direction history (Spiral Attack)
        int slam_state{};            // player work +118..+120: Slam Attack double tap
        std::uint32_t slam_direction{};
        float slam_timer{};
        bool capsule_on{};           // KIND_PLAYER2 work +EC: a player is close
        bool capsule_ended{};        // end_g_000bcaps.lua already started
        int capsule_age{};           // KIND_PLAYER2 work +F0: updates since generated (to 10)
        // AIM map AI (803084B8): maps from the info's "AIM" data, AI work +50.
        const std::vector<ObjectView>* ai_maps{};
        int ai_map{};
        // The RAM's named timer "AITIMER" (data list +0x28; 8031DE44 start,
        // 8031E238 check, 8031E8BC remove), shared by the AIM map and the AIH
        // homing. Absent counts as expired.
        std::optional<NodeTimer> ai_timer;
        std::string ai_request;         // AI work +58
        Vec3f ai_world_target{};        // AI work +10: a WPOS map's target
        const Homing* homing{};         // AIH AI: state (work +1C), steps (+20)
        int homing_state{}, homing_steps{};
        // MOVERL ACTOR (8030AD4C..8030AE40, ctrl work +B4/+B8): the model
        // follows a bone of another model with the same index instead of
        // moving. Like the trace (+B0) they stay after the follow ends
        // (8030A71C only clears +A8) and count only while pos_lock is set.
        std::string follow_model, follow_bone;
        Node* trace{};        // own "TRC" data: an igTransformRecorder
        Node* follow_trace{}; // MOVERL JOINT (+B0): another model's recorder
        // beNDMWMdlPEBase status work (+240..+268), made by the first status
        // command (803AEBB4). Slots: 0 poison, 1 attack down, 2 power down,
        // 3 bind, 4 refract, 5 speed up, 6 power up, 7 metal, 8 sleep, 9 stun,
        // 10 rare icon, 11 the icon frame shown while any of 0..7 runs.
        struct Status {
            std::array<int, 12> icon{};    // +240 icon model RAM, -1 none
            std::array<int, 12> place{};   // +244 icon position shown, -1 none
            std::array<int, 12> timer{};   // +248 poison: 2 s ticks; otherwise frames
            std::array<bool, 11> start{};  // +252 set by a command, taken by the update
            int poison_damage{}, gedoku{}; // +256 / +260
            std::array<int, 6> down{};     // +264 an enemy's DOWN values (players: DOWNDATA)
            std::array<int, 7> up{};       // +268 an enemy's UP values (players: UPDATA)
            bool down_list{}, up_list{};   // +264 / +268 linked (803AE5F8 / 803AE87C)
        };
        std::optional<Status> status;
        // The status work's named RAM timers (POISON, SLEEP, STUN, BIND,
        // ATKDOWN, ...; 8031DE44 / 8031E238 / 8031E8BC) on the world clock.
        std::map<std::string, NodeTimer, std::less<>> named_timers;
    };

    const ModelRam* player(int index) const;
    ModelRam* find(std::string_view name, int index);
    // A player's DOWNDATA entry or an enemy's DOWN value (0 before its first
    // status command); what GETPEBASEWORK hands the FIGHTCALC paths.
    int down_value(Engine& engine, const ModelRam& ram, int index) const;
    // 803B0620: a cure ends the poison of a model.
    void clear_poison(ModelRam& ram);
    const std::vector<std::unique_ptr<ModelRam>>& rams() const { return rams_; }
    // Changes whenever the RAM list does (never the same value twice, also
    // across engines), for test code that keeps lookups over the list.
    std::uint64_t rams_generation() const { return rams_generation_; }

private:
    void update_player(Engine& engine, ModelRam& ram);
    bool advance_motion(Engine& engine, ModelRam& ram);
    void ride_step(ModelRam& ram);
    void land_ride(Engine& engine, ModelRam& ram, const Packet& floor);
    void update_hitboxes(Engine& engine, ModelRam& ram);
    void fire_hitbox(Engine& engine, ModelRam& ram, const ModelRam::Hitbox& box);
    void update_enemy(Engine& engine, ModelRam& ram);
    void update_ai_map(Engine& engine, ModelRam& ram);
    void update_homing(Engine& engine, ModelRam& ram);
    void follow_bone(Engine& engine, ModelRam& ram);
    void apply_control(Engine& engine, ModelRam& ram);
    void update_capsule(Engine& engine, ModelRam& ram);
    void collide(Engine& engine, ModelRam& ram);
    void check_wait(Engine& engine, ModelRam& ram);
    bool capsule_command(Engine& engine, ModelRam& ram, std::string_view command, Packet& packet);
    void update_item(Engine& engine, ModelRam& ram);
    void show_item_label(Engine& engine, ModelRam& ram, int player);
    void update_item_label(Engine& engine, ModelRam& ram);
    void item_erase_hook(Engine& engine, ModelRam& ram);
    void item_sequence_hook(Engine& engine, ModelRam& ram);
    bool pick_item(Engine& engine, ModelRam& ram, int player);
    void update_effects(Engine& engine, ModelRam& ram, std::size_t count);
    void update_sounds(Engine& engine, ModelRam& ram, std::size_t count);
    void place_attached(ModelRam& ram);
    void place_locked(Engine& engine, ModelRam& ram);
    void player_erase_hook(Engine& engine, ModelRam& ram);
    bool player_wait(Engine& engine, ModelRam& ram, std::string_view wait);
    void run_model_script(Engine& engine, ModelRam& ram, std::string_view prefix, std::string_view mode);
    bool hit_check(Engine& engine, ModelRam& ram, Packet& packet);
    void select_sequence(Engine& engine, ModelRam& ram, std::string_view name);
    void release_subcommands(Engine& engine, ModelRam& ram); // 803108A0
    void run_commands(Engine& engine, ModelRam& ram);
    // The player's sequence-change hook (beNDMWMdlPlayer vtable +B8).
    void player_sequence_hook(Engine& engine, ModelRam& ram);
    // Status effects (status.cpp).
    bool status_command(Engine& engine, ModelRam& ram, std::string_view command, Packet& packet);
    void update_status(Engine& engine, ModelRam& ram);
    void status_sequence_check(Engine& engine, ModelRam& ram);
    int status_icon(Engine& engine, ModelRam& ram, int slot, int icon, int place, bool moved);
    void apply_status_values(Engine& engine, ModelRam& ram);
    void set_down(Engine& engine, ModelRam& ram, int index, int value);
    void set_up(Engine& engine, ModelRam& ram, int index, int value);
    std::vector<std::unique_ptr<ModelRam>> rams_;
    std::vector<ModelRam*> player_rams_; // the PLAYER RAMs, in rams_ order (player())
    static std::uint64_t next_rams_generation() {
        static std::uint64_t next = 0;
        return ++next;
    }
    std::uint64_t rams_generation_ = next_rams_generation();
    struct UpdateEntry {
        int rank{};
        ModelRam* ram{};
    };
    std::vector<UpdateEntry> update_order_; // rams_ in class order (update)
    // Per-info data the RAMs point into, keyed by (document, info reference).
    // The key once was document ^ reference: two infos of different documents
    // could share it (e.g. i_fadewn01 and j_startback in stagis10, ~3 % of
    // runs, depending on heap addresses), and the second add_info freed the
    // first info's data under its RAMs (use-after-free, missing sequences).
    using InfoKey = std::pair<const void*, std::uint32_t>;
    std::map<InfoKey, std::unique_ptr<std::vector<Sequence>>> sequences_;
    std::map<InfoKey, std::unique_ptr<HitBody>> hit_bodies_;
    std::map<InfoKey, std::unique_ptr<Homing>> homings_;
    std::map<InfoKey, std::unique_ptr<std::vector<ObjectView>>> ai_maps_;
    // Manager +0x30 (803AB6A0..803AB6D0): one item name label per player,
    // shown on that player's ITNAMEFONT line font00..03.
    std::array<bool, 4> item_labels_{};
};

// PLAYERARMS (beWeapon, commands 80318474): weapon infos. Their sequence rows
// become SETLABEL overrides of the carrying player (WEAPON_RUNTIME.md);
// GENERATE hangs the weapon parts on their bones of the player's actors.
class WeaponManager final : public Manager {
public:
    WeaponManager() : Manager("beWeapon") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beWeaponInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    const std::vector<std::pair<std::string, std::string>>* labels(std::string_view weapon) const;

private:
    // Detaches what GENERATE hung on player index's actors.
    void remove(int index);
    struct Arms {
        std::vector<ObjectView> geometry; // info list 7: the weapon parts
        std::vector<std::string> joints;  // info list 8: their bones
    };
    struct Attached {
        std::vector<Node*> actors;
        std::vector<std::unique_ptr<Node>> nodes; // igModelViewMatrixBoneSelect per part
    };
    std::vector<std::unique_ptr<Ram>> rams_;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>> labels_;
    std::map<std::string, Arms, std::less<>> arms_;
    std::map<int, Attached> attached_; // per player
};

// beActionStarter (registration 802F2244, handler 802F228C): on the
// ACTION_BROADCAST of a target model's action, SETEFFECT this effect model on
// that model (hit sparks, defeat smoke, ...).
// SUBWND (beNDMWStatus, vtable 804E80EC): the status window (START) and the
// lobby programs (shops, Digi-Lab, bank, card). A program is a tree of
// windows (framework 8036B278..8036C170) built in subwnd.cpp.
class SubWindowManager final : public Manager {
public:
    SubWindowManager();
    ~SubWindowManager() override;
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWStatusInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    struct Window;
    // GETSHOPDATA: the stock of the weapon (6), armor (7), chip (8) and junk
    // (9) shops; an empty entry is a gap, abChip 185 a sold-out line.
    using Stock = std::vector<std::optional<ItemDevice>>;
    Stock* shop_stock(int program);
    // GETSHOPDATA 9 also returns COUNTER[1] as it was when the junk stock was made.
    int junk_counter() const { return junk_counter_; }

private:
    // 80351794: a program start fills the empty stocks from shopWeapon /
    // shopArmor / shopChip / shopJunk by player 1's level.
    void fill_stocks(Engine& engine);
    std::array<Stock, 4> stocks_;
    int junk_counter_{};
    std::vector<std::unique_ptr<Ram>> rams_;
    std::unique_ptr<Window> window_;
    bool running_{};
    int state_{}, program_{}, player_{};
    std::array<int, 11> shown_{}; // UPDATE snapshot for ONPREVIEW / OFPREVIEW
    int shown_hp_{}, shown_mp_{};
};

// NDMWPANEL (beNDMWPanelWaza, vtable 804E71E8): the per-player quick panels
// opened with R (techniques), L (disks) and Z (status), and Y's emergency
// heal. A picks the line; the panel stays open for the next use after the
// technique's DELAY (one second for disks). Its info comes with the stage,
// so it updates before beNDMWMdlPlayer (common file): a panel the player's
// R/L/Z opens first updates in the next frame, without that trigger, and
// the base it closes is removed by the next model update.
class PanelManager final : public Manager {
public:
    PanelManager() : Manager("beNDMWPanelWaza") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWPanelWazaInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct List {
        int kind{};                        // 0 techniques, 1 disks, 2 status
        // _lineMode defaults to 2 (beNDMWPanelObject meta, 80337F40).
        int cursor{}, scroll{}, line_mode{2}, count{};
        bool fire_request{};               // emergency heal: fire the preselected line
        std::vector<int> rows, colors;
    };
    struct PanelRam : Ram {
        bool running{}, reopen{}, cursor_shown{}, decided{};
        int state{}, fire{}, counter{}, kind{};
        // The panel RAM's named timers "FIRE" (the wait after a pick,
        // 0x8034A4E0 / 0x8034A988 check, 0x8034A604 / 0x8034A66C start) and
        // "EMGHEAL" (the CURSOR_NG pause of Y, 0x8034B408 / 0x8034B45C);
        // missing counts as run out.
        std::optional<NodeTimer> fire_timer, emgheal_timer;
        std::string label;
        std::array<List, 3> lists;
        bool no_gate_disk{}, no_healing{}, no_para_up{}, no_cureling{};
        std::array<std::optional<std::string>, 48> line_text{};
        std::array<int, 48> line_color{};
    };
    void open(PanelRam& ram, int kind);
    bool emergency_item(Engine& engine, PanelRam& ram);
    bool emergency_waza(Engine& engine, PanelRam& ram);
    void display(Engine& engine, PanelRam& ram, List& list);
    std::int64_t select(Engine& engine, PanelRam& ram, List& list);
    void set_line(Engine& engine, PanelRam& ram, int font, int line, const std::string* text, int color);
    void model(Engine& engine, PanelRam& ram, const char* name, const char* command, bool place, const char* sequence);
    std::vector<std::unique_ptr<PanelRam>> rams_;
};

class ActionStarterManager final : public Manager {
public:
    ActionStarterManager() : Manager("beActionStarter") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beActionStarterInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
};

class ActionManager final : public Manager {
public:
    ActionManager() : Manager("beAction2") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beAction2Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

    struct Command {
        std::string type, name, loop, interpolation;
        float first{}, second{};
    };
    struct Action {
        std::string name;
        std::vector<Command> commands;
    };
    struct ActionRam : Ram {
        Node* node{};
        Node* switch_node{};
        std::size_t switch_child{};
        std::vector<Action>* actions{};
        const Action* current{};
        std::size_t cursor{};
        bool action_active{}, visible{}, frozen{};
        bool motion_immediate{}; // work+0x10: the next MOTIONSET cuts in without a fade
        int motion_work{};       // work+0x14: MOTIONSET's combiner update phase (802F0EB0)
        int joint_state{}, fade_state{}, wait_state{}, flash_state{};
        std::int64_t joint_start{}, joint_end{};
        bool joint_ended{};
        float fade_limit{}, fade_add{}, fade_value{};
        std::optional<Vec4f> color;
        std::optional<bool> blend, texture, lighting;
        std::map<std::string, NodeTimer> timers;
        NodeTimer node_clock;
        AnimationDatabase* database{};
        std::vector<Node*> actors;
        MotionState motion;
        ActorPose pose;
        std::int64_t pose_time = -1;
        std::vector<Mat4> locals;         // the bone locals of `pose`
        std::vector<Mat4> combiner_cache; // combiner +0x40: the last traversal's bone locals
        // Combiner +0x54: the bone matrices the last traversal of the model's
        // actors computed (801DCF30, skeleton vtable +0x5C), which
        // GETBONEMATRIX reads (802F18A4, answered only while action_active).
        // Stays as it is while no actor of the model is traversed (culled,
        // switched off, before the first picture); null skeleton until the
        // first traversal (801D13DC allocates identities).
        ActorPose traversed;
        std::int64_t traversed_time = -1; // the world time of that traversal (traces)
        ActorPose peek;                   // peek_pose's picture (no game state)
        std::vector<Mat4> peek_locals;
    };
    // The actor's pose at `world_time` for the scene traversal: it evaluates
    // the combiner as 801D0C14 does (its update retires finished fades, its
    // output stays in the combiner cache MOTIONSET bakes from) and leaves the
    // bone matrices GETBONEMATRIX reads.
    const ActorPose* pose_for(const Node& actor, std::int64_t world_time);
    // The pose for a picture of an actor the original's frustum cull skips
    // (drawn only in the widened widescreen picture): evaluated without the
    // combiner update and without touching the pose or combiner caches.
    const ActorPose* peek_pose(const Node& actor, std::int64_t world_time);
    // GETAPPEARANCELIST (802F1870): the action of a model (its igActor nodes
    // and animation database with the skeleton).
    const ActionRam* find(std::string_view name, int index) const;
    // Whether the action whose model instance is `node` shows it (its switch
    // bit); true for a node no action owns.
    bool drawn(const Node* node) const;
    // The action switch whose bit mask is the one of `other` (an igSwitch):
    // the same mask object or the same mask words. A kind 0x400 HitLand check
    // switch shares the mask object of its model's action switch, so the
    // action bits (0x802F1EB4 / 0x802F20E8 set on a start, 0x802F1C58 NDSP
    // and 0x802F0898 FADESET HIDE clear) also switch the collision; nullptr
    // when no action switch shares it.
    const Node* mask_switch(const ObjectView& other) const;

private:
    void dispatch(Engine& engine, ActionRam& ram, std::string_view name);
    void run(Engine& engine, ActionRam& ram);
    bool execute(Engine& engine, ActionRam& ram, const Command& command);
    void apply_visuals(ActionRam& ram);
    std::vector<std::unique_ptr<ActionRam>> rams_;
    // Keyed by (document, info reference), see ModelCtrlManager::sequences_.
    std::map<std::pair<const void*, std::uint32_t>, std::unique_ptr<std::vector<Action>>> actions_;
    std::map<std::pair<const void*, std::uint32_t>, std::unique_ptr<AnimationDatabase>> databases_;
    float random_wait_min_{}; // 80535A60: minimum of the random WAITCHECKs
    std::map<const Node*, ActionRam*> actor_owner_;
};

// Game-wide system state (beSystem / beNDMWGameRam script services).
class GameSystem final : public Manager {
public:
    bool accepts(const ObjectView&) const override { return false; }
    void add_info(Engine&, const ObjectView&) override {}
    bool scheduled_at_registration() const override { return true; }
    bool script_call(Engine& engine, const std::string& method, std::span<const Lua4Value> args, std::vector<Lua4Value>& out);
    // GAMERAM messages (803717A4); the row is registered again after each stage load.
    void update(Engine& engine) override;
    void clear_stage(Engine&) override { registered_ = false; }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    Ram ram_{};
    bool registered_{};

    // Player game RAM subset used by FIGHTCALC (GAME_DATA / GAME_STATS).
    struct Stats {
        bool ready{};
        int level = 1;
        int species{};                        // shinka row
        int tsuyosa{}, mamori{}, kashikosa{}, seishin{}, subayasa{};
        int meichuritsu{}, chimeiritsu{}, mahiritsu{}, attack_delay = 30;
        int attack_zokusei = -1;
        std::array<int, 4> resist{}; // HONOOTEIKO, KOORITEIKO, KAMINARITEIKO, YAMITEIKO
        int kaihiritsu{}, bougyoritsu{}, mahiteikou{}, dokuteikou{};
        int exp{}, next_exp{}, exp_rate{}; // EXP, NEXTEXP, EXPRITSU
        int bit_rate{}, luck_rate{};       // BITRITSU, LUCKRITSU
        int hp_drain{}, mp_drain{};        // HPDRAIN, MPDRAIN
        int weapon_record = -1, weapon_skill_no{};
        std::array<int, 9> skill{1, 1, 1, 1, 1, 1, 1, 1, 1};
        std::string weapon_model;
    };
    struct PlayerSlot {
        bool joined{};
        std::uint32_t unique{};        // UNIQUE: the character's id on the card
        std::u16string name;           // PLAYERNAME as entered (full-width forms, at most 8)
        std::string digimon = "p_agumon";
        std::array<std::string, 3> weapons{};
        int hp = 100, max_hp = 100, mp = 50, max_mp = 50;
        Stats stats;
        // DEVICEFOLDER: equipment, at most 36.
        std::vector<ItemDevice> devices;
        // BANKDEVICE: devices kept in the bank, at most 96.
        std::vector<ItemDevice> bank_devices;
        // DISKFOLDER (8036CE1C): 12 slots made for the conItem records 0..11
        // with a count (at most 9); the first 10 are shown and can be reordered.
        struct Disk {
            int record{}, count{};
        };
        std::array<Disk, 12> disks = [] {
            std::array<Disk, 12> slots{};
            for (int i = 0; i < 12; ++i) slots[std::size_t(i)].record = i;
            return slots;
        }();
        int& disk_count(int record) {
            for (auto& slot : disks)
                if (slot.record == record) return slot.count;
            return disks[0].count;
        }
        int bits{};                    // player BIT (0..9999999)
        int bank_bits{};               // BANKBIT: bits kept in the bank (0..9999999)
        int xdata{};                   // XDATA
        std::array<int, 11> upcnt{};   // UPCNT: MHP, MMP, STR, DEF, WIS, SPI, SPD, fire, ice, thunder, dark
        // UPDATA / DOWNDATA status buffs: 0 SUBAYASA, 1 CHIMEIRITSU, 2 MAHITEIKOU,
        // 3 KAIHIRITSU, 4 TSUYOSA, 5 MAMORI, 6 MHP (up) / DELAY (down), 7.. resists.
        std::array<int, 11> updata{}, downdata{};
        // Equipment lists hold DEVICEFOLDER indices (-1 empty) like the
        // original pointer lists: POWERSOCKET 3 weapons ([0] in use) with the
        // POWERPOCKET 5 chips of that weapon, MAINSLOT 1 armor1 with its
        // MAINPOCKET 5 chips, SUBSLOT 1 armor2. Equipped devices carry flag.
        std::vector<int> power_socket = std::vector<int>(3, -1), power_pocket = std::vector<int>(5, -1);
        std::vector<int> main_slot = std::vector<int>(1, -1), main_pocket = std::vector<int>(5, -1);
        std::vector<int> sub_slot = std::vector<int>(1, -1);
        const ItemDevice* device(int index) const {
            return index >= 0 && index < int(devices.size()) ? &devices[std::size_t(index)] : nullptr;
        }
        // Drops DEVICEFOLDER entry index and renumbers the equipment lists.
        void remove_device(int index);
        std::vector<int> wazalist;     // WAZALIST: pcTech rows
        std::map<int, std::uint8_t> base_isuse; // BASE_ISUSE: species reached by this player
        // BASE_LEVEL / BASE_EXP / BASE_SKILL / BASE_UPCNT: what each of the 16
        // species keeps (8036DF70 stores the current species, 8036DBA8 loads
        // one on a change; 8036EB0C starts levels and skills at 1).
        std::array<int, 16> base_level = [] {
            std::array<int, 16> levels{};
            levels.fill(1);
            return levels;
        }();
        std::array<int, 16> base_exp{};
        std::array<std::array<int, 9>, 16> base_skill = [] {
            std::array<std::array<int, 9>, 16> skills{};
            for (auto& row : skills) row.fill(1);
            return skills;
        }();
        std::array<std::array<int, 11>, 16> base_upcnt{};
        std::map<int, std::uint8_t> cards;      // CARDLIST (kept in player 1's RAM)
        // BASE_FLAG / BASE_FLAGFOLDER / BASE_COUNTER of this player's record.
        // Player 1's are the common FLAG / FLAGFOLDER / COUNTER (Engine::flags(),
        // flag_folder, Engine::counters()), which BASE2CURRENT points at them
        // (link_common). New player data (8036CE1C, 0x8036D148-0x8036D174)
        // has BASE_FLAG[960] = 1, the constant byte at 80561411.
        std::map<std::int32_t, std::uint8_t> base_flags{{960, 1}};
        std::map<int, std::uint8_t> base_flag_folder;
        std::map<std::int32_t, std::int32_t> base_counters;
    };
    // Drop tables in generator order (804DE098): conItem, insItem, impItem,
    // weapon, armor1, armor2, abChip.
    static constexpr const char* item_tables[7] = {"conItem", "insItem", "impItem", "weapon", "armor1", "armor2", "abChip"};
    std::map<int, std::uint8_t> flag_folder; // common FLAGFOLDER, set by impItem pickups
    // Common BKCHIPSTAGE / BKCHIPPOS / BKCHIPDATA / BKCHIPNAME: the bit capsule
    // each player leaves when defeated (stage and position, set when it is
    // generated) with the bits and EXP lost and the species that lost them.
    struct BitChip {
        std::string stage; // "" none
        Vec3f position{};
        int bits{}, exp{};
        std::string digimon;
    };
    std::array<BitChip, 4> bit_chips{};
    // BKCHIPBITEXP (80377F70): a defeated player's bits and a fifth of the
    // EXP of its last level go into its capsule.
    void store_bit_chip(int slot);
    // CTRLITEM (GAMERAM 80372054): SYSctlDeviceFolder/DiskFolder/FlagFolder/Bit.
    int control_item(std::string_view folder, std::string_view command, int player, std::string_view table,
                     int a, int b, int c);
    // Creates the player on first use (8036EB0C), otherwise RECALCPLAYER.
    void prepare_player(Engine& engine, int slot);
    // 8036EB0C: species, BASE_LEVEL/SKILL 1, INI_WEAPON1..3 in DEVICEFOLDER and
    // POWERSOCKET, then RECALCPLAYER with full HP/MP.
    void create_player(Engine& engine, int slot);
    // CURRENT2BASE (8036DF70): the current species keeps LEVEL, EXP, SKILL and UPCNT.
    void store_species(int slot);
    // BASE2CURRENT for player 1 (8036DBA8 at 0x8036DE40): the common FLAG,
    // FLAGFOLDER and COUNTER become its BASE_FLAG, BASE_FLAGFOLDER and
    // BASE_COUNTER. Other players keep theirs to themselves.
    void link_common(Engine& engine, int slot);
    // Memory card records (save_game.cpp, docs/SAVES.md).
    save::Record make_record(Engine& engine, int slot);
    void apply_record(Engine& engine, int slot, const save::Record& record, bool common);
    bool load_test_record(Engine& engine, int slot);
    // BASE2CURRENT (8036DBA8): the player becomes species with its kept values.
    void load_species(Engine& engine, int slot, int species);
    // WEAPONEQUIPCHECK (8036E9D4): a folder weapon the species could equip.
    bool weapon_usable(Engine& engine, int slot, int species) const;
    // 8036F808 / 8036FB80 for a kept species: MHP, MMP, STR, DEF, WIS, SPI and
    // SPD at its BASE_LEVEL with its BASE_UPCNT.
    std::array<int, 7> form_stats(Engine& engine, int slot, int species) const;
    // WEAPONEQUIPSTRONG (8036E3B8): only the strongest usable weapon stays socketed.
    void equip_strongest_weapon(Engine& engine, int slot);
    // RECALCPLAYER (8036FDE8): status from level, UPCNT, buffs and equipment,
    // the technique list and the socket weapon.
    void recalc_player(Engine& engine, int slot);
    // P2EEXPUP (80373B48): every live player gains the enemy's EXP (the
    // defeating player the full share), levels up past NEXTEXP with full HP/MP.
    // Returns the defeating player's gain.
    int experience_up(Engine& engine, int killer, int enemy_record);
    // Levels past NEXTEXP: full HP / MP, the level-up effect and HUD numbers.
    bool apply_level_ups(Engine& engine, int slot);
    // Cheats: the EXP for `levels` more levels; bits added up to 9,999,999;
    // HP and MP refilled.
    void cheat_levels(Engine& engine, int slot, int levels);
    void cheat_bits(int slot, int amount);
    void cheat_refill(Engine& engine, int slot);
    // 8036F220 / 8036F364: HP (not for a defeated player) and MP changes with
    // the hpmt/hpnm and mpmt/mpnm HUD and the player model's HP.
    void change_hp(Engine& engine, int slot, int delta);
    void change_mp(Engine& engine, int slot, int delta);
    std::array<PlayerSlot, 4> players{};
    GameSystem() : Manager("beSystem") { players[0].joined = true; }
    std::int32_t difficulty{};
    std::int32_t difficulty_max{};
    // A stage started directly (--stage): the players are only made at their
    // GENERATE, after boot.lua listed the weapon packages (in the game the
    // character select made them), so their weapon packages load then.
    bool direct_start{};
    std::int32_t camera_mode{};
    std::int32_t light_mode{};
    std::int32_t ninzu = 1;
    bool game_started{}; // beSystem byte 4840: set by TITLE2 GAMESTART
    std::int32_t text_call{}; // TEXTORDER call number read by TXTgetCallNo (80535950)
    // GAMERAM GETCOMMON GATESTAGENAME / GATESTAGENO. The name is a string
    // variable (common table 804EA930, type 4) whose string object stays null
    // until 802EBCFC writes it (SYSsetGateStage, the gate disk 803B139C), so
    // SYSgetGateStageName answers nil until then (80269ECC -> 80273330).
    // The memory card holds neither.
    std::optional<std::string> gate_stage_name;
    std::int32_t gate_stage_no{};
    // GAMEDATAINIT of slot 0 (8036CE1C, 0x8036CE3C..0x8036CF70) releases the
    // 13 common variables and makes them again from table 804EA930 (8036C2B0,
    // {type, count, name}, jump table 804EACF0): NINZU, DIFFICULTY, CAMERAMODE
    // and LIGHTMODE (type 0) 0; FLAG[1024] and FLAGFOLDER[12] (type 3, the
    // byte r13-0x3AF0) and COUNTER[16] (type 1, 804EACE4) all 0; BKCHIPSTAGE[4]
    // and BKCHIPNAME[4] (type 5) null strings, BKCHIPPOS[12] (type 7,
    // 804EACE8) and BKCHIPDATA[8] (type 1) 0; GATESTAGENAME (type 4) a null
    // string, GATESTAGENO 0. It runs at boot (8036D37C, slots 0-3), for
    // LOADCTRL2 GENERATE_TITLE (slots 0-3, 0x80368B18) and when player 1 makes
    // or calls a character in the character select or the call shop
    // (0x8035ECC4 / 0x8035EF58). Player 1's BASE_FLAG / BASE_FLAGFOLDER /
    // BASE_COUNTER become the common ones again only with BASE2CURRENT
    // (link_common); LOADCTRL2 writes NINZU back when it closes, DIFFICULTY,
    // CAMERAMODE and LIGHTMODE only in title mode (0x803687C0), so a call-shop
    // character of player 1 leaves them 0. The memory card holds none of them.
    void reset_common(Engine& engine);
    void reset_gate_stage() {
        gate_stage_name.reset();
        gate_stage_no = 0;
    }
};

// FIGHTCALC (GAMERAM dispatcher 803717A4, subcommand names 804E3F38). The
// enemy DOWN values (GETPEBASEWORK) come in as plain numbers.
struct FightResult {
    std::int32_t damage{};
    std::int32_t attribute = -1; // 0 fire .. 3 dark; -1 common
    std::int32_t flag{};         // largest variance step
};

// The posted SETEFFECT of the panel, GAMERAM and item code (80349C88,
// 80371CFC, 80373B40, 803771A4, 803AAA6C, ...): model and sequence with
// delay 1.0, no offset, not tied to a sequence (0: dropped once started) and
// locked to the model (1), sent through 803061BC with 1 frame, so it arrives
// on the next frame, after a sequence change of this frame has cleared the
// subcommands (803108A0).
void post_effect(Engine& engine, std::string category, std::optional<std::string> name, int index, std::string model,
                 std::string sequence);

namespace fight {
// One draw from the system generator (vtable +60, 8021EB40), for every
// random number of the game (see SystemRandom).
std::uint32_t random();
SystemRandom& generator(); // its state, for save states
const char* bullet_name(int kind);
bool player_hits(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind, int evade_down);
bool player_critical(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind);
// GETPATKZOKUSEI (80373580): a technique's ZOKUSEI or the player's ATTACKZOKUSEI
// as 0 fire .. 3 dark (-1 none).
int player_attack_attribute(Engine& engine, const GameSystem::PlayerSlot& player, int kind);
FightResult player_damage(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, bool critical,
                          int defense_down);
int damage_rank(int hp, int max_hp, const FightResult& fight, bool critical, int kind);
// E2P* (803750B8, 8037526C, 80375C60): enemy attacks; the attack id is kind % 10000
// in the DATABASE eneatk table.
bool enemy_hits(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind);
FightResult enemy_damage(Engine& engine, const GameSystem::Stats& stats, int max_hp, int enemy_record, int kind,
                         int strength_down = 0, int critical_down = 0);
int enemy_damage_rank(Engine& engine, int kind, int hp, int damage, int max_hp, const FightResult& fight);
// Status effect times (cases 7..12 E2P*, 14..19 P2E*) in frames; 0 is no
// attempt, -1 a failed one. Down values are positive amounts.
int enemy_stun_time(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, int damage, int rank,
                    int mahiritsu_down);
int enemy_poison(Engine& engine, const GameSystem::PlayerSlot& player, int kind, std::string_view what);
int enemy_sleep_time(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind);
int enemy_bind_time(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind);
int enemy_attack_down(Engine& engine, int slot, int enemy_record, int kind, std::string_view what);
int enemy_power_down(Engine& engine, int slot, int enemy_record, int kind, std::string_view what);
int player_stun_time(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int max_hp, int damage);
int player_poison(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, std::string_view what);
int player_sleep_time(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind);
int player_attack_down(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, std::string_view what);
int player_power_down(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, std::string_view what);
// P2PKOUKAMAGIC (80376EE4): a support technique of the caster (or a disk's
// raise, kind -50); 1 when it acted.
int support_magic(Engine& engine, int target, int shooter, int kind);
// The combat random numbers.
std::uint32_t random();
// P2ESKILLUP (803736C8): the shooter's weapon or technique skill may grow.
void skill_up(Engine& engine, int shooter, int kind, int hit, int critical);
} // namespace fight

} // namespace dw4::engine
