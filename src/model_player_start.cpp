#include "dw4/model_player_start.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/assets.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t text_limit=65535,list_limit=65536,default_read_limit=64;
constexpr std::uint32_t default_table=0x804e7c70;
[[noreturn]] void fail(const char* message) {throw GameDataError(message);}
void text(const std::string& value) {
    if(value.size()>text_limit || value.find('\0')!=std::string::npos)
        fail("Prepared MODELCTRL caller text exceeds the bounded original string profile.");
}
void ram_number(std::int32_t value) {
    if(value<0 || value>=4)fail("Prepared MODELCTRL player caller requires live RAM zero through three.");
}
template<class T> std::shared_ptr<std::vector<T>> bounded(std::shared_ptr<std::vector<T>> value) {
    if(!value || value->size()>list_limit)fail("Prepared MODELCTRL player caller requires a bounded existing list.");
    return value;
}
std::uint32_t be32(const std::uint8_t* bytes) {
    return (std::uint32_t(bytes[0])<<24)|(std::uint32_t(bytes[1])<<16)|
        (std::uint32_t(bytes[2])<<8)|bytes[3];
}
const assets::DolSection& data_section(const assets::DolInfo& info,std::uint32_t address,std::size_t size) {
    for(const auto& section:info.sections)
        if(!section.executable && address>=section.address &&
            std::uint64_t(address)+size<=std::uint64_t(section.address)+section.size)return section;
    fail("Original MODELCTRL default name address is outside an initialized DOL data section.");
}
void read_at(std::ifstream& input,std::uint64_t offset,std::span<std::uint8_t> bytes) {
    input.seekg(static_cast<std::streamoff>(offset));
    input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!input || static_cast<std::size_t>(input.gcount())!=bytes.size())
        fail("Original MODELCTRL default name data is truncated.");
}
}
ModelPlayerStartDefaults::ModelPlayerStartDefaults(std::array<std::string,4> names):names_(std::move(names)) {
    for(const auto& name:names_)text(name);
}
ModelPlayerStartDefaults read_model_player_start_defaults(const std::filesystem::path& main_dol) {
    try {
        const auto info=assets::inspect_dol(main_dol);
        const auto& table=data_section(info,default_table,16);
        std::ifstream input(main_dol,std::ios::binary|std::ios::ate);
        if(!input || input.tellg()<0 || static_cast<std::uint64_t>(input.tellg())!=info.file_size)
            fail("Original MODELCTRL DOL changed or could not be reopened.");
        std::array<std::uint8_t,16> pointers{};
        read_at(input,std::uint64_t(table.file_offset)+(default_table-table.address),pointers);
        std::array<std::string,4> names;
        for(std::size_t i=0;i<names.size();++i) {
            const auto address=be32(pointers.data()+4*i);
            const auto& section=data_section(info,address,1);
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(default_read_limit,
                std::uint64_t(section.address)+section.size-address));
            std::array<std::uint8_t,default_read_limit> bytes{};
            read_at(input,std::uint64_t(section.file_offset)+(address-section.address),std::span(bytes).first(count));
            const auto end=std::find(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(count),std::uint8_t{});
            if(end==bytes.begin()+static_cast<std::ptrdiff_t>(count))
                fail("Original MODELCTRL default name has no terminator in its bounded data range.");
            names[i].assign(reinterpret_cast<const char*>(bytes.data()),static_cast<std::size_t>(end-bytes.begin()));
        }
        return ModelPlayerStartDefaults(std::move(names));
    }catch(const GameDataError&) {throw;}
     catch(const std::exception& error) {throw GameDataError(error.what());}
}

struct ModelPlayerStart::Impl {
    std::optional<ModelPlayerStartOwner> owner;
    ModelPlayerStartCapture capture;
    std::shared_ptr<GameDataList> row;
    ModelPlayerStartDefaults defaults;
    std::optional<ModelGameDataBinding> binding;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::optional<MessengerDispatch> dispatch;
    ModelPlayerStartPhase phase=ModelPlayerStartPhase::binding;
    bool executing{},fault{};
    Impl(ModelPlayerStartCapture value,std::shared_ptr<GameDataList> captured,ModelPlayerStartDefaults names)
        :capture(std::move(value)),row(std::move(captured)),defaults(std::move(names)) {}
    std::int32_t live_ram() const {const auto value=*capture.live_ram_no;ram_number(value);return value;}
    std::optional<ModelPlayerStartResult> deliver() {
        const auto result=events->resume(*dispatch);
        if(result.stop==MessengerStop::faulted)fail("Original MODELCTRL player caller message delivery faulted.");
        if(result.stop!=MessengerStop::completed)return ModelPlayerStartResult{ModelPlayerStartStop::service_required,result.diagnostic};
        dispatch.reset();return {};
    }
};
ModelPlayerStart::ModelPlayerStart(ModelPlayerStartOwner owner,std::shared_ptr<GameDataList> row,
    ModelPlayerStartDefaults defaults) {
    if(!owner.work || !row || !owner.parent_name || !owner.live_ram_no)
        fail("Prepared MODELCTRL player caller requires its captured work, row and live field owners.");
    const auto ram=owner.work->target();
    if(ram.is_native() || !ram.valid() || ram.reference_index==std::numeric_limits<std::uint32_t>::max() ||
        owner.captured_parent_info.is_native() || !owner.captured_parent_info.valid() ||
        owner.captured_parent_info.reference_index==std::numeric_limits<std::uint32_t>::max() ||
        ram.file_index!=owner.captured_parent_info.file_index || owner.parent_source!=owner.work->source())
        fail("Prepared MODELCTRL player caller work and captured info require the same actual source occurrence.");
    ram_number(*owner.live_ram_no);
    if(*owner.live_ram_no!=owner.work->ram_no())
        fail("Prepared MODELCTRL player caller initial RAM number differs from its captured work.");
    if(*owner.parent_name)text(**owner.parent_name);
    ModelPlayerStartCapture capture{owner.work->fields_owner(),owner.parent_name,owner.live_ram_no};
    impl_=std::make_unique<Impl>(std::move(capture),std::move(row),std::move(defaults));
    impl_->owner=std::move(owner);
}
ModelPlayerStart::ModelPlayerStart(ModelPlayerStartCapture capture,std::shared_ptr<GameDataList> row,
    ModelPlayerStartDefaults defaults) {
    if(!capture.fields || !capture.live_ram_no || !row)
        fail("Original MODELCTRL player caller requires retained work, row and live field owners.");
    impl_=std::make_unique<Impl>(std::move(capture),std::move(row),std::move(defaults));
}
ModelPlayerStart::~ModelPlayerStart()=default;
ModelPlayerStart::ModelPlayerStart(ModelPlayerStart&&) noexcept=default;
ModelPlayerStart& ModelPlayerStart::operator=(ModelPlayerStart&&) noexcept=default;
ModelPlayerStartPhase ModelPlayerStart::phase() const {
    if(!impl_)fail("A moved MODELCTRL player caller has no phase.");
    return impl_->phase;
}
bool ModelPlayerStart::prefix_completed() const noexcept {return impl_ && impl_->phase==ModelPlayerStartPhase::later_ui;}
bool ModelPlayerStart::faulted() const noexcept {return !impl_ || impl_->fault;}
const ModelPlayerStartOwner& ModelPlayerStart::owner() const {
    if(!impl_ || !impl_->owner)fail("This MODELCTRL player caller has no prepared script owner.");
    return *impl_->owner;
}
const ModelPlayerStartCapture& ModelPlayerStart::capture() const {
    if(!impl_)fail("A moved MODELCTRL player caller has no captured fields.");
    return impl_->capture;
}
const std::shared_ptr<GameDataList>& ModelPlayerStart::row() const {
    if(!impl_)fail("A moved MODELCTRL player caller has no captured row.");
    return impl_->row;
}
const ModelGameDataBinding& ModelPlayerStart::binding() const {
    if(!impl_ || !impl_->binding)fail("MODELCTRL player caller HP binding has not completed.");
    return *impl_->binding;
}
ModelPlayerStartResult ModelPlayerStart::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    std::uint64_t context_time) {
    return resume(messenger,events,ModelPlayerClock{[context_time]{return context_time;}});
}
ModelPlayerStartResult ModelPlayerStart::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    const ModelPlayerClock& live_clock) {
    if(!impl_ || impl_->fault)fail("An uncertain MODELCTRL player caller cannot be replayed.");
    if(!live_clock)fail("Prepared MODELCTRL player caller requires its explicit context clock reader.");
    auto& state=*impl_;
    if(state.executing || (state.messenger && (state.messenger!=&messenger || state.events!=&events)))
        fail("A retained MODELCTRL player caller requires the same non-recursive owners.");
    state.messenger=&messenger;state.events=&events;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(state.executing);
    try {
        if(state.phase==ModelPlayerStartPhase::binding) {
            state.binding=bind_model_game_data(*state.capture.fields,state.row);
            state.phase=ModelPlayerStartPhase::reset_modifiers;
        }
        if(state.phase==ModelPlayerStartPhase::reset_modifiers) {
            const auto up=bounded(state.row->require<GameDataIntList>("UPDATA")->data);
            std::fill_n(up->begin(),std::min(up->size(),std::size_t{7}),0);
            const auto down=bounded(state.row->require<GameDataIntList>("DOWNDATA")->data);
            std::fill_n(down->begin(),std::min(down->size(),std::size_t{6}),0);
            state.phase=state.row->require<GameDataBool>("ISUSE")->data==0 ?
                ModelPlayerStartPhase::game_start:ModelPlayerStartPhase::weapon_generate;
        }
        if(state.phase==ModelPlayerStartPhase::game_start) {
            if(!state.dispatch) {
                messenger.select("GAMERAM",std::nullopt,-1);
                messenger.buffer().add_int(state.live_ram());
                if(!state.capture.parent_name)
                    fail("Original GAMESTART dereferences the player callback's captured null parent.");
                auto name=state.capture.parent_name->value_or("");text(name);
                messenger.buffer().add_string(std::move(name));
                messenger.buffer().add_string(state.defaults.names()[static_cast<std::size_t>(state.live_ram())]);
                state.dispatch.emplace(messenger.begin_send("GAMESTART"));
            }
            if(auto result=state.deliver())return *result;
            state.phase=ModelPlayerStartPhase::base_isuse;
        }
        if(state.phase==ModelPlayerStartPhase::base_isuse) {
            const auto list=bounded(state.row->require<GameDataBoolList>("BASE_ISUSE")->data);
            std::fill(list->begin(),list->end(),std::uint8_t{1});
            state.phase=ModelPlayerStartPhase::weapon_generate;
        }
        if(state.phase==ModelPlayerStartPhase::weapon_generate) {
            const auto list=bounded(state.row->require<GameDataStringList>("POWERSOCKETNAME")->data);
            if(!list->empty()) {
                // The original count check precedes a second named-list read.
                const auto live=bounded(state.row->require<GameDataStringList>("POWERSOCKETNAME")->data);
                if(live->empty())fail("Original weapon name element disappeared before its retained read.");
                auto name=live->front();if(name)text(*name);
                const auto index=state.live_ram();
                messenger.prepare_deferred("PLAYERARMS",std::move(name),index);
                const auto context_time=live_clock();
                messenger.schedule_deferred("GENERATE",1,context_time);
            }
            state.phase=ModelPlayerStartPhase::recalculate;
        }
        if(state.phase==ModelPlayerStartPhase::recalculate) {
            if(!state.dispatch) {
                messenger.select("GAMERAM",std::nullopt,-1);
                messenger.buffer().add_int(state.live_ram());
                state.dispatch.emplace(messenger.begin_send("RECALCPLAYER"));
            }
            if(auto result=state.deliver())return *result;
            state.phase=ModelPlayerStartPhase::later_ui;
        }
        return {ModelPlayerStartStop::later_ui_required,
            "Original MODELCTRL NUMBERCTRL/METERCTRL and remaining constructor services are required."};
    }catch(...) {state.fault=true;throw;}
}
} // namespace dw4
