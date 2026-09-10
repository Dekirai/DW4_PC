#include "dw4/messenger.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/game_data.hpp"
#include "dw4/game_ram.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* reason) { ++checks;if(!value)throw std::runtime_error(reason); }
template<class F> void rejects(F operation) {
    try {operation();}catch(const MessengerError&){check(true,"rejected");return;}
    check(false,"Expected an invalid mixed identity to reject.");
}
void identity_domains() {
    constexpr ControllerObjectId legacy{4,7};
    static_assert(!legacy.is_native() && legacy.valid() && legacy.file_index==4 && legacy.reference_index==7);
    static_assert(std::is_trivially_copyable_v<ControllerObjectId>);
    const auto first=ControllerObjectId::allocate_native(),second=ControllerObjectId::allocate_native();
    check(first.is_native() && first.valid() && first.native_identity()!=0,"A native allocation lacks its explicit identity domain.");
    check(first!=second && first.native_identity()!=second.native_identity(),"Independent native allocations share an identity.");
    check(first!=ControllerObjectId{0,0} && first!=ControllerObjectId{0,static_cast<std::uint32_t>(first.native_identity())},
        "Native identity aliases an apparent serialized address.");
    const auto copy=first;check(copy==first && copy.native_identity()==first.native_identity(),"Copying identity reallocates the owner.");
    std::set<ControllerObjectId> owned{legacy,first,second,{0,0},{0,7},{4,0}};
    check(owned.size()==6,"Ordered owner keys omit the native domain.");
    for(unsigned i=0;i<4096;++i)check(owned.insert(ControllerObjectId::allocate_native()).second,"Native allocator reused a released or prior identity.");
    check(ControllerObjectId{std::numeric_limits<std::uint32_t>::max(),17}.valid(),"Source occurrence widths were silently narrowed.");
}
void retained_delivery() {
    const ControllerObjectId legacy{0,0};
    const auto a=ControllerObjectId::allocate_native(),b=ControllerObjectId::allocate_native();
    NativeMessenger messenger;
    for(const auto id:{legacy,a,b})messenger.register_recipient(id,"GAMERAM","gameram",0);
    std::map<ControllerObjectId,unsigned> calls;bool ready{};
    MessengerServices services;
    services.active=[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};};
    services.manager=[&](ControllerObjectId id)->MessengerManager {
        return {[&,id](NativeMessenger&,const MessengerDelivery& delivery) {
            check(delivery.recipient==id,"Retained dispatch changed its native recipient identity.");
            ++calls[id];return MessengerCallResult{id!=a || ready,1,"native owner pending"};
        },{}};
    };
    auto send=messenger.begin_send_to("GAMERAM",std::nullopt,-1,"GETPLAYER");
    const auto paused=messenger.resume(send,services);
    check(paused.stop==MessengerStop::service_required && paused.recipient==a && calls[legacy]==1 && calls[a]==1 && calls[b]==0,
        "Native owner suspension collided with a serialized owner or skipped registration order.");
    ready=true;const auto done=messenger.resume(send,services);
    check(done.stop==MessengerStop::completed && calls[legacy]==1 && calls[a]==2 && calls[b]==1,
        "Native owner retry replayed completed recipients or lost the following owner.");
    messenger.buffer().add_object(a);messenger.push_buffer();messenger.buffer().add_object(b);messenger.pop_buffer();
    check(messenger.buffer().objects().size()==1 && std::get<ControllerObjectId>(messenger.buffer().objects()[0])==a,
        "Shared buffer rotation changed or erased a retained native object identity.");
    auto bad=a;bad.reference_index=9;check(!bad.valid(),"Mixed native/serialized fields were accepted as a valid identity.");
    const auto rows=messenger.registration_count();
    rejects([&]{messenger.register_recipient(bad,"GAMERAM","gameram",0);});
    rejects([&]{messenger.buffer().add_object(bad);});
    check(messenger.registration_count()==rows && messenger.buffer().objects().size()==1,
        "Invalid mixed identity partially changed registration or buffer state.");
}
template<class F> void rejects_owner(F operation) {
    try {operation();}catch(const std::exception&){check(true,"rejected");return;}
    check(false,"Expected an invalid provider operation to reject.");
}
void native_provider_ownership() {
    struct Owner { unsigned resolutions{},calls{},effects{}; bool available{},complete{}; };
    auto owner=std::make_shared<Owner>();std::weak_ptr<Owner> retained=owner;
    const auto id=ControllerObjectId::allocate_native();
    {
        ActionEventRuntime runtime;runtime.register_recipient(id,"NATIVE","owner",0);
        auto send=runtime.messenger().begin_send_to("NATIVE","owner",0,"RUN");
        check(runtime.resume(send).stop==MessengerStop::service_required,"A missing native provider fabricated completion.");
        rejects_owner([&]{runtime.messenger();});
        NativeControllerServices providers{
            []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
            [owner]()->MessengerManager {
                ++owner->resolutions;if(!owner->available)return {};
                return {[owner](NativeMessenger&,const MessengerDelivery&) {
                    ++owner->calls;if(owner->calls==1)++owner->effects;
                    return MessengerCallResult{owner->complete,1,"native continuation"};
                },{}};
            }};
        rejects_owner([&]{runtime.register_native_controller({0,0},providers);});
        rejects_owner([&]{runtime.register_native_controller(id,{});});
        auto mixed=id;mixed.file_index=2;
        rejects_owner([&]{runtime.register_native_controller(mixed,providers);});
        runtime.register_native_controller(id,providers);
        rejects_owner([&]{runtime.register_native_controller(id,providers);});
        check(runtime.resume(send).stop==MessengerStop::service_required && owner->resolutions==1 && owner->calls==0,
            "An unresolved native manager was captured as a completed provider.");
        owner->available=true;
        check(runtime.resume(send).stop==MessengerStop::service_required && owner->resolutions==2 && owner->calls==1 && owner->effects==1,
            "Attaching a provider between retries changed or replayed the retained send.");
        owner->available=false;owner->complete=true;
        check(runtime.resume(send).stop==MessengerStop::completed && owner->resolutions==2 && owner->calls==2 && owner->effects==1,
            "Native retry replaced its captured manager or lost its committed prefix.");
        providers={};owner.reset();
        check(!retained.expired(),"Dropping a facade released registered native work.");
        auto next=runtime.messenger().begin_send_to("NATIVE","owner",0,"RUN");
        check(runtime.resume(next).stop==MessengerStop::service_required && retained.lock()->resolutions==3,
            "A subsequent send reused the previous manager instead of the live provider.");
    }
    check(retained.expired(),"Native controller and messenger owners form an unintended retention cycle.");
}
void data_objects_and_integer_reset() {
    NativeMessenger messenger;messenger.select("DATA",{},0);auto& buffer=messenger.buffer();
    auto row=std::make_shared<GameDataList>();std::weak_ptr<GameDataList> retained=row;
    buffer.set_enabled_only(255);buffer.add_bool(7);buffer.add_int(123);buffer.add_int(-9);
    buffer.add_float(2);buffer.add_int64(-1234);buffer.add_vector({1,2,3});
    std::array<float,16> matrix{};matrix[0]=5;buffer.add_matrix(matrix);
    buffer.add_data_list(row);buffer.add_string("retained");
    const auto* pointer=row.get();row.reset();
    buffer.clear_integers();buffer.clear_integers();
    check(buffer.integers().empty() && buffer.enabled_only()==255 && buffer.booleans()==std::vector<std::uint8_t>{7} &&
        buffer.floats()==std::vector<float>{2} && buffer.integers64()==std::vector<std::int64_t>{-1234} &&
        buffer.vectors()==std::vector<std::array<float,3>>{{1,2,3}} && buffer.matrices()==std::vector<std::array<float,16>>{matrix} &&
        buffer.strings()==std::vector<std::optional<std::string>>{"retained"} && buffer.objects().size()==1 &&
        std::get<MessengerDataList>(buffer.objects()[0]).get()==pointer,"Integer reset changed another typed list, gate or shared data identity.");
    rejects([&]{buffer.add_data_list({});});
    for(unsigned i=0;i<32761;++i)buffer.add_int(static_cast<std::int32_t>(i));
    rejects([&]{buffer.add_int(0);});
    buffer.clear_integers();for(unsigned i=0;i<32761;++i)buffer.add_int(-1);
    rejects([&]{buffer.add_bool(0);});
    check(buffer.integers().size()==32761 && buffer.objects().size()==1,"Integer reset did not release exactly its entry accounting.");
    messenger.push_buffer();messenger.buffer().add_data_list(std::make_shared<GameDataList>());messenger.pop_buffer();
    check(!retained.expired() && &messenger.buffer()==&buffer &&
        std::get<MessengerDataList>(buffer.objects()[0]).get()==pointer,"Child-buffer rotation released or replaced the parent data list.");
    buffer.clear();check(retained.expired(),"Cleared messenger retained a generated data list without another owner.");

    // Free integer bytes near the independent storage bound, then consume the
    // released capacity without clearing any of the retained large strings.
    for(unsigned i=0;i<30000;++i)buffer.add_int(1);
    unsigned strings{};for(;;) {
        try {buffer.add_string(std::string(65535,'s'));++strings;}catch(const MessengerError&){break;}
    }
    check(strings>100 && buffer.integers().size()==30000,"Byte-bound fixture stopped at the wrong limit.");
    buffer.clear_integers();buffer.add_string(std::string(65535,'t'));
    check(buffer.strings().size()==strings+1,"Integer-only reset did not return its storage bytes.");
    buffer.clear();bool guarded{};
    buffer.add_data_list(MessengerDataList(new GameDataList,[&](GameDataList* data) {
        try {buffer.clear_integers();}catch(const MessengerError&){guarded=true;}delete data;
    }));
    buffer.clear();check(guarded,"Integer-only reset bypassed the object-release reentrancy guard.");
}
void aggregate_capacity_and_atomic_registration() {
    const NativeControllerServices provider{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        []()->MessengerManager {return {};}};
    ActionEventRuntime runtime;
    const auto row=ControllerObjectId::allocate_native();
    for(unsigned i=0;i<65536;++i)runtime.register_recipient(row,"",{},0);
    rejects_owner([&]{GameRamRuntime rejected(runtime);});
    runtime.messenger().reset();
    const auto retry=ControllerObjectId::allocate_native();
    rejects_owner([&]{runtime.register_native_controller(retry,provider,std::string(65536,'x'),{},0);});
    runtime.register_native_controller(retry,provider,"RECOVERED",{},0);
    check(runtime.messenger().registration_count()==1,"Rejected category registration leaked a provider or partial row.");
    for(unsigned i=1;i<4096;++i)runtime.register_native_controller(ControllerObjectId::allocate_native(),provider);
    rejects_owner([&]{runtime.register_native_controller(ControllerObjectId::allocate_native(),provider);});
    player_runtime_fixture::RuntimeFixture fixture;
    const auto binding=decode_player_runtime_binding(fixture.document(),fixture.runtime_request(),"synthetic/provider-limit.igb",9);
    std::array<float,16> identity{};identity[0]=identity[5]=identity[10]=identity[15]=1;
    rejects_owner([&]{runtime.register_model(binding,ModelControlState(ModelMotionPose(identity)),true);});
    check(runtime.messenger().registration_count()==1,"Serialized model admission ignored native owner capacity or partially registered.");

    ActionEventRuntime reverse;
    reverse.register_model(binding,ModelControlState(ModelMotionPose(identity)),true);
    for(unsigned i=1;i<4096;++i)reverse.register_native_controller(ControllerObjectId::allocate_native(),provider);
    rejects_owner([&]{reverse.register_native_controller(ControllerObjectId::allocate_native(),provider);});
    check(reverse.messenger().registration_count()==1,"Native owner capacity depends on model/native registration order.");
}
void batched_provider_registration() {
    ActionEventRuntime runtime;std::vector<ControllerObjectId> calls;
    const NativeControllerServices provider{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&] {return MessengerManager{[&](NativeMessenger&,const MessengerDelivery& delivery) {
            calls.push_back(delivery.recipient);return MessengerCallResult{true,1,{}};
        },{}};}};
    const ControllerObjectId first{100,1},second{100,2},third{100,3};
    const auto generated=ControllerObjectId::allocate_native();
    const std::array providers{PreparedControllerRegistration{first,provider,"synthetic/batch.igb"},
        PreparedControllerRegistration{second,provider,"synthetic/batch.igb"},PreparedControllerRegistration{generated,provider,{}}};
    auto rows=std::array{MessengerRegistration{second,"BATCH",{},0},MessengerRegistration{first,"BATCH",{},0},
        MessengerRegistration{second,"BATCH",{},1},MessengerRegistration{generated,"BATCH",{},0}};
    rows[3].category=std::string("BAD\0ROW",7);
    rejects_owner([&]{runtime.register_controllers(providers,rows);});
    check(runtime.messenger().registration_count()==0,"Invalid last row published a partial provider batch.");
    rows[3].category="BATCH";runtime.register_controllers(providers,rows);
    auto send=runtime.messenger().begin_send_to("BATCH",{},-1,"RUN");
    check(runtime.resume(send).stop==MessengerStop::completed && calls==std::vector<ControllerObjectId>{second,first,second,generated},
        "Batch publication reordered or deduplicated category occurrences.");
    const std::array conflict{PreparedControllerRegistration{third,provider,"different/source.igb"}};
    rejects_owner([&]{runtime.register_controllers(conflict,{});});
    const std::array valid{PreparedControllerRegistration{third,provider,"synthetic/batch.igb"}};
    const std::array duplicate{valid[0],valid[0]};rejects_owner([&]{runtime.register_controllers(duplicate,{});});
    runtime.register_controllers(valid,{});
    rejects_owner([&]{runtime.register_controllers(valid,{});});
    check(runtime.messenger().registration_count()==4,"Rejected source or repeated provider changed registered rows.");

    // Registration may supply a missing owner from inside an executing live
    // provider. It must not swap/destroy that provider's containing map.
    ActionEventRuntime nested;const auto parent=ControllerObjectId::allocate_native(),child=ControllerObjectId::allocate_native();
    nested.register_recipient(parent,"PARENT",{},0);unsigned active_calls{},child_calls{};
    nested.register_native_controller(parent,{
        [&]()->std::optional<std::uint8_t> {
            if(++active_calls==1) {
                const std::array install{PreparedControllerRegistration{child,{
                    []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
                    [&] {return MessengerManager{[&](NativeMessenger&,const MessengerDelivery&) {
                        ++child_calls;return MessengerCallResult{true,1,{}};
                    },{}};}},{}}};
                const std::array registration{MessengerRegistration{child,"CHILD",{},0}};
                nested.register_controllers(install,registration);
            }
            return std::uint8_t{1};
        },
        [&] {return MessengerManager{[&](NativeMessenger& messenger,const MessengerDelivery&) {
            auto call=messenger.begin_send_to("CHILD",{},0,"RUN");
            return MessengerCallResult{nested.resume(call).stop==MessengerStop::completed,1,{}};
        },{}};}});
    auto outer=nested.messenger().begin_send_to("PARENT",{},0,"RUN");nested.messenger().buffer().set_enabled_only(1);
    check(nested.resume(outer).stop==MessengerStop::completed && active_calls==1 && child_calls==1,
        "Registering inside an active provider invalidated the executing owner or nested delivery.");
}
}
int main() {
    try {identity_domains();retained_delivery();native_provider_ownership();data_objects_and_integer_reset();aggregate_capacity_and_atomic_registration();batched_provider_registration();std::cout<<"Controller identity checks: "<<checks<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<"Controller identity after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
