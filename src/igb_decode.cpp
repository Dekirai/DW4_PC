#include "dw4/igb_decode.hpp"

#include <bit>
#include <cstring>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw IgbDecodeError(reason);}
MessengerCallResult pending(const char* reason) {return {false,0,reason};}
const char* boundary(IgbDecodeServiceKind kind) {
    switch(kind) {
    case IgbDecodeServiceKind::open_stream:return "IGB decode requires actual stream opening8004F870.";
    case IgbDecodeServiceKind::read_header:return "IGB decode requires actual header8004F974.";
    case IgbDecodeServiceKind::field_types:return "IGB decode requires runtime field table800514AC.";
    case IgbDecodeServiceKind::alignments:return "IGB decode requires alignment table80051794.";
    case IgbDecodeServiceKind::object_type_bytes:return "IGB decode requires object-type bytes8004FBE8.";
    case IgbDecodeServiceKind::construct_types:return "IGB decode requires runtime metatype construction8004FC94.";
    case IgbDecodeServiceKind::prepare_types:return "IGB decode requires type/field preparation800505E0.";
    case IgbDecodeServiceKind::table_521ec:return "IGB decode requires versioned table800521EC.";
    case IgbDecodeServiceKind::memory_pool_names:return "IGB decode requires memory-pool names80052554.";
    case IgbDecodeServiceKind::shared_509d4:return "IGB decode requires shared-entry stage800509D4.";
    case IgbDecodeServiceKind::shared_50c68:return "IGB decode requires shared-entry stage80050C68.";
    case IgbDecodeServiceKind::shared_50e80:return "IGB decode requires shared-entry stage80050E80.";
    case IgbDecodeServiceKind::unshared_50f58:return "IGB decode requires unshared stage80050F58.";
    case IgbDecodeServiceKind::table_520e8:return "IGB decode requires versioned table800520E8.";
    case IgbDecodeServiceKind::populate_objects:return "IGB decode requires native object population80051AE0.";
    case IgbDecodeServiceKind::file_payload:return "IGB decode requires the selected file payload virtual+84.";
    case IgbDecodeServiceKind::final_table:return "IGB decode requires final table80051CB0.";
    case IgbDecodeServiceKind::export_infos:return "IGB decode requires actual info export80051450.";
    case IgbDecodeServiceKind::activate_objects:return "IGB decode requires object activation800527D8 and its selected object virtuals.";
    case IgbDecodeServiceKind::postprocess:return "IGB decode requires the selected file postprocess virtual+78.";
    case IgbDecodeServiceKind::close_stream:return "IGB decode requires conditional stream closing80051E04.";
    case IgbDecodeServiceKind::cleanup:return "IGB decode requires actual cleanup80051E30, including the double live field-list release.";
    case IgbDecodeServiceKind::legacy_warning:return "IGB decode requires the reached legacy-format warning8006EEE4.";
    case IgbDecodeServiceKind::timing_log:return "IGB decode requires the reached timing log8006EDA4.";
    }
    return "IGB decode requires its reached native operation.";
}
struct Call {
    IgbDecodeCall request;
    IgbDecodeFactory factory;
    std::shared_ptr<IgbDecodeOperation> operation;
    std::function<MessengerCallResult()> callback;
    void begin(IgbDecodeCall value,const std::shared_ptr<IgbDecodeServices>& services) {
        request=std::move(value);factory=services ? services->factory:IgbDecodeFactory{};
        operation.reset();callback={};
    }
    template<class Check> MessengerCallResult poll(const std::shared_ptr<IgbDecodeServices>& services,Check&& check) {
        if(!factory && services)factory=services->factory;
        if(!factory)return pending(boundary(request.kind));
        if(!operation) {
            operation=factory(request);check();
            if(!operation || !operation->resume)fail("IGB operation factory returned no valid continuation.");
            callback=operation->resume;
        }
        const auto result=callback();check();
        if(result.completed) {callback={};factory={};check();}
        return result;
    }
    void clear() {callback={};factory={};request={};operation.reset();}
};
}

struct IgbDecodeInvocation::Impl {
    enum class Phase {
        entry,diagnostic,first_clock,flags,open,header,legacy_gate,warning,warning_result,
        early_fields_gate,early_fields,early_alignments,type_bytes,construct_types,prepare_types,
        early_521ec_gate,early_521ec,pool_gate,pool_names,shared_gate,shared_a,shared_b,shared_c,
        unshared,early_520e8_gate,early_520e8,populate,payload,late_fields_gate,late_fields,
        late_520e8_gate,late_520e8,late_521ec_gate,late_521ec,final_table,infos,activate,
        post_gate,postprocess,success_close,success_cleanup,success_flags,log_gate,last_clock,
        log,log_result,result,failure_cleanup,failure_flags,failure_close,failure_result,
        call,status,done
    };
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbDecodeState> state;
    SystemStageString filename;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    Call call;
    Phase phase=Phase::entry,after=Phase::done;
    bool owns{},running{},fault{},test_status{};
    std::uint32_t returned{};
    std::int32_t initial_version{},result{};
    double start{},elapsed{};
};
IgbDecodeInvocation::IgbDecodeInvocation(std::shared_ptr<LoaderRequestFile> file,SystemStageString filename):
    impl_(std::make_unique<Impl>()) {impl_->file=std::move(file);impl_->filename=std::move(filename);}
IgbDecodeInvocation::~IgbDecodeInvocation() {
    if(impl_->owns && !completed())impl_->file->decode_fault_=true;
}
bool IgbDecodeInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbDecodeInvocation::faulted() const noexcept {return impl_->fault;}
std::int32_t IgbDecodeInvocation::value() const {
    if(!completed() || faulted())fail("IGB decode result requires a completed, unfaulted invocation.");
    return impl_->result;
}
MessengerCallResult IgbDecodeInvocation::resume(std::shared_ptr<IgbDecodeServices> services) {
    auto& s=*impl_;using P=Impl::Phase;using K=IgbDecodeServiceKind;
    if(s.fault || s.running) {s.fault=true;if(s.owns)s.file->decode_fault_=true;fail("IGB decode requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,std::bit_cast<std::uint32_t>(s.result),{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.running);
    const auto check=[&] {if(s.fault || (s.owns && s.file->decode_fault_))fail("IGB decode was invalidated by a recursive operation.");};
    const auto select=[&](K kind,P after,bool tested=true,SystemStageString filename={},double elapsed=0) {
        s.call.begin({kind,s.file,s.state,std::move(filename),elapsed},services);
        s.after=after;s.test_status=tested;s.phase=P::call;
    };
    const auto finish=[&](std::int32_t value) {
        s.call.clear();check();s.result=value;s.phase=P::done;
        if(s.owns) {s.file->active_decode_.reset();s.owns=false;s.token.reset();}
        return MessengerCallResult{true,std::bit_cast<std::uint32_t>(value),{}};
    };
    const auto stream=[&]() -> IgbStreamState& {
        if(!s.state->stream)fail("IGB decode reaches missing canonical stream-field storage.");
        return *s.state->stream;
    };
    const auto version=[&] {return std::bit_cast<std::int32_t>(stream().version);};
    const auto clock=[&]() -> std::shared_ptr<NativeGameClock> {
        if(!services || !services->clock)return {};
        const auto result=*services->clock;
        if(!result)fail("IGB decode dereferences a represented null global timer.");
        return result;
    };
    try {
        for(;;) {check();switch(s.phase) {
        case P::entry:
            if(!s.file)fail("IGB decode requires its actual file owner.");
            if(s.file->decode_fault_)fail("A faulted IGB file cannot replay its decode prefix.");
            if(!s.file->active_decode_.expired())fail("Another pending invocation owns this IGB file decode.");
            if(!s.file->igb_decode)return pending("IGB decode requires the concrete file fields and reflected defaults.");
            s.state=s.file->igb_decode;
            if(!s.state->stream || !s.state->tables)fail("IGB decode requires canonical stream and table field owners.");
            if(s.state->stream->file.lock()!=s.file)fail("IGB stream fields do not belong to the selected canonical file.");
            s.file->active_decode_=s.token;s.owns=true;s.phase=P::diagnostic;break;
        case P::diagnostic: {
            if(!services)return pending("IGB decode requires its diagnostic global owner.");
            if(!s.filename)fail("IGB readFile formatting reaches a null filename for %s.");
            const auto length=std::strlen(s.filename->c_str());
            if(length>1024*1024)fail("IGB diagnostic exceeds the bounded native filename profile.");
            services->diagnostic="readFile("+std::string(s.filename->c_str(),length)+")";
            s.phase=P::first_clock;break;
        }
        case P::first_clock: {
            const auto selected=clock();
            if(!selected)return pending("IGB decode requires its initial global timer virtual+64.");
            s.start=selected->time_seconds();check();s.phase=P::flags;break;
        }
        case P::flags:
            s.file->succeeded_31=0;s.file->loading=1;s.state->objects_initialized=0;s.phase=P::open;break;
        case P::open:select(K::open_stream,P::header,true,s.filename);break;
        case P::header:select(K::read_header,P::legacy_gate);break;
        case P::legacy_gate:
            s.initial_version=version();
            s.phase=s.initial_version<5 && stream().flag_d5!=0 ? P::warning:P::early_fields_gate;break;
        case P::warning:
            if(!services || !services->warning_suppressed)return pending("IGB decode requires the live legacy-warning suppression byte.");
            if(*services->warning_suppressed!=0) {s.phase=P::failure_cleanup;break;}
            if(!stream().stream)fail("Legacy IGB warning dereferences a null file stream.");
            select(K::legacy_warning,P::warning_result,false,stream().stream->filename);break;
        case P::warning_result:
            if(s.returned==2) {
                if(!services)return pending("IGB warning requires its suppression-byte destination.");
                services->warning_suppressed=std::uint8_t{1};
            }
            s.phase=P::failure_cleanup;break;
        case P::early_fields_gate:s.phase=s.initial_version>=3 ? P::early_fields:P::type_bytes;break;
        case P::early_fields:select(K::field_types,P::early_alignments);break;
        case P::early_alignments:select(K::alignments,P::type_bytes);break;
        case P::type_bytes:select(K::object_type_bytes,P::construct_types);break;
        case P::construct_types:select(K::construct_types,P::prepare_types);break;
        case P::prepare_types:select(K::prepare_types,P::early_521ec_gate);break;
        case P::early_521ec_gate:s.phase=version()>=5 ? P::early_521ec:P::pool_gate;break;
        case P::early_521ec:select(K::table_521ec,P::pool_gate);break;
        case P::pool_gate:s.phase=stream().flag_124!=0 ? P::pool_names:P::shared_gate;break;
        case P::pool_names:select(K::memory_pool_names,P::shared_gate);break;
        case P::shared_gate:s.phase=stream().flag_d6!=0 ? P::shared_a:P::unshared;break;
        case P::shared_a:select(K::shared_509d4,P::shared_b);break;
        case P::shared_b:select(K::shared_50c68,P::shared_c);break;
        case P::shared_c:select(K::shared_50e80,P::early_520e8_gate);break;
        case P::unshared:select(K::unshared_50f58,P::early_520e8_gate);break;
        case P::early_520e8_gate:s.phase=version()>=5 ? P::early_520e8:P::populate;break;
        case P::early_520e8:select(K::table_520e8,P::populate);break;
        case P::populate:select(K::populate_objects,P::payload);break;
        case P::payload:select(K::file_payload,P::late_fields_gate);break;
        case P::late_fields_gate:s.phase=version()<3 ? P::late_fields:P::late_520e8_gate;break;
        case P::late_fields:select(K::field_types,P::late_520e8_gate);break;
        case P::late_520e8_gate:s.phase=version()<5 ? P::late_520e8:P::late_521ec_gate;break;
        case P::late_520e8:select(K::table_520e8,P::late_521ec_gate);break;
        case P::late_521ec_gate:s.phase=version()<5 ? P::late_521ec:P::final_table;break;
        case P::late_521ec:select(K::table_521ec,P::final_table);break;
        case P::final_table:select(K::final_table,P::infos);break;
        case P::infos:select(K::export_infos,P::activate);break;
        case P::activate:select(K::activate_objects,P::post_gate,false);break;
        case P::post_gate:s.phase=s.file->initialize_after_load_115==1 ? P::postprocess:P::success_close;break;
        case P::postprocess:select(K::postprocess,P::success_close);break;
        case P::success_close:select(K::close_stream,P::success_cleanup,false);break;
        case P::success_cleanup:select(K::cleanup,P::success_flags,false);break;
        case P::success_flags:s.file->succeeded_31=1;s.file->loading=0;s.phase=P::log_gate;break;
        case P::log_gate:
            if(!services || !services->timing_suppressed)return pending("IGB decode requires the live timing-log suppression byte.");
            s.phase=*services->timing_suppressed==0 ? P::last_clock:P::result;break;
        case P::last_clock: {
            const auto selected=clock();
            if(!selected)return pending("IGB decode requires its final global timer virtual+64.");
            const auto end=selected->time_seconds();check();s.elapsed=end-s.start;s.phase=P::log;break;
        }
        case P::log:select(K::timing_log,P::log_result,false,s.filename,s.elapsed);break;
        case P::log_result:
            if(s.returned==2) {
                if(!services)return pending("IGB timing log requires its suppression-byte destination.");
                services->timing_suppressed=std::uint8_t{1};
            }
            s.phase=P::result;break;
        case P::result:return finish(s.state->object_count);
        case P::failure_cleanup:select(K::cleanup,P::failure_flags,false);break;
        case P::failure_flags:s.file->loading=0;s.file->succeeded_31=0;s.phase=P::failure_close;break;
        case P::failure_close:select(K::close_stream,P::failure_result,false);break;
        case P::failure_result:return finish(-1);
        case P::call: {
            const auto result=s.call.poll(services,check);
            if(!result.completed)return result;
            s.returned=result.value;s.call.clear();check();s.phase=s.test_status ? P::status:s.after;break;
        }
        case P::status:
            if(!stream().statuses || !stream().statuses->failure)
                return pending("IGB decode requires the live failure word after its completed helper.");
            s.phase=s.returned==*stream().statuses->failure ? P::failure_cleanup:s.after;break;
        case P::done:return {true,std::bit_cast<std::uint32_t>(s.result),{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.file->decode_fault_=true;throw;}
}

IgbDecodeFactory make_native_igb_stage_factory(std::weak_ptr<IgbDecodeServices> services,IgbDecodeFactory remaining) {
    return [services=std::move(services),remaining=std::move(remaining)](const IgbDecodeCall& request) {
        using K=IgbDecodeServiceKind;
        if(request.kind!=K::open_stream && request.kind!=K::read_header && request.kind!=K::field_types &&
            request.kind!=K::alignments && request.kind!=K::object_type_bytes && request.kind!=K::construct_types) {
            if(remaining)return remaining(request);
            auto operation=std::make_shared<IgbDecodeOperation>();
            operation->resume=[kind=request.kind] {return pending(boundary(kind));};return operation;
        }
        const auto live=services.lock();
        if(!live)fail("Native IGB stages require a live service owner.");
        if(!request.state)fail("Native IGB stage requires its concrete file state.");
        auto operation=std::make_shared<IgbDecodeOperation>();
        if(request.kind==K::open_stream) {
            const auto call=std::make_shared<IgbOpenInvocation>(request.state->stream,request.filename);
            operation->resume=[call,live] {return call->resume(live->stream);};
        }else if(request.kind==K::read_header) {
            const auto call=std::make_shared<IgbHeaderInvocation>(request.state->stream);
            operation->resume=[call,live] {return call->resume(live->stream);};
        }else if(request.kind==K::construct_types) {
            const auto call=std::make_shared<IgbTypeMappingInvocation>(request.file,request.state->stream,
                request.state->tables,request.state->type_mapping);
            operation->resume=[call,live] {return call->resume(live->type_mapping);};
        }else {
            const auto kind=request.kind==K::field_types ? IgbDecodeTableKind::field_types:
                request.kind==K::alignments ? IgbDecodeTableKind::alignments:IgbDecodeTableKind::object_type_bytes;
            const auto call=std::make_shared<IgbDecodeTablesInvocation>(request.file,request.state->stream,request.state->tables,kind);
            operation->resume=[call,live] {return call->resume(live->tables);};
        }
        return operation;
    };
}
LoaderRequestFactory make_native_igb_loader_factory(std::weak_ptr<IgbDecodeServices> services,LoaderRequestFactory remaining) {
    return [services=std::move(services),remaining=std::move(remaining)](const LoaderRequestCall& request) {
        auto operation=std::make_shared<LoaderRequestOperation>();
        if(request.kind!=LoaderRequestServiceKind::decode_igb) {
            if(remaining)return remaining(request);
            operation->resume=[] {return pending("Native IGB loader still requires its reached file factory, queue or other loader operation.");};
            return operation;
        }
        const auto live=services.lock();
        if(!live)fail("Native IGB loader requires a live decoder service owner.");
        const auto call=std::make_shared<IgbDecodeInvocation>(request.file,request.filename);
        const std::weak_ptr<LoaderRequestOperation> destination=operation;
        operation->resume=[call,live,destination] {
            const auto result=call->resume(live);
            if(result.completed) {
                const auto output=destination.lock();
                if(!output)fail("Native IGB decoder result owner was destroyed.");
                output->load_result=call->value();
            }
            return result;
        };
        return operation;
    };
}
} // namespace dw4
