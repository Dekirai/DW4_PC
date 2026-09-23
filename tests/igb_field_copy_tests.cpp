#include "dw4/igb_field_copy.hpp"
#include "dw4/igb_stream.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbFieldCopyServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbScratchBuffer> bytes(unsigned size,std::uint8_t value){auto b=std::make_shared<IgbScratchBuffer>();
    b->bytes.assign(size,value);b->initialized.assign(size,1);return b;}
struct Fixture {
    std::shared_ptr<IgbFieldCopyServices> services=std::make_shared<IgbFieldCopyServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbDecodeRuntimeField> source=std::make_shared<IgbDecodeRuntimeField>(),destination=std::make_shared<IgbDecodeRuntimeField>();
    IgbBootstrapPointer allocation;std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>();
    std::vector<IgbFieldCopyCall> calls;std::function<void(const IgbFieldCopyCall&,IgbFieldCopyOperation&)> action;
    unsigned polls{};bool ready=true;
    Fixture(){
        services->fields=std::make_shared<IgbMetaFieldFactoryServices>();services->fields->bootstrap=std::make_shared<IgbBootstrapServices>();
        auto b=services->fields->bootstrap;b->runtime=std::make_shared<IgbRuntimeTypesServices>();b->runtime->singleton=core;
        b->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(metadata);metadata->allocation_prefix=0x20;
        allocation.storage=std::make_shared<IgbObjectStorage>();allocation.storage->original_address=0x10000;
        allocation.storage->bytes=bytes(0x44,0xcc);allocation.offset=8;allocation.storage->bindings.push_back({8,{},{},{},{},destination});
        source->reference_word=0x12800090;source->offset=0x1234;source->flag_25=0x81;source->type_ordinal=0xffff;
        source->ordinal=0xfffe;source->size=4;source->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};source->global_slot_address=0;
        source->name_string=std::shared_ptr<IgbInternedString>{};source->declaring_type=std::weak_ptr<IgbRuntimeMetaType>{};
        source->word_30=0x12345678;source->default_mode=0xff;source->flag_27=1;source->flag_28=2;
        source->flag_29=3;source->flag_2a=4;source->flag_2b=5;source->flag_2c=6;source->flag_26=0x7f;
        source->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4,0x57),0};source->default_bytes_address=0x20000;
        services->factory=[this](const IgbFieldCopyCall& c){calls.push_back(c);auto op=std::make_shared<IgbFieldCopyOperation>();
            op->resume=[this,c,out=op.get()]{++polls;if(!ready)return MessengerCallResult{false,0,"prepared copy endpoint"};
                if(c.kind==K::allocation_pool || c.kind==K::tagged_pool)out->pool_result=pool;
                else if(c.kind==K::base_metadata)out->metadata_result=metadata;
                else if(c.kind==K::allocate_default){out->buffer_result={bytes(c.bytes,0xa5),0};out->buffer_address=0x30000;}
                if(action)action(c,*out);return MessengerCallResult{true,c.kind==K::pool_index?0x1a5u:0u,{}};
            };return op;};
    }
};
void enabled_and_pending(){
    Fixture f;f.core->enabled=1;f.ready=false;IgbFieldCopyInvocation call(f.allocation,f.source);
    check(!call.resume(f.services).completed && f.calls.size()==1 && f.calls[0].kind==K::allocation_pool &&
        f.calls[0].address==0xffe8,"Enabled base copy lost captured metadata prefix or allocation address.");
    check(igb_bootstrap_read_word(f.allocation,0)==0x804717f0 && igb_bootstrap_read_word(f.allocation,4)==0x00800001,
        "Base reset/bit7 copy differs before the pending owner lookup.");
    f.metadata->allocation_prefix=0x900;f.ready=true;
    check(call.resume(f.services).completed && call.result()==f.destination,"Enabled base copy did not resume.");
    check(f.calls.size()==4 && f.calls[1].kind==K::pool_index && f.calls[1].pool==f.pool &&
        f.calls[2].kind==K::tagged_pool && f.calls[2].handle==0xa5 && f.calls[3].kind==K::allocate_default && f.calls[3].bytes==4,
        "Base copy substituted pool+16C or changed low-byte5920C/D8 arguments.");
    check(f.destination->reference_word==0xa5800001 && igb_bootstrap_read_word(f.allocation,4)==0xa5800001 &&
        igb_bootstrap_read_word(f.allocation,0x10)==0xfffffffe && f.destination->default_bytes->buffer->bytes==std::vector<std::uint8_t>(4,0x57),
        "Enabled base copy changed packed references, signed ordinals or default bytes.");
    check(f.allocation.storage->bytes->bytes[8+0x16]==0xcc && f.allocation.storage->bytes->bytes[8+0x2d]==0xcc,
        "Base copy initialized untouched padding.");
    const auto polls=f.polls;check(call.resume(f.services).completed && f.polls==polls,"Completed copy repeated operations.");

    Fixture null_pool;null_pool.core->enabled=1;
    null_pool.action=[&](const IgbFieldCopyCall& c,IgbFieldCopyOperation& op){if(c.kind==K::allocation_pool)op.pool_result.reset();};
    IgbFieldCopyInvocation classified(null_pool.allocation,null_pool.source);
    check(classified.resume(null_pool.services).completed && null_pool.calls[1].kind==K::pool_index && !null_pool.calls[1].pool,
        "A null allocation owner incorrectly skipped the reached5920C classification endpoint.");
}
void live_gate_and_copy(){
    Fixture f;f.services->fields->bootstrap->runtime->singleton.reset();IgbFieldCopyInvocation call(f.allocation,f.source);
    check(!call.resume(f.services).completed && f.calls.empty(),"Unknown core pointer became a disabled core.");
    igb_bootstrap_write_word(f.allocation,0,0x12345678);f.core->enabled=1;f.services->fields->bootstrap->runtime->singleton=f.core;
    f.action=[&](const IgbFieldCopyCall& c,IgbFieldCopyOperation&){if(c.kind==K::allocate_default){
        check(c.bytes==4,"Default allocation lost captured pre-callback size.");
        igb_field_store_size(f.destination,2);f.source->default_bytes=IgbRuntimeFieldDefaultSource{bytes(2,0x96),0};f.source->default_bytes_address=0x40000;
    }};
    check(call.resume(f.services).completed && f.calls[0].kind==K::base_metadata && f.calls[0].vtable_word==0x12345678,
        "Resumed core gate skipped its live metadata virtual.");
    check(f.destination->default_bytes->buffer->bytes==std::vector<std::uint8_t>({0x96,0x96,0xa5,0xa5}) && f.destination->size==2,
        "Default copy failed to reload source pointer and destination size after allocation.");
    Fixture bad;bad.source.reset();IgbFieldCopyInvocation null_source(bad.allocation,bad.source);
    rejects([&]{null_source.resume(bad.services);},"Null source was not rejected at its reached flag read.");
    check(igb_bootstrap_read_word(bad.allocation,4)==1 && igb_bootstrap_read_word(bad.allocation,0)==0x804717f0,
        "Null source rejection lost original destination reset prefix.");
    rejects([&]{null_source.resume(bad.services);},"Faulted copy resumed.");
}
void captured_destination_and_identity(){
    Fixture f;f.source->default_bytes.reset();f.source->default_bytes_address.reset();
    IgbFieldCopyInvocation call(f.allocation,f.source);
    check(!call.resume(f.services).completed && f.calls.size()==2,"Missing source pointer did not suspend after default allocation.");
    const auto captured=*f.destination->default_bytes;auto replacement=bytes(4,0xee);
    igb_field_store_default(f.destination,{replacement,0},0x50000);igb_field_store_size(f.destination,2);
    f.source->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4,0x96),0};f.source->default_bytes_address=0x40000;
    check(call.resume(f.services).completed && f.calls.size()==2 &&
        captured.buffer->bytes==std::vector<std::uint8_t>({0x96,0x96,0xa5,0xa5}) && replacement->bytes==std::vector<std::uint8_t>(4,0xee) &&
        igb_bootstrap_read_word(f.allocation,0x20)==0x50000,"Late source binding redirected the captured default destination or replayed allocation.");
    Fixture changed;changed.ready=false;IgbFieldCopyInvocation invalid(changed.allocation,changed.source);
    check(!invalid.resume(changed.services).completed,"Identity test did not suspend in its selected pool callback.");
    const auto physical=changed.allocation.storage->bytes->bytes;
    changed.allocation.storage->bindings[0].field=std::make_shared<IgbDecodeRuntimeField>();changed.ready=true;
    rejects([&]{invalid.resume(changed.services);},"Copy resumed with a different canonical physical owner.");
    check(invalid.faulted() && changed.allocation.storage->bytes->bytes==physical && changed.calls.size()==1,
        "Changed identity reached later callback or physical store.");
}
void partial_bytes_and_recursion(){
    Fixture f;auto source=bytes(4,0x11);source->initialized[1]=0;f.source->default_bytes=IgbRuntimeFieldDefaultSource{source,0};
    IgbFieldCopyInvocation call(f.allocation,f.source);rejects([&]{call.resume(f.services);},"Unavailable source byte did not fault.");
    check(f.destination->default_bytes->buffer->bytes==std::vector<std::uint8_t>({0xa5,0xa5,0x11,0x11}),
        "Backward031E8 fault lost its completed byte-store prefix.");
    Fixture recursive;IgbFieldCopyInvocation invocation(recursive.allocation,recursive.source);
    recursive.action=[&](const IgbFieldCopyCall&,IgbFieldCopyOperation&){try{invocation.resume(recursive.services);}catch(const std::runtime_error&) {}};
    rejects([&]{invocation.resume(recursive.services);},"Caught recursive resume did not invalidate the outer copy.");
    check(invocation.faulted() && recursive.calls.size()==1,"Recursive copy reached later allocation.");
}
}
int main(){try{enabled_and_pending();live_gate_and_copy();captured_destination_and_identity();partial_bytes_and_recursion();
    std::cout<<"igb_field_copy_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Field copy failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
