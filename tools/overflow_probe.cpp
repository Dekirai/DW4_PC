// Data Overflow (docs/SURVIVAL.md 15.1, lane B): the O1a spikes, headless.
// A development tool: it drives a run engine (OverflowRules, the Violet Ring
// stagBS02, the run's donors) directly and prints what it measured; the
// notes are docs/SURVIVAL_O1A.md. It writes nothing but its output (the
// memory card folder is a throwaway one under the system temp folder).
//
//   dw4_overflow_probe DATA approach [RING] [FRAMES]   enemy approach and speed per family (4.3)
//   dw4_overflow_probe DATA push                       the body push against a crowd, the encirclement (N1)
//   dw4_overflow_probe DATA camera                     the box camera's ground view (H7)
//   dw4_overflow_probe DATA jump                       SQ_JUMP's MUTEKI per form, and at run time
//   dw4_overflow_probe DATA weapon                     the weapon model change in hand (5.3)
//   dw4_overflow_probe DATA memory none|all            the working set with no / all 21 families
//   dw4_overflow_probe DATA fight STAGE FORM N RING FRAMES   a diagnostic fight (hits, distances)
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/overflow.hpp"
#include "dw4/engine/randomizer.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/engine/igb_view.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace dw4::engine;
using namespace dw4::engine::overflow;
using Clock = std::chrono::steady_clock;

namespace {

std::filesystem::path g_data;

struct ProbeDirector : Director {
    std::function<void(Engine&)> on_step;
    int hits{}, damaged{};
    void step(Engine& engine) override {
        if (on_step) on_step(engine);
    }
    void player_hit(Engine&, const HitTaken& hit) override {
        ++hits;
        if (hit.damage > 0) ++damaged;
    }
};

std::array<dw4::PadSample, 4> pads(float x = 0, float y = 0, std::uint32_t buttons = 0) {
    std::array<dw4::PadSample, 4> out{};
    dw4::HostPadInput input;
    input.stick = {x, y};
    input.buttons = buttons;
    out[0] = dw4::host_pad_sample(input);
    out[0].connected = true;
    return out;
}

void step(Engine& engine, const std::array<dw4::PadSample, 4>& p) {
    engine.step(p);
    if (!engine.held()) engine.traverse_scene();
}

Vec3f pos(const ModelCtrlManager::ModelRam& ram) {
    return ram.node ? Vec3f{ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)} : Vec3f{};
}
float flat(const Vec3f& a, const Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }

double working_set_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc))
        return double(pmc.WorkingSetSize) / 1048576.0;
#endif
    return 0;
}

struct Run {
    std::unique_ptr<Engine> engine;
    std::shared_ptr<OverflowRules> rules;
    ProbeDirector director;
    double load_seconds{};
    std::uint64_t load_frames{};
};

// A run engine on `stage` with `families` as donors (god mode: the probes
// measure, they do not fight).
std::unique_ptr<Run> make_run(const std::vector<std::string>& families, const std::string& form = "p_agumon",
                              const std::string& stage = "stagbs02", bool god = true, bool with_rules = true) {
    auto run = std::make_unique<Run>();
    run->engine = std::make_unique<Engine>(g_data);
    auto& engine = *run->engine;
    const auto cards = std::filesystem::temp_directory_path() / "dw4_overflow_probe_cards";
    std::filesystem::create_directories(cards);
    engine.set_card_root(cards);
    engine.cheats().fast_loading = true;
    engine.cheats().god = god;
    if (with_rules) {
        run->rules = std::make_shared<OverflowRules>(RuleSet::run);
        run->rules->director = &run->director;
        run->rules->donors.families = families;
        engine.set_overflow_rules(run->rules);
    }
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[0].digimon = form;
    const auto t0 = Clock::now();
    engine.request_stage(stage);
    int after = -1;
    for (int i = 0; i < 6000; ++i) {
        step(engine, pads());
        if (!engine.loading() && engine.stage_active() && engine.stage_name() == stage) {
            if (after < 0) {
                after = 0;
                run->load_frames = engine.frame();
                run->load_seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            }
            if (++after >= 120) break;
        }
    }
    {
        const Messenger::PortOnly scope(engine.messenger());
        Packet on;
        engine.messenger().send(engine, "PLAYER", std::nullopt, -1, "ONCTRL", on);
    }
    engine.set_pad_input_enabled(true);
    return run;
}

ModelCtrlManager::ModelRam* player(Engine& engine) {
    auto* sys = engine.get<GameSystem>();
    return engine.get<ModelCtrlManager>()->find(sys->players[0].digimon, 0);
}

int record_of(Engine& engine, const std::string& family) {
    auto* db = engine.get<DatabaseManager>();
    for (int r = 0; r < db->rows("enemy"); ++r)
        if (db->text("enemy", r, "MODEL") == family) return r;
    return -1;
}

int rams_of(Engine& engine, const std::string& family) {
    int n = 0;
    for (const auto& ram : engine.get<ModelCtrlManager>()->rams()) n += ram->info_name == family ? 1 : 0;
    return n;
}

// GENERATE one enemy of `family` at (x, z) on the floor; its RAM or -1.
int spawn(Engine& engine, const std::string& family, float x, float z, float y_hint) {
    const Messenger::PortOnly scope(engine.messenger());
    Packet find;
    engine.messenger().send(engine, "MODELCTRL", family, -1, "FIND_SPACE", find);
    if (find.ints.empty()) return -1;
    Vec3f at{x, y_hint, z};
    if (auto* lands = engine.get<HitLandManager>()) {
        const auto land = lands->world().check_land({x, y_hint + 60.0f, z}, 1.0f, 400.0f, 400.0f);
        if (land.found) at.y = land.position.y;
    }
    const int record = record_of(engine, family);
    Packet generate;
    generate.vectors = {at, Vec3f{0, 0, 0}};
    generate.strings.emplace_back(std::string("SQ_ENTR"));
    generate.ints = {record, engine.get<DatabaseManager>()->number("enemy", record, "HPMAX").value_or(50)};
    engine.messenger().send(engine, "MODELCTRL", family, find.ints[0], "GENERATE", generate);
    Packet on;
    engine.messenger().send(engine, "MODELCTRL", family, find.ints[0], "ONCTRL", on);
    return find.ints[0];
}

void remove_all(Engine& engine) {
    const Messenger::PortOnly scope(engine.messenger());
    for (const auto& ram : engine.get<ModelCtrlManager>()->rams())
        if (ram->generated && ram->category == "ENEMY" && ram->info_name.starts_with("e_")) {
            Packet p;
            engine.messenger().send(engine, "MODELCTRL", ram->info_name, ram->index, "REMOVE", p);
        }
}

std::vector<std::string> all_families() {
    std::vector<std::string> out;
    for (const auto& f : randomizer::families())
        if (!f.boss && f.donor) out.emplace_back(f.name);
    return out;
}

// ---------------------------------------------------------------------------
// 4.3: approach and speed per family
// ---------------------------------------------------------------------------
int approach(float ring, int frames) {
    const auto families = all_families();
    auto run = make_run(families);
    auto& engine = *run->engine;
    std::printf("approach: stagbs02, %zu families as donors, ring %.0f, %d frames, the player standing at its start\n", families.size(),
                ring, frames);
    std::printf("%-12s %4s %4s %7s %7s %8s %9s %s\n", "family", "rams", "n", "moved", "<=30", "t(<=30)", "u/frame", "sequences");
    auto* self = player(engine);
    for (const auto& family : families) {
        const Vec3f center = pos(*self);
        const int n = std::min(4, rams_of(engine, family));
        std::vector<int> spawned;
        for (int k = 0; k < n; ++k) {
            const float a = 6.2831853f * float(k) / float(n) + 0.4f;
            if (const int ram = spawn(engine, family, center.x + ring * std::cos(a), center.z + ring * std::sin(a), center.y); ram >= 0)
                spawned.push_back(ram);
        }
        auto* models = engine.get<ModelCtrlManager>();
        std::vector<float> start(spawned.size()), best(spawned.size(), 1e9f), at240(spawned.size());
        std::vector<int> reach(spawned.size(), -1);
        std::set<std::string> seqs;
        for (int f = 0; f < frames; ++f) {
            step(engine, pads());
            const Vec3f me = pos(*self);
            for (std::size_t k = 0; k < spawned.size(); ++k) {
                auto* ram = models->find(family, spawned[k]);
                if (!ram || !ram->generated) continue;
                const float d = flat(pos(*ram), me);
                if (f == 0) start[k] = d;
                if (f == 240) at240[k] = d;
                best[k] = std::min(best[k], d);
                if (reach[k] < 0 && d <= 30.0f) reach[k] = f;
                if (f % 30 == 0) seqs.insert(ram->seq_name);
            }
        }
        int moved = 0, closed = 0, sum_t = 0;
        double speed = 0;
        for (std::size_t k = 0; k < spawned.size(); ++k) {
            if (start[k] - best[k] > 5.0f) ++moved;
            if (reach[k] >= 0) {
                ++closed;
                sum_t += reach[k];
            }
            speed += (start[k] - at240[k]) / 240.0;
        }
        std::string s;
        for (const auto& q : seqs) s += " " + q;
        std::printf("%-12s %4d %4zu %4d/%-2zu %4d/%-2zu %8s %9.3f%s\n", family.c_str(), rams_of(engine, family), spawned.size(), moved,
                    spawned.size(), closed, spawned.size(), closed ? std::to_string(sum_t / closed).c_str() : "-",
                    spawned.empty() ? 0.0 : speed / double(spawned.size()), s.c_str());
        remove_all(engine);
        for (int f = 0; f < 4; ++f) step(engine, pads());
        // The player back to its start (the probe stands still, enemies may push it).
    }
    std::printf("player walk speed for comparison: about 0.70 units a frame (Agumon, full stick)\n");
    return 0;
}

// ---------------------------------------------------------------------------
// N1: the body push and the encirclement
// ---------------------------------------------------------------------------
int push() {
    auto run = make_run({"e_goburi", "e_nume", "e_cockatri"});
    auto& engine = *run->engine;
    auto* models = engine.get<ModelCtrlManager>();
    auto* self = player(engine);
    // (a) a free walk for the reference.
    Vec3f a = pos(*self);
    for (int f = 0; f < 90; ++f) step(engine, pads(1.0f, 0.0f));
    const Vec3f b = pos(*self);
    std::printf("push: free walk 90 frames: %.1f units (%.3f / frame)\n", flat(a, b), flat(a, b) / 90.0);
    for (int f = 0; f < 30; ++f) step(engine, pads());
    // (b) a tight crowd of 12 around the player, then walk through it.
    const Vec3f c = pos(*self);
    const std::vector<std::string> families = {"e_goburi", "e_nume", "e_cockatri"};
    std::map<std::string, Vec3f> placed;
    for (int k = 0; k < 12; ++k) {
        const float ang = 6.2831853f * float(k) / 12.0f;
        const auto& fam = families[std::size_t(k) % families.size()];
        if (const int ram = spawn(engine, fam, c.x + 9.0f * std::cos(ang), c.z + 9.0f * std::sin(ang), c.y); ram >= 0) placed[fam + "#" + std::to_string(ram)] = {};
    }
    for (int f = 0; f < 2; ++f) step(engine, pads());
    for (auto& [key, p] : placed) {
        const auto hash = key.find('#');
        if (auto* ram = models->find(key.substr(0, hash), std::stoi(key.substr(hash + 1)))) p = pos(*ram);
    }
    std::map<std::string, int> pushed;
    const Vec3f d0 = pos(*self);
    int hits0 = run->director.hits;
    for (int f = 0; f < 90; ++f) {
        step(engine, pads(1.0f, 0.0f));
        for (const auto& [key, p] : placed) {
            const auto hash = key.find('#');
            if (auto* ram = models->find(key.substr(0, hash), std::stoi(key.substr(hash + 1))); ram && ram->seq_name == "SQ_PUSHED") ++pushed[key];
        }
    }
    const Vec3f d1 = pos(*self);
    float moved_max = 0;
    for (const auto& [key, p] : placed) {
        const auto hash = key.find('#');
        if (auto* ram = models->find(key.substr(0, hash), std::stoi(key.substr(hash + 1)))) moved_max = std::max(moved_max, flat(pos(*ram), p));
    }
    std::printf("push: through a ring of %zu enemies at r 9: the player walked %.1f units in 90 frames (free: %.1f); %zu enemies showed SQ_PUSHED; "
                "an enemy moved at most %.1f units; enemy hits on the player %d\n",
                placed.size(), flat(d0, d1), flat(a, b), pushed.size(), moved_max, run->director.hits - hits0);
    remove_all(engine);
    for (int f = 0; f < 30; ++f) step(engine, pads());
    // (c) the encirclement: 16 at r 45, three seconds to close in, then walk out.
    const Vec3f e = pos(*self);
    int ring = 0;
    for (int k = 0; k < 16; ++k) {
        const float ang = 6.2831853f * float(k) / 16.0f;
        ring += spawn(engine, families[std::size_t(k) % families.size()], e.x + 45.0f * std::cos(ang), e.z + 45.0f * std::sin(ang), e.y) >= 0;
    }
    float nearest = 1e9f;
    for (int f = 0; f < 180; ++f) step(engine, pads());
    for (const auto& ram : models->rams())
        if (ram->generated && ram->category == "ENEMY" && ram->info_name.starts_with("e_")) nearest = std::min(nearest, flat(pos(*ram), pos(*self)));
    float mean = 0;
    int counted = 0;
    for (const auto& ram : models->rams())
        if (ram->generated && ram->category == "ENEMY" && ram->info_name.starts_with("e_")) {
            mean += flat(pos(*ram), pos(*self));
            ++counted;
        }
    const int hits1 = run->director.hits;
    const Vec3f e1 = pos(*self);
    float out = 0;
    int frames_out = -1;
    for (int f = 0; f < 300; ++f) {
        step(engine, pads(0.0f, 1.0f));
        out = flat(pos(*self), e1);
        if (frames_out < 0 && out > 60.0f) frames_out = f;
    }
    std::printf("push: encirclement of %d at r 45: after 180 frames the nearest is %.1f, the mean %.1f units away; walking out: %.1f units in "
                "300 frames, past 60 at frame %d; enemy hits while walking out %d\n",
                ring, nearest, counted ? mean / float(counted) : 0.0f, out, frames_out, run->director.hits - hits1);
    return 0;
}

// ---------------------------------------------------------------------------
// H7: the box camera's view of the ground
// ---------------------------------------------------------------------------
int camera() {
    auto run = make_run({});
    auto& engine = *run->engine;
    for (int f = 0; f < 60; ++f) step(engine, pads());
    FrameCamera cam;
    engine.get<CameraManager>()->frame_camera(engine, cam);
    Mat4 inverse_view;
    invert(cam.view, inverse_view);
    const Vec3f eye{inverse_view(3, 0), inverse_view(3, 1), inverse_view(3, 2)};
    const Vec3f me = pos(*player(engine));
    std::printf("camera: eye (%.1f, %.1f, %.1f), player (%.1f, %.1f, %.1f): %.1f units back, %.1f up\n", eye.x, eye.y, eye.z, me.x, me.y,
                me.z, flat(eye, me), eye.y - me.y);
    Mat4 vp = cam.view * cam.projection, inverse_vp;
    invert(vp, inverse_vp);
    auto unproject = [&](float x, float y, float z) {
        const float X = x * inverse_vp(0, 0) + y * inverse_vp(1, 0) + z * inverse_vp(2, 0) + inverse_vp(3, 0);
        const float Y = x * inverse_vp(0, 1) + y * inverse_vp(1, 1) + z * inverse_vp(2, 1) + inverse_vp(3, 1);
        const float Z = x * inverse_vp(0, 2) + y * inverse_vp(1, 2) + z * inverse_vp(2, 2) + inverse_vp(3, 2);
        const float W = x * inverse_vp(0, 3) + y * inverse_vp(1, 3) + z * inverse_vp(2, 3) + inverse_vp(3, 3);
        return Vec3f{X / W, Y / W, Z / W};
    };
    for (const float widen : {1.0f, 4.0f / 3.0f}) {
        std::printf("camera: %s picture, the ground (y %.0f) it shows, distance from the player:\n", widen == 1.0f ? "4:3" : "16:9", me.y);
        float far_min = 1e9f, any_max = 0;
        for (int iy = 0; iy <= 4; ++iy) {
            std::printf("   ");
            for (int ix = 0; ix <= 4; ++ix) {
                const float nx = (-1.0f + 0.5f * float(ix)) * widen, ny = -1.0f + 0.5f * float(iy);
                const Vec3f a = unproject(nx, ny, 0.0f), b = unproject(nx, ny, 0.5f);
                const float dy = b.y - a.y;
                if (std::fabs(dy) < 1e-6f || (me.y - a.y) / dy < 0) {
                    std::printf("   sky");
                    continue;
                }
                const float t = (me.y - a.y) / dy;
                const Vec3f g{a.x + (b.x - a.x) * t, me.y, a.z + (b.z - a.z) * t};
                const float d = flat(g, me);
                std::printf(" %5.0f", d);
                any_max = std::max(any_max, d);
                if (ix == 0 || ix == 4 || iy == 0 || iy == 4) far_min = std::min(far_min, d);
            }
            std::printf("\n");
        }
        std::printf("   the nearest picture edge on the ground: %.0f units, the farthest ground point: %.0f\n", far_min, any_max);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// The jump's MUTEKI: the forms' SQ_JUMP data, and at run time
// ---------------------------------------------------------------------------
int jump() {
    static const char* forms[16] = {"agumon", "vmon", "girumon", "dorumon", "weregaru", "hekabut", "wargrey", "angelr",
                                    "beelzeb", "alpha", "bwargrey", "impdrafm", "impdrapm", "metalgaru", "dukecrim", "susanoo"};
    const auto resources = dw4::MountedResources::open(g_data);
    for (const char* form : forms) {
        const std::string path = std::string("_common/p_") + form + ".igb";
        const auto doc = dw4::read_igb_document(resources.read(path));
        std::map<std::string, std::string> found; // sequence -> MUTEKI subcommands
        for (std::size_t i = 0; i < doc.objects.size(); ++i) {
            const auto& o = doc.objects[i];
            if (doc.metadata.object_types[o.type_index].name != "beModelCtrlInfo") continue;
            const ObjectView info(&doc, o.reference_index);
            for (const auto& data : info.list(5)) {
                if (!data || data.str(2) != "SEQ") continue;
                for (const auto& seq : data.list(4)) {
                    if (!seq) continue;
                    const std::string name(seq.str(2));
                    if (name != "SQ_JUMP" && name != "SQ_ATCKTM") continue;
                    std::string mutekis;
                    for (const auto& c : seq.list(3))
                        if (c && c.type() == "beModelCtrlMUTEKI") {
                            const std::string what(c.str(2));
                            if (what == "ON" || what == "OFF" || what.empty()) mutekis += " " + (what.empty() ? std::string("ON") : what) + "(" + std::to_string(int(c.f32(3))) + ")";
                            else if (what != "OFFWALL" && what != "ONWALL" && what != "OFFFLOOR" && what != "ONFLOOR") mutekis += " " + what;
                        }
                    found[name] += mutekis.empty() ? std::string(" none") : mutekis;
                }
            }
        }
        std::printf("jump: p_%-10s SQ_JUMP MUTEKI:%s | SQ_ATCKTM MUTEKI:%s\n", form, found["SQ_JUMP"].c_str(), found["SQ_ATCKTM"].c_str());
    }
    // At run time: X in the arena, the player's MUTEKI count each frame.
    auto run = make_run({});
    auto& engine = *run->engine;
    auto* self = player(engine);
    float most = 0;
    int nonzero = 0;
    std::set<std::string> seqs;
    for (int f = 0; f < 80; ++f) {
        step(engine, pads(0, 0, f == 2 ? dw4::pad_button::x : 0));
        most = std::max(most, self->muteki);
        nonzero += self->muteki != 0 ? 1 : 0;
        seqs.insert(self->seq_name);
    }
    std::string s;
    for (const auto& q : seqs) s += " " + q;
    std::printf("jump: Agumon presses X in the Violet Ring: sequences%s; frames with MUTEKI %d, highest count %.1f\n", s.c_str(), nonzero, most);
    return 0;
}

// ---------------------------------------------------------------------------
// 5.3: the weapon model change in hand
// ---------------------------------------------------------------------------
void subtree_names(const Node* node, std::multiset<std::string>& out, int depth = 0) {
    if (!node || depth > 64) return;
    if (!node->name.empty()) out.insert(node->name);
    for (const auto* child : node->children) subtree_names(child, out, depth + 1);
}

int weapon() {
    auto run = make_run({});
    auto& engine = *run->engine;
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    auto* weapons = engine.get<WeaponManager>();
    auto* self = player(engine);
    auto& p = sys->players[0];
    const std::string before = p.weapons[0];
    std::multiset<std::string> names_before;
    subtree_names(self->node, names_before);
    const std::string label_before = self->labels.contains("SQ_ATCK1") ? self->labels.at("SQ_ATCK1") : self->label;
    // Battle Hawk alpha (weapon record 0, the axe family 1) into the socket, as the ladder would.
    ItemDevice axe;
    axe.type = 0;
    axe.record = 0;
    axe.flag = true;
    p.devices.push_back(axe);
    p.power_socket[0] = int(p.devices.size()) - 1;
    sys->recalc_player(engine, 0);
    const std::string model = p.weapons[0];
    const bool had = weapons->labels(model) != nullptr;
    const auto t0 = Clock::now();
    const bool loaded = had || engine.load_common_now("_common/" + model + ".igb");
    const double load_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    {
        const Messenger::PortOnly scope(engine.messenger());
        Packet arms;
        engine.messenger().send(engine, "PLAYERARMS", model, 0, "GENERATE", arms);
    }
    for (int f = 0; f < 10; ++f) step(engine, pads());
    std::multiset<std::string> names_after;
    subtree_names(self->node, names_after);
    std::vector<std::string> gone, added;
    std::set_difference(names_before.begin(), names_before.end(), names_after.begin(), names_after.end(), std::back_inserter(gone));
    std::set_difference(names_after.begin(), names_after.end(), names_before.begin(), names_before.end(), std::back_inserter(added));
    const std::string label_after = self->labels.contains("SQ_ATCK1") ? self->labels.at("SQ_ATCK1") : self->label;
    std::printf("weapon: %s -> %s (%s, TSUYOSA %d): package %s (%.1f ms), PLAYERARMS GENERATE; player nodes -%zu +%zu", before.c_str(),
                model.c_str(), dw4::engine::game_text_utf8(db->text("weapon", 0, "NAME")).c_str(), p.stats.tsuyosa,
                had ? "was loaded" : loaded ? "loaded now" : "NOT loaded", load_ms, gone.size(), added.size());
    for (std::size_t i = 0; i < std::min<std::size_t>(added.size(), 6); ++i) std::printf(" +%s", added[i].c_str());
    for (std::size_t i = 0; i < std::min<std::size_t>(gone.size(), 6); ++i) std::printf(" -%s", gone[i].c_str());
    std::printf("; SQ_ATCK1 label %s -> %s; weapon skill column %d\n", label_before.c_str(), label_after.c_str(), p.stats.weapon_skill_no);
    return 0;
}

// ---------------------------------------------------------------------------
// The memory of all 21 families on one arena
// ---------------------------------------------------------------------------
int memory(const std::string& which) {
    const double base = working_set_mb();
    const auto families = which == "all" ? all_families() : std::vector<std::string>{};
    auto run = make_run(families);
    auto& engine = *run->engine;
    int rams = 0;
    for (const auto& family : families) rams += rams_of(engine, family);
    // Fill every RAM once (all enemies alive), the worst case of the pools.
    int spawned = 0;
    const Vec3f me = pos(*player(engine));
    for (const auto& family : families)
        for (int k = 0; k < rams_of(engine, family); ++k) {
            const float a = 0.37f * float(spawned);
            const float r = 30.0f + float(spawned % 7) * 10.0f;
            spawned += spawn(engine, family, me.x + r * std::cos(a), me.z + r * std::sin(a), me.y) >= 0;
        }
    double step_ms = 0;
    for (int f = 0; f < 120; ++f) {
        const auto t0 = Clock::now();
        step(engine, pads());
        step_ms += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }
    std::printf("memory: %s: %zu donor families, %d RAMs, %d spawned; load %llu frames, %.2f s; working set %.0f MB (process start %.0f MB); "
                "step+traverse %.2f ms per frame with all alive\n",
                which.c_str(), families.size(), rams, spawned, (unsigned long long)run->load_frames, run->load_seconds, working_set_mb(), base,
                step_ms / 120.0);
    for (const auto& line : engine.log_lines())
        if (line.find("overflow:") != std::string::npos) std::printf("  %s\n", line.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// A diagnostic fight on any stage (no rules)
// ---------------------------------------------------------------------------
int fight_mode(const std::string& stage, const std::string& form, int n, float ring, int frames) {
    auto run = make_run({}, form, stage, false, false);
    auto& engine = *run->engine;
    auto* models = engine.get<ModelCtrlManager>();
    std::set<std::string> fams;
    for (const auto& ram : models->rams())
        if (ram->category == "ENEMY" && ram->info_name.starts_with("e_")) fams.insert(ram->info_name);
    const Vec3f me = pos(*player(engine));
    int spawned = 0;
    std::vector<std::string> list(fams.begin(), fams.end());
    for (int k = 0; k < n * 4 && spawned < n && !list.empty(); ++k) {
        const float a = 6.2831853f * float(spawned) / float(n);
        spawned += spawn(engine, list[std::size_t(k) % list.size()], me.x + ring * std::cos(a), me.z + ring * std::sin(a), me.y) >= 0;
    }
    auto* sys = engine.get<GameSystem>();
    int hits = 0, last = sys->players[0].hp;
    for (int f = 0; f < frames; ++f) {
        step(engine, pads(std::cos(float(f) * 0.01f) * 0.6f, std::sin(float(f) * 0.01f) * 0.6f));
        if (sys->players[0].hp < last) ++hits;
        last = sys->players[0].hp;
        if (f % 600 == 0) {
            float nearest = 1e9f;
            int alive = 0;
            for (const auto& ram : models->rams())
                if (ram->generated && ram->category == "ENEMY" && ram->info_name.starts_with("e_")) {
                    nearest = std::min(nearest, flat(pos(*ram), pos(*player(engine))));
                    ++alive;
                }
            std::printf("fight %s f%d: alive %d nearest %.1f player hp %d seq %s ctrl %d pad %d\n", stage.c_str(), f, alive, nearest,
                        sys->players[0].hp, player(engine)->seq_name.c_str(), player(engine)->control_enabled, engine.pad_input_enabled());
        }
    }
    std::printf("fight %s: %d spawned (families %zu), %d hits in %d frames\n", stage.c_str(), spawned, fams.size(), hits, frames);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "dw4_overflow_probe DATA approach|push|camera|jump|weapon|memory|fight ...\n");
        return 2;
    }
    g_data = argv[1];
    const std::string mode = argv[2];
    try {
        if (mode == "approach") return approach(argc > 3 ? float(std::atof(argv[3])) : 80.0f, argc > 4 ? std::atoi(argv[4]) : 480);
        if (mode == "push") return push();
        if (mode == "camera") return camera();
        if (mode == "jump") return jump();
        if (mode == "weapon") return weapon();
        if (mode == "memory") return memory(argc > 3 ? argv[3] : "all");
        if (mode == "fight" && argc > 7)
            return fight_mode(argv[3], argv[4], std::atoi(argv[5]), float(std::atof(argv[6])), std::atoi(argv[7]));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "probe failed: %s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}
