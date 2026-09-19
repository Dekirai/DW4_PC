#include "dw4/igb_registration_callbacks.hpp"

#include <bit>
#include <iostream>

namespace {
using namespace dw4;using K=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbRegistrationCallbackList> list(std::initializer_list<std::uint32_t> functions){
    auto result=std::make_shared<IgbRegistrationCallbackList>();result->count=static_cast<std::int32_t>(functions.size());
    result->capacity=result->count;result->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(functions);return result;
}
// Explicit physical allocator/free/function endpoints; none register a type.
struct Fixture {
    std::shared_ptr<IgbRegistrationCallbacksServices> services=std::make_shared<IgbRegistrationCallbacksServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbRegistrationCallbacksCall> calls;
    std::function<void(const IgbRegistrationCallbacksCall&,IgbRegistrationCallbacksOperation&)> action;
    std::function<std::shared_ptr<IgbRegistrationCallbacksOperation>(const IgbRegistrationCallbacksCall&)> override;
    Fixture(){
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;
        core->registration_callback_pool=pool;services->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        services->prerequisites->initialized=0;services->prerequisites->started=255;
        services->factory=[this](const IgbRegistrationCallbacksCall& call){
            calls.push_back(call);if(override)if(auto operation=override(call))return operation;
            auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
            operation->resume=[this,call,out=operation.get()]{
                if(call.kind==K::allocate_backing || call.kind==K::reallocate_backing){
                    if(call.bytes>4096)throw std::runtime_error("Prepared allocator exceeded fixture limit.");
                    auto backing=std::make_shared<IgbRegistrationCallbackList::Storage>(call.bytes/4u,0xfeedbeefu);
                    if(call.backing)for(std::size_t i=0;i<backing->size() && i<call.backing->size();++i)(*backing)[i]=(*call.backing)[i];
                    out->backing_result=std::move(backing);
                }
                if(action)action(call,*out);return MessengerCallResult{true,0,{}};
            };return operation;
        };
    }
};
void append_basics(){
    Fixture f;auto callbacks=std::make_shared<IgbRegistrationCallbackList>();callbacks->count=9;callbacks->capacity=-1;
    callbacks->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(3,123);
    auto previous=callbacks->backing;std::weak_ptr<IgbRegistrationCallbackList::Storage> weak=previous;previous.reset();
    igb_initialize_registration_callbacks(callbacks);
    check(!callbacks->backing && callbacks->count==0 && callbacks->capacity==0,"constructor performs backing/capacity/count stores");
    check(!weak.expired(),"constructor raw store does not free an overwritten allocation");
    IgbRegistrationCallbackAppendInvocation first(callbacks,0x12345678);
    check(first.resume(f.services).completed,"initial append completes");
    check(callbacks->count==1 && callbacks->capacity==4 && (*callbacks->backing)[0]==0x12345678,"initial capacity and first slot");
    check(f.calls.size()==1 && f.calls[0].kind==K::allocate_backing && f.calls[0].bytes==16 && f.calls[0].pool==f.pool,"allocation captures actual core pool and bytes");
    check(first.resume().completed && f.calls.size()==1,"completed append does not replay");
    for(std::uint32_t i=1;i<4;++i){IgbRegistrationCallbackAppendInvocation next(callbacks,i);check(next.resume().completed,"spare-capacity append needs no services");}
    IgbRegistrationCallbackAppendInvocation grow(callbacks,99);check(grow.resume(f.services).completed,"reallocation completes");
    check(callbacks->capacity==24 && callbacks->count==5,"original nonlinear growth four becomes twenty-four");
    check(f.calls.size()==2 && f.calls.back().kind==K::reallocate_backing && f.calls.back().bytes==96,"reallocation bytes");
    check((*callbacks->backing)[0]==0x12345678 && (*callbacks->backing)[4]==99,"reallocation retains previous functions and appends");
    auto odd=list({1,2,3});IgbRegistrationCallbackAppendInvocation odd_growth(odd,4);
    check(odd_growth.resume(f.services).completed && odd->capacity==12,"capacity three uses truncated nine halves then multiplies three");
    auto unequal=list({0});unequal->count=0;unequal->capacity=-4;
    IgbRegistrationCallbackAppendInvocation direct(unequal,0);check(direct.resume().completed && (*unequal->backing)[0]==0,"inequality bypasses growth and permits storing null function");
    auto wrapped=list({0});wrapped->count=0x40000000;wrapped->capacity=-1;
    IgbRegistrationCallbackAppendInvocation wrap_index(wrapped,42);
    check(wrap_index.resume().completed && wrapped->count==0x40000001 && (*wrapped->backing)[0]==42,"original shifted byte index wraps before physical store");
    rejects([]{igb_initialize_registration_callbacks({});},"null constructor destination rejected");
    IgbRegistrationCallbackAppendInvocation null({},7);rejects([&]{null.resume();},"null append list faults");
    check(null.faulted(),"append fault recorded");rejects([&]{null.resume(f.services);},"append fault is permanent");
    callbacks->released=true;IgbRegistrationCallbackAppendInvocation released(callbacks,1);
    rejects([&]{released.resume();},"released list rejected");
}
void append_prefixes(){
    Fixture f;auto callbacks=std::make_shared<IgbRegistrationCallbackList>();
    IgbRegistrationCallbackAppendInvocation invocation(callbacks,7);
    check(!invocation.resume().completed && callbacks->capacity==4,"growth is committed before missing ArkCore");
    callbacks->capacity=8;f.core->registration_callback_pool.reset();
    check(!invocation.resume(f.services).completed,"missing core pool field pauses");
    callbacks->capacity=12;auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->registration_callback_pool=std::shared_ptr<ModelSceneObject>{};
    f.services->runtime->singleton=replacement;f.core->registration_callback_pool=f.pool;
    check(invocation.resume(f.services).completed,"captured core survives global replacement");
    check(f.calls.front().bytes==32 && f.calls.front().pool==f.pool,"bytes captured before missing pool and pool owner retained");
    check(callbacks->capacity==12 && callbacks->count==1,"allocator does not rewrite live capacity");

    Fixture mutation;auto changed=list({1,2});std::weak_ptr<IgbRegistrationCallbackList::Storage> injected;
    mutation.action=[&](const auto& call,auto&){if(call.kind==K::reallocate_backing){changed->count=2;changed->capacity=91;
        changed->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(3,99);injected=changed->backing;}};
    IgbRegistrationCallbackAppendInvocation live(changed,77);check(live.resume(mutation.services).completed,"mutation allocation completes");
    check(changed->count==3 && changed->capacity==91 && (*changed->backing)[2]==77 && (*changed->backing)[0]==1,"count reload after allocator and returned backing raw publication");
    check(!injected.expired(),"raw result publication does not release a callback-injected allocation");

    Fixture failed;auto no_memory=std::make_shared<IgbRegistrationCallbackList>();
    failed.action=[](const auto& call,auto& op){if(call.kind==K::allocate_backing)op.backing_result.reset();};
    IgbRegistrationCallbackAppendInvocation oom(no_memory,4);
    rejects([&]{oom.resume(failed.services);},"null allocation faults only at indexed store");
    check(no_memory->count==1 && no_memory->capacity==4 && !no_memory->backing,"allocation failure preserves count prefix");
    auto short_backing=list({0});short_backing->count=2;short_backing->capacity=4;
    IgbRegistrationCallbackAppendInvocation outside(short_backing,1);rejects([&]{outside.resume();},"short physical backing faults");
    check(short_backing->count==3,"out-of-range store keeps published count");

    Fixture negative;auto strange=list({0});strange->count=-1;strange->capacity=-1;
    negative.override=[](const auto& call){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        op->resume=[call,out=op.get()]{out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>();return MessengerCallResult{true,0,{}};};return op;};
    IgbRegistrationCallbackAppendInvocation signed_growth(strange,8);rejects([&]{signed_growth.resume(negative.services);},"negative count still reaches physical store");
    check(strange->capacity==1 && strange->count==0 && negative.calls[0].bytes==4,"signed truncated negative multiplication preserves original prefix");

    Fixture paused;auto retained=std::make_shared<IgbRegistrationCallbackList>();unsigned polls=0,selections=0;
    paused.override=[&](const auto& call){++selections;auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        op->resume=[&,call,out=op.get()]{if(++polls==1)return MessengerCallResult{false,0,"prepared allocation"};
            out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(4);return MessengerCallResult{true,0,{}};};return op;};
    IgbRegistrationCallbackAppendInvocation held(retained,55);check(!held.resume(paused.services).completed,"allocator can pause");
    paused.services->factory={};check(held.resume(paused.services).completed && polls==2 && selections==1,"selected allocator retained across binding removal");
}
void callback_runner(){
    Fixture f;auto callbacks=list({11,22,33});std::vector<std::uint32_t> trace;
    f.action=[&](const auto& call,auto& op){if(call.kind!=K::invoke_function)return;
        trace.push_back(call.function);if(call.function==99)op.callbacks_result=callbacks;
        if(call.function==11){callbacks->count=0;callbacks->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(std::initializer_list<std::uint32_t>{999,44,55});}};
    IgbRegistrationCallbacksInvocation run(99);check(run.resume(f.services).completed,"runner completes actual captured list");
    check(trace==std::vector<std::uint32_t>({99,11,44,55}),"captured count with live backing/functions");
    check(callbacks->released && f.calls.size()==6 && f.calls[4].kind==K::free_backing && f.calls[5].kind==K::free_list,"backing freed before list");
    check(run.resume().completed && f.calls.size()==6,"completed runner does not replay");
    for(const std::int32_t count:{0,-1}){
        Fixture empty;auto nonpositive=std::make_shared<IgbRegistrationCallbackList>();nonpositive->count=count;
        empty.action=[&](const auto& call,auto& op){if(call.kind==K::invoke_function)op.callbacks_result=nonpositive;};
        IgbRegistrationCallbacksInvocation skip(1);check(skip.resume(empty.services).completed,"nonpositive count permits null backing");
        check(empty.calls.size()==2 && empty.calls.back().kind==K::free_list && nonpositive->released,"nonpositive count still deletes list");
    }
    Fixture null_result;IgbRegistrationCallbacksInvocation null(1);check(null.resume(null_result.services).completed && null_result.calls.size()==1,"null returned list needs no cleanup");
    IgbRegistrationCallbacksInvocation null_call(0);rejects([&]{null_call.resume();},"null initial function faults before service binding");
    Fixture bad;auto invalid=list({0});bad.action=[&](const auto&,auto& op){op.callbacks_result=invalid;};
    IgbRegistrationCallbacksInvocation zero(1);rejects([&]{zero.resume(bad.services);},"null deferred function faults");
    check(zero.faulted() && !invalid->released && bad.calls.size()==1,"failure does not invent cleanup");
    invalid->backing.reset();IgbRegistrationCallbacksInvocation short_list(1);
    rejects([&]{short_list.resume(bad.services);},"positive count requires physical backing");
}
void runner_continuations(){
    Fixture f;auto callbacks=list({7,8});unsigned polls=0,created=0;
    f.action=[&](const auto& call,auto& op){if(call.kind==K::invoke_function && call.function==1)op.callbacks_result=callbacks;};
    f.override=[&](const auto& call){if(call.kind!=K::invoke_function || call.function!=7)return std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        ++created;auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{return MessengerCallResult{++polls==2,0,"prepared callback"};};return op;};
    IgbRegistrationCallbacksInvocation run(1);check(!run.resume(f.services).completed,"deferred function pauses");
    (*callbacks->backing)[0]=90;callbacks->count=0;check(run.resume(f.services).completed,"runner resumes selected callback");
    check(created==1 && polls==2 && f.calls[2].function==8,"pending callback target fixed and captured count retained");

    Fixture destruction;auto doomed=list({});std::weak_ptr<IgbRegistrationCallbackList> weak=doomed;
    destruction.action=[&](const auto& call,auto& op){if(call.kind==K::invoke_function)op.callbacks_result=doomed;};
    destruction.override=[&](const auto& call){if(call.kind!=K::free_list)return std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{doomed.reset();destruction.calls.clear();return MessengerCallResult{false,0,"prepared delete"};};return op;};
    {IgbRegistrationCallbacksInvocation abandoned(1);check(!abandoned.resume(destruction.services).completed,"list deletion pauses after callback completion");}
    check(!weak.expired(),"abandonment retains original selected list owner");
    destruction.services.reset();check(weak.expired(),"retention is scoped to services lifetime");

    Fixture recurse;IgbRegistrationCallbacksInvocation reentrant(1);
    recurse.action=[&](const auto&,auto&){try{reentrant.resume(recurse.services);}catch(const IgbRegistrationCallbacksError&){};};
    rejects([&]{reentrant.resume(recurse.services);},"caught reentrant attempt still poisons outer invocation");
    check(reentrant.faulted(),"recursive runner records permanent fault");
}
void prerequisites(){
    Fixture f;IgbRegistrationPrerequisitesInvocation run;
    check(!run.resume().completed,"missing prerequisite storage pauses");
    check(run.resume(f.services).completed,"native prerequisite guard invokes ordered functions");
    std::vector<std::uint32_t> actual;for(const auto& call:f.calls)actual.push_back(call.function);
    check(actual==std::vector<std::uint32_t>({0x80023880,0x800240cc,0x80023c40,0x80031250,0x800374e8}),"all five wrappers in original order");
    check(*f.services->prerequisites->initialized==1 && *f.services->prerequisites->started==1,"both once bytes published");
    IgbRegistrationPrerequisitesInvocation again;check(again.resume(f.services).completed && f.calls.size()==5,"subsequent guard does not repeat");
    for(unsigned value=1;value<256;++value){Fixture nonzero;nonzero.services->prerequisites->initialized=static_cast<std::uint8_t>(value);
        nonzero.services->prerequisites->started=static_cast<std::uint8_t>(value);IgbRegistrationPrerequisitesInvocation skip;
        check(skip.resume(nonzero.services).completed && nonzero.calls.empty(),"all nonzero signed initialization bytes and started bytes skip");}
    Fixture pending;pending.services->factory={};IgbRegistrationPrerequisitesInvocation first;
    check(!first.resume(pending.services).completed && *pending.services->prerequisites->started==1,"started published before missing function");
    IgbRegistrationPrerequisitesInvocation entered;check(entered.resume(pending.services).completed,"distinct recursive guard observes committed started byte");
    auto state=pending.services->prerequisites;state->initialized.reset();state->started.reset();IgbRegistrationPrerequisitesInvocation unknown;
    check(!unknown.resume(pending.services).completed && !state->started,"unknown initialized byte does not touch started");
    state->initialized=1;check(!unknown.resume(pending.services).completed,"unknown started byte pauses after initialized read");
}
void wrappers_and_adapters(){
    auto file=std::make_shared<IgbFileFactoryServices>();auto types=std::make_shared<IgbTypeRegistrationServices>();Fixture f;
    const auto args=igb_file_type_registration_arguments(file);
    check(args.metadata_slot.get()==&file->file_metadata && *args.name=="igIGBFile" && args.flag==0 && args.native_size==0x134,"file argument name/size and canonical alias");
    check(args.parent_registration==0x80032950 && args.parent_getter==0x8002f23c && args.field_callback==0x8002f21c,"actual parent and type getter identities");
    check(args.layout_probe==0x8002ec9c && args.defaults_callback==0x8002f244 && args.trailing_callback==0x8002f724,"actual layout/default/trailing callbacks");
    check(*args.extra_callbacks==std::vector<std::uint32_t>({0x80030078,0x80034130,0x800344a0,0x8002a6b0,0x80036684,0x8002bf48,0x80029054,0}),"actual file extension callback table");
    auto meta=std::make_shared<IgbRuntimeMetaType>();file->file_metadata=meta;
    check(args.metadata_slot->value()==meta,"alias reads later canonical metadata publication");
    rejects([]{igb_file_type_registration_arguments({});},"argument helper rejects missing canonical storage");

    // Explicit already-registered metadata profile. This tests that the real
    // runner/guard/generic registrar still resolve the metadata pool and flags;
    // it does not claim fresh file fields were constructed.
    meta->class_flags=4;types->metadata_pool_handle=7;unsigned pool_calls=0;
    types->factory=[&](const IgbTypeRegistrationCall& call){check(call.kind==IgbTypeRegistrationServiceKind::pool_for_handle && call.handle==7,"ready metadata still reaches pool handle");
        auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[&,out=op.get()]{++pool_calls;out->object_result=f.pool;return MessengerCallResult{true,0,{}};};return op;};
    f.services->prerequisites->initialized=1;f.services->prerequisites->started=1;
    f.services->factory=make_native_igb_registration_function_factory(file,types,f.services,f.services->factory);
    auto factory=make_native_igb_file_registration_factory(f.services);IgbFileFactoryCall request;request.kind=IgbFileFactoryServiceKind::register_file_type;
    const auto op=factory(request);check(op && op->resume().completed,"actual file wrapper through generic ready path");
    check(pool_calls==1 && meta->class_flags==4,"generic busy flag clear and no fabricated fresh fields");
    check(op->resume().completed && pool_calls==1,"adapter retains completed invocation");

    Fixture wrapped;std::vector<std::uint32_t> functions;wrapped.action=[&](const auto& call,auto&){functions.push_back(call.function);};
    wrapped.services->factory=make_native_igb_registration_function_factory(file,types,wrapped.services,wrapped.services->factory);
    IgbRegistrationPrerequisitesInvocation bootstrap;check(bootstrap.resume(wrapped.services).completed,"prerequisite wrappers compose actual66188");
    check(functions==std::vector<std::uint32_t>({0x800238a8,0x800240f4,0x80023c68,0x80031278,0x80037510}),"exact five concrete registration callbacks reached");

    Fixture bridge;auto callbacks=std::make_shared<IgbRegistrationCallbackList>();auto append=make_native_igb_registration_list_factory(bridge.services);
    IgbTypeRegistrationCall append_call;append_call.kind=IgbTypeRegistrationServiceKind::append_callback;append_call.callbacks=callbacks;append_call.function=123;
    check(append(append_call)->resume().completed && callbacks->count==1 && (*callbacks->backing)[0]==123,"generic list adapter composes native append");
    append_call.kind=IgbTypeRegistrationServiceKind::defaults;check(!append(append_call),"unrecognized list adapter operation stays absent");
    auto weak=std::weak_ptr<IgbRegistrationCallbacksServices>(bridge.services);bridge.services.reset();check(weak.expired(),"adapter has weak service ownership");
    append_call.kind=IgbTypeRegistrationServiceKind::append_callback;rejects([&]{append(append_call);},"expired adapter services fail explicitly");
}
}
int main(){try{append_basics();append_prefixes();callback_runner();runner_continuations();prerequisites();wrappers_and_adapters();
    std::cout<<"IGB registration callbacks: "<<checks<<" checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"IGB registration callbacks failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
