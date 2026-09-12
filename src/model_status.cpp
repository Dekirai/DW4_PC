#include "dw4/model_status.hpp"
#include "dw4/action_event_runtime.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw ModelScriptError(message); }
constexpr std::array<std::string_view,16> commands={"SETPOISON","SETSLEEP","SETSTUN","SETBIND",
    "SETATTACKDOWN","SETPOWERDOWN","SETRAREICON","SETSPEEDUP","SETPOWERUP","SETREFRACT",
    "SETMETAL","CLEARMETAL","CHECKSTATUS","CLEARSTATUS","ALLCLEARSTATUS","GETPEBASEWORK"};
template<class T> const std::shared_ptr<std::vector<T>>& list(const std::shared_ptr<std::vector<T>>& value) {
    if(!value || value->size()>65536)fail("Original model status requires an existing bounded list at this read.");
    return value;
}
std::shared_ptr<ModelStatusFields> status_fields(std::shared_ptr<ModelStatusFields> fields) {
    if(!fields)fail("Model status requires its actual canonical field owner.");
    return fields;
}
std::shared_ptr<ModelStatusFields> prepared_status_fields(const std::shared_ptr<ModelScriptWork>& work,std::uint8_t player) {
    if(!work)fail("Prepared model status requires its actual shared model work owner.");
    auto fields=work->status_fields_owner();
    if(!fields)fields=std::make_shared<ModelStatusFields>();
    fields->is_player=player;return fields;
}
}
ModelStatusWork::ModelStatusWork(std::shared_ptr<ModelScriptWork> work,std::uint8_t player)
    :ModelStatusWork(work,prepared_status_fields(work,player)) {}
ModelStatusWork::ModelStatusWork(std::shared_ptr<ModelScriptWork> work,std::shared_ptr<ModelStatusFields> fields)
    :fields_(status_fields(std::move(fields))),actual_work_(std::move(work)) {
    if(!actual_work_)fail("Prepared model status requires its actual shared model work owner.");
    if(actual_work_->status_fields_owner() && actual_work_->status_fields_owner()!=fields_)
        fail("Model status fields conflict with the captured canonical script-work generation.");
}
struct ModelStatusInvocation::Impl {
    enum class Phase { prelude,command,down_data,query_prepare,query,completed };
    ModelStatusOwner owner;
    std::string command;
    std::size_t command_index{};
    Phase phase=Phase::prelude;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::optional<MessengerDispatch> dispatch;
    std::shared_ptr<GameDataList> row;
    bool fault{},executing{};
    std::uint32_t returned{};
    Impl(ModelStatusOwner value,std::string name):owner(std::move(value)),command(std::move(name)) {
        command_index=static_cast<std::size_t>(std::find(commands.begin(),commands.end(),command)-commands.begin());
    }
};
ModelStatusInvocation::ModelStatusInvocation(ModelStatusOwner owner,std::optional<std::string> command) {
    if(!command || command->size()>65535 || command->find('\0')!=std::string::npos)
        fail("Prepared model status requires its actual non-null bounded command string.");
    impl_=std::make_unique<Impl>(std::move(owner),std::move(*command));
}
ModelStatusInvocation::~ModelStatusInvocation()=default;
ModelStatusInvocation::ModelStatusInvocation(ModelStatusInvocation&&) noexcept=default;
ModelStatusInvocation& ModelStatusInvocation::operator=(ModelStatusInvocation&&) noexcept=default;
bool ModelStatusInvocation::completed() const noexcept {return impl_ && impl_->phase==Impl::Phase::completed;}
bool ModelStatusInvocation::faulted() const noexcept {return !impl_ || impl_->fault;}
const ModelStatusOwner& ModelStatusInvocation::owner() const {
    if(!impl_)fail("A moved model status invocation has no owner.");return impl_->owner;
}
const std::shared_ptr<GameDataList>& ModelStatusInvocation::captured_row() const {
    if(!impl_)fail("A moved model status invocation has no captured row.");return impl_->row;
}
MessengerCallResult ModelStatusInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!impl_ || impl_->fault)fail("An uncertain model status invocation cannot be replayed.");
    auto& state=*impl_;
    if(state.executing || (state.messenger && (state.messenger!=&messenger || state.events!=&events)))
        fail("A retained model status invocation requires the same nonrecursive messenger owners.");
    if(state.phase==Impl::Phase::completed)return {true,state.returned,{}};
    state.messenger=&messenger;state.events=&events;
    struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(state.executing);
    try {
        const auto& work=state.owner.work;
        if(state.phase==Impl::Phase::prelude) {
            // Classifier -1 declines before the shared initialization prefix.
            if(state.command_index==commands.size()) {
                state.phase=Impl::Phase::completed;return {true,0,{}};
            }
            if(state.command_index!=12 && state.command_index!=13 && work && !work->display_ram_numbers()) {
                // Publish each original list before constructing the next. An
                // exception leaves the already committed prefix in place.
                work->display_ram_numbers()=std::make_shared<std::vector<std::int32_t>>(12,-1);
                work->display_positions()=std::make_shared<std::vector<std::int32_t>>(12,-1);
                work->timers()=std::make_shared<std::vector<std::int32_t>>(12,0);
                work->first_flags()=std::make_shared<std::vector<std::uint8_t>>(11,std::uint8_t{0});
            }
            state.phase=Impl::Phase::command;
        }
        if(state.phase==Impl::Phase::command) {
            if(state.command_index!=10)
                return {false,0,"Original model status command requires its native service: "+state.command};
            if(!work) {state.returned=1;state.phase=Impl::Phase::completed;return {true,1,{}};}
            const auto& flags=list(work->first_flags());
            if(flags->size()>7)(*flags)[7]=1;
            const auto& timers=list(work->timers());
            if(timers->size()>7)(*timers)[7]=1;
            if(work->down_data()) {state.returned=1;state.phase=Impl::Phase::completed;return {true,1,{}};}
            state.phase=Impl::Phase::down_data;
        }
        if(state.phase==Impl::Phase::down_data) {
            if(work->is_player()==0) {
                work->down_data()=std::make_shared<std::vector<std::int32_t>>(6,0);
                state.returned=1;state.phase=Impl::Phase::completed;return {true,1,{}};
            }
            state.phase=Impl::Phase::query_prepare;
        }
        if(state.phase==Impl::Phase::query_prepare) {
            if(!messenger.has_category("GAMERAM"))
                return {false,0,"Original model status DOWNDATA requires the actual GAMERAM provider."};
            messenger.push_buffer();
            messenger.select("GAMERAM",std::nullopt,-1);
            if(!state.owner.live_ram_no)fail("Original model status DOWNDATA requires its live RAM field owner.");
            messenger.buffer().add_int(*state.owner.live_ram_no);
            state.dispatch.emplace(messenger.begin_send("GETPLAYER"));
            state.phase=Impl::Phase::query;
        }
        const auto result=events.resume(*state.dispatch);
        if(result.stop==MessengerStop::faulted)fail("Original model status GETPLAYER delivery faulted.");
        if(result.stop!=MessengerStop::completed)return {false,0,result.diagnostic};
        const auto& objects=messenger.buffer().objects();
        const auto* row=objects.empty()?nullptr:std::get_if<MessengerDataList>(&objects[0]);
        if(!row || !*row)fail("Original model status GETPLAYER requires its non-null data row at object zero.");
        state.row=*row;
        const auto wrapper=state.row->require<GameDataIntList>("DOWNDATA");
        if(wrapper->data && wrapper->data->size()>65536)
            fail("Original model status DOWNDATA exceeds the bounded native list profile.");
        // The original helper permits a null payload and overwrites any value
        // installed by a nested callback before restoring the parent's packet.
        work->down_data()=wrapper->data;
        messenger.pop_buffer();
        state.dispatch.reset();state.returned=1;state.phase=Impl::Phase::completed;
        return {true,1,{}};
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
