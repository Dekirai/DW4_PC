#pragma once

#include "dw4/adx.hpp"
#include "dw4/engine/collision.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/motion.hpp"
#include "dw4/sfp.hpp"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::save {
struct Record;
}

namespace dw4::engine {

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
    void frame_camera(Engine& engine, FrameCamera& camera);

    struct CameraPath {
        std::string name;
        std::vector<Node*> nodes; // root .. igCamera
        Node* camera{};
    };

private:
    struct CameraRam : Ram {
        std::vector<CameraPath> paths;
        // beCameraDemoData ([7]): a demo camera info's own path and length.
        std::optional<CameraPath> demo_data;
        std::int64_t demo_data_duration{};
        int demo_path = -1; // index into paths, or -2 for demo_data
        std::int64_t demo_start{}, demo_duration{};
        bool demo_active{};
        bool box_camera{};
    };
    std::vector<std::unique_ptr<CameraRam>> rams_;
    std::optional<FrameCamera> current_;
};

class StageCtlManager final : public Manager {
public:
    StageCtlManager() : Manager("beNDMWStageCtl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWStageCtlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
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
    struct LogoRam : Ram { int state{}; };
    std::vector<std::unique_ptr<LogoRam>> rams_;
};

// Sound, sound effects and movies (beAsmSound2 command handler 80314F14).
// The BGM (type 1) and ambience (type 2) streams are the CRI ADX files the
// stage's sound list names as "[archive.entry]"; the host's audio service
// pulls them mixed through render(). Sound effects (types 3/4) are requests
// into the sfp banks (the common allse.sfp and the stage's beStaticInfo
// banks) or, without a request id, ADX files on the SE stream channels.
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
    // Interleaved stereo 16-bit PCM at kOutputRate of the running streams.
    void render(std::int16_t* out, std::size_t frames);
    static constexpr int kOutputRate = 48000;
    // An ACTION2 action started on a model: the model's SE set (beSeInfo)
    // plays the action's sounds after their delays; a looping sound of the
    // model's previous action stops.
    void action_started(Engine& engine, const std::string& model, int index, std::string_view action);
    // The movie playing: its video stream, picture rate and count; the
    // picture shown is the time since start at that rate.
    struct Movie {
        std::string name, script; // beMovieInfoData [2] file, [5] end script
        std::vector<std::uint8_t> video;
        double frame_rate = 30000.0 / 1001.0;
        std::size_t pictures{};
        std::int64_t start{};
        std::uint32_t serial{}; // changes with every PLAY
    };
    const Movie* movie() const { return movie_ ? &*movie_ : nullptr; }

private:
    // beSoundData: [4] type (1 BGM, 2 ambience, 3/4 sound effects), [5] file
    // list index, [6] SE request id (-1: an ADX file), [8] volume in 0.1 dB.
    struct SoundData {
        int type{}, file{}, volume{};
        std::uint32_t request{0xffffffffu};
    };
    struct Stream {
        std::string name;
        std::unique_ptr<adx::Decoder> decoder;
        float gain = 1.0f;
        int fade_left{}; // frames of a running fade-out (10, 8031D5D0)
        bool paused{};   // PLAYatPAUSE: buffered, waiting for PAUSEOFF (8031D294)
        int ready_in{};  // frames until the stream reports ready (buffer fill)
        float ramp_from = 1.0f, ramp_to = 1.0f; // VOLUMECTL2 gain ramp
        int ramp_frames{}, ramp_left{};
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
    // A running request sequence (803C49F4, one of 16 tracks).
    struct SeTrack {
        std::shared_ptr<SeBank> bank;
        sfp::Sequence sequence;
        std::string name, owner;
        float gain = 1.0f;
        double wait_ms{};
    };
    // A playing split sample.
    struct SeVoice {
        std::shared_ptr<SeBank> bank;
        int sample{};
        std::string name, owner;
        std::size_t position{};
        double phase{};
        double pitch = 1.0; // random detune
        float left = 1.0f, right = 1.0f;
        bool loop{};
    };
    bool play(Engine& engine, Packet& packet, bool paused = false);
    bool play_request(Engine& engine, const std::string& name, const SoundData& data, const std::string& owner = {});
    void play_cue(Engine& engine, const std::string& sound, const std::string& owner);
    void stop_effects(const std::string& name, const std::string& owner);
    bool open_stream(Engine& engine, Stream& stream, const std::string& name, const SoundData& data);
    void play_movie(Engine& engine, const Ram& ram, const std::string& name);
    void end_movie(Engine& engine);
    void mix_stream(Stream& stream, float* mix, std::size_t frames);
    void add_bank(Engine& engine, const ObjectView& data, bool common);
    void run_track(SeTrack& track);
    void mix_voices(float* mix, std::size_t frames);
    Stream* stream_named(std::string_view name);
    void stop(int channel);
    std::vector<std::unique_ptr<Ram>> rams_;
    std::map<std::string, SoundData, std::less<>> sounds_;
    std::vector<std::string> files_;
    static constexpr std::size_t kSeStreams = 2;
    std::array<Stream, 2 + kSeStreams> streams_; // BGM, ambience, SE streams
    std::vector<std::shared_ptr<SeBank>> banks_;
    bool common_loaded_{};
    std::vector<SeTrack> tracks_;
    std::vector<SeVoice> voices_;
    // beSeInfoData: per model name, action -> sound after [4] frames.
    struct SeCue {
        std::string action, sound;
        int delay{};
    };
    std::map<std::string, std::vector<SeCue>, std::less<>> se_sets_;
    struct PendingCue {
        std::string sound, owner;
        int frames{};
    };
    std::vector<PendingCue> cues_;
    std::map<std::string, std::string, std::less<>> owner_loops_; // "model#index" -> its looping sound
    std::uint32_t random_ = 0x12345678u;
    std::string current_bgm_;       // +0x24
    int se_volume_{};               // SNDFVOLUME: SNDF master 1..127, 0 unset (+56)
    std::optional<Movie> movie_;
    Stream movie_stream_;
    std::uint32_t movie_serial_{};
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
// switches it off after the info's duration ([10]).
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
        std::int64_t start{};
        Node* transform{};  // ram [11]
        Node* timer{};      // ram [12] ParticleTimer
        std::string owner;  // followed model (lock)
        int owner_index = -1;
        Vec3f offset{};
    };
    void stop(ParticleRam& ram);
    std::vector<std::unique_ptr<ParticleRam>> rams_;
    std::uint32_t random_ = 0x2545F491u;
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
        GeometryData* geometry{};
        bool searched{};
    };
    bool inside(const Water& w, const Vec3f& p) const;
    void push(Water& w, const Vec3f& p, float strength);
    void step(Water& w);
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
// above the players' centre (8031476C, 8031C1FC) and looks down.
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

private:
    std::vector<Node*> casters_, receivers_;
    bool shader_{};
    float darkness_ = 0.4f;
    int size_ = 256;
    float extent_ = 100.0f;             // [48]/[49]: the light's box half size
    float near_ = 0.1f, far_ = 1000.0f; // [27]/[28]
    std::optional<Vec3f> light_;
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

// beHitLandModel: static collision geometry (provisional host queries).
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
    // beHitLandDelivInfoRam [13]: a model's collision radius (0 when unknown).
    float radius(std::string_view model) const {
        const auto found = radii_.find(model);
        return found != radii_.end() ? found->second : 0.0f;
    }
    // beHitLandDelivInfoRam [12]: the model's HitLand query mask (0 when unknown).
    std::uint32_t mask(std::string_view model) const {
        const auto found = masks_.find(model);
        return found != masks_.end() ? found->second : 0u;
    }

private:
    struct HitRam : Ram {
        std::uint32_t group{};
    };
    // A kind 0x400 child: its dynamic collision and the model node it follows.
    struct Follower {
        std::uint32_t id{};
        ObjectView source;   // beCopyTransform [12], a model instance's node
        std::string info;    // owning HITLANDMODEL row
    };
    std::vector<Follower> followers_;
    std::map<std::string, bool, std::less<>> info_off_; // HITLANDMODEL OFFHIT on the whole info
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<std::unique_ptr<HitRam>> children_;
    std::map<std::string, float, std::less<>> radii_;
    std::map<std::string, std::uint32_t, std::less<>> masks_;
    CollisionWorld world_;
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
        int align{};              // 0 left, 1 center, 2 right
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
        int edit_camera{}, edit_light{}, edit_sound{}; // +44 / +48 / +52
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
        std::int64_t timer_end = -1; // BOOT_TIMER / GENE_TIMER
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

private:
    struct Mat {
        std::string name;
        Vec3f min{}, max{};
        bool enabled = true;
        std::uint32_t current{}, previous{};
    };
    struct MatRam : Ram {
        bool running = true;  // RAM +20: PAUSE / RESTART
        Node* visuals{};      // info [7] switch
        std::vector<Mat> mats;
    };
    std::vector<std::unique_ptr<MatRam>> rams_;
    Mat* context_{};
    std::optional<bool> pending_run_; // MATsetRun for the running mat (80302DA8)
};

// SAVEUTILITY (beSaveUtil, commands 80323D20): the memory card front end.
// One request runs at a time and fills the deliver record (beSvDeliver);
// results follow the beSv*Api codes: 2 done, 3 error, 4 no file, 8 both
// data copies broken, 9 not enough space, 10 no free file entry, 12 no card.
// The cards are GCI folders (Engine::card_root()/Card A|B); card A is
// always inserted, card B when its folder exists.
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
        int slot{};
        std::vector<std::uint8_t>* data{}; // game data after the copy header (40952 bytes)
        int result = 1;
        int operation{};
        std::uint32_t free_bytes{}, need_bytes{};
        bool enough_space{}, free_entry{}, done = true;
        // Per card slot (lists [3], [15], [12], [14]): card state (1 usable,
        // 0 none), free and needed bytes, room for the file.
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
        bool generated{}, ready{}, ending{};
        int version = 4;
        int pending = -1; // request waiting for the next update
        Deliver deliver;
    };
    void run(Engine& engine, UtilRam& ram);
    std::vector<std::uint8_t> header_; // 8 KB file header from beSvPlatDataGC
    std::vector<std::unique_ptr<UtilRam>> rams_;
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
        std::optional<std::string> wait_name;
        std::int32_t hp = 100, max_hp = 100;
        Mat4 base_matrix{};
        // Motion / control work (beModelCtrlInfoWork subset).
        dw4::MotionChannels channels;
        int ctrl_mode{}, ctrl_phase{};
        std::array<float, 4> pdmove{};
        bool pdmove_set{};
        bool fall_check = true;
        std::array<float, 3> applied{}, desired{};
        float turn_rate_y = 7.0f; // 80456F68/8045700C player rates (radians/frame)
        bool is_player{};
        bool control_enabled = true; // ONCTRL / OFCTRL
        bool key_input{};
        bool on_ground = true;
        // KIND_ITEM work (beNDMWMdlItem): drop table id and record (+EC/+10),
        // the rolled equipment (+F0), the nearest player (+FC), the name line.
        int item_table = -1, item_record{};
        ItemDevice item_device;
        int item_player = -1, item_label = -1;
        bool item_dim{};
        std::int64_t item_blink{};
        Vec3f last_ground{};         // PROVISIONAL fall-out recovery
        bool has_ground{};
        float fall_speed{};
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
        // An effect placed on an owner model (GENERATE with attachment strings).
        std::string attach_name;
        int attach_index = -1;
        Vec3f attach_offset{}, attach_rotation{};
        bool attach_lock{};
        std::uint32_t hitbox_generation{}; // bumped when the queue is cleared
        float muteki{};              // MUTEKI invulnerability frames
        std::int32_t record = -1;    // GENERATE record number (enemy DATABASE row)
        std::int64_t attack_ready{}; // player ATDELAY timer end
        int combo_phase{};           // player work +128
        bool combo_released{};       // player work +12C
        bool panel_open{};           // player work +124: an NDMWPANEL panel is up
        bool frozen{};               // ONFREEZE: control, AI and motion paused
        bool previous_live{};        // enemy AI: generated last frame
        int ai_timer{};              // PROVISIONAL enemy AI cooldown
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
        std::int64_t ai_timer_end = -1; // AITIMER
        std::string ai_request;         // AI work +58
        Vec3f ai_world_target{};        // AI work +10: a WPOS map's target
        const Homing* homing{};         // AIH AI: state (work +1C), steps (+20)
        int homing_state{}, homing_steps{};
        // MOVERL "JOINT" (8030ADC0, ctrl work +A8/+B4/+B8): the model follows a
        // bone of another model with the same index instead of moving.
        std::string follow_model, follow_bone;
        Node* trace{};        // own "TRC" data: an igTransformRecorder
        Node* follow_trace{}; // MOVERL ACTOR: another model's recorder
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
        // Named model timers (8031DE44 / 8031E238 / 8031E8BC): time left in ns.
        std::map<std::string, std::int64_t, std::less<>> named_timers;
    };

    const ModelRam* player(int index) const;
    ModelRam* find(std::string_view name, int index);
    // A player's DOWNDATA entry or an enemy's DOWN value (0 before its first
    // status command); what GETPEBASEWORK hands the FIGHTCALC paths.
    int down_value(Engine& engine, const ModelRam& ram, int index) const;
    // 803B0620: a cure ends the poison of a model.
    void clear_poison(ModelRam& ram);
    const std::vector<std::unique_ptr<ModelRam>>& rams() const { return rams_; }

private:
    void update_player(Engine& engine, ModelRam& ram);
    void advance_motion(Engine& engine, ModelRam& ram);
    void update_hitboxes(Engine& engine, ModelRam& ram);
    void fire_hitbox(Engine& engine, ModelRam& ram, const ModelRam::Hitbox& box);
    void update_enemy(Engine& engine, ModelRam& ram);
    void update_ai_map(Engine& engine, ModelRam& ram);
    void update_homing(Engine& engine, ModelRam& ram);
    void follow_bone(Engine& engine, ModelRam& ram);
    void apply_control(Engine& engine, ModelRam& ram);
    void update_capsule(Engine& engine, ModelRam& ram);
    bool capsule_command(Engine& engine, ModelRam& ram, std::string_view command, Packet& packet);
    void update_item(Engine& engine, ModelRam& ram);
    void item_label(Engine& engine, ModelRam& ram, bool show);
    bool pick_item(Engine& engine, ModelRam& ram, int player);
    void update_effects(Engine& engine, ModelRam& ram);
    void place_attached(ModelRam& ram);
    bool player_wait(Engine& engine, ModelRam& ram, std::string_view wait);
    void run_model_script(Engine& engine, ModelRam& ram, std::string_view prefix, std::string_view mode);
    bool hit_check(Engine& engine, ModelRam& ram, Packet& packet);
    void select_sequence(Engine& engine, ModelRam& ram, std::string_view name);
    void run_commands(Engine& engine, ModelRam& ram);
    // Status effects (status.cpp).
    bool status_command(Engine& engine, ModelRam& ram, std::string_view command, Packet& packet);
    void update_status(Engine& engine, ModelRam& ram);
    void status_sequence_check(Engine& engine, ModelRam& ram);
    int status_icon(Engine& engine, ModelRam& ram, int slot, int icon, int place, bool moved);
    void apply_status_values(Engine& engine, ModelRam& ram);
    void set_down(Engine& engine, ModelRam& ram, int index, int value);
    void set_up(Engine& engine, ModelRam& ram, int index, int value);
    std::vector<std::unique_ptr<ModelRam>> rams_;
    std::map<const void*, std::unique_ptr<std::vector<Sequence>>> sequences_;
    std::map<const void*, std::unique_ptr<HitBody>> hit_bodies_;
    std::map<const void*, std::unique_ptr<Homing>> homings_;
    std::map<const void*, std::unique_ptr<std::vector<ObjectView>>> ai_maps_;
    std::array<bool, 4> item_labels_{}; // ITNAMEFONT font00..03 in use
};

// PLAYERARMS (beWeapon, commands 80318474): weapon infos. Their sequence rows
// become SETLABEL overrides of the carrying player (WEAPON_RUNTIME.md); the
// weapon geometry attachment to the hand bone is not reconstructed yet.
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
// technique's DELAY (one second for disks).
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
        int cursor{}, scroll{}, line_mode{}, count{};
        bool fire_request{};               // emergency heal: fire the preselected line
        std::vector<int> rows, colors;
    };
    struct PanelRam : Ram {
        bool running{}, reopen{}, cursor_shown{}, decided{};
        int state{}, fire{}, counter{}, kind{};
        std::int64_t fire_end{}, emergency_ng{};
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
    };
    const ActorPose* pose_for(const Node& actor, std::int64_t world_time);
    // GETAPPEARANCELIST (802F1870): the action of a model (its igActor nodes
    // and animation database with the skeleton).
    const ActionRam* find(std::string_view name, int index) const;
    // Whether the action whose model instance is `node` shows it (its switch
    // bit); true for a node no action owns.
    bool drawn(const Node* node) const;

private:
    void dispatch(Engine& engine, ActionRam& ram, std::string_view name);
    void run(Engine& engine, ActionRam& ram);
    bool execute(Engine& engine, ActionRam& ram, const Command& command);
    void apply_visuals(ActionRam& ram);
    std::vector<std::unique_ptr<ActionRam>> rams_;
    std::map<const void*, std::unique_ptr<std::vector<Action>>> actions_;
    std::map<std::pair<const void*, std::uint32_t>, std::unique_ptr<AnimationDatabase>> databases_;
    std::map<const Node*, ActionRam*> actor_owner_;
};

// Game-wide system state (beSystem / beNDMWGameRam script services).
class GameSystem final : public Manager {
public:
    bool accepts(const ObjectView&) const override { return false; }
    void add_info(Engine&, const ObjectView&) override {}
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
    // 8036F220 / 8036F364: HP (not for a defeated player) and MP changes with
    // the hpmt/hpnm and mpmt/mpnm HUD and the player model's HP.
    void change_hp(Engine& engine, int slot, int delta);
    void change_mp(Engine& engine, int slot, int delta);
    std::array<PlayerSlot, 4> players{};
    GameSystem() : Manager("beSystem") { players[0].joined = true; }
    std::int32_t difficulty{};
    std::int32_t difficulty_max{};
    std::int32_t camera_mode{};
    std::int32_t light_mode{};
    std::int32_t ninzu = 1;
    bool game_started{}; // beSystem byte 4840: set by TITLE2 GAMESTART
    std::int32_t text_call{}; // TEXTORDER call number read by TXTgetCallNo (80535950)
    std::string gate_stage_name;
    std::int32_t gate_stage_no{};
    std::int32_t player_pos{}; // PLAYERgetPos: how the players entered this stage
};

// FIGHTCALC (GAMERAM dispatcher 803717A4, subcommand names 804E3F38). The
// enemy DOWN values (GETPEBASEWORK) come in as plain numbers.
struct FightResult {
    std::int32_t damage{};
    std::int32_t attribute = -1; // 0 fire .. 3 dark; -1 common
    std::int32_t flag{};         // largest variance step
};
namespace fight {
std::uint32_t random();
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
// P2PKOUKAMAGIC (80376EE4): a support technique of the caster; 1 when it acted.
int support_magic(Engine& engine, int target, int shooter, int kind);
// P2ESKILLUP (803736C8): the shooter's weapon or technique skill may grow.
void skill_up(Engine& engine, int shooter, int kind, int hit, int critical);
} // namespace fight

} // namespace dw4::engine
