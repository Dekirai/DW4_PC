#include "player_runtime_binding_fixture.hpp"
#include "dw4/resources.hpp"

#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace player_runtime_fixture;
int checks{};
void check(bool condition,const char* reason) { ++checks; if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason,std::string_view message={}) {
    bool caught=false;
    try { operation(); }
    catch (const PlayerRuntimeBindingError& error) {
        caught=true;
        if (!message.empty()) check(std::string_view(error.what()).find(message)!=std::string_view::npos,
            "Malformed fixture stopped before its intended ownership guard");
    }
    check(caught,reason);
}
void synthetic() {
    RuntimeFixture f;
    const auto binding=f.binding();
    check(binding.source()=="synthetic/player.igb" && binding.source_occurrence()==3,"Source occurrence is explicit and retained");
    check(binding.model_reference()==f.model && binding.action_reference()==f.action && binding.model_name()=="Synthetic player",
        "Both exported info identities and their exact target name are retained");
    check(binding.model_ram_reference()==f.ram && binding.one_reference()==f.one && binding.node_reference()==f.modelnode &&
        binding.lod_reference()==f.lod && binding.sequence_reference()==f.sequence,"MODELCTRL instance chain is retained as typed IDs");
    check(binding.action_ram_reference()==f.action_rams[0] && binding.combiner_reference()==f.combiners[0],
        "The actual corresponding ACTION RAM and combiner are retained");
    check(binding.instance_index()==0 && binding.branch_index()==1 && binding.selected_actor_index()==0,
        "Selected LOD actor is matched by identity rather than branch ordinal");
    check(binding.actors().size()==3 && binding.actors()[0].actor_reference==f.alternate_actor && binding.actors()[2].actor_reference==f.actor,
        "All actor metadata preserves ACTION RAM order");
    check(binding.skin_binding().actor_index()==0 && binding.skin_binding().actor_reference()==f.alternate_actor &&
        binding.skin_binding().geometry_reference()==f.geom && binding.skin_binding().skin().source_positions.size()==3,
        "Only the explicitly selected actor owns the retained decoded skin");
    check(binding.action_program().source()==binding.source() && binding.sequence_program().source()==binding.source() &&
        binding.sequence_program().sequences().size()==1,"Programs retain the same explicit source and actual SEQ data");
    const auto doc=f.document();
    const auto second=decode_player_runtime_binding(doc,f.runtime_request(1,1),binding.source(),3);
    check(second.model_ram_reference()==f.model_rams[1] && second.node_reference()==f.nodes[1] &&
        second.action_ram_reference()==f.action_rams[1] && second.selected_actor_index()==2,
        "Second instance follows both real node chains and its independently ordered actor list");
    const auto repeated=decode_player_runtime_binding(doc,f.runtime_request(),binding.source(),4);
    check(repeated.source()==binding.source() && repeated.source_occurrence()!=binding.source_occurrence() &&
        repeated.model_ram_reference()==binding.model_ram_reference(),"Repeated loaded paths preserve distinct source occurrences");
    const auto copy=binding;
    check(&copy.sequence_program()==&binding.sequence_program() && &copy.skin_binding()==&binding.skin_binding(),
        "Binding copies share immutable owned data without re-decoding or losing frame identity");
    auto retained=[] { RuntimeFixture temporary; return temporary.binding(9); }();
    check(retained.source_occurrence()==9 && retained.skin_binding().skin().source_positions[0].x==2,
        "Binding lifetime does not borrow document or request storage");
    const auto differently_named=decode_player_runtime_binding(doc,f.runtime_request(),"other/occurrence.igb",7);
    check(differently_named.source()=="other/occurrence.igb" && differently_named.skin_binding().source()==differently_named.source(),
        "Caller source qualification propagates without inferring a document path");
}
void malformed() {
    RuntimeFixture f;
    const auto reject=[&](const RuntimeFixture& value,std::string_view message={}) {
        const auto doc=value.document();
        rejects([&]{decode_player_runtime_binding(doc,value.runtime_request(),"synthetic/player.igb",3);},"Malformed player graph rejects",message);
    };
    auto bad=f; bad.scalar(bad.action_rams[0],11,bad.nodes[1]); bad.scalar(bad.action_rams[1],11,bad.nodes[0]);
    reject(bad,"ONE node differs");
    bad=f; bad.contents(bad.instances,{bad.model_rams[1],bad.model_rams[0]}); reject(bad,"ONE node differs");
    bad=f; bad.contents(bad.instances,{bad.ram,bad.ram}); reject(bad,"aliased MODELCTRL RAM");
    bad=f; bad.scalar(bad.model_rams[1],10,bad.data); reject(bad,"aliased ONE record");
    bad=f; bad.scalar(bad.ones[1],4,bad.modelnode); reject(bad,"aliased model node");
    bad=f; bad.scalar(bad.ones[1],5,bad.one); reject(bad,"serialized ONE work");
    bad=f; bad.contents(bad.actor_lists[1],bad.ordered_actors[0]);
    bad.scalar(bad.action_rams[1],12,bad.appearance_lists[0]); reject(bad,"aliased actor");
    bad=f; bad.contents(bad.branches,{bad.actor,bad.actor,bad.alternate_actor}); reject(bad,"aliased LOD actor");
    bad=f; bad.contents(bad.branches,{bad.actor,bad.alternate_actor,bad.ordered_actors[1][0]}); reject(bad,"absent from the actual model LOD");
    bad=f; bad.contents(bad.actor_lists[0],{bad.ordered_actors[0][0],bad.ordered_actors[0][1]});
    // Keep the actor graph itself complete so the branch ownership check is reached.
    const auto original_doc=f.document();
    bad.contents(bad.appearance_lists[0],{*original_doc.field_reference(original_doc.object(bad.ordered_actors[0][0]),11),
        *original_doc.field_reference(original_doc.object(bad.ordered_actors[0][1]),11)});
    reject(bad,"LOD branches and ordered ACTION actors differ");
    bad=f; bad.text(bad.action,2,"another player"); reject(bad,"names do not identify");
    bad=f; bad.contents(bad.info_reference,{bad.model}); reject(bad,"not an explicit export");
    bad=f; bad.contents(bad.info_reference,{bad.model,bad.action,bad.action}); reject(bad,"aliased export");
    bad=f; bad.text(bad.model,7,"KIND_ENEMY"); reject(bad,"not KIND_PLAYER");
    bad=f; bad.contents(bad.model_data,{bad.sequence,bad.sequence}); reject(bad,"ambiguous SEQ");
    bad=f; bad.contents(bad.model_data,{}); reject(bad,"no explicit SEQ");
    bad=f; bad.scalar(bad.instances,3,1025); reject(bad,"list allocation");
    const auto doc=f.document();
    rejects([&]{decode_player_runtime_binding(doc,f.runtime_request(2),"synthetic/player.igb",3);},"Out-of-range instance rejects");
    rejects([&]{decode_player_runtime_binding(doc,f.runtime_request(0,3),"synthetic/player.igb",3);},"Out-of-range explicit branch rejects");
    rejects([&]{decode_player_runtime_binding(doc,f.runtime_request(),"",3);},"Source cannot be inferred");
    rejects([&]{decode_player_runtime_binding(doc,f.runtime_request(),std::string("a\0b",3),3);},"Source rejects embedded NUL");
    rejects([&]{decode_player_runtime_binding(doc,f.runtime_request(),std::string(4097,'a'),3);},"Source byte allocation is bounded");
    rejects([&]{decode_player_runtime_binding(doc,f.runtime_request(),"synthetic/player.igb",std::numeric_limits<std::size_t>::max());},
        "Invalid source occurrence sentinel rejects");
    auto aliases=doc;
    aliases.references[f.model_rams[1]].value_index=aliases.references[f.model_rams[0]].value_index;
    rejects([&]{decode_player_runtime_binding(aliases,f.runtime_request(),"synthetic/player.igb",3);},
        "Distinct serialized reference numbers cannot disguise one RAM object","aliased MODELCTRL RAM");
    auto typed=doc; auto& model=typed.objects[typed.references[f.model].value_index];
    const_cast<IgbFieldValue*>(typed.field(model,5))->type_index=1;
    rejects([&]{decode_player_runtime_binding(typed,f.runtime_request(),"synthetic/player.igb",3);},
        "Model data reference metadata is checked","field type or width");
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    constexpr std::array<std::uint32_t,4> model_rams{1725,6417,6420,6622};
    constexpr std::array<std::uint32_t,4> action_rams{250,251,252,253};
    constexpr std::array<std::uint32_t,4> nodes{6390,6457,6524,6590};
    constexpr std::array<std::uint32_t,4> combiners{6396,6463,6530,6596};
    constexpr std::array<std::uint32_t,3> geometries{413,5665,262};
    for (std::uint32_t instance=0;instance<4;++instance) for (std::uint32_t branch=0;branch<3;++branch) {
        const auto binding=decode_player_runtime_binding(doc,{1386,546,instance,branch},"_common/p_agumon.igb",17);
        check(binding.model_ram_reference()==model_rams[instance] && binding.action_ram_reference()==action_rams[instance] &&
            binding.node_reference()==nodes[instance] && binding.combiner_reference()==combiners[instance],
            "Original MODELCTRL/ACTION RAM nodes and combiners must agree for all players");
        check(binding.model_name()=="p_agumon" && binding.sequence_reference()==14 && binding.sequence_program().sequences().size()==28,
            "Original model SEQ belongs to the explicit player export");
        check(binding.actors().size()==3 && binding.skin_binding().geometry_reference()==geometries[branch] &&
            binding.actors()[binding.selected_actor_index()].actor_reference==binding.skin_binding().actor_reference(),
            "All original appearance branches bind their actual ordered actor and geometry");
        check(binding.source_occurrence()==17 && binding.skin_binding().skin().bones.size()==25 && binding.skin_binding().skin().joints.size()==20,
            "Original selected skin retains qualified source and complete skeleton binding");
    }
    std::cout<<"Original player bindings=12 instances=4 branches=3\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        synthetic(); malformed();
        if (args.size()==2) original(args[1]); else if (args.size()!=1) throw std::runtime_error("Usage: dw4_player_runtime_binding_tests [EXTRACTED_ROOT]");
        std::cout<<checks<<" player runtime binding checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Player runtime binding failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
