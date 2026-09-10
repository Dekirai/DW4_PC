#include "dw4/model_subcommands.hpp"
#include "dw4/motion_timer.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    ++checks;try{action();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid subcommand operation accepted.");
}
ModelSubcommandContext context() {return {{"source",7},ModelSubcommandOwner{"attached",11}};}
ModelEffectSubcommand effect(bool sequence_dead=true) {
    return {0,"effect","sequence",{1,2,3},{4,5,6},sequence_dead,true};
}
void defaults_delay_and_results() {
    static_assert(!std::is_copy_constructible_v<ModelSubcommandQueue>);
    ModelSubcommandQueue queue;
    queue.append_sound(6,"footstep"); queue.append_sound(-1,std::nullopt);
    check(queue.entries().size()==2 && !queue.entries()[0].started && queue.entries()[0].elapsed==0,
        "Append must own separate unstarted entries with zero elapsed frames.");
    std::vector<std::optional<std::string>> names;
    ModelSubcommandServices services;
    services.play_sound=[&](const auto& name) {names.push_back(name);return ModelSoundPlayResult{true,0,4};};
    check(queue.update(3,context(),services).complete() && names.size()==1 && !names[0],
        "Negative-delay null sound starts first; unavailable and invalid handles differ.");
    check(queue.entries().size()==1 && queue.entries()[0].elapsed==3,"Positive delay remains pending.");
    queue.update(2.5f,context(),services);
    check(names.size()==1,"Subcommands must not round a fractional delay upward.");
    queue.update(0.5f,context(),services);
    check(names.size()==2 && names[1]=="footstep" && queue.entries().empty(),"Equality starts on the sixth cached frame.");
    queue.append_sound(6,"uncapped");queue.update(10,context(),services);
    check(names.size()==3 && names.back()=="uncapped" && queue.entries().empty(),
        "System frame delta must not acquire the separate motion timer's three-frame cap.");
    for(const auto result:{ModelSoundPlayResult{true,0,4},
                          ModelSoundPlayResult{true,std::numeric_limits<std::uint64_t>::max(),4},
                          ModelSoundPlayResult{true,0x100000002ULL,3}}) {
        queue.append_sound(0,""); services.play_sound=[&](const auto&){return result;};
        queue.update(0,context(),services);
        check(queue.entries().empty(),"Completed original sound failure/non-type4 must not retry.");
    }
}
void pending_order_and_cleanup() {
    ModelSubcommandQueue queue;
    const auto first=queue.append_sound(0,"first"),second=queue.append_sound(0,"second");
    const auto third=queue.append_sound(0,"third");
    std::vector<std::string> played; bool unavailable=true;
    ModelSubcommandServices services;
    services.play_sound=[&](const auto& name) {
        if(*name=="second" && unavailable) return ModelSoundPlayResult{};
        played.push_back(*name);return ModelSoundPlayResult{true,static_cast<std::uint64_t>(played.size()+100),4};
    };
    const auto pending=queue.update(1,context(),services);
    check(pending.stop==ModelSubcommandStop::sound_play_required && pending.entry_id==second && queue.pending(),
        "Unavailable sound must retain its exact pass position.");
    check(played==std::vector<std::string>{"first"} && queue.entries()[0].id==first &&
        queue.entries()[1].started && !queue.entries()[2].started,"Only the committed ordered prefix advances.");
    rejects([&]{queue.update(2,context(),services);});
    unavailable=false; queue.resume(services);
    check(played==std::vector<std::string>{"first","second","third"} && !queue.pending(),
        "Resume must not replay the earlier sound.");
    for(const auto& entry:queue.entries())check(entry.elapsed==1,"Resume must not add elapsed frames again.");
    queue.update(-0.5f,context(),services);
    check(played.size()==3 && queue.entries()[2].id==third && queue.entries()[2].elapsed==0.5f,
        "Retained sounds update without playback; negative delta is preserved.");
    std::vector<std::uint64_t> down;
    services.sound_volume_down=[&](std::uint64_t handle) {if(handle==102)return false;down.push_back(handle);return true;};
    const auto cleanup=queue.cleanup(services);
    check(cleanup.stop==ModelSubcommandStop::sound_volume_down_required && cleanup.entry_id==second &&
        down==std::vector<std::uint64_t>{101} && queue.entries().size()==3,"Cleanup retains list ownership until all virtual cleanup calls complete.");
    rejects([&]{queue.append_sound(0,"late");});
    services.sound_volume_down=[&](std::uint64_t handle){down.push_back(handle);return true;};
    queue.resume(services);
    check(down==std::vector<std::uint64_t>{101,102,103} && queue.entries().empty(),"Resume must not repeat volume-down effects.");
}
void effect_binding_and_interruption() {
    ModelSubcommandQueue queue;
    const auto id=queue.append_effect(effect());
    ModelSubcommandServices services; int searches{},generations{},erases{};
    services.find_effect_space=[&](const auto& name){++searches;check(name=="effect","Effect name routing.");return ModelEffectSpaceResult{true,13};};
    auto pending=queue.update(0,context(),services);
    check(pending.stop==ModelSubcommandStop::effect_generate_required && pending.entry_id==id && searches==1,
        "Selected effect RAM must persist across an unavailable generation service.");
    services.generate_effect=[&](const ModelEffectGenerateRequest& request) {
        ++generations;
        check(request.model_name=="effect" && request.sequence_name=="sequence" && request.ram_no==13,
            "Effect must generate the selected first-space RAM.");
        check(request.source==ModelSubcommandOwner{"source",7} && request.attachment==ModelSubcommandOwner{"attached",11},
            "Effect source and attachment identities must remain separate.");
        check(request.offset==std::array<float,3>{1,2,3} && request.rotation==std::array<float,3>{4,5,6} && request.position_lock,
            "Effect payload must preserve its transform and position lock.");
        return true;
    };
    queue.resume(services); queue.update(3,context(),services);
    check(searches==1 && generations==1 && queue.entries().size()==1,"Retained effect does not regenerate.");
    services.erase_effect=[&](const auto& name,std::int32_t ram){++erases;check(name=="effect" && ram==13,"Effect erasure target.");return true;};
    queue.cleanup(services);check(erases==1 && queue.entries().empty(),"Sequence-dead effect erases on interruption.");
    auto detached=effect(false);detached.model_name.reset();detached.sequence_name.reset();
    services.find_effect_space=[&](const auto& name){check(!name,"Null effect model must remain null.");return ModelEffectSpaceResult{true,3};};
    services.generate_effect=[&](const auto& request){check(!request.sequence_name && request.attachment==request.source,
        "Null sequence and unattached source fallback.");++generations;return true;};
    queue.append_effect(detached); queue.update(0,{{"source",7},{}},services);
    check(queue.entries().empty() && generations==2,"Non-sequence-dead generation leaves the queue immediately.");
    queue.cleanup(services); check(erases==1,"Detached effects must not be erased by sequence cleanup.");
    queue.append_effect(effect());services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{true,{}};};
    queue.update(0,context(),services);check(queue.entries().empty(),"Completed no-space result is terminal.");
    queue.append_effect(effect());services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{};};
    check(queue.update(0,context(),services).stop==ModelSubcommandStop::effect_space_required,"Unavailable space service remains pending.");
    queue.cleanup(services);check(queue.entries().empty() && erases==1,"Unselected effect can be cancelled without a manager erase.");
    queue.append_effect(effect());services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{true,8};};
    services.generate_effect={};queue.update(0,context(),services);
    services.erase_effect=[&](const auto&,std::int32_t ram){++erases;check(ram==8,"Interrupted selected effect cleanup target.");return true;};
    queue.cleanup(services);check(erases==2,"Original started/RAM cleanup guard applies after selection, before generation.");
}
void callbacks_moves_and_bounds() {
    ModelSubcommandQueue queue;queue.append_sound(0,"outer");
    ModelSubcommandServices services;int plays{};
    services.play_sound=[&](const auto&) {
        ++plays;queue.append_sound(0,"appended");
        rejects([&]{queue.cleanup(services);});rejects([&]{queue.resume(services);});
        rejects([&]{ModelSubcommandQueue moving(std::move(queue));});
        return ModelSoundPlayResult{true,0,4};
    };
    queue.update(1,context(),services);
    check(plays==1 && queue.entries().size()==1 && queue.entries()[0].elapsed==0 && !queue.faulted(),
        "Callback append waits for next original list-count snapshot; rejected recursion must not corrupt owner.");
    check(queue.update(1,context()).stop==ModelSubcommandStop::sound_play_required,"Missing service must be explicit.");
    ModelSubcommandQueue moved(std::move(queue));
    check(queue.entries().empty() && !queue.pending() && moved.pending(),"Move transfers the pending pass and ownership.");
    ModelSubcommandQueue occupied;occupied.append_sound(99,"owned");rejects([&]{occupied=std::move(moved);});
    check(moved.pending() && occupied.entries().size()==1,"Move assignment must not discard existing owned work.");
    occupied.cleanup();occupied=std::move(moved);check(occupied.pending() && !moved.pending(),"Clean destination accepts moved pass.");
    occupied.cleanup();check(occupied.entries().empty(),"Pending unplayed sound cancels without volume-down.");
    rejects([&]{occupied.append_sound(std::numeric_limits<float>::infinity(),"bad");});
    auto bad=effect();bad.rotation[1]=std::numeric_limits<float>::quiet_NaN();rejects([&]{occupied.append_effect(bad);});
    check(occupied.entries().empty(),"Malformed append must be atomic.");
    occupied.append_sound(std::numeric_limits<float>::max(),"far");occupied.update(std::numeric_limits<float>::max()/2,context());
    const float previous=occupied.entries()[0].elapsed;
    rejects([&]{occupied.update(std::numeric_limits<float>::max(),context());});
    check(!occupied.faulted() && occupied.entries()[0].elapsed==previous,"Numeric overflow rejection precedes mutation.");
    occupied.cleanup();occupied.append_sound(0,"uncertain");
    services.play_sound=[&](const auto&)->ModelSoundPlayResult {++plays;throw std::runtime_error("Manager failed after possible commit.");};
    rejects([&]{occupied.update(1,context(),services);});check(occupied.faulted(),"Uncertain callback outcome must fault the owner.");
    const int before=plays;rejects([&]{occupied.resume(services);});rejects([&]{occupied.cleanup(services);});
    check(plays==before,"A throwing service must never be replayed automatically.");
}
void original_timing_and_ni() {
    check(original_elapsed_frames(0)==0 && original_elapsed_frames(16666667)==1 &&
        original_elapsed_frames(-16666667)==-1,"System elapsed conversion preserves sign and nominal units.");
    check(original_system_frame_delta(100000002,0)==6,"System frame delta is uncapped.");
    check(original_system_frame_delta(2,std::numeric_limits<std::uint64_t>::max()-2)==original_elapsed_frames(5),
        "System timestamp subtraction wraps before signed conversion.");
    check(original_system_frame_delta(0,1)==original_elapsed_frames(-1),"Backwards world time remains negative.");
    const std::int64_t midpoint=(std::int64_t{1}<<62)+(std::int64_t{1}<<38)+1;
    check(std::bit_cast<std::uint32_t>(original_elapsed_frames(midpoint))==0x5280d959,
        "Original signed conversion must retain the intermediate binary64 rounding stage.");
    ModelSubcommandServices services;services.play_sound=[](const auto&){return ModelSoundPlayResult{true,1,4};};
    for(const auto row:{std::array<std::uint32_t,3>{0x00800000,0x807fffff,0x00000000},
                        std::array<std::uint32_t,3>{0x80800000,0x007fffff,0x80000000},
                        std::array<std::uint32_t,3>{0x00800000,0x00000001,0x00800001},
                        std::array<std::uint32_t,3>{0x80800000,0x80000001,0x80800001},
                        std::array<std::uint32_t,3>{0x00400000,0x00400000,0x00000000}}) {
        ModelSubcommandQueue queue;queue.append_sound(0,"numeric");
        queue.update(std::bit_cast<float>(row[0]),context(),services);
        queue.update(std::bit_cast<float>(row[1]),context(),services);
        check(std::bit_cast<std::uint32_t>(queue.entries()[0].elapsed)==row[2],
            "Scalar fadds must retain tiny inputs and flush each tiny result with its sign.");
        services.sound_volume_down=[](auto){return true;};queue.cleanup(services);
    }
}

int numeric_oracle() {
    char kind{};
    while(std::cin>>kind) {
        try {
            if(kind=='T') {
                std::uint64_t current{},previous{};std::cin>>current>>previous;
                std::cout<<std::bit_cast<std::uint32_t>(original_system_frame_delta(current,previous))<<'\n';
            } else if(kind=='A') {
                std::uint32_t a{},b{};std::cin>>a>>b;
                ModelSubcommandQueue queue;queue.append_sound(0,"numeric");
                ModelSubcommandServices services;services.play_sound=[](const auto&){return ModelSoundPlayResult{true,1,4};};
                queue.update(std::bit_cast<float>(a),context(),services);
                queue.update(std::bit_cast<float>(b),context(),services);
                std::cout<<std::bit_cast<std::uint32_t>(queue.entries()[0].elapsed)<<'\n';
            } else throw std::runtime_error("Unknown numeric oracle operation.");
        } catch(const std::exception&) {std::cout<<"ERROR\n";}
    }
    return 0;
}

// Ignored original-instruction probe protocol. Each case owns mixed sound/effect
// commands, applies one cached-frame delta repeatedly, then clears the sequence.
// Kind 0=sound, 1=sequence-dead effect, 2=detached effect; services are completed.
int oracle() {
    std::size_t count{},updates{};std::uint32_t delta_word{};
    while(std::cin>>count>>updates>>delta_word) {
        ModelSubcommandQueue queue;std::vector<int> trace;
        for(std::size_t i=0;i<count;++i) {
            int kind{};std::uint32_t delay{};std::cin>>kind>>delay;
            if(kind==0)queue.append_sound(std::bit_cast<float>(delay),std::to_string(i));
            else {auto command=effect(kind==1);command.delay=std::bit_cast<float>(delay);command.model_name=std::to_string(i);queue.append_effect(command);}
        }
        ModelSubcommandServices services;
        services.play_sound=[&](const auto& name){const int id=std::stoi(*name);trace.push_back(100+id);return ModelSoundPlayResult{true,static_cast<std::uint64_t>(id+1),4};};
        services.find_effect_space=[](const auto& name){return ModelEffectSpaceResult{true,std::stoi(*name)+1};};
        services.generate_effect=[&](const auto& request){trace.push_back(200+std::stoi(*request.model_name));return true;};
        services.sound_volume_down=[&](std::uint64_t handle){trace.push_back(300+static_cast<int>(handle)-1);return true;};
        services.erase_effect=[&](const auto& name,std::int32_t){trace.push_back(400+std::stoi(*name));return true;};
        for(std::size_t tick=0;tick<updates;++tick)queue.update(std::bit_cast<float>(delta_word),context(),services);
        std::cout<<queue.entries().size();
        for(const auto& entry:queue.entries())std::cout<<' '<<entry.id<<' '<<std::bit_cast<std::uint32_t>(entry.elapsed)<<' '<<entry.started;
        queue.cleanup(services);std::cout<<' '<<trace.size();for(int event:trace)std::cout<<' '<<event;std::cout<<'\n';
    }
    return 0;
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--oracle")return oracle();
        if(argc==2 && std::string(argv[1])=="--numeric-oracle")return numeric_oracle();
        defaults_delay_and_results();pending_order_and_cleanup();effect_binding_and_interruption();callbacks_moves_and_bounds();
        original_timing_and_ni();
        std::cout<<"model subcommand checks: "<<checks<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
