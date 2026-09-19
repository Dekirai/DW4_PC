#include "dw4/igb_decode.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using K=IgbDecodeServiceKind;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
struct Fixture {
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbDecodeState> state=std::make_shared<IgbDecodeState>();
    std::shared_ptr<IgbDecodeServices> services=std::make_shared<IgbDecodeServices>();
    std::vector<K> events;
    std::vector<K> selections;
    std::function<MessengerCallResult(const IgbDecodeCall&)> hook;
    std::uint64_t now{1000000001};
    unsigned samples{};
    Fixture() {
        file->igb_decode=state;file->filename=text("original-name.igb");
        state->stream->file=file;state->stream->version=6;state->stream->expected_cookie=0xfada;
        state->stream->statuses->success=0;state->stream->statuses->failure=1;
        state->object_count=7;state->objects_initialized=17;
        file->loading=7;file->succeeded_31=9;file->initialize_after_load_115=1;
        services->warning_suppressed=0;services->timing_suppressed=0;
        auto timer=std::make_shared<NativeGameClock>([this] {++samples;return now;});
        timer->state().stopped=0;services->clock=timer;
        services->factory=[this](const IgbDecodeCall& call) {
            selections.push_back(call.kind);
            auto operation=std::make_shared<IgbDecodeOperation>();
            operation->resume=[this,call] {
                events.push_back(call.kind);
                if(hook)return hook(call);
                return MessengerCallResult{true,0,{}};
            };
            return operation;
        };
    }
    bool saw(K kind) const {return std::find(events.begin(),events.end(),kind)!=events.end();}
    unsigned count(K kind) const {return static_cast<unsigned>(std::count(events.begin(),events.end(),kind));}
};
void success_order_and_live_result() {
    Fixture f;const auto filename=text("selected-path.igb");
    bool saw_open{},saw_close{},saw_cleanup{},saw_log{};
    double elapsed{};
    f.hook=[&](const IgbDecodeCall& call) {
        if(call.kind==K::open_stream) {
            saw_open=call.filename==filename && call.file==f.file && call.state==f.state &&
                f.file->loading==1 && f.file->succeeded_31==0 && f.state->objects_initialized==0 &&
                f.services->diagnostic=="readFile(selected-path.igb)" && f.samples==1;
        }
        if(call.kind==K::activate_objects) {f.state->objects_initialized=1;return MessengerCallResult{true,1,{}};}
        if(call.kind==K::close_stream)saw_close=f.file->loading==1 && f.file->succeeded_31==0;
        if(call.kind==K::cleanup) {saw_cleanup=saw_close && f.file->loading==1 && f.file->succeeded_31==0;f.now=2500000003;}
        if(call.kind==K::timing_log) {
            saw_log=call.filename==filename && f.file->loading==0 && f.file->succeeded_31==1;
            elapsed=call.elapsed_seconds;f.state->object_count=-7;return MessengerCallResult{true,2,{}};
        }
        return MessengerCallResult{true,0,{}};
    };
    IgbDecodeInvocation invocation(f.file,filename);
    check(invocation.resume(f.services).completed && invocation.value()==-7,"Successful main did not return the live post-log directory count.");
    const std::vector expected{K::open_stream,K::read_header,K::field_types,K::alignments,K::object_type_bytes,
        K::construct_types,K::prepare_types,K::table_521ec,K::unshared_50f58,K::table_520e8,K::populate_objects,
        K::file_payload,K::final_table,K::export_infos,K::activate_objects,K::postprocess,K::close_stream,K::cleanup,K::timing_log};
    check(f.events==expected,"Version6 nonshared caller order changed.");
    check(saw_open && saw_close && saw_cleanup && saw_log,"Main caller publication/argument order changed.");
    check(f.samples==2 && *f.services->timing_suppressed==1,"Timing did not sample twice or preserve exact log suppression result2.");
    check(std::bit_cast<std::uint64_t>(elapsed)==std::bit_cast<std::uint64_t>((2.0+500000003.0/1000000000.0)-(1.0+1.0/1000000000.0)),
        "Loader elapsed time was narrowed or evaluated in different precision.");
    const auto event_count=f.events.size();
    check(invocation.resume(f.services).value==std::bit_cast<std::uint32_t>(-7) && f.events.size()==event_count,
        "Completed main replayed operations or changed signed return bits.");
}
void failure_order() {
    const std::vector tested{K::open_stream,K::read_header,K::field_types,K::alignments,K::object_type_bytes,
        K::construct_types,K::prepare_types,K::table_521ec,K::memory_pool_names,K::shared_509d4,
        K::shared_50c68,K::shared_50e80,K::table_520e8,K::populate_objects,K::file_payload,K::final_table,
        K::export_infos,K::postprocess};
    for(const auto failed:tested) {
        Fixture f;f.state->stream->flag_d6=255;f.state->stream->flag_124=2;
        bool cleanup_prefix{},close_prefix{};
        f.hook=[&](const IgbDecodeCall& call) {
            if(call.kind==K::cleanup)cleanup_prefix=f.file->loading==1 && f.file->succeeded_31==0;
            if(call.kind==K::close_stream)close_prefix=cleanup_prefix && f.file->loading==0 && f.file->succeeded_31==0;
            return MessengerCallResult{true,call.kind==failed ? 1u:0u,{}};
        };
        IgbDecodeInvocation invocation(f.file,text("failure.igb"));
        check(invocation.resume(f.services).completed && invocation.value()==-1,"A checked helper failure did not return signed-1.");
        check(f.saw(failed) && cleanup_prefix && close_prefix && f.events[f.events.size()-2]==K::cleanup &&
            f.events.back()==K::close_stream && !f.saw(K::timing_log),"Failure cleanup/status/close order changed.");
        check(!invocation.faulted() && f.samples==1,"Ordinary original status failure became a native fault or sampled an end time.");
    }
    Fixture f;
    f.hook=[&](const IgbDecodeCall& call) {
        if(call.kind==K::open_stream)return MessengerCallResult{true,1,{}};
        if(call.kind==K::cleanup)throw IgbDecodeError("Prepared destructor failure.");
        return MessengerCallResult{true,0,{}};
    };
    IgbDecodeInvocation invocation(f.file,text("cleanup-failure"));
    rejects([&] {invocation.resume(f.services);},"Cleanup exception was swallowed.");
    check(f.file->loading==1 && f.file->succeeded_31==0 && !f.saw(K::close_stream) && invocation.faulted(),
        "Failed cleanup fabricated later failure-tail stores or closing.");
}
void pending_status_and_live_gates() {
    Fixture f;unsigned opens{};
    f.hook=[&](const IgbDecodeCall& call) {
        if(call.kind==K::open_stream) {++opens;f.state->stream->statuses->failure.reset();return MessengerCallResult{true,0x80000002u,{}};}
        return MessengerCallResult{true,0,{}};
    };
    IgbDecodeInvocation invocation(f.file,text("status"));
    check(!invocation.resume(f.services).completed && opens==1 && !f.saw(K::read_header),"Missing live failure word did not pause after the helper.");
    f.state->stream->statuses->failure=0x80000002u;
    check(invocation.resume(f.services).completed && invocation.value()==-1 && opens==1,"Status resume replayed the helper or used boolean acceptance.");

    Fixture g;g.services->timing_suppressed=255;g.file->initialize_after_load_115=255;
    g.state->stream->flag_d6=2;g.state->stream->flag_124=255;
    g.hook=[&](const IgbDecodeCall& call) {
        if(call.kind==K::prepare_types)g.state->stream->version=2;
        if(call.kind==K::shared_509d4)g.state->stream->flag_d6=0;
        if(call.kind==K::field_types && g.count(K::field_types)==2)g.state->stream->version=4;
        if(call.kind==K::table_520e8)g.state->stream->version=6;
        return MessengerCallResult{true,0x80000000u,{}};
    };
    IgbDecodeInvocation gates(g.file,text("gates"));
    check(gates.resume(g.services).completed && gates.value()==7,"Nonfailure raw status word was rejected.");
    check(g.count(K::field_types)==2 && g.count(K::alignments)==1 && g.count(K::table_520e8)==1 &&
        g.count(K::table_521ec)==0,"Version gates were snapshotted or late version was not reloaded.");
    check(g.saw(K::shared_509d4) && g.saw(K::shared_50c68) && g.saw(K::shared_50e80) &&
        !g.saw(K::unshared_50f58) && g.saw(K::memory_pool_names),"Selected shared branch retested its flag or noncanonical flags were rejected.");
    check(!g.saw(K::postprocess) && !g.saw(K::timing_log) && g.samples==1,"Postprocess was not exact1 or log suppression did not accept all nonzero bytes.");
}
void legacy_warning() {
    for(const auto answer:{0u,1u,2u,255u}) {
        Fixture f;f.state->stream->version=4;f.state->stream->flag_d5=255;
        const auto stream_name=text("stream-name");f.state->stream->stream=igb_stream_from_bytes(stream_name,{});
        bool name_match{};
        f.hook=[&](const IgbDecodeCall& call) {
            if(call.kind==K::legacy_warning) {name_match=call.filename==stream_name;return MessengerCallResult{true,answer,{}};}
            return MessengerCallResult{true,0,{}};
        };
        IgbDecodeInvocation invocation(f.file,text("input-name"));
        check(invocation.resume(f.services).completed && invocation.value()==-1 && name_match && !f.saw(K::field_types),
            "Legacy external format did not warn with captured stream name and fail before tables.");
        check(*f.services->warning_suppressed==(answer==2 ? 1:0),"Legacy warning suppression did not use exact result2.");
    }
    Fixture f;f.state->stream->version=0xffffffffu;f.state->stream->flag_d5=1;f.services->warning_suppressed=2;
    IgbDecodeInvocation suppressed(f.file,text("suppressed"));
    check(suppressed.resume(f.services).completed && suppressed.value()==-1 && !f.saw(K::legacy_warning),
        "Suppressed warning dereferenced the absent stream, ignored signed version or returned success.");
}
void pending_identity_and_faults() {
    Fixture f;unsigned polls{};
    f.hook=[&](const IgbDecodeCall& call) {
        if(call.kind==K::construct_types && polls++==0)return MessengerCallResult{false,0,"Prepared pending type constructor."};
        return MessengerCallResult{true,0,{}};
    };
    IgbDecodeInvocation invocation(f.file,text("pending"));
    check(!invocation.resume(f.services).completed && f.file->loading==1 && f.samples==1,"Pending type construction fabricated success.");
    f.services->factory=[](const IgbDecodeCall&) -> std::shared_ptr<IgbDecodeOperation> {throw IgbDecodeError("Replacement must not select the pending call.");};
    // Pending call's selected callback survives replacement. The next call does
    // select the replacement, so reaching its exception proves no prefix replay.
    rejects([&] {invocation.resume(f.services);},"Later factory replacement was never reached.");
    check(f.count(K::open_stream)==1 && f.count(K::construct_types)==2 && polls==2 && invocation.faulted(),
        "Pending callback selection or prefix retention changed.");
    rejects([&] {invocation.resume(f.services);},"Faulted invocation replayed its prefix.");

    Fixture g;
    {
        IgbDecodeInvocation abandoned(g.file,text("abandoned"));
        g.services->factory={};check(!abandoned.resume(g.services).completed,"Missing concrete stage was treated as success.");
    }
    IgbDecodeInvocation restart(g.file,text("restart"));
    rejects([&] {restart.resume(g.services);},"Abandoned decoder prefix could restart on the same file.");

    Fixture h;IgbDecodeInvocation recursive(h.file,text("recursive"));bool inner{};
    h.hook=[&](const IgbDecodeCall&) {
        try {recursive.resume(h.services);}catch(const IgbDecodeError&) {inner=true;}
        return MessengerCallResult{true,0,{}};
    };
    rejects([&] {recursive.resume(h.services);},"Caught recursive entry completed the outer decoder.");
    check(inner && recursive.faulted() && h.events.size()==1,"Recursive entry did not retain only the reached prefix.");

    Fixture missing;
    missing.hook=[&](const IgbDecodeCall&) {missing.state->stream.reset();return MessengerCallResult{true,0,{}};};
    IgbDecodeInvocation missing_fields(missing.file,text("missing-fields"));
    rejects([&] {missing_fields.resume(missing.services);},"Removed native stream-field storage was dereferenced unchecked.");
    check(missing_fields.faulted() && missing.file->loading==1 && !missing.saw(K::read_header),
        "Missing native fields did not retain the actual helper prefix.");
}
void timer_boundaries() {
    Fixture f;f.services->clock.reset();
    IgbDecodeInvocation invocation(f.file,text(std::string("name\0ignored",12)));
    check(!invocation.resume(f.services).completed && f.services->diagnostic=="readFile(name)" &&
        f.file->loading==7 && f.file->succeeded_31==9 && f.state->objects_initialized==17,"Initial timer boundary occurred after load stores or filename formatting changed.");
    f.services->diagnostic="after-format";
    f.services->clock=std::make_shared<NativeGameClock>();f.services->timing_suppressed=1;
    check(invocation.resume(f.services).completed && f.services->diagnostic=="after-format","Resuming the initial clock replayed diagnostic formatting.");
    Fixture g;g.services->clock=std::shared_ptr<NativeGameClock>{};
    IgbDecodeInvocation null_clock(g.file,text("null-clock"));
    rejects([&] {null_clock.resume(g.services);},"A represented null timer became a timestamp or pending singleton.");
    check(g.file->loading==7 && g.events.empty(),"Null timer fault occurred after initial flag stores.");
}
void native_composition_and_loader() {
    Fixture f;f.services->stream=std::make_shared<IgbStreamServices>();
    f.services->tables=std::make_shared<IgbDecodeTablesServices>();
    f.file->pool_118=std::make_shared<ModelSceneData>();
    igb_bind_native_scratch_pool(f.state->stream,f.file->pool_118);
    // A real, minimal v6 native-order stream reaches the runtime field registry.
    // Field-list factories are deliberately absent, so it must not claim a scene.
    std::vector<std::uint8_t> bytes(48);
    const auto put=[&](std::size_t at,std::uint32_t value) {for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(i*8));};
    put(44,6);put(40,0xfada);
    const auto path=text("native.igb");
    f.services->stream=make_igb_byte_stream_services(path,bytes);
    f.services->factory=make_native_igb_stage_factory(f.services);
    auto loader_services=std::make_shared<LoaderRequestServices>();
    loader_services->factory=make_native_igb_loader_factory(f.services);
    auto loader=std::make_shared<SystemStageLoader>();f.file->filename=path;
    LoaderFileLoadInvocation request(loader,f.file);
    const auto result=request.resume(loader_services);
    check(!result.completed && f.file->loading==1 && f.file->succeeded_31==0,"Native loader adapter marked incomplete deserialization successful.");
    check(f.state->stream->version==6 && f.state->stream->stream && f.state->stream->stream->position==48,
        "Loader-to-decoder composition did not run the actual open/header bodies.");
    check(result.diagnostic.find("registry")!=std::string::npos,"Native table composition did not stop at the actual runtime field registry.");

    // Factories retain services only while an operation is selected; installed
    // weak captures and the weak stream/file link must not leak the whole graph.
    std::weak_ptr<IgbDecodeServices> observed;
    {auto owner=std::make_shared<IgbDecodeServices>();observed=owner;owner->factory=make_native_igb_stage_factory(owner);}
    check(observed.expired(),"Installed stage factory created a service ownership cycle.");
    std::weak_ptr<LoaderRequestFile> observed_file;
    {auto owner=std::make_shared<LoaderRequestFile>();observed_file=owner;owner->igb_decode=std::make_shared<IgbDecodeState>();owner->igb_decode->stream->file=owner;}
    check(observed_file.expired(),"Persistent decoder fields created a file ownership cycle.");
    auto expired=make_native_igb_loader_factory(observed);
    LoaderRequestCall call;call.kind=LoaderRequestServiceKind::decode_igb;
    rejects([&] {expired(call);},"Expired decoder services produced a fabricated operation.");
}
} // namespace
int main() {
    try {
        success_order_and_live_result();failure_order();pending_status_and_live_gates();legacy_warning();
        pending_identity_and_faults();timer_boundaries();native_composition_and_loader();
        std::cout<<"Passed "<<checks<<" native IGB decoder orchestration checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Native IGB decoder test failed: "<<error.what()<<'\n';return 1;}
}
