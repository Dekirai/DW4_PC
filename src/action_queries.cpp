#include "dw4/action_queries.hpp"

#include <limits>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) { throw MessengerError(reason); }
ControllerObjectId identity(const PlayerRuntimeBinding& binding) {
    if (binding.source_occurrence()>std::numeric_limits<std::uint32_t>::max() ||
        binding.instance_index()>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        fail("ACTION query binding identity exceeds the original messenger widths.");
    return {static_cast<std::uint32_t>(binding.source_occurrence()),binding.action_ram_reference()};
}
void validate(const PlayerRuntimeBinding& binding,const ActionRuntime& runtime) {
    const auto& program=runtime.program();
    if (program.source()!=binding.source() || program.reference()!=binding.action_reference() ||
        program.name()!=binding.model_name() || binding.instance_index()>=program.rams().size())
        fail("ACTION query runtime differs from its bound source or info.");
    const auto& ram=program.rams()[binding.instance_index()];
    if (ram.reference!=binding.action_ram_reference() || ram.node_reference!=binding.node_reference() ||
        ram.combiner_reference!=binding.combiner_reference() || runtime.combiner().source()!=binding.source() ||
        runtime.combiner().reference()!=binding.combiner_reference())
        fail("ACTION query runtime differs from its bound RAM, node or combiner.");
    const auto* actors=runtime.actors();
    if (!actors || actors->source()!=binding.source() || actors->ram_reference()!=binding.action_ram_reference() ||
        actors->combiner_reference()!=binding.combiner_reference() || actors->definitions().size()!=binding.actors().size())
        fail("ACTION query requires the binding's actual owned actor RAM.");
    for (std::size_t i=0;i<binding.actors().size();++i) {
        const auto& actor=actors->definitions()[i]; const auto& expected=binding.actors()[i];
        if (actor.reference!=expected.actor_reference || actor.appearance_reference!=expected.appearance_reference ||
            actor.bone_count!=binding.skin_binding().skin().bones.size() ||
            actor.joint_count!=binding.skin_binding().skin().joints.size())
            fail("ACTION query actor ownership differs from its bound graph.");
    }
}
}
MessengerManager make_action_query_manager(const PlayerRuntimeBinding& binding,std::shared_ptr<ActionRuntime> runtime) {
    if (!runtime) fail("ACTION query requires a retained live runtime.");
    const auto recipient=identity(binding); validate(binding,*runtime);
    return {
        [](NativeMessenger&,const MessengerDelivery&) { return MessengerCallResult{true,0,{}}; },
        [binding,runtime=std::move(runtime),recipient](NativeMessenger& messenger,const MessengerDelivery& delivery,
            std::string_view command) {
            if (delivery.recipient!=recipient) fail("ACTION query delivered to a different qualified RAM.");
            validate(binding,*runtime); // A shared object's reassignment must not change ownership silently.
            if (command!="ISCOMEND")
                return MessengerCallResult{false,0,"This ACTION command requires its original controller service."};
            const auto result=runtime->query_command_end();
            if (!result) return MessengerCallResult{false,0,"ACTION ISCOMEND requires selected command work."};
            messenger.buffer().add_bool(*result ? 1 : 0);
            return MessengerCallResult{true,1,{}};
        }
    };
}
} // namespace dw4
