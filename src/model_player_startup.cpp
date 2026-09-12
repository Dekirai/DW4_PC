#include "dw4/model_player_startup.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/assets.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw GameDataError(message); }
void text(const std::string& value) {
    if(value.size()>65535 || value.find('\0')!=std::string::npos)
        fail("Prepared player-start map name exceeds its bounded string profile.");
}
const assets::DolSection& section(const assets::DolInfo& dol,std::uint32_t address,std::size_t size) {
    for(const auto& value:dol.sections)
        if(!value.executable && address>=value.address &&
            std::uint64_t(address)+size<=std::uint64_t(value.address)+value.size)return value;
    fail("Player-start map table points outside initialized DOL data.");
}
void read(std::ifstream& input,std::uint64_t offset,std::span<std::uint8_t> bytes) {
    input.seekg(static_cast<std::streamoff>(offset));
    input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!input || static_cast<std::size_t>(input.gcount())!=bytes.size())fail("Player-start map data is truncated.");
}
std::uint32_t be32(const std::uint8_t* bytes) {
    return (std::uint32_t(bytes[0])<<24)|(std::uint32_t(bytes[1])<<16)|(std::uint32_t(bytes[2])<<8)|bytes[3];
}
}
ModelPlayerMapRules::ModelPlayerMapRules(std::array<std::string,5> metal,std::array<std::string,4> lobby)
    :metal_(std::move(metal)),main_lobby_(std::move(lobby)) {
    for(const auto& value:metal_)text(value);
    for(const auto& value:main_lobby_)text(value);
}
ModelPlayerMapRules read_model_player_map_rules(const std::filesystem::path& path) {
    const auto dol=assets::inspect_dol(path);
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<0 || static_cast<std::uint64_t>(input.tellg())!=dol.file_size)
        fail("Player-start DOL changed or could not be reopened.");
    constexpr std::uint32_t table=0x804e7c80;
    const auto& location=section(dol,table,36);std::array<std::uint8_t,36> pointers{};
    read(input,std::uint64_t(location.file_offset)+table-location.address,pointers);
    std::array<std::string,5> metal;std::array<std::string,4> lobby;
    for(std::size_t i=0;i<9;++i) {
        const auto address=be32(pointers.data()+4*i);const auto& data=section(dol,address,1);
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(64,std::uint64_t(data.address)+data.size-address));
        std::array<std::uint8_t,64> bytes{};
        read(input,std::uint64_t(data.file_offset)+address-data.address,std::span(bytes).first(count));
        const auto end=std::find(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(count),std::uint8_t{});
        if(end==bytes.begin()+static_cast<std::ptrdiff_t>(count))fail("Player-start map name has no bounded terminator.");
        (i<5 ? metal[i] : lobby[i-5]).assign(reinterpret_cast<const char*>(bytes.data()),static_cast<std::size_t>(end-bytes.begin()));
    }
    return ModelPlayerMapRules(std::move(metal),std::move(lobby));
}
float original_player_hp_percent(std::int32_t hp,std::int32_t maximum) {
    // Each conversion and arithmetic instruction rounds separately to binary32.
    // No int32 input fraction here can underflow into the Gekko NI tiny range.
    const float current=static_cast<float>(hp);
    const float scaled=100.0f*current;
    const float denominator=static_cast<float>(maximum);
    return scaled/denominator;
}
float original_player_mp_percent(std::int32_t mp,std::int32_t maximum) {
    const auto scaled=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(mp)*100u);
    if(maximum==0 || (scaled==std::numeric_limits<std::int32_t>::min() && maximum==-1))
        fail("Original player MP percent reached an unresolved exceptional divw result.");
    return static_cast<float>(scaled/maximum);
}

struct ModelPlayerStartup::Impl {
    ModelPlayerStart prefix;
    ModelPlayerMapRules maps;
    ModelPlayerStartupPhase phase=ModelPlayerStartupPhase::prefix;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::optional<MessengerDispatch> dispatch;
    std::shared_ptr<const std::string> map;
    bool executing{},failed{};
    Impl(ModelPlayerStartOwner owner,std::shared_ptr<GameDataList> row,ModelPlayerStartDefaults defaults,ModelPlayerMapRules rules)
        :prefix(std::move(owner),std::move(row),std::move(defaults)),maps(std::move(rules)) {}
    std::int32_t ram() const {
        const auto value=*prefix.owner().live_ram_no;
        if(value<0 || value>3)fail("Player-start UI requires the live original player RAM in 0..3.");
        return value;
    }
    std::int32_t value(std::string_view name) const {return prefix.row()->require<GameDataInt>(name)->data;}
    void select(const char* category,std::optional<std::string> name) {
        const auto index=ram();messenger->select(category,std::move(name),index);
    }
    void prepare() {
        if(phase==ModelPlayerStartupPhase::level) {
            select("NUMBERCTRL","lvnm");messenger->buffer().add_int(value("LEVEL"));
        } else if(phase==ModelPlayerStartupPhase::hp_meter) {
            select("METERCTRL","hpmt");const auto maximum=value("MHP"),hp=value("HP");
            messenger->buffer().add_float(original_player_hp_percent(hp,maximum));
        } else if(phase==ModelPlayerStartupPhase::hp_number) {
            select("NUMBERCTRL","hpnm");messenger->buffer().add_int(value("HP"));
        } else if(phase==ModelPlayerStartupPhase::mp_meter) {
            select("METERCTRL","mpmt");const auto mp=value("MP"),maximum=value("MMP");
            messenger->buffer().add_float(original_player_mp_percent(mp,maximum));
        } else if(phase==ModelPlayerStartupPhase::mp_number) {
            select("NUMBERCTRL","mpnm");messenger->buffer().add_int(value("MP"));
        } else {
            select("PLAYER",std::nullopt);dispatch.emplace(messenger->begin_send("SETMETAL"));return;
        }
        const bool meter=phase==ModelPlayerStartupPhase::hp_meter || phase==ModelPlayerStartupPhase::mp_meter;
        dispatch.emplace(messenger->begin_send(meter ? "SETPERCENT" : "SETVALUE"));
    }
};
ModelPlayerStartup::ModelPlayerStartup(ModelPlayerStartOwner owner,std::shared_ptr<GameDataList> row,
    ModelPlayerStartDefaults defaults,ModelPlayerMapRules maps)
    :impl_(std::make_unique<Impl>(std::move(owner),std::move(row),std::move(defaults),std::move(maps))) {}
ModelPlayerStartup::~ModelPlayerStartup()=default;
ModelPlayerStartup::ModelPlayerStartup(ModelPlayerStartup&&) noexcept=default;
ModelPlayerStartup& ModelPlayerStartup::operator=(ModelPlayerStartup&&) noexcept=default;
ModelPlayerStartupPhase ModelPlayerStartup::phase() const {if(!impl_)fail("A moved player startup has no phase.");return impl_->phase;}
bool ModelPlayerStartup::completed() const noexcept {return impl_ && impl_->phase==ModelPlayerStartupPhase::completed;}
bool ModelPlayerStartup::faulted() const noexcept {return !impl_ || impl_->failed;}
const ModelPlayerStart& ModelPlayerStartup::prefix() const {if(!impl_)fail("A moved player startup has no prefix.");return impl_->prefix;}
const std::shared_ptr<const std::string>& ModelPlayerStartup::captured_map() const {
    if(!impl_)fail("A moved player startup has no captured map.");return impl_->map;
}
ModelPlayerStartupResult ModelPlayerStartup::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    const ModelPlayerClock& clock,const ModelPlayerMapReader& map_reader) {
    if(!impl_ || impl_->failed)fail("An uncertain player startup cannot be replayed.");
    auto& state=*impl_;
    if(state.executing || (state.messenger && (state.messenger!=&messenger || state.events!=&events)))
        fail("A retained player startup requires the same nonrecursive messenger owners.");
    if(!clock)fail("Player startup requires the actual prepared context clock.");
    state.messenger=&messenger;state.events=&events;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(state.executing);
    try {
        if(state.phase==ModelPlayerStartupPhase::prefix) {
            const auto result=state.prefix.resume(messenger,events,clock);
            if(result.stop!=ModelPlayerStartStop::later_ui_required)return {ModelPlayerStartupStop::service_required,result.diagnostic};
            state.phase=ModelPlayerStartupPhase::level;
        }
        while(state.phase!=ModelPlayerStartupPhase::completed) {
            if(state.phase==ModelPlayerStartupPhase::map_capture) {
                if(!map_reader)return {ModelPlayerStartupStop::map_context_required,"Original player startup requires its actual map-name field owner."};
                state.map=map_reader();
                if(!state.map)fail("Original player startup captured a null map-name pointer.");
                text(*state.map);
                state.phase=std::find(state.maps.metal().begin(),state.maps.metal().end(),*state.map)!=state.maps.metal().end() ?
                    ModelPlayerStartupPhase::metal:ModelPlayerStartupPhase::main_lobby;
            } else if(state.phase==ModelPlayerStartupPhase::main_lobby) {
                if(std::find(state.maps.main_lobby().begin(),state.maps.main_lobby().end(),*state.map)!=state.maps.main_lobby().end())
                    state.prefix.owner().work->original_is_main_lobby=std::uint8_t{1};
                state.phase=ModelPlayerStartupPhase::completed;
            } else {
                if(!state.dispatch)state.prepare();
                const auto result=events.resume(*state.dispatch);
                if(result.stop==MessengerStop::faulted)fail("Original player startup message delivery faulted.");
                if(result.stop!=MessengerStop::completed)return {ModelPlayerStartupStop::service_required,result.diagnostic};
                state.dispatch.reset();
                switch(state.phase) {
                case ModelPlayerStartupPhase::level:state.phase=ModelPlayerStartupPhase::hp_meter;break;
                case ModelPlayerStartupPhase::hp_meter:state.phase=ModelPlayerStartupPhase::hp_number;break;
                case ModelPlayerStartupPhase::hp_number:state.phase=ModelPlayerStartupPhase::mp_meter;break;
                case ModelPlayerStartupPhase::mp_meter:state.phase=ModelPlayerStartupPhase::mp_number;break;
                case ModelPlayerStartupPhase::mp_number:state.phase=ModelPlayerStartupPhase::map_capture;break;
                case ModelPlayerStartupPhase::metal:state.phase=ModelPlayerStartupPhase::main_lobby;break;
                default:fail("Original player startup has an invalid retained send phase.");
                }
            }
        }
        return {ModelPlayerStartupStop::completed,{}};
    } catch(...) {state.failed=true;throw;}
}
} // namespace dw4
