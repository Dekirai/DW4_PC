#include "dw4/bone_select.hpp"
#include "dw4/actor_scene.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& action) {
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
    check(rejected,"Unsafe bone-selector operation was accepted.");
}
const AnimationMatrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
const AnimationMatrix root{0,1,0,0,-1,0,0,0,0,0,1,0,10,20,30,1};
const AnimationMatrix bone{2,0,0,0,0,3,0,0,0,0,1,0,4,5,6,1};
const AnimationMatrix product{0,2,0,0,-3,0,0,0,0,0,1,0,5,24,36,1};
std::shared_ptr<const ActorBoneSelector> selector(std::int32_t index) {
    return std::make_shared<const ActorBoneSelector>(index,std::vector<std::shared_ptr<const ActorSceneNode>>{});
}
BoneSelectTraversal start(BoneSelectContext& context,std::shared_ptr<const ActorBoneSelector> node,
    const std::vector<AnimationMatrix>& bones) {return context.begin(std::move(node),std::span<const AnimationMatrix>(bones));}

void common_order() {
    static_assert(!std::is_copy_constructible_v<BoneSelectContext>);
    static_assert(!std::is_copy_constructible_v<BoneSelectTraversal>);
    BoneSelectContext context(identity);const auto base=context.model_view();
    auto node=selector(1);std::vector<AnimationMatrix> bones{root,bone};
    auto traversal=start(context,node,bones);const auto attribute=context.model_view();
    check(context.pool_cursor()==1 && context.pool_size()==1 && context.stack_depth()==2 &&
        attribute!=base && attribute->matrix()==product,"Selector did not push root*bone after consuming its pool slot.");
    bones={identity,identity}; // begin must have consumed the live array already.
    check(context.resume(traversal).stop==BoneSelectStop::children_required && !traversal.completed(),
        "Missing actual child service was reported as a completed traversal.");
    unsigned calls{};
    const auto result=context.resume(traversal,[&](BoneSelectContext& live,const auto& retained)->std::optional<std::int32_t> {
        ++calls;check(&live==&context && retained==node && live.model_view()==attribute && attribute->matrix()==product,
            "Child service lost selector identity or captured model-view.");
        return 2;
    });
    check(result.stop==BoneSelectStop::completed && result.value==2 && traversal.completed() && !context.pending(),
        "Exact child result2 was not returned.");
    check(context.model_view()==base && context.stack_depth()==1 && context.pool_cursor()==1,
        "Completion did not restore the previous model-view without rewinding the pool.");
    check(context.resume(traversal,{}).value==2 && calls==1,"Completed child effects replayed.");
    context.rewind_pool();
    auto zero=start(context,selector(0),{root,bone});
    const AnimationMatrix square{-1,0,0,0,0,-1,0,0,0,0,1,0,-10,30,60,1};
    check(context.model_view()==attribute && attribute->matrix()==square,"Pool reuse changed identity or index0 failed to square root0.");
    check(context.resume(zero,[](auto&,const auto&)->std::optional<std::int32_t>{return -7;}).value==0,
        "Non2 child result was not mapped to zero.");
    context.reset();check(context.model_view()==base && context.pool_size()==0 && context.pool_cursor()==0,
        "Explicit reset did not restore prepared base state.");
    auto fresh=start(context,selector(1),{root,bone});
    check(context.model_view()!=attribute && attribute->matrix()==square,"Reset reused a detached retained attribute.");
    context.resume(fresh,[](auto&,const auto&)->std::optional<std::int32_t>{return 0;});
}
void gates_and_numeric_failures() {
    BoneSelectContext context;std::vector<AnimationMatrix> invalid{identity};invalid[0][0]=std::numeric_limits<float>::quiet_NaN();
    for(const auto index:{std::numeric_limits<std::int32_t>::min(),-2,-1}) {
        auto handle=start(context,selector(index),invalid);
        check(handle.completed() && context.resume(handle).value==0 && !context.model_view(),"Negative gate inspected matrices or traversed children.");
    }
    auto absent=context.begin(selector(99),std::nullopt);
    check(absent.completed() && context.pool_cursor()==4 && context.pool_size()==4 && !context.pending(),
        "Absent bone pointer failed to consume its pool slot before returning.");
    rejects([&]{context.begin(nullptr,std::nullopt);});check(context.pool_cursor()==4,"Invalid selector preflight changed pool state.");
    rejects([&]{start(context,selector(0),{});});
    check(context.pool_cursor()==5 && context.faulted() && !context.model_view(),"Checked positive bounds failure lost its committed prefix.");
    rejects([&]{context.rewind_pool();});context.reset();
    rejects([&]{start(context,selector(0),invalid);});
    check(context.pool_cursor()==1 && context.faulted() && context.stack_depth()==0,"Nonfinite selected math published a partial attribute.");
    context.reset();
    std::vector<AnimationMatrix> with_unused{root,bone,invalid[0]};
    auto valid=start(context,selector(1),with_unused);
    check(context.resume(valid,[](auto&,const auto&)->std::optional<std::int32_t>{return 4;}).value==0,
        "Unused nonfinite bones were inspected or non2 return mapping changed.");
    auto overflow=identity;overflow[0]=std::numeric_limits<float>::max();
    rejects([&]{start(context,selector(0),{overflow});});
    check(context.faulted() && context.pool_cursor()==2,"Finite multiplication overflow did not retain its pool prefix.");
    rejects([&]{BoneSelectContext bad(invalid[0]);});
}
void retained_service_and_moves() {
    BoneSelectContext context(identity),foreign;auto node=selector(1);
    auto handle=start(context,node,{root,bone});auto attr=context.model_view();
    unsigned calls{},prefix{},replacement{};bool ready=false;
    BoneSelectChildren service=[&](auto&,const auto&)->std::optional<std::int32_t> {
        if(++calls==1)++prefix;return ready ? std::optional<std::int32_t>{1} : std::nullopt;
    };
    check(context.resume(handle,service).stop==BoneSelectStop::children_required && context.model_view()==attr,
        "Incomplete child call was popped.");
    rejects([&]{start(context,node,{identity,identity});});rejects([&]{context.rewind_pool();});
    rejects([&]{foreign.resume(handle);});check(!context.faulted(),"Rejected owner operation faulted a valid continuation.");
    BoneSelectContext moved(std::move(context));rejects([&]{context.model_view();});
    auto moved_handle=std::move(handle);check(!handle.id() && handle.faulted() && moved_handle.id(),"Traversal move duplicated ownership.");
    ready=true;
    check(moved.resume(moved_handle,[&](auto&,const auto&)->std::optional<std::int32_t>{++replacement;return 2;}).value==0 &&
        calls==2 && prefix==1 && replacement==0 && moved.pool_cursor()==1 && moved.stack_depth()==1,
        "Retry replaced the invoked callback or replayed a completed prefix.");
    auto pending=start(moved,node,{root,bone});
    rejects([&]{moved=BoneSelectContext{};});moved.reset();
    check(moved.resume(pending).stop==BoneSelectStop::faulted && !pending.completed(),"Reset failed to invalidate a retained handle.");
    BoneSelectContext abandoned;std::weak_ptr<const ActorBoneSelector> weak;
    {auto owned=selector(1);weak=owned;auto lost=start(abandoned,std::move(owned),{root,bone});}
    check(!weak.expired() && abandoned.pending(),"Abandonment silently released a pending selector.");
    rejects([&]{abandoned.begin(selector(-1),std::nullopt);});abandoned.reset();
    check(weak.expired() && !abandoned.pending(),"Reset retained abandoned selector ownership.");
}
void nested_lifo() {
    BoneSelectContext context(identity);auto base=context.model_view();
    auto outer=start(context,selector(1),{root,bone});auto outer_attr=context.model_view();
    std::optional<BoneSelectTraversal> inner;bool ready=false;unsigned parent_calls{},child_calls{},prefix{};
    BoneSelectChildren child=[&](BoneSelectContext& live,const auto&)->std::optional<std::int32_t> {
        ++child_calls;if(child_calls==1)++prefix;
        check(live.stack_depth()==3 && live.model_view()->matrix()==bone,
            "Nested selector multiplied the current model-view rather than its supplied root0.");
        return ready ? std::optional<std::int32_t>{2} : std::nullopt;
    };
    BoneSelectChildren parent=[&](BoneSelectContext& live,const auto&)->std::optional<std::int32_t> {
        ++parent_calls;
        rejects([&]{live.reset();});rejects([&]{BoneSelectContext moved(std::move(live));});
        rejects([&]{live.resume(outer);});
        if(!inner)inner.emplace(start(live,selector(1),{identity,bone}));
        const auto result=live.resume(*inner,child);
        if(result.stop!=BoneSelectStop::completed)return std::nullopt;
        check(live.model_view()==outer_attr && live.stack_depth()==2,"Inner completion did not restore its parent attribute.");
        return 2;
    };
    check(context.resume(outer,parent).stop==BoneSelectStop::children_required && context.pool_cursor()==2,
        "Nested pause lost its two committed pool entries.");
    rejects([&]{context.resume(*inner,child);});rejects([&]{start(context,selector(0),{identity});});
    ready=true;check(context.resume(outer).value==2 && parent_calls==2 && child_calls==2 && prefix==1 &&
        context.model_view()==base && context.pool_cursor()==2 && !context.pending(),"Nested retry violated retained callable/LIFO ownership.");
}
void faults_and_limits() {
    BoneSelectContext context;auto handle=start(context,selector(0),{identity});unsigned calls{};
    check(context.resume(handle,[&](auto&,const auto&)->std::optional<std::int32_t>{++calls;throw std::runtime_error("uncertain child");}).stop==BoneSelectStop::faulted,
        "Throwing child was reported as complete.");
    check(context.stack_depth()==1 && handle.faulted() && context.faulted() && context.resume(handle).stop==BoneSelectStop::faulted && calls==1,
        "Faulted child was popped or replayed.");
    context.reset();
    auto parent=start(context,selector(0),{identity});std::optional<BoneSelectTraversal> unfinished;
    check(context.resume(parent,[&](auto& live,const auto&)->std::optional<std::int32_t>{
        unfinished.emplace(start(live,selector(0),{identity}));return 2;
    }).stop==BoneSelectStop::faulted && context.stack_depth()==2,"Parent completed over an unfinished child.");
    context.reset();
    BoneSelectChildren descend;
    descend=[&](auto& live,const auto&)->std::optional<std::int32_t> {
        if(live.stack_depth()==64) {rejects([&]{start(live,selector(0),{identity});});return 2;}
        auto child=start(live,selector(0),{identity});const auto result=live.resume(child,descend);
        check(result.stop==BoneSelectStop::completed,"Bounded synchronous nesting did not complete.");return result.value;
    };
    auto top=start(context,selector(0),{identity});
    check(context.resume(top,descend).value==2 && context.pool_cursor()==64 && context.stack_depth()==0,
        "Nested selector bound or final stack changed.");
    context.reset();const auto skipped=selector(-1);
    for(std::size_t i=0;i<65536;++i) {auto slot=context.begin(skipped,std::nullopt);if(!slot.completed())throw std::runtime_error("Skipped slot was pending.");}
    rejects([&]{context.begin(skipped,std::nullopt);});
    check(context.pool_cursor()==65536 && context.pool_size()==65536 && !context.faulted(),"Attribute pool bound changed its committed entries.");
}

void owner_release_guard() {
    BoneSelectContext context;unsigned rejected{},destroyed{};
    {
        auto handle=start(context,selector(0),{identity});
        auto owner=std::shared_ptr<int>(new int(1),[&](int* value) {
            auto attempt=[&](auto&& operation) {try{operation();}catch(const BoneSelectError&){++rejected;}};
            attempt([&]{context.begin(selector(-1),std::nullopt);});
            attempt([&]{context.reset();});
            attempt([&]{context.rewind_pool();});
            attempt([&]{context.model_view();});
            ++destroyed;delete value;
        });
        context.resume(handle,[owner](auto&,const auto&)->std::optional<std::int32_t>{return std::nullopt;});
    }
    check(context.pending() && !destroyed,"Pending context did not retain its invoked service owner.");
    context.reset();
    check(destroyed==1 && rejected==4 && !context.pending() && context.pool_size()==0,
        "Reset allowed callback-owner destruction to reenter partial stack/pool release.");
}

int oracle() {
    // Asset-free word protocol for an ignored original-instruction comparison.
    // S index present child-result count followed by count * 16 uint32 words.
    char mode{};
    while(std::cin>>mode) {
        std::int32_t index{},child{};unsigned present{};std::size_t count{};
        if(mode!='S' || !(std::cin>>index>>present>>child>>count) || present>1 || count>64)return 2;
        std::vector<AnimationMatrix> bones(count);
        for(auto& matrix:bones)for(auto& value:matrix) {std::uint32_t word{};if(!(std::cin>>word))return 2;value=std::bit_cast<float>(word);}
        try {
            BoneSelectContext context;auto handle=present ? start(context,selector(index),bones) : context.begin(selector(index),std::nullopt);
            unsigned calls{};std::optional<AnimationMatrix> captured;
            const auto result=context.resume(handle,[&](auto& live,const auto&)->std::optional<std::int32_t>{
                ++calls;captured=live.model_view()->matrix();return child;
            });
            if(result.stop!=BoneSelectStop::completed || !result.value) {std::cout<<"ERROR\n";continue;}
            std::cout<<*result.value<<' '<<context.pool_cursor()<<' '<<calls<<' '<<context.stack_depth()<<' '<<bool(captured);
            if(captured)for(const auto value:*captured)std::cout<<' '<<std::bit_cast<std::uint32_t>(value);
            std::cout<<'\n';
        }catch(const std::exception&){std::cout<<"ERROR\n";}
    }
    return 0;
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--oracle")return oracle();
        if(argc!=1)return 2;
        common_order();gates_and_numeric_failures();retained_service_and_moves();nested_lifo();faults_and_limits();owner_release_guard();
        std::cout<<"Bone-selector tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
