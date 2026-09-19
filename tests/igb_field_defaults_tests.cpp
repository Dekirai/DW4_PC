#include "dw4/igb_field_defaults.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& action,const char* why) {++checks;try {action();}catch(const std::runtime_error&) {return;}throw std::runtime_error(why);}
std::shared_ptr<IgbScratchBuffer> bytes(std::initializer_list<std::uint8_t> data) {
    auto result=std::make_shared<IgbScratchBuffer>();result->bytes=data;result->initialized.assign(data.size(),1);return result;
}
struct Fixture {
    IgbObjectInstance object;
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    IgbObjectInitFactory factory=make_native_igb_scalar_field_factory();
    Fixture() {
        object.storage=std::make_shared<IgbObjectStorage>();object.file=std::make_shared<LoaderRequestFile>();
        object.storage->bytes=std::make_shared<IgbScratchBuffer>();object.storage->bytes->bytes.assign(0x140,0xcd);
        object.storage->bytes->initialized.assign(0x140,0);object.storage->bindings={{0,object.file}};
        field->vtable_word=0x80475b60;field->offset=0x60;field->size=4;field->default_mode=0;
        field->default_bytes=IgbRuntimeFieldDefaultSource{bytes({0,0,0xfa,0xda}),0};
    }
    std::shared_ptr<IgbObjectInitOperation> operation(std::uint32_t slot) {
        IgbObjectInitCall call;call.kind=IgbObjectInitServiceKind::field_virtual;call.object=object;call.field=field;call.virtual_slot=slot;
        return factory(call);
    }
};
void leaves_and_binding() {
    for(const auto table:{0x80475b60u,0x80473e30u})for(const auto slot:{0x84u,0x7cu,0x74u,0xf0u}) {
        Fixture f;f.field->vtable_word=table;f.field->default_bytes.reset();f.object.storage.reset();
        const auto op=f.operation(slot);check(op && op->resume().completed && op->resume().value==0,"Verified scalar leaf accessed default or object storage.");
    }
    Fixture f;check(!f.operation(0x80),"Unknown scalar slot was fabricated.");
    f.field->vtable_word.reset();check(!f.operation(0x70),"Missing vtable identity was inferred.");
    f.field->vtable_word=0x804762fc;check(!f.operation(0x70),"Object-reference defaults were replaced by scalar copying.");
    unsigned delegated{};f.factory=make_native_igb_scalar_field_factory([&](const auto&) {++delegated;return std::make_shared<IgbObjectInitOperation>();});
    check(f.operation(0x70) && delegated==1,"Unimplemented reference field did not reach its explicit provider.");
}
void copy_and_capture() {
    for(const auto slot:{0x70u,0x78u}) {
        Fixture f;const auto op=f.operation(slot);check(op->resume().completed && igb_buffer_word(f.object.storage->bytes,0x60)==0xfadau,"Scalar default copy changed big-endian bytes.");
        f.field->default_bytes->buffer->bytes={1,2,3,4};
        check(op->resume().completed && igb_buffer_word(f.object.storage->bytes,0x60)==0xfadau,"Completed operation replayed byte writes.");
    }
    Fixture f;f.field->default_bytes.reset();const auto op=f.operation(0x70);
    check(!op->resume().completed,"Unrepresented default pointer became zero bytes.");
    f.field->offset=0x64;f.field->size=1;f.field->vtable_word=0x804762fc;
    f.field->default_bytes=IgbRuntimeFieldDefaultSource{bytes({0x12,0x34,0x56,0x78}),0};
    check(op->resume().completed && igb_buffer_word(f.object.storage->bytes,0x60)==0x12345678u,"Pending source read changed captured offset, size or selected handler.");
    check(f.object.storage->bytes->initialized[0x64]==0,"Pending scalar copy wrote its mutated destination.");
    Fixture zero;zero.field->size=0;zero.field->default_bytes=IgbRuntimeFieldDefaultSource{};zero.object.storage.reset();
    check(zero.operation(0x70)->resume().completed,"Zero-size original copy dereferenced null storage.");
}
void aliases_and_physical_faults() {
    for(const auto source:{0u,1u,4u})for(const auto destination:{0u,1u,4u}) {
        Fixture f;f.object.file.reset();f.object.storage->bytes=bytes({0,1,2,3,4,5,6,7,8,9,10,11});
        f.field->offset=destination;f.field->size=6;f.field->default_bytes=IgbRuntimeFieldDefaultSource{f.object.storage->bytes,source};
        const auto before=f.object.storage->bytes->bytes;
        check(f.operation(0x70)->resume().completed,"Aliased scalar copy did not complete.");
        for(unsigned i=0;i<6;++i)check(f.object.storage->bytes->bytes[destination+i]==before[source+i],"Original forward/backward copy direction was lost.");
    }
    Fixture missing;missing.field->default_bytes->buffer->initialized[2]=0;const auto op=missing.operation(0x70);
    rejects([&] {op->resume();},"Uninitialized source default byte was accepted.");
    check(std::none_of(missing.object.storage->bytes->initialized.begin(),missing.object.storage->bytes->initialized.end(),[](auto b) {return b!=0;}),"Rejected complete source range partially wrote its destination.");
    missing.field->default_bytes->buffer->initialized[2]=1;rejects([&] {op->resume();},"Faulted scalar copy restarted.");
    Fixture short_destination;short_destination.field->offset=0x13e;const auto before=short_destination.object.storage->bytes->bytes;
    rejects([&] {short_destination.operation(0x70)->resume();},"Short destination was accepted.");
    check(short_destination.object.storage->bytes->bytes==before,"Rejected complete destination range wrote a prefix.");
    Fixture released;released.field->default_bytes->buffer->released=true;rejects([&] {released.operation(0x70)->resume();},"Released default bytes were read.");
    Fixture null;null.field->default_bytes=IgbRuntimeFieldDefaultSource{};rejects([&] {null.operation(0x70)->resume();},"Positive-size copy accepted a represented null source.");
    Fixture header;header.field->offset=4;check(header.operation(0x70)->resume().completed && header.object.file->reference_word==0xfadau,"Explicit header alias left the canonical reference view stale.");
}
void actual_initializer() {
    Fixture f;
    auto boolean=std::make_shared<IgbDecodeRuntimeField>();boolean->vtable_word=0x80473e30;boolean->offset=0x115;
    boolean->size=1;boolean->default_mode=2;boolean->default_bytes=IgbRuntimeFieldDefaultSource{bytes({1}),0};
    auto meta=std::make_shared<IgbRuntimeMetaType>();meta->type_word=0x804724c4;meta->allocation_prefix=0;meta->class_flags=4;
    meta->instance_count=0;meta->instances=std::shared_ptr<IgbObjectInstanceList>{};
    meta->fields=std::make_shared<IgbDecodeFieldRegistry>();meta->fields->count=2;
    meta->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(IgbDecodeFieldRegistry::Storage{f.field,boolean});
    auto base=std::make_shared<IgbRuntimeMetaType>();base->fields=std::make_shared<IgbDecodeFieldRegistry>();
    auto services=std::make_shared<IgbObjectInitServices>();services->runtime=std::make_shared<IgbRuntimeTypesServices>();
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->vtable_offset=0;core->enabled=1;core->field_initialization_enabled=1;
    services->runtime->singleton=core;services->object_metadata=base;
    unsigned unexpected_fields{};
    services->factory=make_native_igb_scalar_field_factory([&](const IgbObjectInitCall& call) {
        if(call.kind==IgbObjectInitServiceKind::field_virtual) {++unexpected_fields;return std::shared_ptr<IgbObjectInitOperation>{};}
        // Explicit object/pool endpoints isolate the real scalar field bodies.
        auto op=std::make_shared<IgbObjectInitOperation>();if(call.kind==IgbObjectInitServiceKind::object_virtual && call.virtual_slot==0x58)op->metadata_result=meta;
        op->resume=[] {return MessengerCallResult{true,0,{}};};return op;
    });
    IgbObjectInitInvocation init(f.object.storage,0,meta);check(init.resume(services).completed,"Actual initializer did not compose with concrete scalar defaults.");
    check(unexpected_fields==0 && init.result()==f.object.file,"Initializer used a substituted scalar field endpoint.");
    check(igb_buffer_word(f.object.storage->bytes,0x60)==0xfadau && igb_buffer_bytes(f.object.storage->bytes,0x115,1)[0]==1,"Actual scalar defaults failed to publish cookie and boolean bytes.");
    check(meta->instance_count==1 && f.object.file->reference_word==1,"Scalar composition skipped initializer header or tail.");
    check(!f.object.file->igb_decode,"Scalar initialization manufactured a completed decoder.");
}
}
int main(int argc,char**) {
    try {if(argc!=1)throw std::runtime_error("Usage: igb_field_defaults_tests");
        leaves_and_binding();copy_and_capture();aliases_and_physical_faults();actual_initializer();
        std::cout<<"igb_field_defaults_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
