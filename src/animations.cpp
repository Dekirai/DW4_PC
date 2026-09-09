#include "dw4/animations.hpp"
#include "dw4/animation_math.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <span>

namespace dw4 {
namespace {
constexpr std::size_t max_bones = 1024;
constexpr std::size_t max_clips = 4096;
constexpr std::size_t max_links = 4u * 1024u * 1024u;
[[noreturn]] void fail(const std::string& reason) { throw AnimationError("Animation: " + reason); }

std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) fail("truncated little-endian word");
    return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1]) << 8) |
        (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
}
std::uint32_t reference(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    const auto value = doc.field_reference(object,slot);
    if (!value) fail("required local reference is null");
    return *value;
}
std::span<const std::uint8_t> field(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot, std::size_t size) {
    const auto* value = doc.field(object,slot);
    if (!value) fail("required serialized field is missing");
    const auto bytes = doc.field_bytes(*value);
    if (bytes.size() != size) fail("serialized field has an unexpected size");
    return bytes;
}
std::uint64_t ticks(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    const auto bytes = field(doc,object,slot,8);
    return word(bytes,0) | (std::uint64_t(word(bytes,4)) << 32);
}
bool finite(Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

class Context {
public:
    const IgbDocument& doc;
    const SkinningData& skin;
    std::vector<std::uint32_t> animations;
    std::vector<Vec3> offsets;

    Context(const IgbDocument& document, const SkinningData& data) : doc(document), skin(data) {
        if (doc.metadata.header.byte_order != IgbByteOrder::little) fail("big-endian animation streams are unverified");
        if (!skin.info.supported || !skin.info.database_reference || !skin.info.skeleton_reference || !skin.info.skin_reference)
            fail("a decoded skin with an explicit database and skeleton is required");
        if (skin.bones.empty() || skin.bones.size() > max_bones) fail("bone count is outside 1..1,024");
        const auto& database = typed(*skin.info.database_reference,"igAnimationDatabase");
        const auto skins = list(reference(doc,database,7),"igSkinList",max_links);
        if (std::count(skins.begin(),skins.end(),*skin.info.skin_reference) != 1)
            fail("database does not uniquely include the selected skin");
        typed(*skin.info.skin_reference,"igSkin");
        const auto skeletons = list(reference(doc,database,5),"igSkeletonList",max_bones);
        if (skeletons.size() != 1 || skeletons[0] != *skin.info.skeleton_reference)
            fail("database does not uniquely select the skin skeleton");
        const auto& skeleton = typed(*skin.info.skeleton_reference,"igSkeleton");
        const auto bones = list(reference(doc,skeleton,4),"igSkeletonBoneInfoList",max_bones);
        if (bones.size() != skin.bones.size()) fail("skin bone count differs from its source skeleton");
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const auto& bone = typed(bones[i],"igSkeletonBoneInfo");
            if (bones[i] != skin.bones[i].reference_index || doc.field_string(bone,2) != skin.bones[i].name ||
                doc.field_i32(bone,3) != skin.bones[i].parent || doc.field_i32(bone,4) != skin.bones[i].joint_index)
                fail("skin bone identity differs from its source skeleton");
        }
        const auto bytes = doc.memory(reference(doc,skeleton,3));
        if (bytes.size() != bones.size() * 12) fail("constant translation array must have one float3 per bone");
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const Vec3 offset{std::bit_cast<float>(word(bytes,i*12)),std::bit_cast<float>(word(bytes,i*12+4)),
                std::bit_cast<float>(word(bytes,i*12+8))};
            if (!finite(offset)) fail("constant translation is non-finite");
            offsets.push_back(offset);
        }
        animations = list(reference(doc,database,6),"igAnimationList",max_clips);
        const std::set<std::uint32_t> unique(animations.begin(),animations.end());
        if (unique.size() != animations.size()) fail("database animation list contains duplicate references");
    }

    AnimationInfo inspect(std::uint32_t ref) {
        AnimationInfo info; info.reference_index = ref;
        try { validate(ref,info); }
        catch (const std::exception& error) { info.supported = false; info.diagnostic = error.what(); }
        return info;
    }
    std::vector<bool> translation_mask(const IgbObject& animation) {
        std::vector<bool> result(skin.bones.size());
        const auto mask_ref = doc.field_reference(animation,10);
        if (!mask_ref) return result;
        const auto& mask = typed(*mask_ref,"igBitMask");
        const auto count = doc.field_i32(mask,2), capacity = doc.field_i32(mask,3);
        const auto words = (skin.bones.size()+31)/32;
        if (doc.field_u32(mask,5) != skin.bones.size() || count != static_cast<std::int32_t>(words) || capacity != count)
            fail("translation bit mask count/capacity differs from the bone count");
        const auto bytes = doc.memory(reference(doc,mask,4));
        if (bytes.size() != words*4) fail("translation bit mask memory has an unexpected size");
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = (word(bytes,(i/32)*4) & (1u << (i%32))) != 0;
        return result;
    }
    void validate(std::uint32_t ref, AnimationInfo& info) {
        if (std::find(animations.begin(),animations.end(),ref) == animations.end()) fail("clip is outside the selected animation database");
        const auto& animation = typed(ref,"igAnimation");
        info.reference_index = ref; info.name = std::string(doc.field_string(animation,2));
        info.skeleton_reference = *skin.info.skeleton_reference;
        if (ticks(doc,animation,7) != 0 || ticks(doc,animation,8) != 0) fail("nonzero clip offsets/start times are outside the profile");
        const auto duration = ticks(doc,animation,9);
        if (duration == 0 || duration > 600ull * 1000000000ull) fail("duration is outside (0,600] seconds");
        info.duration_seconds = static_cast<double>(duration) / 1e9;
        info.duration_ticks = static_cast<std::int64_t>(duration);
        info.priority = doc.field_i32(animation,3);
        if (const auto transitions=doc.field_reference(animation,6))
            info.transition_count=static_cast<std::uint32_t>(list(*transitions,"igAnimationTransitionDefinitionList",max_links).size());
        const auto bindings = list(reference(doc,animation,4),"igAnimationBindingList",max_bones);
        if (bindings.size() != 1) fail("clip must have exactly one binding");
        const auto& binding = typed(bindings[0],"igAnimationBinding");
        if (doc.field_reference(binding,2) != skin.info.skeleton_reference) fail("clip binding selects a different skeleton");
        if (doc.field_reference(binding,5) || doc.field_reference(binding,6)) fail("additional binding state is outside the profile");
        const auto tracks = list(reference(doc,animation,5),"igAnimationTrackList",max_bones);
        if (tracks.size() != skin.bones.size() || doc.field_u32(binding,4) != tracks.size())
            fail("binding/track counts do not match the complete skeleton");
        const auto map = doc.memory(reference(doc,binding,3));
        if (map.size() != tracks.size()*4) fail("binding map size does not match the track count");
        std::optional<std::uint32_t> shared;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (word(map,i*4) != i) fail("nonidentity track/bone mappings are outside the verified profile");
            const auto& track = typed(tracks[i],"igAnimationTrack");
            if (doc.field_string(track,2) != skin.bones[i].name) fail("bound track name differs from its bone");
            const auto& source = typed(reference(doc,track,3),"igEnbayaTransformSource");
            if (doc.field_u32(source,2) != i) fail("transform source index differs from the bound track ordinal");
            const auto next = reference(doc,source,3);
            if (shared && *shared != next) fail("tracks do not share one compressed source");
            shared = next;
        }
        if (!shared) fail("clip has no transform source");
        const auto& source = typed(*shared,"igEnbayaAnimationSource");
        const auto interpolation = field(doc,source,3,3);
        if (interpolation[0] != 1 || interpolation[1] != 3 || interpolation[2] != 1 ||
            field(doc,source,4,1)[0] != 3 || doc.field_u32(source,5) != 0)
            fail("unsupported interpolation, driven-channel mask or play mode");
        const auto stream = doc.memory(reference(doc,source,2));
        if (stream.size() < 80 || word(stream,4) != tracks.size()) fail("compressed header track count is inconsistent");
        const auto seconds = std::bit_cast<float>(word(stream,12));
        if (!std::isfinite(seconds) || std::abs(static_cast<double>(seconds)-info.duration_seconds) > 1.1e-9)
            fail("compressed and serialized durations do not agree within nanosecond precision");
        translation_mask(animation);
        info.track_count = static_cast<std::uint32_t>(tracks.size()); info.source_reference = *shared;
        info.provenance = "Database " + std::to_string(*skin.info.database_reference) + " -> animation " + std::to_string(ref) +
            " -> binding " + std::to_string(bindings[0]) + " -> skeleton " + std::to_string(info.skeleton_reference) +
            " / Enbaya source " + std::to_string(*shared);
        info.supported = true; info.diagnostic.clear();
    }
private:
    std::size_t links{};
    const IgbObject& typed(std::uint32_t ref, const char* name) const {
        const auto& object = doc.object(ref);
        if (!doc.is_a(object,name)) fail(std::string("expected ") + name + " at a referenced object");
        return object;
    }
    std::vector<std::uint32_t> list(std::uint32_t ref, const char* name, std::size_t limit) {
        const auto& object = typed(ref,name);
        const auto count = doc.field_i32(object,2);
        if (count < 0 || static_cast<std::size_t>(count) > limit || static_cast<std::size_t>(count) > max_links-links)
            fail("animation list exceeds its bounded count");
        links += static_cast<std::size_t>(count);
        const auto values = doc.object_list(ref);
        std::vector<std::uint32_t> result; result.reserve(values.size());
        for (const auto value : values) { if (!value) fail("animation graph list contains a null object"); result.push_back(*value); }
        return result;
    }
};
} // namespace

std::vector<AnimationInfo> list_igb_animations(const IgbDocument& document, const SkinningData& skin) {
    try {
        Context context(document,skin); std::vector<AnimationInfo> result;
        for (const auto ref : context.animations) result.push_back(context.inspect(ref));
        return result;
    } catch (const IgbError& error) { throw AnimationError(error.what()); }
}

AnimationClip decode_igb_animation(const IgbDocument& document, const SkinningData& skin, std::uint32_t ref) {
    try {
        Context context(document,skin); AnimationInfo info; context.validate(ref,info);
        const auto& source = document.object(info.source_reference);
        auto channels = decode_enbaya(document.memory(reference(document,source,2)));
        if (channels.track_count() != info.track_count ||
            std::abs(static_cast<double>(channels.duration())-info.duration_seconds) > 1.1e-9)
            fail("decoded channel counts or duration differ from the binding");
        AnimationClip result{std::move(info),std::move(channels),{},std::move(context.offsets),
            context.translation_mask(document.object(ref))};
        for (const auto& bone : skin.bones) result.bone_references.push_back(bone.reference_index);
        return result;
    } catch (const IgbError& error) { throw AnimationError(error.what()); }
      catch (const EnbayaError& error) { throw AnimationError(error.what()); }
}

std::vector<EnbayaTransform> sample_animation(const AnimationClip& clip, double time_seconds) {
    const auto count = clip.channels.track_count();
    if (!clip.info.supported || count == 0 || count > max_bones || count != clip.info.track_count ||
        clip.bone_references.size() != count || clip.constant_translations.size() != count || clip.use_sample_translation.size() != count)
        fail("owned animation counts are inconsistent");
    if (!std::isfinite(clip.info.duration_seconds) || clip.info.duration_seconds <= 0 ||
        std::abs(static_cast<double>(clip.channels.duration())-clip.info.duration_seconds) > 1.1e-9)
        fail("owned animation duration is inconsistent");
    if (!std::isfinite(time_seconds) || time_seconds < 0 || time_seconds > clip.info.duration_seconds)
        fail("sample time must be finite and within the clip duration");
    std::vector<EnbayaTransform> result;
    try { result = sample_enbaya_original(clip.channels,static_cast<float>(time_seconds)); }
    catch (const EnbayaError& error) { throw AnimationError(error.what()); }
    if (result.size() != count) fail("sampled transform count differs from the binding");
    for (std::size_t i = 0; i < count; ++i) {
        if (!finite(clip.constant_translations[i])) fail("owned constant translation is non-finite");
        if (!clip.use_sample_translation[i]) result[i].translation = clip.constant_translations[i];
    }
    return result;
}

SkinPose sample_animation_pose(const AnimationClip& clip, const SkinningData& skin, double time_seconds) {
    try {
        if (!skin.info.skeleton_reference || *skin.info.skeleton_reference != clip.info.skeleton_reference ||
            skin.bones.size() != clip.bone_references.size() || skin.bones.empty())
            fail("animation pose requires the same bound skeleton");
        if (skin.bones[0].parent != -1 || skin.bones[0].joint_index != -1)
            fail("original pose profile requires an unparented helper at bone zero");
        std::int32_t next_joint = 0;
        for (std::size_t i = 0; i < skin.bones.size(); ++i) {
            const auto& bone = skin.bones[i];
            if (bone.reference_index != clip.bone_references[i]) fail("animation pose bone identity is inconsistent");
            if (i > 0 && (bone.parent < 0 || static_cast<std::size_t>(bone.parent) >= i))
                fail("original pose profile requires parents before children");
            if (bone.joint_index != -1 && bone.joint_index != next_joint++)
                fail("original pose profile requires joint indices in traversal order");
        }
        if (static_cast<std::size_t>(next_joint) != skin.joints.size()) fail("original pose joint count is inconsistent");
        const auto values = sample_animation(clip,time_seconds);
        std::vector<SkinMatrix> locals; locals.reserve(values.size());
        for (const auto& value : values) locals.push_back(original_bone_transform(value.rotation,value.translation));
        for (std::size_t i = 0; i < 16; ++i) {
            const double identity = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0 : 0.0;
            if (std::abs(locals[0][i]-identity) > 1e-6) fail("bone zero is not identity; original skipped-root behavior is unsupported");
        }
        return evaluate_bone_transforms(skin,locals);
    } catch (const SkinningError& error) { throw AnimationError(error.what()); }
      catch (const EnbayaError& error) { throw AnimationError(error.what()); }
}

namespace {
double checked_source_time(const AnimationClip& clip, std::int64_t local_time,
    AnimationSourceClock& source, AnimationWrapContext& context) {
    if (source.duration() != clip.channels.duration())
        fail("source clock duration does not match the decoded clip");
    const double seconds = source.sample_time(local_time, context);
    if (seconds > clip.info.duration_seconds + 1.1e-9)
        fail("source timing exceeds the decoded clip; short-source terminal sampling is unverified");
    // Serialized nanoseconds and the Enbaya duration agree within 1.1 ns.
    return std::min(seconds, clip.info.duration_seconds);
}
} // namespace
std::vector<EnbayaTransform> sample_animation_at_local_time(const AnimationClip& clip,
    std::int64_t local_time, AnimationSourceClock& source, AnimationWrapContext& context) {
    auto candidate_source = source; auto candidate_context = context;
    auto result = sample_animation(clip, checked_source_time(clip, local_time, candidate_source, candidate_context));
    source = candidate_source; context = candidate_context;
    return result;
}
SkinPose sample_animation_pose_at_local_time(const AnimationClip& clip, const SkinningData& skin,
    std::int64_t local_time, AnimationSourceClock& source, AnimationWrapContext& context) {
    auto candidate_source = source; auto candidate_context = context;
    auto result = sample_animation_pose(clip, skin, checked_source_time(clip, local_time, candidate_source, candidate_context));
    source = candidate_source; context = candidate_context;
    return result;
}
std::vector<EnbayaTransform> blend_animation_samples(const std::vector<EnbayaTransform>& base,
    const std::vector<EnbayaTransform>& next, float weight) {
    if (base.empty() || base.size() > max_bones || base.size() != next.size())
        fail("animation layer bone counts are inconsistent");
    if (!std::isfinite(weight) || weight < 0 || weight > 1)
        fail("animation layer weight must be finite and within [0,1]");
    const float inverse = 1.0f - weight;
    std::vector<EnbayaTransform> result; result.reserve(base.size());
    for (std::size_t i = 0; i < base.size(); ++i) {
        if (!finite(base[i].translation) || !finite(next[i].translation))
            fail("animation layer translation is non-finite");
        EnbayaTransform value;
        try { value.rotation = original_interpolate_rotation(base[i].rotation, next[i].rotation, weight); }
        catch (const AnimationMathError& error) { throw AnimationError(error.what()); }
        const auto component = [&](float a, float b) {
            const float weighted = weight * b;
            const float mixed = std::fma(a, inverse, weighted);
            if (!std::isfinite(mixed)) fail("animation layer translation overflow");
            return mixed;
        };
        value.translation = {component(base[i].translation.x, next[i].translation.x),
            component(base[i].translation.y, next[i].translation.y), component(base[i].translation.z, next[i].translation.z)};
        result.push_back(value);
    }
    return result;
}
} // namespace dw4
