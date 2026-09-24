#include "dw4/igb_field_values.hpp"
#include "dw4/igb_stream.hpp"

#include <array>
#include <iostream>

namespace {
using namespace dw4;using K=IgbFieldOperationKind;using SK=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbScratchBuffer> bytes(unsigned count,std::uint8_t value=0){auto p=std::make_shared<IgbScratchBuffer>();p->bytes.assign(count,value);p->initialized.assign(count,1);return p;}
void word(const std::shared_ptr<IgbScratchBuffer>& b,std::uint32_t value){for(unsigned i=0;i<4;++i)b->bytes[i]=static_cast<std::uint8_t>(value>>((3-i)*8));}
std::shared_ptr<IgbDecodeRuntimeField> field(std::uint32_t table=0x80475b60){auto f=std::make_shared<IgbDecodeRuntimeField>();
    f->vtable_word=table;f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(16,0xcc),0};f->default_bytes_address=0x2000;return f;}
std::shared_ptr<IgbInternedString> string(std::uint32_t refs=1){auto h=std::make_shared<IgbInternedString>();h->references=refs;h->value=std::make_shared<const std::string>("value");return h;}
struct Fixture {
    std::shared_ptr<IgbFieldValuesServices> services=std::make_shared<IgbFieldValuesServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbFieldOperationCall> calls;
    std::function<void(const IgbFieldOperationCall&,IgbFieldOperation&)> action;
    unsigned polls{};bool ready=true;
    Fixture(){services->fields=std::make_shared<IgbFieldOperationsServices>();auto& f=*services->fields;
        f.unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};services->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};services->string_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};services->pointer_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->unsigned_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};services->memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->signed_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};services->signed_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        f.factory=make_native_igb_field_values_factory(services,[this](const IgbFieldOperationCall& c){calls.push_back(c);auto op=std::make_shared<IgbFieldOperation>();
            op->resume=[this,c,out=op.get()]{++polls;if(!ready)return MessengerCallResult{false,0,"prepared physical pool"};
                std::uint32_t value{};if(c.kind==K::allocation_pool || c.kind==K::object_pool)out->pool_result=pool;
                else if(c.kind==K::allocation_size)value=16;
                else if(c.kind==K::allocate_default){out->buffer_result={bytes(c.bytes),0};out->buffer_address=0x3000;}
                else if(c.kind==K::free_default)c.buffer.buffer->released=true;
                else throw std::runtime_error("Unknown prepared field operation");if(action)action(c,*out);return MessengerCallResult{true,value,{}};};return op;});
        f.dependencies=std::make_shared<IgbDependenciesServices>();f.dependencies->globals=std::make_shared<SystemStageServices>();f.dependencies->globals->string_pool=pool;
    }
};
void known_virtuals(){
    constexpr std::array<std::uint32_t,12> tables={0x80476e6c,0x80475b60,0x80473e30,0x8047693c,0x804762fc,0x80476630,0x80477054,0x80476088,0x80476848,0x80473f24,0x804758e4,0x80470a44};
    constexpr std::array<std::uint32_t,12> widths={4,4,1,4,4,4,2,4,2,1,4,1};Fixture f;
    f.services->enumeration_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    f.services->unsigned_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    auto alignment=make_native_igb_field_alignment_factory();
    for(std::size_t i=0;i<tables.size();++i){IgbFieldOperationCall c;c.kind=K::field_virtual;c.vtable_word=tables[i];c.virtual_slot=0x64;
        const auto size=f.services->fields->factory(c);check(size && size->resume().value==widths[i],"Known original size leaf differs.");
        c.virtual_slot=0x5c;const auto type=f.services->fields->factory(c);check(type && type->resume().completed && !type->field_result,"Known null prototype was not returned.");
        IgbMetadataLayoutCall a;a.kind=IgbMetadataLayoutKind::field_alignment;a.vtable_word=tables[i];a.virtual_slot=0x6c;
        check(alignment(a)->resume().value==widths[i],"Known original alignment leaf differs.");
        auto descriptor=field(tables[i]);auto input=bytes(4,0x73);c.virtual_slot=0x8c;c.field=descriptor;c.source=IgbRuntimeFieldDefaultSource{input,0};
        const auto copy=f.services->fields->factory(c);check(copy && copy->resume().completed,"Known default copy did not complete.");
        for(unsigned j=0;j<widths[i];++j)check(descriptor->default_bytes->buffer->bytes[j]==0x73,"Known field copied a wrong width.");
        check(descriptor->default_bytes->buffer->bytes[widths[i]]==0xcc,"Known field copied past its original width.");
    }
    IgbFieldOperationCall c;c.kind=K::field_virtual;c.vtable_word=0x80475b60;c.virtual_slot=0x5c;
    f.services->signed_int_prototype.reset();const auto selected=f.services->fields->factory(c);check(!selected->resume().completed,"Missing prototype global was inferred null.");
    auto prototype=field();prototype->ordinal=27;f.services->signed_int_prototype=prototype;
    check(selected->resume().completed && selected->field_result==prototype,"Prototype getter failed to reload its canonical cell.");
    auto exact=make_native_igb_field_values_factory(f.services);
    c.virtual_slot=0x60;check(!exact(c),"Adapter invented an unknown virtual.");
    c.virtual_slot=0x8c;c.argument=0x1234;check(!exact(c),"Unbound nonnull default source was treated as null.");
}
void live_copy_and_overlap(){
    Fixture f;auto descriptor=field();auto source=bytes(8);for(unsigned i=0;i<8;++i)source->bytes[i]=static_cast<std::uint8_t>(i+1);
    auto replacement=bytes(12,0xa5);f.action=[&](const IgbFieldOperationCall& c,IgbFieldOperation&){if(c.kind==K::allocation_size){
        descriptor->default_bytes=IgbRuntimeFieldDefaultSource{replacement,2};descriptor->default_bytes_address=0x4002;descriptor->size=6;source->bytes[0]=0x55;}};
    IgbFieldValueInvocation copy(descriptor,IgbRuntimeFieldDefaultSource{source,0},0x9000);
    check(copy.resume(f.services).completed && descriptor->size==6,"Copy did not observe post-buffer live size.");
    check(replacement->bytes[2]==0x55 && replacement->bytes[7]==6 && replacement->bytes[8]==0xa5,"Copy used stale destination or source bytes.");
    for(const auto offsets:std::array<std::array<unsigned,2>,3>{{{0,1},{1,0},{0,0}}}){
        Fixture overlap;auto shared=bytes(8);for(unsigned i=0;i<8;++i)shared->bytes[i]=static_cast<std::uint8_t>(i+1);
        auto expected=shared->bytes;std::array<std::uint8_t,4> temp{};for(unsigned i=0;i<4;++i)temp[i]=expected[offsets[0]+i];
        for(unsigned i=0;i<4;++i)expected[offsets[1]+i]=temp[i];auto d=field();d->default_bytes=IgbRuntimeFieldDefaultSource{shared,offsets[1]};d->default_bytes_address=0x2000u+offsets[1];
        IgbFieldValueInvocation move(d,IgbRuntimeFieldDefaultSource{shared,offsets[0]});check(move.resume(overlap.services).completed && shared->bytes==expected,"Overlapping031E8 direction differs.");
    }
    Fixture paused;paused.ready=false;auto d=field();auto input=bytes(4,0x6d);IgbFieldValueInvocation wait(d,IgbRuntimeFieldDefaultSource{input,0});
    check(!wait.resume(paused.services).completed && paused.calls.size()==1,"Physical buffer lookup did not suspend.");
    paused.ready=true;check(wait.resume(paused.services).completed && paused.calls.size()==2 && paused.polls==3,"Copy replayed selected physical lookup.");
    Fixture null;auto null_field=field();null_field->default_bytes=IgbRuntimeFieldDefaultSource{};null_field->default_bytes_address=0;
    IgbFieldValueInvocation initialize(null_field);check(initialize.resume(null.services).completed && null_field->default_bytes->buffer && null.calls.size()==2,"Null source skipped required buffer preparation.");
}
void partial_faults_and_zero(){
    for(const bool backwards:{false,true}){Fixture f;auto d=field();auto source=bytes(4,0x44);source->initialized[2]=0;
        IgbFieldValueInvocation copy(d,IgbRuntimeFieldDefaultSource{source,0},backwards?0x1000u:0x3000u);
        rejects([&]{copy.resume(f.services);},"Unknown source byte was silently copied.");check(copy.faulted(),"Partial copy did not poison its invocation.");
        const auto& out=d->default_bytes->buffer->bytes;
        check(backwards?(out[3]==0x44 && out[0]==0xcc && out[1]==0xcc):(out[0]==0x44 && out[1]==0x44 && out[3]==0xcc),"Partial copy prefix used the wrong pointer direction.");
    }
    Fixture late;auto d=field();auto source=bytes(4,0x33);source->initialized[2]=0;IgbFieldValueInvocation unknown(d,IgbRuntimeFieldDefaultSource{source,0});
    check(!unknown.resume(late.services).completed && d->default_bytes->buffer->bytes[0]==0xcc,"Unknown pointer ordering fabricated a partial copy.");
    late.services->buffers.push_back({source,0x9000});rejects([&]{unknown.resume(late.services);},"Late directional binding did not reach the actual read fault.");
    check(d->default_bytes->buffer->bytes[0]==0x33 && d->default_bytes->buffer->bytes[1]==0x33,"Late binding lost committed forward stores.");
    Fixture zero;auto z=field();zero.action=[&](const IgbFieldOperationCall& c,IgbFieldOperation&){if(c.kind==K::allocation_size){z->size=0;z->default_bytes=IgbRuntimeFieldDefaultSource{};z->default_bytes_address=0;}};
    IgbFieldValueInvocation empty(z,IgbRuntimeFieldDefaultSource{},0x1000);check(empty.resume(zero.services).completed,"Zero-length copy dereferenced source or destination.");
    for(const bool present:{false,true}){Fixture mismatch;auto descriptor=field();
        IgbFieldValueInvocation inconsistent(descriptor,present?std::optional<IgbRuntimeFieldDefaultSource>{{bytes(4),0}}:std::nullopt,present?0u:0x1000u);
        rejects([&]{inconsistent.resume(mismatch.services);},"Contradictory source identity was accepted.");
        check(mismatch.calls.size()==2 && descriptor->size==4,"Source identity failure skipped prior buffer preparation.");
    }
    Fixture alias;auto descriptor=field();IgbFieldValueInvocation conflicting(descriptor,*descriptor->default_bytes,0x9000);
    rejects([&]{conflicting.resume(alias.services);},"Same allocation acquired contradictory arithmetic addresses.");
}
struct StringFixture : Fixture {
    std::vector<IgbDependenciesCall> string_calls;std::shared_ptr<IgbInternedString> incoming;
    std::function<void(const IgbDependenciesCall&)> string_action;bool release_ready=true;unsigned releases{},interns{},pool_creations{},pool_initializations{};
    StringFixture(){services->fields->dependencies->factory=[this](const IgbDependenciesCall& c){string_calls.push_back(c);auto op=std::make_shared<IgbDependenciesOperation>();
        op->resume=[this,c,out=op.get()]{
            if(c.kind==SK::create_string_pool){++pool_creations;out->pool_result=pool;}
            else if(c.kind==SK::initialize_string_pool){++pool_initializations;out->pool_result=pool;}
            else if(c.kind==SK::intern_name){++interns;out->string_result=incoming;}
            else if(c.kind==SK::release_string){++releases;if(!release_ready)return MessengerCallResult{false,0,"prepared string release"};}
            else throw std::runtime_error("Unexpected string-default dependency");if(string_action)string_action(c);return MessengerCallResult{true,0,{}};};return op;};}
};
void string_capture_and_release(){
    StringFixture f;auto d=field(0x8047693c);auto original=d->default_bytes->buffer;word(original,0x1110);auto old=string();old->container=f.pool;
    f.incoming=string(7);igb_bind_field_string(f.services,0x1110,old);igb_bind_field_string(f.services,0x2220,f.incoming);
    auto replacement=bytes(4,0xab);f.string_action=[&](const IgbDependenciesCall& c){if(c.kind==SK::intern_name){d->default_bytes=IgbRuntimeFieldDefaultSource{replacement,0};old->references=1;}};
    f.release_ready=false;IgbFieldStringDefaultInvocation call(d,SystemStageString{std::make_shared<const std::string>("new")});
    check(!call.resume(f.services).completed && old->references==0 && f.interns==1 && f.releases==1 && igb_buffer_word(original,0)==0x1110,"String release pause lost original capture/prefix.");
    f.release_ready=true;check(call.resume(f.services).completed && f.releases==2 && f.interns==1,"String release replayed interning or decrement.");
    check(igb_buffer_word(original,0)==0x2220 && igb_buffer_word(replacement,0)==0xabababab && f.incoming->references==7,"String publication used live field buffer or invented a retain.");
    check(call.resume().completed && f.releases==2,"Completed string setter replayed.");
    for(const auto references:{0u,0xffffffffu}){StringFixture wrap;auto descriptor=field();word(descriptor->default_bytes->buffer,0x5000);auto header=string(references);
        igb_bind_field_string(wrap.services,0x5000,header);IgbFieldStringDefaultInvocation clear(descriptor,{});
        check(clear.resume(wrap.services).completed && header->references==references-1 && wrap.releases==0 && igb_buffer_word(descriptor->default_bytes->buffer,0)==0,"String references used packed object semantics.");}
    StringFixture same;auto descriptor=field();same.incoming=string(2);igb_bind_field_string(same.services,0x7000,same.incoming);word(descriptor->default_bytes->buffer,0x7000);
    IgbFieldStringDefaultInvocation alias(descriptor,{});check(alias.resume(same.services).completed && same.incoming->references==1 && same.releases==0,"Aliased string assignment skipped its actual decrement.");
}
void string_pool_and_missing_bindings(){
    StringFixture f;f.services->fields->dependencies->globals->string_pool.reset();auto d=field();word(d->default_bytes->buffer,0);
    IgbFieldStringDefaultInvocation null(d,{});check(null.resume(f.services).completed && f.pool_creations==1 && f.pool_initializations==1 && f.interns==1 && f.calls.empty(),"Null input skipped lazy pool creation or called buffer preparation.");
    StringFixture pending;auto descriptor=field();word(descriptor->default_bytes->buffer,0x1230);pending.incoming=string(2);
    IgbFieldStringDefaultInvocation bind(descriptor,{});check(!bind.resume(pending.services).completed && pending.interns==1,"Unbound old pointer was inferred null.");
    auto old=string(4);igb_bind_field_string(pending.services,0x1230,old);
    check(!bind.resume(pending.services).completed && old->references==3 && igb_buffer_word(descriptor->default_bytes->buffer,0)==0x1230,"Missing new address skipped prior decrement or published a fabricated pointer.");
    igb_bind_field_string(pending.services,0x4560,pending.incoming);check(bind.resume(pending.services).completed && old->references==3 && pending.interns==1 && igb_buffer_word(descriptor->default_bytes->buffer,0)==0x4560,"Late address binding replayed ownership work.");
    rejects([&]{igb_bind_field_string(pending.services,0x4560,string());},"Conflicting string pointer was accepted.");
    rejects([&]{igb_bind_field_string(pending.services,0x7890,pending.incoming);},"One header acquired two arithmetic addresses.");
    StringFixture bad;auto invalid=field();invalid->default_bytes=IgbRuntimeFieldDefaultSource{};IgbFieldStringDefaultInvocation fault(invalid,{});
    rejects([&]{fault.resume(bad.services);},"Null captured buffer was accepted.");check(bad.interns==1 && fault.faulted(),"Null buffer fault skipped earlier interning.");
}
void adapters_reentry_lifetime(){
    StringFixture f;auto descriptor=field();word(descriptor->default_bytes->buffer,0);auto adapter=make_native_igb_metadata_string_default_factory(f.services);
    IgbMetadataDefaultsCall c;c.field=descriptor;check(adapter(c)->resume().completed,"Metadata71694 adapter failed.");
    c.argument=1;check(!adapter(c),"Metadata adapter invented nonnull string input.");
    Fixture recursion;auto d=field();IgbFieldValueInvocation call(d,IgbRuntimeFieldDefaultSource{bytes(4),0});
    recursion.action=[&](const IgbFieldOperationCall&,IgbFieldOperation&){try{call.resume(recursion.services);}catch(const std::runtime_error&){};};
    rejects([&]{call.resume(recursion.services);},"Swallowed recursive copy error did not invalidate the outer call.");
    StringFixture recur;auto fd=field();word(fd->default_bytes->buffer,0);IgbFieldStringDefaultInvocation string_call(fd,{});
    recur.string_action=[&](const IgbDependenciesCall&){try{string_call.resume(recur.services);}catch(const std::runtime_error&){};};
    rejects([&]{string_call.resume(recur.services);},"Swallowed recursive string setter did not poison the outer call.");
    Fixture abandoned;abandoned.ready=false;std::weak_ptr<IgbScratchBuffer> captured;
    {auto source=bytes(4);captured=source;IgbFieldValueInvocation paused(field(),IgbRuntimeFieldDefaultSource{source,0});check(!paused.resume(abandoned.services).completed,"Prepared copy did not suspend.");}
    check(!captured.expired(),"Abandoned pending copy lost its source backing.");abandoned.services.reset();check(captured.expired(),"Copy retention formed a permanent service cycle.");
    auto services=std::make_shared<IgbFieldValuesServices>();auto expired=make_native_igb_field_values_factory(services);services.reset();IgbFieldOperationCall request;
    request.kind=K::field_virtual;request.vtable_word=0x80475b60;request.virtual_slot=0x64;rejects([&]{expired(request);},"Expired native field services were recreated.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_values_tests");
    known_virtuals();live_copy_and_overlap();partial_faults_and_zero();string_capture_and_release();string_pool_and_missing_bindings();adapters_reentry_lifetime();
    std::cout<<"igb_field_values_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Field values failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
