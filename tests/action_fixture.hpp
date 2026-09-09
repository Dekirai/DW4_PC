#pragma once

#include "animation_fixture.hpp"
#include "dw4/actions.hpp"

namespace action_fixture {
using namespace animation_fixture;
inline constexpr const char* source_name="synthetic/actions.igb";
struct Fixture : animation_fixture::Fixture {
    std::uint32_t info, actions_list, rams_list, combiners_list;
    std::vector<std::uint32_t> actions, rams, nodes, combiners;
    Fixture() {
        SkinMatrix identity{}; identity[0]=identity[5]=identity[10]=identity[15]=1;
        data.joints.push_back({1,-1,identity,identity,identity});
        data.palette_to_joint={0}; data.influences={{0,0,1}}; data.source_positions={{1,0,0}};
        word(records[offsets].data,0,0);
        info=add(action_info); text(info,2,"actor"); scalar(info,8,database);
        actions_list=list(action_data_list,{}); scalar(info,9,actions_list);
        for (int i=0;i<4;++i) {
            const auto node=add(action_node); scalar(node,13,std::bit_cast<std::uint32_t>(1.0f)); nodes.push_back(node);
            const auto ram=add(action_ram); scalar(ram,11,node); rams.push_back(ram);
            const auto combiner=add(combiner_type); scalar(combiner,3,skeleton);
            scalar(combiner,6,list(state_list,{}));
            scalar(combiner,4,list(combiner_lists,{list(combiner_tracks,{}),list(combiner_tracks,{})}));
            scalar(combiner,5,list(int_list,{0,0}));
            Bytes matrices;
            for (int bone=0;bone<2;++bone) for (int at=0;at<16;++at)
                push32(matrices,std::bit_cast<std::uint32_t>(at%5==0 ? 1.0f : 0.0f));
            scalar(combiner,8,memory(matrices)); records[combiner].values[14]=words({null_ref,null_ref});
            combiners.push_back(combiner);
        }
        rams_list=list(action_ram_list,rams); scalar(info,6,rams_list);
        combiners_list=list(action_combiner_list,combiners); scalar(database,9,combiners_list);
    }
    std::uint32_t command(Type type,const std::string& name,float first=0,float second=0,const std::string& loop="LP") {
        const auto ref=add(type); text(ref,2,name);
        if (type==action_motion) { text(ref,3,"RR"); scalar(ref,4,std::bit_cast<std::uint32_t>(first)); text(ref,5,loop); }
        if (type==action_joint || type==action_fade || type==action_wait) scalar(ref,3,std::bit_cast<std::uint32_t>(first));
        if (type==action_joint || type==action_fade) scalar(ref,4,std::bit_cast<std::uint32_t>(second));
        if (type==action_joint) text(ref,5,loop);
        return ref;
    }
    std::uint32_t action(const std::string& name,const std::vector<std::uint32_t>& commands) {
        const auto ref=add(action_data); text(ref,2,name); scalar(ref,3,list(named_list,commands));
        actions.push_back(ref); contents(actions_list,actions); return ref;
    }
    ActionProgram program() const { return decode_action_program(document(),info,source_name); }
    AnimationSourceRuntime library() const {
        AnimationSourceRuntime result(source_name,{decode_igb_animation(document(),data,clip)});
        result.initialize_action_offset(); return result;
    }
    ActionRuntime runtime(const AnimationSourceRuntime& sources,std::int32_t ram=0) const {
        return decode_action_runtime(document(),program(),sources,ram);
    }
};
} // namespace action_fixture
