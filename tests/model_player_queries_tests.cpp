#include "dw4/action_event_runtime.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    bool rejected=false;try {operation();}catch(const std::exception&){rejected=true;}check(rejected,reason);
}
ModelControlState model() {return ModelControlState(ModelMotionPose({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}));}
struct Fixture {
    pf::RuntimeFixture graph;
    ActionEventRuntime events;
    std::array<ControllerObjectId,2> ids;
    std::array<ModelQueryOwner,2> fields;
    std::shared_ptr<std::optional<std::string>> name=std::make_shared<std::optional<std::string>>("Synthetic player");
    std::vector<std::uint32_t> labels;
    explicit Fixture(bool prepare=true,bool active=true) {
        for(const auto text:{"first","second"}) {
            const auto label=graph.add(pf::unknown_node);graph.text(label,2,text);labels.push_back(label);
        }
        const auto initial=graph.document();
        const auto sequences=*initial.field_reference(initial.object(graph.sequence),4);
        const auto wait=*initial.object_list(sequences).front();
        graph.scalar(wait,3,graph.list(pf::named_list,labels));
        auto doc=graph.document();
        for(const auto ref:labels) {
            auto& object=doc.objects.at(doc.references.at(ref).value_index);
            auto descriptor=doc.metadata.object_types.at(object.type_index);
            descriptor.name="beModelCtrlLABEL";descriptor.parent_index=pf::object;
            object.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            doc.metadata.object_types.push_back(std::move(descriptor));
        }
        for(std::uint32_t index=0;index<2;++index) {
            auto bound=decode_player_runtime_binding(doc,graph.runtime_request(index),"synthetic/player.igb",20);
            ids[index]=events.register_model(bound,model(),active);
            fields[index]={{20,graph.model},bound.source(),name,std::make_shared<std::int32_t>(static_cast<std::int32_t>(index)),
                std::make_shared<std::uint8_t>(active?std::uint8_t{1}:std::uint8_t{0}),std::make_shared<bool>(true)};
            if(prepare)events.prepare_model_queries(ids[index],fields[index]);
        }
    }
    MessengerDispatch send(std::string category,std::optional<std::string> target,std::int32_t index,std::string command,
        std::vector<std::optional<std::string>> arguments={}) {
        auto& messenger=events.messenger();messenger.select(std::move(category),std::move(target),index);
        for(auto& argument:arguments)messenger.buffer().add_string(std::move(argument));
        return messenger.begin_send(std::move(command));
    }
};

void explicit_preparation_and_inactive_gates() {
    Fixture f(false,false);
    check(!f.events.messenger().has_category("PLAYER"),"register_model implicitly promoted its owner to PLAYER.");
    rejects([&]{f.events.register_player(f.ids[0]);},"PLAYER registration accepted an unprepared parent/RAM owner.");
    auto inactive=f.send("MODELCTRL",{},0,"GETINFONAME_RUN");
    check(f.events.resume(inactive).stop==MessengerStop::completed && f.events.messenger().buffer().strings().empty(),
        "Inactive GETINFONAME_RUN required name work or appended a fabricated result.");
    auto label=f.send("MODELCTRL",{},0,"SETLABEL");
    check(f.events.resume(label).stop==MessengerStop::completed && f.events.sequence(f.ids[0]).labels().empty(),
        "Inactive SETLABEL read missing payload or prepared label work.");
    auto& native=f.events.messenger();
    auto query=f.send("MODELCTRL",{},0,"GETINFONAME");
    check(f.events.resume(query).stop==MessengerStop::service_required,"Missing live name owner was silently copied from binding metadata.");
    *f.name=std::nullopt;
    f.events.prepare_model_queries(f.ids[0],f.fields[0]);
    check(f.events.resume(query).stop==MessengerStop::completed && native.buffer().strings()==std::vector<std::optional<std::string>>{""},
        "Prepared missing name service did not resume or null name was not appended as non-null empty.");
    rejects([&]{f.events.prepare_model_queries(f.ids[0],f.fields[0]);},"Live prepared fields could be replaced.");
    const auto count=native.registration_count();f.events.register_player(f.ids[0]);
    check(native.registration_count()==count+1,"Explicit PLAYER registration did not append its occurrence.");
    auto registered=f.send("PLAYER",std::string{},0,"GETINFONAME");
    check(f.events.resume(registered).stop==MessengerStop::completed && native.buffer().strings()==std::vector<std::optional<std::string>>{""},
        "PLAYER null parent name did not register under non-null empty.");
}

void actual_live_fields_and_registration_snapshots() {
    Fixture f;
    f.events.register_player(f.ids[0]);f.events.register_player(f.ids[1]);
    *f.name="live name";
    *f.fields[0].active=255;*f.fields[1].active=0;
    auto active=f.send("PLAYER","Synthetic player",-1,"GETINFONAME_RUN");
    check(f.events.resume(active).stop==MessengerStop::completed &&
        f.events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"live name"},
        "GETINFONAME_RUN used stale registration names or normalized the raw active gate incorrectly.");
    auto all=f.send("PLAYER","Synthetic player",-1,"GETINFONAME");
    check(f.events.resume(all).stop==MessengerStop::completed &&
        f.events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"live name","live name"},
        "Ungated GETINFONAME omitted the inactive recipient or deduplicated matching actual RAMs.");
    f.events.set_active(f.ids[0],false);f.events.set_active(f.ids[1],true);
    check(*f.fields[0].active==0 && *f.fields[1].active==1,"set_active maintained a contradictory private model activity flag.");
    auto gated=f.send("PLAYER","Synthetic player",-1,"GETINFONAME");
    f.events.messenger().buffer().set_enabled_only(1);
    check(f.events.resume(gated).stop==MessengerStop::completed &&
        f.events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"live name"},
        "Messenger active filtering did not share the endpoint's live active byte.");
    *f.fields[0].ram_no=1;f.events.register_player(f.ids[0]);
    *f.fields[0].active=2;
    auto fresh=f.send("PLAYER","live name",1,"GETINFONAME_RUN");
    check(f.events.resume(fresh).stop==MessengerStop::completed &&
        f.events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"live name"},
        "Explicit later registration failed to capture current parent-name/RAM fields.");
    auto old=f.send("PLAYER","Synthetic player",0,"GETINFONAME_RUN");
    check(f.events.resume(old).stop==MessengerStop::completed &&
        f.events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"live name"},
        "Live RAM-field changes rewrote an earlier registered routing index.");

    // A different uncached key observes both explicit duplicate occurrences.
    *f.name="duplicate";f.events.register_player(f.ids[0]);f.events.register_player(f.ids[0]);
    auto duplicate=f.send("PLAYER","duplicate",1,"GETINFONAME_RUN");
    check(f.events.resume(duplicate).stop==MessengerStop::completed &&
        f.events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"duplicate","duplicate"},
        "PLAYER collapsed duplicate registrations or imposed a single-name result policy.");
    f.events.messenger().buffer().add_int(91);
    check(f.events.resume(duplicate).stop==MessengerStop::completed && f.events.messenger().buffer().strings().size()==2 &&
        f.events.messenger().buffer().integers()==std::vector<std::int32_t>{91},"Completed query retry appended results or cleared another typed list.");
}

void nullable_labels_and_work_gate() {
    Fixture f;
    *f.fields[0].work_available=false;
    auto absent=f.send("MODELCTRL",{},0,"SETLABEL");
    check(f.events.resume(absent).stop==MessengerStop::completed && f.events.sequence(f.ids[0]).labels().empty(),
        "Absent original work failed to skip SETLABEL payload reads.");
    *f.fields[0].work_available=true;*f.fields[0].active=128;
    auto nulls=f.send("MODELCTRL",{},0,"SETLABEL",{std::nullopt,std::nullopt,"ignored"});
    auto& messenger=f.events.messenger();messenger.buffer().add_int(7);messenger.buffer().add_bool(3);
    check(f.events.resume(nulls).stop==MessengerStop::completed && f.events.sequence(f.ids[0]).labels().size()==1 &&
        !f.events.sequence(f.ids[0]).labels()[0].sequence && !f.events.sequence(f.ids[0]).labels()[0].label,
        "SETLABEL failed to preserve both nullable arguments.");
    check(messenger.buffer().strings()==std::vector<std::optional<std::string>>{std::nullopt,std::nullopt,"ignored"} &&
        messenger.buffer().integers()==std::vector<std::int32_t>{7} && messenger.buffer().booleans()==std::vector<std::uint8_t>{3},
        "SETLABEL consumed extra arguments or fabricated a result packet.");
    auto empty=f.send("MODELCTRL",{},0,"SETLABEL",{std::string{},std::string{}});
    check(f.events.resume(empty).stop==MessengerStop::completed && f.events.sequence(f.ids[0]).labels().size()==2 &&
        f.events.sequence(f.ids[0]).labels()[1].label==std::optional<std::string>{""},"Empty SETLABEL was merged with the null-key entry.");
    auto selected=f.send("MODELCTRL",{},0,"SETLABEL",{"SQ_WAIT","second"});
    check(f.events.resume(selected).stop==MessengerStop::completed,"SETLABEL did not complete its actual sequence mutation.");
    f.events.select_sequence(f.ids[0],"SQ_WAIT");
    check(f.events.sequence(f.ids[0]).command_index()==1 && f.events.sequence(f.ids[0]).labels()[2].cached_offset==1,
        "Messenger SETLABEL did not drive the actual sequence's label selection.");
    auto replace=f.send("MODELCTRL",{},0,"SETLABEL",{"SQ_WAIT",std::nullopt});
    check(f.events.resume(replace).stop==MessengerStop::completed && f.events.sequence(f.ids[0]).labels().size()==3 &&
        !f.events.sequence(f.ids[0]).labels()[2].label && f.events.sequence(f.ids[0]).labels()[2].cached_offset==-1,
        "Replacing a cached label lost null or failed to invalidate the same first-key entry.");
    rejects([&]{f.events.select_sequence(f.ids[0],"SQ_WAIT");},"Null label was silently normalized during original unsafe lookup.");
    check(f.events.sequence(f.ids[0]).command_index()==1 && !f.events.sequence(f.ids[0]).selection_pending(),
        "Rejected null-label lookup partially changed sequence ownership/cursor.");

    Fixture malformed;
    auto missing=malformed.send("MODELCTRL",{},0,"SETLABEL",{"SQ_WAIT"});
    check(malformed.events.resume(missing).stop==MessengerStop::faulted && malformed.events.sequence(malformed.ids[0]).labels().empty(),
        "Active SETLABEL with one string fabricated its second nullable slot.");
    check(malformed.events.resume(missing).stop==MessengerStop::faulted,"Faulted malformed label payload was replayed.");
}

void partial_delivery_preserves_live_name_prefix() {
    Fixture f;
    const auto first=ControllerObjectId::allocate_native(),last=ControllerObjectId::allocate_native();
    unsigned prefix_calls{},tail_calls{};
    f.events.register_native_controller(first,{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&]{return MessengerManager{[&](NativeMessenger& current,const MessengerDelivery&) {
            ++prefix_calls;*f.name="changed inside first callback";current.buffer().add_string("prefix");
            return MessengerCallResult{true,1,{}};
        },{}};}},"PLAYER","earlier",0);
    f.events.register_player(f.ids[0]);
    f.events.register_recipient(last,"PLAYER","later",0);
    auto& native=f.events.messenger();
    auto query=f.send("PLAYER",{},0,"GETINFONAME_RUN");
    native.buffer().add_int(77);
    const auto paused=f.events.resume(query);
    const NativeMessenger& observed=native;
    const auto packet=observed.buffer_owner();
    check(paused.stop==MessengerStop::service_required && paused.recipients_completed==2 && prefix_calls==1 &&
        packet->strings()==std::vector<std::optional<std::string>>{"prefix","changed inside first callback"},
        "Name query captured a stale name before the real earlier callback or lost its completed recipient prefix.");
    *f.name="changed while paused";
    f.events.register_native_controller(last,{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&]{return MessengerManager{[&](NativeMessenger& current,const MessengerDelivery&) {
            ++tail_calls;current.buffer().add_string("tail");return MessengerCallResult{true,1,{}};
        },{}};}});
    check(f.events.resume(query).stop==MessengerStop::completed && prefix_calls==1 && tail_calls==1 &&
        packet==observed.buffer_owner() && packet->integers()==std::vector<std::int32_t>{77} &&
        packet->strings()==std::vector<std::optional<std::string>>{"prefix","changed inside first callback","tail"},
        "Resumed query replayed a completed model recipient, replaced its earlier name result or changed the live packet owner.");
}

void label_ownership_guards_and_valid_nested_delivery() {
    Fixture f;
    f.events.select_sequence(f.ids[0],"SQ_WAIT");
    auto sound=f.send("MODELCTRL",{},0,"SETSOUND",{"owned loop"});
    f.events.messenger().buffer().add_float(0);
    check(f.events.resume(sound).stop==MessengerStop::completed,"Guard fixture failed to append its real sound work.");
    ModelSubcommandServices services;
    unsigned sound_starts{};
    services.play_sound=[&](const auto& name) {
        ++sound_starts;check(name=="owned loop","Guard fixture changed the retained sound owner.");
        return ModelSoundPlayResult{true,101,4};
    };
    check(f.events.update_subcommands(f.ids[0],0,services).complete() && sound_starts==1,
        "Guard fixture failed to retain a started looping sound.");
    check(f.events.select_sequence(f.ids[0],"SQ_WAIT").stop==ModelSubcommandStop::sound_volume_down_required &&
        f.events.sequence(f.ids[0]).selection_pending(),"Guard fixture failed to suspend the original sequence cleanup.");
    const auto cursor=f.events.sequence(f.ids[0]).command_index();
    auto label=f.send("MODELCTRL",{},0,"SETLABEL",{"SQ_WAIT","second"});
    const auto conflict=f.events.resume(label);
    check(conflict.stop==MessengerStop::faulted && f.events.sequence(f.ids[0]).selection_pending() &&
        f.events.sequence(f.ids[0]).command_index()==cursor && f.events.sequence(f.ids[0]).labels().empty() &&
        f.events.subcommands(f.ids[0]).entries().size()==1,
        "Conflicting SETLABEL mutated pending work or invented an unresolvable missing-service continuation.");
    check(f.events.resume(label).stop==MessengerStop::faulted && sound_starts==1,
        "Conflicting label retry replayed completed sound work.");

    Fixture nested;
    const auto sender=ControllerObjectId::allocate_native();
    unsigned calls{};
    nested.events.register_native_controller(sender,{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&]{return MessengerManager{[&](NativeMessenger& messenger,const MessengerDelivery&) {
            ++calls;messenger.select("MODELCTRL",{},1);messenger.buffer().add_string("SQ_WAIT");
            messenger.buffer().add_string("second");auto child=messenger.begin_send("SETLABEL");
            check(nested.events.resume(child).stop==MessengerStop::completed,"Valid nested SETLABEL delivery was rejected.");
            return MessengerCallResult{true,1,{}};
        },{}};}},"HOST","sender",0);
    auto outer=nested.send("HOST","sender",0,"apply label");
    check(nested.events.resume(outer).stop==MessengerStop::completed && calls==1 &&
        nested.events.sequence(nested.ids[1]).labels().size()==1 && nested.events.sequence(nested.ids[0]).labels().empty(),
        "A valid nested delivery failed to mutate only its actual target sequence.");
    nested.events.select_sequence(nested.ids[1],"SQ_WAIT");
    check(nested.events.sequence(nested.ids[1]).command_index()==1,"Nested SETLABEL did not drive real target label lookup.");
}

void owner_validation_and_missing_service() {
    Fixture f(false);
    const auto base=f.fields[0];
    for(unsigned variant=0;variant<8;++variant) {
        auto invalid=base;
        switch(variant) {
        case 0:invalid.source="alien.igb";break;
        case 1:++invalid.parent_info.file_index;break;
        case 2:++invalid.parent_info.reference_index;break;
        case 3:invalid.parent_info=ControllerObjectId::allocate_native();break;
        case 4:invalid.ram_no=std::make_shared<std::int32_t>(1);break;
        case 5:invalid.active.reset();break;
        case 6:invalid.parent_name.reset();break;
        case 7:invalid.work_available.reset();break;
        }
        rejects([&]{f.events.prepare_model_queries(f.ids[0],invalid);},"Alien/missing MODELCTRL field ownership was accepted.");
    }
    auto& messenger=f.events.messenger();
    auto unavailable=f.send("MODELCTRL",{},0,"SETLABEL",{"SQ_WAIT","first"});
    check(f.events.resume(unavailable).stop==MessengerStop::service_required && f.events.sequence(f.ids[0]).labels().empty(),
        "Active missing query/work owner was treated as absent work or a successful label effect.");
    f.events.prepare_model_queries(f.ids[0],base);
    check(f.events.resume(unavailable).stop==MessengerStop::completed && f.events.sequence(f.ids[0]).labels()[0].label=="first" &&
        messenger.buffer().strings()==std::vector<std::optional<std::string>>{"SQ_WAIT","first"},
        "Supplying the missing owner failed to resume the retained SETLABEL packet.");
    auto alias=f.fields[1];alias.parent_name=std::make_shared<std::optional<std::string>>(*base.parent_name);
    rejects([&]{f.events.prepare_model_queries(f.ids[1],alias);},"Two RAMs of the same physical info owned independent name fields.");
    alias=f.fields[1];alias.active=base.active;
    rejects([&]{f.events.prepare_model_queries(f.ids[1],alias);},"Distinct actual RAMs shared their physical active-byte field.");
    f.events.prepare_model_queries(f.ids[1],f.fields[1]);
    *f.fields[1].ram_no=-1;
    const auto before=messenger.registration_count();
    rejects([&]{f.events.register_player(f.ids[1]);},"Invalid live prepared RAM ordinal was registered.");
    check(messenger.registration_count()==before,"Failed PLAYER registration partially appended a row.");
    auto unknown=f.send("MODELCTRL",{},0,"SETMETAL");
    check(f.events.resume(unknown).stop==MessengerStop::service_required,"Unimplemented PC status prelude/effect was reported complete.");
}

void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    ActionEventRuntime events;
    auto name=std::make_shared<std::optional<std::string>>("p_agumon");
    std::array<ControllerObjectId,4> ids;
    std::array<ModelQueryOwner,4> fields;
    for(std::uint32_t index=0;index<4;++index) {
        auto binding=decode_player_runtime_binding(doc,{1386,546,index,0},"_common/p_agumon.igb",30);
        ids[index]=events.register_model(binding,model(),true);
        fields[index]={{30,1386},binding.source(),name,std::make_shared<std::int32_t>(static_cast<std::int32_t>(index)),
            std::make_shared<std::uint8_t>(std::uint8_t{1}),std::make_shared<bool>(true)};
        events.prepare_model_queries(ids[index],fields[index]);events.register_player(ids[index]);
    }
    for(std::int32_t index=0;index<4;++index) {
        auto& messenger=events.messenger();messenger.select("PLAYER",{},index);auto query=messenger.begin_send("GETINFONAME_RUN");
        check(events.resume(query).stop==MessengerStop::completed && messenger.buffer().strings()==std::vector<std::optional<std::string>>{"p_agumon"},
            "Actual prepared PLAYER query failed its exact one-name result.");
        messenger.select("MODELCTRL","p_agumon",index);messenger.buffer().add_string("SQ_WAIT");messenger.buffer().add_string("slash");
        auto label=messenger.begin_send("SETLABEL");
        check(events.resume(label).stop==MessengerStop::completed,"Actual MODELCTRL SETLABEL did not complete.");
        events.select_sequence(ids[static_cast<std::size_t>(index)],"SQ_WAIT");
        check(events.sequence(ids[static_cast<std::size_t>(index)]).command_index()==4,
            "Actual slash SETLABEL did not select the authored SQ_WAIT label ordinal.");
        *fields[static_cast<std::size_t>(index)].active=0;
        messenger.select("PLAYER",{},index);auto inactive=messenger.begin_send("GETINFONAME_RUN");
        check(events.resume(inactive).stop==MessengerStop::completed && messenger.buffer().strings().empty(),
            "Actual inactive PLAYER appended a name.");
    }
}
} // namespace
int main(int argc,char** argv) {
    try {
        explicit_preparation_and_inactive_gates();actual_live_fields_and_registration_snapshots();nullable_labels_and_work_gate();
        partial_delivery_preserves_live_name_prefix();label_ownership_guards_and_valid_nested_delivery();owner_validation_and_missing_service();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_model_player_queries_tests [EXTRACTED_ROOT]");
        std::cout<<"MODELCTRL player query checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"MODELCTRL player query after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
