#include "dw4/igb_field_init.hpp"

#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
struct Fixture {
    std::shared_ptr<IgbFieldInitServices> services=std::make_shared<IgbFieldInitServices>();
    IgbObjectInitCall call;
    Fixture() {
        services->values=std::make_shared<IgbFieldValuesServices>();services->values->fields=std::make_shared<IgbFieldOperationsServices>();
        services->values->fields->factory=make_native_igb_field_values_factory(services->values);
        call.kind=IgbObjectInitServiceKind::field_virtual;call.field=std::make_shared<IgbDecodeRuntimeField>();
        call.field->vtable_word=0x804762fc;call.field->offset=0x38;call.field->flag_34=0;
        call.object.storage=std::make_shared<IgbObjectStorage>();call.object.storage->original_address=0x100000;
        call.object.storage->bytes=std::make_shared<IgbScratchBuffer>();
        call.object.storage->bytes->bytes.assign(0x54,0xcc);call.object.storage->bytes->initialized.assign(0x54,0);
        call.object.offset=0x10;call.object.field=std::make_shared<IgbDecodeRuntimeField>();call.object.field->vtable_word=0x804762fc;
        call.object.storage->bindings.push_back({0x10,{},{},{},{},call.object.field});
    }
    std::shared_ptr<IgbObjectInitOperation> select(std::uint32_t slot) {
        call.virtual_slot=slot;return make_native_igb_field_init_factory(services)(call);
    }
};
void reference_clear() {
    for(const auto table:{0x8047693cu,0x804762fcu,0x80476630u,0x80476088u}) {
        Fixture f;f.call.field->vtable_word=table;f.call.field->size=0xffff;
        const auto op=f.select(0x84);check(op && op->resume().completed,"Actual reference clear did not finish.");
        check(igb_buffer_word(f.call.object.storage->bytes,0x48)==0 && f.call.object.field->object_type &&
            igb_field_declaring_type_is_null(*f.call.object.field->object_type),"Reference clear lost physical/canonical null state.");
        check(f.call.object.storage->bytes->initialized[0x47]==0 && f.call.object.storage->bytes->initialized[0x4c]==0,
            "Reference clear used the descriptor's stored size or touched padding.");
    }
    Fixture f;unsigned resumes{};f.services->values->fields->factory=[&](const IgbFieldOperationCall& call) {
        check(call.virtual_slot==0x64 && call.vtable_word==0x804762fc,"Reference clear changed its selected size virtual.");
        auto op=std::make_shared<IgbFieldOperation>();op->resume=[&]{++resumes;
            f.call.field->offset=0x20;f.call.field->vtable_word=0x80473e30;
            return MessengerCallResult{resumes>1,0x10004,{}};};return op;
    };
    const auto op=f.select(0x84);check(!op->resume().completed && op->resume().completed,"Reference clear lost its pending size operation.");
    check(igb_buffer_word(f.call.object.storage->bytes,0x48)==0 && f.call.object.storage->bytes->initialized[0x30]==0,
        "Pending reference size changed captured destination or low16 truncation.");
    check(op->resume().completed && resumes==2,"Completed reference clear repeated the size callback.");
    Fixture bad;bad.call.field->offset=0x43;const auto invalid=bad.select(0x84);
    rejects([&]{invalid->resume();},"Reference clear accepted a truncated destination.");
    rejects([&]{invalid->resume();},"Faulted reference clear resumed.");
    Fixture null;const auto null_table=null.select(0x84);null.call.field->vtable_word=0;
    rejects([&]{null_table->resume();},"A represented null live vtable became a pending virtual lookup.");
    Fixture file;file.call.object.field.reset();file.call.object.file=std::make_shared<LoaderRequestFile>();
    file.call.object.file->reference_word=0x12345678;file.call.field->offset=4;
    check(file.select(0x84)->resume().completed && file.call.object.file->reference_word==0,
        "A reference clear aliasing the file header left the typed reference stale.");
}
void gates_and_null_defaults() {
    for(const auto table:{0x8047693cu,0x804762fcu,0x80476630u,0x80476088u}) {
        Fixture f;f.call.field->vtable_word=table;
        check(f.select(0xf0)->resume().value==0 && f.select(0x74)->resume().completed,"Reference gate or original mode3 leaf differs.");
        f.call.field->flag_34=0xff;check(f.select(0xf0)->resume().value==0xff,"Reference gate normalized the original byte.");
        f.call.field->flag_34.reset();const auto missing=f.select(0xf0);check(!missing->resume().completed,"Unknown construct flag became zero.");
        f.call.field->flag_34=2;check(missing->resume().value==2,"Reference gate did not reload its represented byte.");
    }
    for(const auto table:{0x804762fcu,0x80476088u}) {
        Fixture f;f.call.field->vtable_word=table;check(f.select(0x7c)->resume().completed,"Zero construct flag did not take the actual return branch.");
        f.call.field->flag_34=1;const auto recursive=f.select(0x7c);check(!recursive->resume().completed,"Unrecovered member allocation was silently skipped.");
        f.call.field->flag_34=0;check(!recursive->resume().completed,"Pending positive branch was replaced by a later zero gate.");
    }
    Fixture f;check(f.select(0x84)->resume().completed && f.select(0x70)->resume().completed,"Null object member default did not return.");
    f.call.object.storage->bytes->bytes[0x4b]=1;const auto nonnull=f.select(0x70);
    check(!nonnull->resume().completed,"Nonnull member reset was silently skipped.");
    f.call.object.storage->bytes->bytes[0x4b]=0;check(!nonnull->resume().completed,"Pending member reset lost the captured nonnull target.");
}
void object_virtuals() {
    Fixture f;f.call.kind=IgbObjectInitServiceKind::object_virtual;f.call.vtable_word=0x804762fc;
    for(const auto slot:{0x0cu,0x34u,0x24u})check(f.select(slot)->resume().completed,"Verified object-field BLR did not return.");
    auto registration=std::make_shared<IgbFieldTypeRegistrationServices>();registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
    registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=std::make_shared<IgbRegistrationMetaSlot>();
    f.services->registration=registration;
    const auto getter=f.select(0x58);check(!getter->resume().completed,"Unknown object-field metadata became null.");
    auto cell=igb_field_type_metadata_slot(registration,IgbFieldTypeKind::object);*cell=std::shared_ptr<IgbRuntimeMetaType>{};
    check(getter->resume().completed && !getter->metadata_result,"Plain metadata getter rejected a represented null.");
    auto meta=std::make_shared<IgbRuntimeMetaType>();*cell=meta;
    check(f.select(0x58)->resume().completed,"Plain metadata getter did not read its live canonical cell.");
    check(!f.select(0x44),"Unknown object virtual was fabricated.");
    igb_bootstrap_write_word({f.call.object.storage,f.call.object.offset},0,0x80473e30);
    f.call.object.field->vtable_word=0x804762fc;
    f.services->values->fields->factory=[&](const IgbFieldOperationCall& request) {
        check(request.virtual_slot==0x8c && request.vtable_word==0x80473e30 && f.call.object.field->vtable_word==0x80473e30 &&
            !request.source && request.argument==0,"Object-field tail lost its fresh physical vtable or null source.");
        auto op=std::make_shared<IgbFieldOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;
    };
    check(f.select(0x2c)->resume().completed,"Object-field tail did not select its actual buffer virtual.");
    f.call.vtable_word=0x123;check(!f.select(0x24),"Unknown object table inherited unrelated leaves.");
}
}
int main(){try{reference_clear();gates_and_null_defaults();object_virtuals();
    std::cout<<"igb_field_init_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
