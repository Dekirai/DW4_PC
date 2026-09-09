#include "dw4/animation_combiner.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) { throw AnimationError(reason); }
constexpr std::size_t max_states=1024, max_samples=4u*1024u*1024u;
void validate_matrices(const std::vector<AnimationMatrix>& matrices, std::size_t bones) {
    if (matrices.size()!=bones) fail("Animation combiner requires one cached matrix per bound bone.");
    for (const auto& matrix:matrices) for (auto value:matrix)
        if (!std::isfinite(value)) fail("Animation combiner matrix is non-finite.");
}
}

AnimationSourceRuntime::AnimationSourceRuntime(std::string source, std::vector<AnimationClip> clips) {
    if (source.empty() || source.size()>4096 || clips.empty() || clips.size()>4096)
        fail("Animation runtime requires an explicit source and bounded clip library.");
    auto library=std::make_shared<Library>(); library->source=std::move(source); library->clips=std::move(clips);
    const auto& first=library->clips.front(); std::size_t samples=0;
    for (std::size_t i=0;i<library->clips.size();++i) {
        const auto& clip=library->clips[i];
        if (!clip.info.supported || clip.info.skeleton_reference!=first.info.skeleton_reference ||
            clip.bone_references!=first.bone_references || clip.bone_references.empty() || clip.bone_references.size()>1024)
            fail("Animation library clips must share the complete validated skeleton binding.");
        if (!library->indices.emplace(clip.info.reference_index,i).second) fail("Duplicate animation library reference.");
        if (clip.info.duration_ticks<=0 || clip.info.duration_ticks>600000000000ll ||
            clip.info.duration_seconds!=static_cast<double>(clip.info.duration_ticks)/1e9)
            fail("Animation library has inconsistent serialized duration.");
        const auto count=clip.channels.frame_count()*clip.channels.track_count();
        if (count>max_samples-samples) fail("Animation library decoded sample budget exceeded.");
        samples+=count;
        sample_animation(clip,0); // Validate the owned graph/channel arrays before publication.
        auto [entry,inserted]=library->sources.emplace(clip.info.source_reference,clocks_.size());
        if (inserted) clocks_.emplace_back(clip.channels.duration());
        else if (clocks_[entry->second].duration()!=clip.channels.duration())
            fail("Shared animation source has inconsistent duration.");
    }
    library_=std::move(library);
}
const std::string& AnimationSourceRuntime::source() const noexcept { return library_->source; }
const AnimationClip& AnimationSourceRuntime::clip(std::uint32_t reference) const {
    const auto it=library_->indices.find(reference);
    if (it==library_->indices.end()) fail("Animation reference is outside the owning library.");
    return library_->clips[it->second];
}
void AnimationSourceRuntime::set_mode(std::uint32_t reference, AnimationPlayMode mode) {
    clocks_[library_->sources.at(clip(reference).info.source_reference)].set_mode(mode);
}
AnimationPlayMode AnimationSourceRuntime::mode(std::uint32_t reference) const {
    return clocks_[library_->sources.at(clip(reference).info.source_reference)].mode();
}
std::vector<EnbayaTransform> AnimationSourceRuntime::sample(std::uint32_t reference, std::int64_t time) {
    const auto& value=clip(reference);
    return sample_animation_at_local_time(value,time,clocks_[library_->sources.at(value.info.source_reference)],wrapping_);
}

AnimationCombiner::AnimationCombiner(const AnimationSourceRuntime& sources, std::vector<AnimationMatrix> cached_matrices)
    : library_(sources.library_), matrices_(std::move(cached_matrices)) {
    validate_matrices(matrices_,library_->clips.front().bone_references.size());
}
AnimationCombiner decode_animation_combiner(const IgbDocument& doc, const AnimationSourceRuntime& sources, std::uint32_t reference) {
    try {
        if (doc.metadata.header.byte_order!=IgbByteOrder::little) fail("Combiner byte order is outside the verified profile.");
        const auto& object=doc.object(reference); const auto& clip=sources.clips().front();
        if (!doc.is_a(object,"igAnimationCombiner") || doc.field_reference(object,3)!=clip.info.skeleton_reference)
            fail("Combiner does not select the animation library skeleton.");
        const auto ref=[&](const IgbObject& value,std::uint16_t slot) {
            auto result=doc.field_reference(value,slot); if (!result) fail("Required combiner reference is null."); return *result;
        };
        const auto word=[](std::span<const std::uint8_t> bytes,std::size_t offset) {
            if (offset>bytes.size() || bytes.size()-offset<4) fail("Truncated combiner word.");
            return std::uint32_t(bytes[offset])|(std::uint32_t(bytes[offset+1])<<8)|
                (std::uint32_t(bytes[offset+2])<<16)|(std::uint32_t(bytes[offset+3])<<24);
        };
        const auto time=[&](std::uint16_t slot) {
            auto field=doc.field(object,slot); if (!field || field->size!=8) fail("Invalid combiner time field.");
            auto bytes=doc.field_bytes(*field); return std::uint64_t(word(bytes,0))|(std::uint64_t(word(bytes,4))<<32);
        };
        const auto list=[&](std::uint32_t value,const char* type) {
            if (!doc.is_a(doc.object(value),type)) fail("Unexpected combiner list type.");
            return doc.object_list(value);
        };
        if (!list(ref(object,6),"igAnimationStateList").empty()) fail("Serialized active combiner states require full state restoration.");
        const auto lists=list(ref(object,4),"igAnimationCombinerBoneInfoListList");
        if (lists.size()!=clip.bone_references.size()) fail("Combiner track-list count differs from the bound skeleton.");
        for (auto entry:lists) if (!entry || !list(*entry,"igAnimationCombinerBoneInfoList").empty()) fail("Serialized combiner tracks must be empty before activation.");
        const auto& bases=doc.object(ref(object,5));
        if (!doc.is_a(bases,"igIntList")) fail("Unexpected combiner base-index type.");
        if (doc.field_u32(bases,2)!=lists.size() || doc.field_u32(bases,3)<lists.size()) fail("Combiner base-index count mismatch.");
        const auto base_bytes=doc.memory(ref(bases,4));
        if (base_bytes.size()<lists.size()*4) fail("Truncated combiner base indices.");
        for (std::size_t i=0;i<lists.size();++i) if (word(base_bytes,4*i)!=0) fail("Empty combiner has a nonzero base index.");
        const auto bytes=doc.memory(ref(object,8));
        if (bytes.size()!=lists.size()*64) fail("Combiner cache requires one float matrix per bone.");
        std::vector<AnimationMatrix> matrices(lists.size());
        for (std::size_t bone=0;bone<matrices.size();++bone) for (std::size_t i=0;i<16;++i)
            matrices[bone][i]=std::bit_cast<float>(word(bytes,bone*64+i*4));
        AnimationCombiner result(sources,std::move(matrices));
        const auto valid=doc.field(object,10);
        if (!valid || valid->size!=1 || doc.field_bytes(*valid)[0]>1) fail("Invalid combiner cache-valid byte.");
        result.cache_valid_=doc.field_bytes(*valid)[0]!=0; result.cached_time_=time(9); result.clock_override_=time(14);
        return result;
    } catch (const AnimationError&) { throw; }
      catch (const std::exception& error) { throw AnimationError(error.what()); }
}
void AnimationCombiner::require_sources(const AnimationSourceRuntime& sources) const {
    if (library_!=sources.library_) fail("Animation combiner and source runtime belong to different libraries.");
}
std::vector<std::size_t> AnimationCombiner::order() const {
    std::vector<std::size_t> result(states_.size()); std::iota(result.begin(),result.end(),0);
    std::stable_sort(result.begin(),result.end(),[&](auto a,auto b){return states_[a].priority<states_[b].priority;});
    return result;
}
void AnimationCombiner::add(const AnimationSourceRuntime& sources, std::uint32_t ref, std::uint64_t time, bool blend) {
    require_sources(sources); const auto& clip=sources.clip(ref);
    if (states_.size()>=max_states || next_id_==std::numeric_limits<std::uint64_t>::max())
        fail("Animation combiner state budget exceeded.");
    AnimationCombinerState state;
    state.id=next_id_; state.clip_reference=ref; state.priority=clip.info.priority;
    state.combine_mode=blend && !states_.empty() ? 1 : 0;
    state.transition_mode=blend ? 1 : 4; state.status=blend ? 1 : 0;
    state.blend.start_time=0; state.time_requested=time;
    state.channels.resize(matrices_.size());
    for (std::size_t i=0;i<state.channels.size();++i) {
        state.channels[i].rotation={}; // Original recycled track defaults.
        if (!clip.use_sample_translation[i]) state.channels[i].translation=clip.constant_translations[i];
    }
    states_.push_back(std::move(state)); ++next_id_; cache_valid_=false;
}
void AnimationCombiner::add_base(const AnimationSourceRuntime& sources, std::uint32_t reference, std::uint64_t time) {
    add(sources,reference,time,false);
}
void AnimationCombiner::append_blend(const AnimationSourceRuntime& sources, std::uint32_t reference,
    AnimationBlendWindow blend, std::uint64_t time, std::int64_t start) {
    if (!std::isfinite(blend.start_ratio) || !std::isfinite(blend.ratio_range)) fail("Non-finite animation blend fields.");
    add(sources,reference,time,true);
    blend.start_time=1; states_.back().blend=blend; states_.back().animation_start_time=start;
}
void AnimationCombiner::snapshot() {
    if (next_id_==std::numeric_limits<std::uint64_t>::max()) fail("Animation state identity budget exceeded.");
    AnimationCombinerState state; state.id=next_id_; state.status=1; state.transition_mode=4; state.blend.start_time=0;
    state.channels.reserve(matrices_.size());
    try {
        for (const auto& matrix:matrices_) state.channels.push_back({original_matrix_rotation(matrix),{matrix[12],matrix[13],matrix[14]}});
    } catch (const AnimationMathError& error) { throw AnimationError(error.what()); }
    std::vector<AnimationCombinerState> replacement; replacement.push_back(std::move(state));
    states_=std::move(replacement); ++next_id_; cache_valid_=false;
}
std::size_t AnimationCombiner::update_impl(AnimationSourceRuntime& sources, std::uint64_t time) {
    require_sources(sources); const auto count=states_.size();
    for (auto& state:states_) if (state.status==0 || state.is_new) advance_animation_clock(state.clock,sources.state_clock_,time);
    const auto ordered=order();
    for (std::size_t index=0;index<states_.size();++index) {
        auto& state=states_[index];
        if (state.combine_mode==0 || state.status==3) continue;
        if (state.is_new) {
            if (state.blend.start_time==0) {
                const auto& clip=sources.clip(*state.clip_reference);
                if (clip.info.transition_count) fail("Named animation transition resolution is not implemented.");
                state.animation_start_time=0; state.blend.duration=clip.info.duration_ticks/3;
                state.blend.start_ratio=0; state.blend.ratio_range=1;
                begin_animation_blend(state.clock,state.blend,sources.state_clock_,time,0);
                state.status=0;
                continue; // Original missing-transition fallback skips weight update this call.
            }
            begin_animation_blend(state.clock,state.blend,sources.state_clock_,time,state.animation_start_time);
            state.status=0;
        }
        if (state.status!=0) continue;
        state.weight=animation_blend_ratio(state.blend,time);
        if (state.weight>=1) {
            // 801CFEA8 visits tracks in priority order, not state creation order.
            for (auto earlier:ordered) { if (earlier==index) break; states_[earlier].status=3; }
            state.combine_mode=0; cache_valid_=false;
        }
    }
    const auto removed=std::erase_if(states_,[](const auto& state){return state.status==3;});
    if (removed) { cache_valid_=false; if (states_.size()==1) states_.front().combine_mode=0; }
    return count;
}
std::size_t AnimationCombiner::update(AnimationSourceRuntime& sources, std::uint64_t time) {
    auto candidate=*this; auto candidate_sources=sources;
    const auto count=candidate.update_impl(candidate_sources,time);
    *this=std::move(candidate); sources=std::move(candidate_sources); return count;
}
void AnimationCombiner::evaluate_impl(AnimationSourceRuntime& sources, std::uint64_t time) {
    require_sources(sources);
    if (cache_valid_ && cached_time_==time) return;
    if (!update_impl(sources,time)) return;
    if (states_.size()==1) {
        auto& state=states_.front();
        if (state.is_new || state.status==0) {
            if (state.clip_reference) state.channels=sources.sample(*state.clip_reference,state.clock.local_time);
            for (std::size_t bone=0;bone<matrices_.size();++bone) {
                const auto& value=state.channels[bone]; original_rotation_matrix(value.rotation,matrices_[bone]);
                matrices_[bone][12]=value.translation.x; matrices_[bone][13]=value.translation.y; matrices_[bone][14]=value.translation.z;
            }
        }
        state.is_new=false;
    } else {
        const auto ordered=order(); std::size_t first=0;
        for (std::size_t i=0;i<ordered.size();++i) if (states_[ordered[i]].combine_mode==0) first=i;
        for (std::size_t bone=0;bone<matrices_.size();++bone) {
            auto& base=states_[ordered[first]]; base.is_new=false;
            if (base.clip_reference && base.status==0) base.channels[bone]=sources.sample(*base.clip_reference,base.clock.local_time)[bone];
            auto value=base.channels[bone];
            for (std::size_t i=first+1;i<ordered.size();++i) {
                auto& state=states_[ordered[i]];
                if (state.clip_reference && (state.status==0 || (state.is_new && state.status==1)))
                    state.channels[bone]=sources.sample(*state.clip_reference,state.clock.local_time)[bone];
                value=blend_animation_samples({value},{state.channels[bone]},state.weight)[0];
                state.is_new=false;
            }
            original_rotation_matrix(value.rotation,matrices_[bone]);
            matrices_[bone][12]=value.translation.x; matrices_[bone][13]=value.translation.y; matrices_[bone][14]=value.translation.z;
        }
    }
    cached_time_=time; cache_valid_=true;
}
const std::vector<AnimationMatrix>& AnimationCombiner::evaluate(AnimationSourceRuntime& sources, std::uint64_t time) {
    auto candidate=*this; auto candidate_sources=sources;
    try { candidate.evaluate_impl(candidate_sources,time); }
    catch (const AnimationMathError& error) { throw AnimationError(error.what()); }
    *this=std::move(candidate); sources=std::move(candidate_sources); return matrices_;
}
SkinPose AnimationCombiner::pose(AnimationSourceRuntime& sources, const SkinningData& skin, std::uint64_t time) {
    require_sources(sources); const auto& clip=library_->clips.front();
    if (!skin.info.skeleton_reference || *skin.info.skeleton_reference!=clip.info.skeleton_reference || skin.bones.size()!=matrices_.size())
        fail("Animation combiner pose requires its bound skeleton.");
    std::int32_t joint=0;
    for (std::size_t i=0;i<skin.bones.size();++i) {
        const auto& bone=skin.bones[i];
        if (bone.reference_index!=clip.bone_references[i] || (i==0 && (bone.parent!=-1 || bone.joint_index!=-1)) ||
            (i>0 && (bone.parent<0 || static_cast<std::size_t>(bone.parent)>=i)) ||
            (bone.joint_index!=-1 && bone.joint_index!=joint++)) fail("Unsupported original combiner skeleton hierarchy.");
    }
    if (static_cast<std::size_t>(joint)!=skin.joints.size()) fail("Animation combiner joint count mismatch.");
    auto candidate=*this; auto candidate_sources=sources;
    const auto& matrices=candidate.evaluate(candidate_sources,time);
    for (std::size_t i=0;i<16;++i) if (std::abs(matrices[0][i]-((i%5)==0 ? 1.0f : 0.0f))>1e-6f)
        fail("Animated skipped root remains outside the original pose profile.");
    std::vector<SkinMatrix> locals(matrices.size());
    for (std::size_t i=0;i<matrices.size();++i) std::copy(matrices[i].begin(),matrices[i].end(),locals[i].begin());
    try {
        auto result=evaluate_bone_transforms(skin,locals);
        *this=std::move(candidate); sources=std::move(candidate_sources); return result;
    } catch (const SkinningError& error) { throw AnimationError(error.what()); }
}
void AnimationCombiner::clear_new_flags() noexcept { for (auto& state:states_) state.is_new=false; }
void AnimationCombiner::set_state_speed(std::size_t index, std::uint64_t time, float speed) {
    if (index>=states_.size()) fail("Animation speed target is outside the state list.");
    change_animation_speed(states_[index].clock,time,speed);
}
std::int64_t AnimationCombiner::advance_state_clock(AnimationSourceRuntime& sources, std::size_t index, std::uint64_t time) {
    require_sources(sources);
    if (index>=states_.size()) fail("Animation clock target is outside the state list.");
    return advance_animation_clock(states_[index].clock,sources.state_clock_,time);
}

void apply_animation_motion(AnimationCombiner& combiner, AnimationSourceRuntime& sources, AnimationMotionWork& work,
    const AnimationMotionCommand& command, std::uint64_t system_time, std::uint64_t node_time) {
    if (command.interpolation_mode.size()<2 || command.loop_mode.empty()) fail("MOTIONSET requires its authored mode characters.");
    auto next=combiner; auto next_sources=sources; auto next_work=work;
    const auto blend=prepare_animation_blend(command.blend_frames,next_work.no_interpolation);
    if (next_work.no_interpolation || command.blend_frames==0) next_work.no_interpolation=false;
    if (next.states().size()>=2) next.snapshot();
    next.append_blend(next_sources,command.animation_reference,blend,next.request_time(system_time));
    next.update(next_sources,node_time);
    if (command.interpolation_mode[0]=='S') next.set_state_speed(0,system_time,0);
    if (command.interpolation_mode[1]=='S') next.set_state_speed(next.states().size()-1,system_time,0);
    // The verified graph has one shared Enbaya source; original exits after the first such track.
    next_sources.set_mode(command.animation_reference,command.loop_mode[0]=='L' ? AnimationPlayMode::repeat : AnimationPlayMode::offset_clamp);
    next_work.state=1; next_work.is_motion=true;
    combiner=std::move(next); sources=std::move(next_sources); work=next_work;
}
void advance_animation_motion(AnimationCombiner& combiner, AnimationSourceRuntime& sources, AnimationMotionWork& work,
    std::uint64_t time, float speed) {
    if (work.state==1) { work.state=2; return; }
    if (work.state!=2) return;
    auto next=combiner; auto next_sources=sources; auto next_work=work;
    if (next.states().size()!=1) { next.update(next_sources,time); next.clear_new_flags(); }
    if (next.states().size()==1) {
        if (next.states()[0].clock.time_scale!=speed) next.set_state_speed(0,time,speed);
        next_work.state=0;
    }
    combiner=std::move(next); sources=std::move(next_sources); work=next_work;
}
} // namespace dw4
