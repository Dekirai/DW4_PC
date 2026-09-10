#include "dw4/model_subcommands.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    ++checks;try{action();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid hitbox operation accepted.");
}
ModelHitboxSubcommand command(float delay=1,float length=2,std::int32_t kind=60000) {
    return {kind,delay,length,{0,4,7},{3,3,3}};
}
ModelSubcommandContext context() {return {{"source",7},ModelSubcommandOwner{"attachment",11}};}
const ModelHitboxSubcommandState& hitbox(const ModelSubcommandEntry& entry) {
    return std::get<ModelHitboxSubcommandState>(entry.value);
}
void append_and_numeric() {
    ModelSubcommandQueue queue;
    const std::vector<std::pair<std::int32_t,std::int32_t>> kinds{
        {std::numeric_limits<std::int32_t>::min(),0},{-10000,0},{-9999,0},{-1,0},{0,0},
        {999,0},{1000,2},{9999,2},{10000,0},{59999,0},{60000,1},{69999,1},
        {70000,2},{79999,2},{80000,2},{89999,2},{90000,0},{std::numeric_limits<std::int32_t>::max(),0}};
    for(const auto [kind,mode]:kinds) {
        const auto id=queue.append_hitbox(command(1.5f,2.25f,kind));
        const auto& entry=queue.entries().back();
        check(entry.id==id && !entry.started && entry.elapsed==0 && entry.delay==1.5f &&
            hitbox(entry).bullet_mode==mode && hitbox(entry).end_frame==3.75f && hitbox(entry).command.kind==kind,
            "Append must preserve authored kind and original signed category classification.");
    }
    queue.append_hitbox(command(123,-1));
    check(hitbox(queue.entries().back()).end_frame==100000000.0f,"Length -1 uses the finite absolute sentinel.");
    for(const auto row:{std::array<std::uint32_t,3>{0x00800000,0x807fffff,0},
                        std::array<std::uint32_t,3>{0x80800000,0x007fffff,0x80000000},
                        std::array<std::uint32_t,3>{0x00800000,1,0x00800001}}) {
        queue.append_hitbox(command(std::bit_cast<float>(row[0]),std::bit_cast<float>(row[1])));
        check(std::bit_cast<std::uint32_t>(hitbox(queue.entries().back()).end_frame)==row[2],
            "End-frame scalar sum retains tiny inputs and flushes tiny results with sign.");
    }
    const auto count=queue.entries().size();
    rejects([&]{queue.append_hitbox(command(std::numeric_limits<float>::max(),std::numeric_limits<float>::max()));});
    auto invalid=command();invalid.box_size[2]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{queue.append_hitbox(invalid);});
    invalid=command();invalid.offset[0]=std::numeric_limits<float>::infinity();
    rejects([&]{queue.append_hitbox(invalid);});
    check(queue.entries().size()==count && !queue.faulted(),"Invalid hitbox append must be atomic.");
    queue.clear_hitboxes();check(queue.entries().empty(),"AllClear removes unstarted hitboxes without a service.");
}
void timing_and_request() {
    ModelSubcommandQueue queue;const auto id=queue.append_hitbox(command());
    std::vector<ModelHitboxCheckRequest> requests;
    ModelSubcommandServices services;services.check_hitbox=[&](const auto& request){requests.push_back(request);return true;};
    queue.update(0.5f,context(),services);
    check(requests.empty() && !queue.entries()[0].started,"Hitbox delay uses exact fractional cached frames.");
    auto owner=context();queue.update(0.5f,owner,services);owner.source.model_name="mutated";
    check(requests.size()==1 && requests[0].entry_id==id && requests[0].bullet_mode==1 &&
        requests[0].command.offset==std::array<float,3>{0,4,7} && requests[0].command.box_size==std::array<float,3>{3,3,3} &&
        requests[0].context.source==ModelSubcommandOwner{"source",7} &&
        requests[0].context.attachment==ModelSubcommandOwner{"attachment",11},"Check owns the exact payload and context.");
    queue.update(0,context(),services);queue.update(-1,context(),services);queue.update(2.5f,context(),services);
    check(requests.size()==4 && queue.entries()[0].elapsed==2.5f,
        "Every active pass queries, including zero/negative deltas below the original delay.");
    queue.update(0.5f,context(),services);
    check(requests.size()==4 && queue.entries().empty(),"Already-started hitbox expires at end equality before querying.");
    for(const float length:{0.0f,-2.0f,1.0f}) {
        queue.append_hitbox(command(2,length));const auto before=requests.size();
        queue.update(10,context(),services);
        check(requests.size()==before+1 && queue.entries().size()==1,"First start queries even when its end was overshot.");
        queue.update(0,context(),services);
        check(requests.size()==before+1 && queue.entries().empty(),"Overshot first start expires on the subsequent pass.");
    }
}
void pending_and_interruptions() {
    ModelSubcommandQueue queue;const auto id=queue.append_hitbox(command(0,10));
    queue.append_sound(0,"later");
    ModelSubcommandServices services;int calls{},effects{},sounds{};
    services.check_hitbox=[&](const auto& request) {
        ++calls;check(request.entry_id==id && request.context.source.model_name=="source","Resume preserves check identity/context.");
        if(calls==1){++effects;return false;}return true;
    };
    services.play_sound=[&](const auto&){++sounds;return ModelSoundPlayResult{true,0,4};};
    auto result=queue.update(1,context(),services);
    check(result.stop==ModelSubcommandStop::hitbox_check_required && result.entry_id==id && queue.entries()[0].started &&
        queue.entries()[1].elapsed==0 && sounds==0,"Incomplete check retains published start and ordered tail.");
    rejects([&]{queue.cleanup(services);});rejects([&]{queue.clear_hitboxes();});
    rejects([&]{queue.update(4,context(),services);});
    check(!queue.faulted(),"Rejected premature interruption must leave a resumable owner.");
    ModelSubcommandQueue moved(std::move(queue));
    check(moved.resume(services).complete() && calls==2 && effects==1 && sounds==1 && moved.entries()[0].elapsed==1,
        "Moved pending check resumes once without replaying a completed internal prefix or elapsed delta.");
    calls=0;effects=0;
    check(moved.update(1,context(),services).stop==ModelSubcommandStop::hitbox_check_required,"Active check can also suspend.");
    rejects([&]{moved.clear_hitboxes();});moved.resume(services);
    check(calls==2 && effects==1 && moved.entries()[0].elapsed==2,"Active resume preserves the same pass.");
    moved.cleanup();check(moved.entries().empty(),"Completed hitbox cleanup has no external collision erase.");

    queue.append_hitbox(command(0,10));queue.append_sound(0,"tail");
    services.check_hitbox={};
    check(queue.update(1,context(),services).stop==ModelSubcommandStop::hitbox_check_required,"Missing callback stops explicitly.");
    queue.clear_hitboxes();queue.resume(services);
    check(queue.entries().empty() && sounds==2,"Never-invoked check may clear while preserving the unadvanced tail.");
    queue.append_hitbox(command(0,10));queue.update(1,context());queue.cleanup();
    check(queue.entries().empty() && !queue.pending(),"Missing check may be removed by sequence cleanup.");
}
void mixed_clear_and_reentrancy() {
    ModelSubcommandQueue queue;queue.append_hitbox(command(0,10));
    const auto sound=queue.append_sound(0,"sound");queue.append_hitbox(command(0,10));
    const auto effect=queue.append_effect({0,"effect","sequence",{},{},true,false});queue.append_hitbox(command(0,10));
    ModelSubcommandServices services;std::vector<int> trace;
    services.check_hitbox=[&](const auto&) {
        trace.push_back(1);rejects([&]{queue.clear_hitboxes();});rejects([&]{queue.cleanup();});return true;
    };
    services.play_sound=[](const auto&){return ModelSoundPlayResult{};};
    queue.update(1,context(),services);queue.append_hitbox(command(0,10));
    queue.clear_hitboxes();
    check(queue.entries().size()==2 && queue.entries()[0].id==sound && queue.entries()[1].id==effect &&
        queue.entries()[0].elapsed==1 && queue.entries()[1].elapsed==0,
        "AllClear removes only hitboxes, including appended entries, and adjusts a pending mixed cursor.");
    services.play_sound=[&](const auto&){trace.push_back(2);return ModelSoundPlayResult{true,11,4};};
    services.find_effect_space=[](const auto&){return ModelEffectSpaceResult{true,3};};
    services.generate_effect=[&](const auto&){trace.push_back(3);return true;};
    queue.resume(services);
    check(trace==std::vector<int>{1,2,3} && queue.entries()[1].elapsed==1,"Clear and resume retain surviving order without repeated effects.");
    services.sound_volume_down=[&](auto){trace.push_back(4);return true;};
    services.erase_effect=[&](const auto&,auto){trace.push_back(5);return true;};
    queue.cleanup(services);
    check(trace==std::vector<int>{1,2,3,4,5},"Selective clear preserves later sound/effect cleanup ownership.");

    queue.append_hitbox(command(0,10));services.check_hitbox=[&](const auto&) {
        queue.append_hitbox(command(0,10));return true;
    };
    queue.update(1,context(),services);
    check(queue.entries().size()==2 && queue.entries()[1].elapsed==0 && !queue.entries()[1].started,
        "Hitbox callback append waits for the next list-count snapshot.");
    queue.clear_hitboxes();queue.append_hitbox(command());
    services.check_hitbox=[](const auto&)->bool {throw std::runtime_error("Possibly committed query.");};
    rejects([&]{queue.update(1,context(),services);});
    check(queue.faulted(),"Uncertain hitbox service outcome faults the owner.");
    rejects([&]{queue.resume(services);});rejects([&]{queue.clear_hitboxes();});rejects([&]{queue.cleanup();});
}

// Asset-free transport for the ignored original-instruction verifier. Types:
// 0 sound, 1 retained effect, 2 detached effect, 3 hitbox. Operations: 0 update,
// 1 selective hitbox clear, 2 sequence cleanup. Each operation emits a snapshot.
int oracle() {
    std::size_t count{},operations{};
    while(std::cin>>count>>operations) {
        ModelSubcommandQueue queue;std::vector<int> trace;
        for(std::size_t i=0;i<count;++i) {
            int type{};std::int32_t kind{};std::uint32_t delay{},length{};
            std::cin>>type>>kind>>delay>>length;
            if(type==0)queue.append_sound(std::bit_cast<float>(delay),std::to_string(i));
            else if(type==3)queue.append_hitbox(command(std::bit_cast<float>(delay),std::bit_cast<float>(length),kind));
            else queue.append_effect({std::bit_cast<float>(delay),std::to_string(i),"sequence",{},{},type==1,false});
        }
        ModelSubcommandServices services;
        services.play_sound=[&](const auto& name){const int id=std::stoi(*name);trace.push_back(100+id);return ModelSoundPlayResult{true,static_cast<std::uint64_t>(id+1),4};};
        services.find_effect_space=[](const auto& name){return ModelEffectSpaceResult{true,std::stoi(*name)+1};};
        services.generate_effect=[&](const auto& request){trace.push_back(200+std::stoi(*request.model_name));return true;};
        services.sound_volume_down=[&](auto handle){trace.push_back(300+static_cast<int>(handle)-1);return true;};
        services.erase_effect=[&](const auto& name,auto){trace.push_back(400+std::stoi(*name));return true;};
        services.check_hitbox=[&](const auto& request){trace.push_back(500+static_cast<int>(request.entry_id));return true;};
        const auto snapshot=[&] {
            std::cout<<' '<<queue.entries().size();
            for(const auto& entry:queue.entries()) {
                const auto* hit=std::get_if<ModelHitboxSubcommandState>(&entry.value);
                std::cout<<' '<<entry.id<<' '<<std::bit_cast<std::uint32_t>(entry.elapsed)<<' '<<entry.started<<' '
                    <<(hit ? hit->bullet_mode : -1)<<' '<<(hit ? std::bit_cast<std::uint32_t>(hit->end_frame) : 0);
            }
        };
        snapshot();
        for(std::size_t i=0;i<operations;++i) {
            int operation{};std::uint32_t delta{};std::cin>>operation>>delta;
            if(operation==0)queue.update(std::bit_cast<float>(delta),context(),services);
            else if(operation==1)queue.clear_hitboxes();
            else queue.cleanup(services);
            snapshot();
        }
        queue.cleanup(services);std::cout<<' '<<trace.size();for(int event:trace)std::cout<<' '<<event;std::cout<<'\n';
    }
    return 0;
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--oracle")return oracle();
        append_and_numeric();timing_and_request();pending_and_interruptions();mixed_clear_and_reentrancy();
        std::cout<<"model hitbox checks: "<<checks<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
