#include "dw4/animations.hpp"
#include "animation_fixture.hpp"
#include "dw4/assets.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
using namespace animation_fixture;
int checks{};
void check(bool value, const std::string& reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation, const std::string& reason) {
    bool caught = false; try { operation(); } catch (const AnimationError&) { caught = true; }
    check(caught,reason);
}

void synthetic() {
    Fixture f; const auto document=f.document();
    const auto infos=list_igb_animations(document,f.data);
    check(infos.size()==1 && infos[0].supported,"Valid original-style animation graph");
    check(infos[0].reference_index==f.clip && infos[0].name=="Synthetic clip" && infos[0].track_count==2,"Animation listing identity");
    const auto clip=decode_igb_animation(document,f.data,f.clip);
    check(clip.info.duration_ticks==100000001 && clip.info.priority==0 && clip.info.transition_count==0,
        "Preserve exact serialized duration and default animation metadata");
    auto metadata=f; metadata.scalar(metadata.clip,3,0xfffffffdu);
    metadata.scalar(metadata.clip,6,metadata.list(transition_list,{}));
    check(decode_igb_animation(metadata.document(),metadata.data,metadata.clip).info.priority==-3,
        "Preserve signed clip priority with an empty original transition definition list");
    metadata.scalar(metadata.clip,6,metadata.list(object_list,{}));
    rejects([&]{decode_igb_animation(metadata.document(),metadata.data,metadata.clip);},"Reject wrong transition list type");
    const auto pose=sample_animation(clip,0.05);
    check(pose.size()==2 && pose[0].translation.x==10 && pose[1].translation.y==0,"Translation mask selects constant versus sampled channels");
    check(pose[0].rotation[3]==1 && pose[1].rotation[3]==1,"Constant fixture rotation");
    auto bad=f; bad.scalar(bad.clip,10,null_ref);
    const auto constant=sample_animation(decode_igb_animation(bad.document(),bad.data,bad.clip),0);
    check(constant[1].translation.y==2,"Null translation mask means skeleton constants");
    auto rejected=[&](const Fixture& value,const std::string& reason) {
        const auto d=value.document();
        rejects([&]{ decode_igb_animation(d,value.data,value.clip); },reason);
    };
    bad=f; bad.scalar(bad.binding,2,bad.skin); rejected(bad,"Wrong binding skeleton");
    bad=f; bad.contents(bad.bindings_list,{bad.binding,bad.binding}); rejected(bad,"Ambiguous animation binding");
    bad=f; bad.scalar(bad.binding,5,bad.skin); rejected(bad,"Unsupported additional binding state");
    bad=f; bad.records[bad.mapping].data=words({1,0}); rejected(bad,"Unverified nonidentity mapping");
    bad=f; bad.records[bad.mapping].data.resize(4); rejected(bad,"Truncated binding map");
    bad=f; bad.scalar(bad.binding,4,1); rejected(bad,"Binding count mismatch");
    bad=f; bad.contents(bad.tracks_list,{bad.tracks[0]}); rejected(bad,"Incomplete bone tracks");
    bad=f; bad.text(bad.tracks[0],2,"Wrong bone"); rejected(bad,"Wrong named track");
    bad=f; bad.scalar(bad.transforms[1],2,0); rejected(bad,"Wrong channel ordinal");
    bad=f; bad.scalar(bad.transforms[1],3,bad.skin); rejected(bad,"Different compressed source");
    bad=f; bad.records[bad.source].values[3]=Bytes{1,1,1}; rejected(bad,"Unsupported quaternion interpolation");
    bad=f; bad.scalar(bad.source,4,1); rejected(bad,"Unsupported driven channels");
    bad=f; bad.scalar(bad.source,5,1); rejected(bad,"Unsupported play mode");
    bad=f; bad.records[bad.clip].values[7]=words({1,0}); rejected(bad,"Nonzero start offset");
    bad=f; bad.records[bad.clip].values[8]=words({0,1}); rejected(bad,"Nonzero high-word start time");
    bad=f; bad.records[bad.clip].values[9]=words({0,0}); rejected(bad,"Zero duration");
    bad=f; bad.records[bad.clip].values[9]=words({0,0x80000000}); rejected(bad,"Negative or excessive duration");
    bad=f; word(bad.records[bad.compressed].data,12,std::bit_cast<std::uint32_t>(0.5f)); rejected(bad,"Compressed duration mismatch");
    bad=f; bad.scalar(bad.mask,5,1); rejected(bad,"Mask bit count mismatch");
    bad=f; bad.scalar(bad.mask,3,2); rejected(bad,"Mask capacity mismatch");
    bad=f; bad.records[bad.mask_memory].data.resize(1); rejected(bad,"Truncated mask memory");
    bad=f; bad.records[bad.offsets].data.resize(12); rejected(bad,"Truncated constant translations");
    bad=f; word(bad.records[bad.offsets].data,0,0x7fc00000); rejected(bad,"Non-finite constant translation");
    bad=f; bad.contents(bad.skeletons_list,{bad.skeleton,bad.skeleton}); rejected(bad,"Ambiguous database skeleton");
    bad=f; bad.contents(bad.clips_list,{bad.clip,bad.clip}); rejected(bad,"Duplicate database clips");
    bad=f; bad.contents(bad.clips_list,{}); rejected(bad,"Unlisted clip selection");
    bad=f; bad.data.bones[0].reference_index=bad.bones[1]; rejected(bad,"Skin from different source skeleton");
    bad=f; bad.data.bones[0].parent=1; rejected(bad,"Mismatched source parent");
    bad=f; bad.scalar(bad.source,4,0);
    const auto unsupported=list_igb_animations(bad.document(),bad.data);
    check(unsupported.size()==1 && !unsupported[0].supported && !unsupported[0].diagnostic.empty(),"Unsupported clip has explicit listing diagnostic");
    bad=f; word(bad.records[bad.compressed].data,0,0);
    rejected(bad,"Invalid compressed stream is rejected at decode");
    rejects([&]{ sample_animation(clip,-0.1); },"Negative sample time");
    rejects([&]{ sample_animation(clip,1); },"Out-of-duration sample time");
    rejects([&]{ sample_animation(clip,std::numeric_limits<double>::quiet_NaN()); },"Non-finite sample time");
    auto invalid=clip; invalid.use_sample_translation.clear();
    rejects([&]{ sample_animation(invalid,0); },"Malformed owned clip counts");
    invalid=clip; invalid.constant_translations[0].x=std::numeric_limits<float>::infinity();
    rejects([&]{ sample_animation(invalid,0); },"Malformed owned constant translation");
    sample_animation(clip,0); sample_animation(clip,clip.info.duration_seconds);
    check(sample_animation(clip,0)[0].translation.x==10,"Random sampling preserves original owned data");
    auto posed_clip=clip; posed_clip.constant_translations[0]={};
    auto skin=f.data; SkinMatrix identity{}; identity[0]=identity[5]=identity[10]=identity[15]=1;
    skin.joints.push_back({1,-1,identity,identity,identity});
    skin.palette_to_joint={0}; skin.influences={{0,0,1}}; skin.source_positions={{1,0,0}};
    const auto posed=sample_animation_pose(posed_clip,skin,0);
    check(posed.positions.size()==1 && posed.positions[0].x==1 && posed.joint_positions.size()==1,"Original one-clip pose wrapper");
    AnimationSourceClock source(clip.channels.duration()); AnimationWrapContext timing;
    const auto timed=sample_animation_at_local_time(clip,2000000000,source,timing);
    check(timed.size()==2 && timed[0].translation.x==10,"Original integer-time sampling preserves selected translations");
    AnimationSourceClock posed_source(posed_clip.channels.duration());
    check(sample_animation_pose_at_local_time(posed_clip,skin,2000000000,posed_source,timing).positions[0].x==1,
        "Integer-time path reaches the actual skin pose evaluator");
    AnimationSourceClock wrong_source(1);
    rejects([&]{sample_animation_at_local_time(clip,0,wrong_source,timing);},"Reject timing for a different source duration");
    AnimationSourceClock failed_source(clip.channels.duration(),AnimationPlayMode::clamp);
    auto malformed=clip; malformed.bone_references.clear();
    rejects([&]{sample_animation_at_local_time(malformed,2000000000,failed_source,timing);},"Reject malformed timed clip");
    failed_source.set_mode(AnimationPlayMode::repeat);
    check(failed_source.sample_time(2000000000,timing)<clip.channels.duration(),"Failed pose must not commit source cache");
    rejects([&]{ sample_animation_pose(clip,skin,0); },"Reject animated/translated skipped root");
    auto invalid_skin=skin; invalid_skin.bones[0].joint_index=0;
    rejects([&]{ sample_animation_pose(posed_clip,invalid_skin,0); },"Original root must be a helper");
    invalid_skin=skin; invalid_skin.bones[1].parent=1;
    rejects([&]{ sample_animation_pose(posed_clip,invalid_skin,0); },"Original parent must precede child");
    invalid_skin=skin; invalid_skin.bones[1].joint_index=1;
    rejects([&]{ sample_animation_pose(posed_clip,invalid_skin,0); },"Original joints must follow traversal order");
    invalid_skin=skin; invalid_skin.info.skeleton_reference=99;
    rejects([&]{ sample_animation_pose(posed_clip,invalid_skin,0); },"Reject another pose skeleton");
    invalid_skin=skin; invalid_skin.bones[1].reference_index=99;
    rejects([&]{ sample_animation_pose(posed_clip,invalid_skin,0); },"Reject another pose bone");

    // Cross a word boundary: bone 30 uses a constant, while bits 31 and 32
    // select sampled zero translations from different words of the mask.
    auto wide=f;
    for (std::uint32_t i=2; i<33; ++i) {
        const auto bone=wide.add(bone_type); wide.text(bone,2,"Bone "+std::to_string(i));
        wide.scalar(bone,3,i-1); wide.scalar(bone,4,i-1); wide.bones.push_back(bone);
        wide.data.bones.push_back({bone,"Bone "+std::to_string(i),static_cast<std::int32_t>(i-1),static_cast<std::int32_t>(i-1)});
        const auto transform=wide.add(transform_type); wide.scalar(transform,2,i); wide.scalar(transform,3,wide.source);
        const auto track=wide.add(track_type); wide.text(track,2,"Bone "+std::to_string(i)); wide.scalar(track,3,transform);
        wide.tracks.push_back(track);
    }
    wide.contents(wide.bones_list,wide.bones); wide.contents(wide.tracks_list,wide.tracks); wide.scalar(wide.binding,4,33);
    Bytes mappings, translations;
    for (std::uint32_t i=0;i<33;++i) { push32(mappings,i); append(translations,floats({100.f+static_cast<float>(i),0,0})); }
    wide.records[wide.mapping].data=std::move(mappings); wide.records[wide.offsets].data=std::move(translations);
    wide.scalar(wide.mask,2,2); wide.scalar(wide.mask,3,2); wide.scalar(wide.mask,5,33);
    wide.records[wide.mask_memory].data=words({0x80000000u,1});
    Bytes stream(80); word(stream,0,0x002abf84); word(stream,4,33); word(stream,8,std::bit_cast<std::uint32_t>(0.5f));
    word(stream,12,std::bit_cast<std::uint32_t>(0.1f)); word(stream,16,30);
    const std::uint32_t lengths[]{58,33,0,0,0,0,0,0,0,1,0,2,0,33};
    for (std::size_t i=0;i<14;++i) word(stream,20+4*i,lengths[i]);
    Bytes initial_bits(58);
    for (std::size_t i=0;i<33;++i) { const auto bit=i*14+6; initial_bits[bit/8]|=static_cast<std::uint8_t>(1u<<(6-bit%8)); }
    append(stream,Bytes{255,255}); append(stream,initial_bits); append(stream,Bytes(33,2));
    append(stream,Bytes{0x80}); append(stream,Bytes(33,0)); wide.records[wide.compressed].data=std::move(stream);
    const auto boundary=sample_animation(decode_igb_animation(wide.document(),wide.data,wide.clip),0.05);
    check(boundary.size()==33 && boundary[30].translation.x==130,"Unset bit before word boundary selects constant");
    check(boundary[31].translation.x==0 && boundary[32].translation.x==0,"Mask high bit and next word low bit select sampled channels");
}

void layers() {
    std::vector<EnbayaTransform> a(2), b(2);
    a[0].translation={8,-4,2}; b[0].translation={-8,12,6};
    a[1].rotation={0,0,0,2}; b[1].rotation={0,0,0,-2};
    auto result=blend_animation_samples(a,b,0.25f);
    check(result.size()==2 && result[0].translation.x==4 && result[0].translation.y==0 && result[0].translation.z==3,
        "Ordered layer translation weights");
    check(result[1].rotation[3]==2,"Blending must preserve original sign selection without normalization");
    check(a[0].translation.x==8 && b[0].translation.x==-8,"Layer mixing mutated input channels");
    check(blend_animation_samples(b,a,0.25f)[0].translation.x==-4,"Reversing ordered layers must change the result");
    a[0].translation.x=1e10f; b[0].translation.x=-1e10f;
    const auto cancellation=blend_animation_samples(a,b,std::nextafter(0.5f,1.0f));
    check(std::bit_cast<std::uint32_t>(cancellation[0].translation.x)==0xc48a817c,
        "Original fused translation rounding was replaced by a separately rounded sum");
    rejects([&]{blend_animation_samples({},b,0);},"Reject absent base layer");
    rejects([&]{blend_animation_samples(a,{b[0]},0);},"Reject mismatched layer bone counts");
    rejects([&]{blend_animation_samples(a,b,-0.1f);},"Reject negative blend weight at pose boundary");
    rejects([&]{blend_animation_samples(a,b,std::numeric_limits<float>::quiet_NaN());},"Reject NaN blend weight at pose boundary");
    b[0].rotation[0]=std::numeric_limits<float>::infinity();
    rejects([&]{blend_animation_samples(a,b,0);},"Weight zero does not skip invalid incoming rotation");
    b[0].rotation={0,0,0,1}; b[0].translation.x=std::numeric_limits<float>::infinity();
    rejects([&]{blend_animation_samples(a,b,1);},"Reject non-finite layer translation");
}

int run(const std::vector<std::filesystem::path>& args) {
    try {
        synthetic(); layers();
        if (args.size()==4) {
            const auto doc=read_igb_document(assets::AfsArchive::open(args[1]).read(std::stoul(args[2].string())));
            const auto skin=decode_igb_skinning(doc,static_cast<std::uint32_t>(std::stoul(args[3].string())));
            const auto infos=list_igb_animations(doc,skin); std::size_t decoded{}, samples{};
            for (const auto& info:infos) {
                check(info.supported,"Required original clip graph profile: "+info.name+" / "+info.diagnostic);
                const auto clip=decode_igb_animation(doc,skin,info.reference_index);
                for (const double fraction:{0.0,0.25,0.75,1.0}) {
                    const auto values=sample_animation(clip,info.duration_seconds*fraction);
                    check(values.size()==skin.bones.size(),"Original sampled bone count"); samples+=values.size();
                    const auto pose=sample_animation_pose(clip,skin,info.duration_seconds*fraction);
                    check(pose.positions.size()==skin.source_positions.size() && pose.joint_positions.size()==skin.joints.size(),"Original evaluated pose counts");
                }
                AnimationWrapContext timing; timing.initialize_action_offset();
                for (const auto mode:{AnimationPlayMode::repeat,AnimationPlayMode::clamp,AnimationPlayMode::bounce,
                        AnimationPlayMode::loop,AnimationPlayMode::offset_clamp}) {
                    AnimationSourceClock source(clip.channels.duration(),mode);
                    for (const double fraction:{0.0,0.25,0.75,1.0,1.25,2.75}) {
                        const auto ticks=static_cast<std::int64_t>(info.duration_seconds*fraction*1000000000.0);
                        const auto timed=sample_animation_at_local_time(clip,ticks,source,timing);
                        const auto pose=sample_animation_pose_at_local_time(clip,skin,ticks,source,timing);
                        check(timed.size()==skin.bones.size() && pose.positions.size()==skin.source_positions.size(),
                            "Original timed clip sampling and pose counts");
                        const auto mixed=blend_animation_samples(timed,timed,0.5f);
                        check(mixed.size()==timed.size() && std::isfinite(mixed.back().rotation[3]),"Original timed layer evaluation");
                    }
                }
                ++decoded;
                std::cout<<info.name<<" reference="<<info.reference_index<<" duration="<<info.duration_seconds<<" tracks="<<info.track_count<<'\n';
            }
            check(decoded>0,"Original database must contain clips");
            std::cout<<"Original clips="<<decoded<<" sampled transforms="<<samples<<'\n';
        } else if (args.size()!=1) throw std::runtime_error("Usage: dw4_animations_tests [archive.afs entry geometry-reference]");
        std::cout<<checks<<" animation graph checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Animation test failure: "<<error.what()<<'\n'; return 1; }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
