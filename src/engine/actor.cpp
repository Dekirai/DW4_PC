#include "dw4/engine/actor.hpp"

#include "dw4/skinning.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace dw4::engine {
namespace {
Mat4 to_mat4(const SkinMatrix& m) {
    Mat4 r;
    for (std::size_t i = 0; i < 16; ++i) r.m[i] = static_cast<float>(m[i]);
    return r;
}
Mat4 lerp_matrix(const Mat4& a, const Mat4& b, float t) {
    Mat4 r;
    for (std::size_t i = 0; i < 16; ++i) r.m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;
    return r;
}
} // namespace

AnimationDatabase::AnimationDatabase(const ObjectView& database) : object_(database) {
    const auto skeletons = database.list(5);
    if (!skeletons.empty() && skeletons[0]) {
        const auto& s = skeletons[0];
        skeleton_.object = s;
        const auto bones = s.list(4);
        const auto constants = s.memory(3);
        const auto inverse = s.memory(5);
        const auto joint_count = s.i32(6);
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const auto& bone = bones[i];
            skeleton_.names.emplace_back(bone ? std::string(bone.str(2)) : std::string());
            skeleton_.parent.push_back(bone ? bone.i32(3, -1) : -1);
            skeleton_.joint.push_back(bone ? bone.i32(4, -1) : -1);
            Vec3f c{};
            if (constants.size() >= (i + 1) * 12)
                c = {read_f32le(constants, i * 12), read_f32le(constants, i * 12 + 4), read_f32le(constants, i * 12 + 8)};
            skeleton_.constant_translation.push_back(c);
        }
        skeleton_.joint_bone.assign(std::size_t(std::max(joint_count, 0)), 0);
        skeleton_.inverse_joint.assign(std::size_t(std::max(joint_count, 0)), Mat4{});
        for (int j = 0; j < joint_count; ++j) {
            if (inverse.size() >= std::size_t(j + 1) * 64)
                for (std::size_t k = 0; k < 16; ++k) skeleton_.inverse_joint[std::size_t(j)].m[k] = read_f32le(inverse, std::size_t(j) * 64 + k * 4);
        }
        for (std::size_t b = 0; b < skeleton_.joint.size(); ++b) {
            const int j = skeleton_.joint[b];
            if (j >= 0 && j < joint_count) skeleton_.joint_bone[std::size_t(j)] = int(b);
        }
        skeleton_.valid = !bones.empty();
    }
    animations_ = database.list(6);
}

bool AnimationDatabase::has_clip(std::string_view name) const {
    for (const auto& a : animations_) if (a && a.str(2) == name) return true;
    return false;
}

std::string AnimationDatabase::first_clip_name() const {
    for (const auto& a : animations_) if (a) return std::string(a.str(2));
    return {};
}

bool AnimationDatabase::looping(std::string_view name) const {
    const auto found = looping_.find(name);
    return found == looping_.end() || found->second;
}

void AnimationDatabase::set_looping(std::string_view name, bool loop) {
    if (auto found = looping_.find(name); found != looping_.end()) found->second = loop;
    else looping_.emplace(std::string(name), loop);
}

const Clip* AnimationDatabase::clip(std::string_view name) {
    if (auto found = clips_.find(name); found != clips_.end()) return found->second->valid ? found->second.get() : nullptr;
    auto clip = std::make_unique<Clip>();
    clip->name = std::string(name);
    try {
        ObjectView animation;
        for (const auto& a : animations_) if (a && a.str(2) == name) { animation = a; break; }
        if (animation && skeleton_.valid) {
            clip->duration = animation.i64(9);
            const auto bindings = animation.list(4);
            const auto tracks = animation.list(5);
            std::vector<int> map(tracks.size());
            for (std::size_t i = 0; i < tracks.size(); ++i) map[i] = int(i);
            if (!bindings.empty() && bindings[0]) {
                const auto bytes = bindings[0].memory(3);
                for (std::size_t i = 0; i < tracks.size() && (i + 1) * 4 <= bytes.size(); ++i) map[i] = int(read_u32le(bytes, i * 4));
            }
            ObjectView source;
            for (const auto& track : tracks) {
                if (!track) continue;
                const auto transform = track.ref(3);
                if (transform) { source = transform.ref(3); if (source) break; }
            }
            if (source) {
                clip->channels = decode_enbaya(source.memory(2));
                clip->track_bone = map;
                clip->use_sample_translation.assign(skeleton_.names.size(), false);
                if (const auto mask = animation.ref(10)) {
                    const auto bytes = mask.memory(4);
                    for (std::size_t b = 0; b < skeleton_.names.size(); ++b) {
                        const std::size_t word = b / 32;
                        if ((word + 1) * 4 <= bytes.size()) clip->use_sample_translation[b] = (read_u32le(bytes, word * 4) >> (b % 32)) & 1u;
                    }
                }
                clip->valid = clip->channels.track_count() > 0 && clip->duration > 0;
            }
        }
    } catch (const std::exception&) {
        clip->valid = false;
    }
    auto* raw = clip.get();
    clips_.emplace(std::string(name), std::move(clip));
    return raw->valid ? raw : nullptr;
}

float motion_source_time(const Clip& clip, std::int64_t local, bool loop) {
    // 801E52FC: the state's 64-bit time >> 18 as a signed 32-bit count of
    // 2^18 ns, times 2.62144e-4 s (0x3989705f at 80566D50); the source's
    // duration (vtable +0x18 of its enbaya data) of at most 1/30 s counts as
    // 0.05 s (0x801E5394-0x801E53A4).
    const auto coarse = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(local) >> 18));
    const float t = static_cast<float>(coarse) * std::bit_cast<float>(0x3989705fu);
    float duration = clip.channels.duration();
    if (duration <= std::bit_cast<float>(0x3d088889u)) duration = std::bit_cast<float>(0x3d4cccceu);
    // 801E4FE8 with the source mode MOTIONSET set (0x802F1488/0x802F1494):
    // mode 4 (played once) limits the time to duration + [8056578C], which
    // ACTION2's init 802EFDC8 sets to -1/30 s (0xbd088889 at 80424F80), and
    // returns the limit past it (0x801E5018-0x801E502C), so a finished clip
    // holds its last full 1/30 s key instead of reaching the partial key the
    // stream appends behind the duration. Mode 0 (loop) and the times inside
    // the limit take the remainder path: the truncated cycle count (fctiwz),
    // t - cycles * duration fused (fnmsubs), nothing below 0. At exactly the
    // limit that path gives 0, as on the original.
    if (!loop) {
        duration = duration + std::bit_cast<float>(0xbd088889u);
        if (t > duration) return duration;
        if (t < 0.0f) return 0.0f;
    }
    const float cycles = std::trunc(t / duration);
    const float wrapped = std::fma(-cycles, duration, t);
    return wrapped > 0.0f ? wrapped : 0.0f;
}

namespace {
bool sample_locals(AnimationDatabase& database, const std::string& name, std::int64_t start, std::int64_t time,
                   std::vector<Mat4>& locals) {
    const Clip* clip = database.clip(name);
    const auto& skel = database.skeleton();
    if (!clip) return false;
    const float seconds = motion_source_time(*clip, time - start, database.looping(name));
    std::vector<EnbayaTransform> samples;
    try {
        samples = sample_enbaya_original(clip->channels, seconds);
    } catch (const std::exception&) {
        return false;
    }
    locals.assign(skel.names.size(), Mat4{});
    for (std::size_t t = 0; t < samples.size() && t < clip->track_bone.size(); ++t) {
        const int bone = clip->track_bone[t];
        if (bone < 0 || std::size_t(bone) >= locals.size()) continue;
        Vec3 translation = samples[t].translation;
        if (std::size_t(bone) < clip->use_sample_translation.size() && !clip->use_sample_translation[std::size_t(bone)]) {
            const auto& c = skel.constant_translation[std::size_t(bone)];
            translation = {c.x, c.y, c.z};
        }
        locals[std::size_t(bone)] = to_mat4(original_bone_transform(samples[t].rotation, translation));
    }
    return true;
}
} // namespace

float MotionState::weight(std::int64_t time) const {
    // A state that cuts in (first == 0 or the first MOTIONSET after the
    // activation) starts at ratio 1 with no range (0x802F11F8-0x802F1208).
    if (blend <= 0) return 1.0f;
    // 801D0668-801D06AC: elapsed time and duration through double to float,
    // divided, clamped above 1, then start ratio 0 + range 1 * that.
    float ratio = static_cast<float>(static_cast<double>(time - blend_start)) / static_cast<float>(static_cast<double>(blend));
    if (ratio > 1.0f) ratio = 1.0f;
    return ratio;
}

void MotionState::update(std::int64_t time) {
    // A replacing base state (combine mode 0) retires nothing (801CFFB4 skips
    // it); only a fade that reached weight 1 does.
    if (has_previous() && !(weight(time) < 1.0f)) {
        previous_clip.clear();
        previous_pose.clear();
        hidden = false;
    }
}

bool motion_locals(AnimationDatabase& database, const MotionState& motion, std::int64_t time, std::vector<Mat4>& locals) {
    if (motion.clip.empty() || !sample_locals(database, motion.clip, motion.start, time, locals)) return false;
    if (motion.fading(time)) {
        std::vector<Mat4> sampled;
        const std::vector<Mat4>* previous = &motion.previous_pose;
        if (motion.previous_pose.empty())
            previous = sample_locals(database, motion.previous_clip, motion.previous_start, time, sampled) ? &sampled : nullptr;
        if (previous) {
            const float t = std::clamp(motion.weight(time), 0.0f, 1.0f);
            for (std::size_t i = 0; i < locals.size() && i < previous->size(); ++i) locals[i] = lerp_matrix((*previous)[i], locals[i], t);
        }
    }
    return true;
}

bool evaluate_pose(AnimationDatabase& database, const MotionState& motion, std::int64_t time, ActorPose& pose,
                   std::vector<Mat4>& locals) {
    const auto& skel = database.skeleton();
    pose.skeleton = &skel;
    pose.bone_world.assign(skel.names.size(), Mat4{});
    if (!motion_locals(database, motion, time, locals)) {
        locals.clear();
        pose.bind_pose = true;
        return false;
    }
    for (std::size_t b = 0; b < locals.size(); ++b) {
        const int p = skel.parent[b];
        pose.bone_world[b] = (p >= 0 && std::size_t(p) < b) ? locals[b] * pose.bone_world[std::size_t(p)] : locals[b];
    }
    pose.bind_pose = false;
    return true;
}

} // namespace dw4::engine
