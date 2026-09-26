#include "dw4/igb_field_init.hpp"
#include "dw4/igb_file_factory.hpp"

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
    f.call.vtable_word=0x80475b60;
    for(const auto slot:{0x0cu,0x24u,0x2cu,0x34u})check(f.select(slot)->resume().completed,"Original integer-field object BLR was not composed.");
    auto int_cell=std::make_shared<IgbRegistrationMetaSlot>();registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::signed_int)]=int_cell;
    auto int_getter=f.select(0x58);check(!int_getter->resume().completed,"Unknown integer metadata reused the object-field global.");
    *int_cell=std::shared_ptr<IgbRuntimeMetaType>{};check(int_getter->resume().completed && !int_getter->metadata_result,"Integer getter registered a represented null class.");
    *int_cell=meta;auto reloaded=f.select(0x58);check(reloaded->resume().completed && reloaded->metadata_result==meta,"Integer getter failed fresh canonical805619E0.");
    f.call.vtable_word=0x80476088;
    for(const auto slot:{0x0cu,0x24u,0x2cu,0x34u})check(f.select(slot)->resume().completed,"Original memory-field object BLR was not composed.");
    auto memory_cell=std::make_shared<IgbRegistrationMetaSlot>();registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::memory_reference)]=memory_cell;
    auto memory_getter=f.select(0x58);check(!memory_getter->resume().completed,"Unknown memory metadata reused another field global.");
    *memory_cell=std::shared_ptr<IgbRuntimeMetaType>{};check(memory_getter->resume().completed && !memory_getter->metadata_result,"Memory getter registered a represented null class.");
    auto memory_meta=std::make_shared<IgbRuntimeMetaType>();*memory_cell=memory_meta;
    auto memory_reloaded=f.select(0x58);check(memory_reloaded->resume().completed && memory_reloaded->metadata_result==memory_meta,
        "Memory getter failed fresh canonical80561818.");
    f.call.vtable_word=0x80473e30;
    for(const auto slot:{0x0cu,0x24u,0x2cu,0x34u})check(f.select(slot)->resume().completed,"Original boolean-field object BLR was not composed.");
    auto boolean_cell=std::make_shared<IgbRegistrationMetaSlot>();registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::boolean)]=boolean_cell;
    auto boolean_getter=f.select(0x58);check(!boolean_getter->resume().completed,"Unknown boolean metadata reused another field global.");
    *boolean_cell=std::shared_ptr<IgbRuntimeMetaType>{};check(boolean_getter->resume().completed && !boolean_getter->metadata_result,"Boolean getter registered a represented null class.");
    auto boolean_meta=std::make_shared<IgbRuntimeMetaType>();*boolean_cell=boolean_meta;
    auto boolean_reloaded=f.select(0x58);check(boolean_reloaded->resume().completed && boolean_reloaded->metadata_result==boolean_meta,
        "Boolean getter failed fresh canonical80561E48.");
}

void string_and_byte_virtuals(){
    for(const auto kind:{IgbMetaFieldKind::string,IgbMetaFieldKind::signed_byte,IgbMetaFieldKind::enumeration,IgbMetaFieldKind::pointer,IgbMetaFieldKind::unsigned_byte,IgbMetaFieldKind::unsigned_int,IgbMetaFieldKind::unsigned_short}){
        Fixture f;const bool string=kind==IgbMetaFieldKind::string;
        f.call.kind=IgbObjectInitServiceKind::object_virtual;f.call.vtable_word=igb_meta_field_profile(kind).vtable;
        f.services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
        f.services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        auto cell=std::make_shared<IgbRegistrationMetaSlot>();
        if(kind==IgbMetaFieldKind::unsigned_int){f.services->registration->meta_fields->metadata=cell;
            f.services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(kind)]=
                std::make_shared<IgbRegistrationMetaSlot>(std::make_shared<IgbRuntimeMetaType>());}
        else f.services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(kind)]=cell;
        for(const auto slot:{0x0cu,0x24u,0x34u})check(f.select(slot)->resume().completed,"Scalar/string verified object leaf did not return.");
        if(!string)check(f.select(0x2c)->resume().completed,"Scalar tail invented default-buffer work.");
        auto getter=f.select(0x58);check(!getter->resume().completed,"Unknown scalar/string metadata became a ready object.");
        *cell=std::shared_ptr<IgbRuntimeMetaType>{};check(getter->resume().completed && !getter->metadata_result,"Plain scalar/string getter registered a null class.");
        auto type=std::make_shared<IgbRuntimeMetaType>();*cell=type;
        check(getter->resume().completed && getter->metadata_result==type,"Scalar/string getter failed its fresh global read.");
    }
    Fixture f;f.call.kind=IgbObjectInitServiceKind::object_virtual;f.call.vtable_word=0x8047693c;
    igb_bootstrap_write_word({f.call.object.storage,f.call.object.offset},0,0x8047693c);
    unsigned selections{},polls{};bool ready{};
    f.services->values->fields->factory=[&](const IgbFieldOperationCall& request){
        ++selections;check(request.vtable_word==0x8047693c && request.virtual_slot==0x8c && !request.source,
            "String tail did not select its actual null-input buffer virtual.");
        auto op=std::make_shared<IgbFieldOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,{}};};return op;
    };
    auto tail=f.select(0x2c);check(!tail->resume().completed && selections==1 && polls==1,"String tail skipped its retained field virtual.");
    igb_bootstrap_write_word({f.call.object.storage,f.call.object.offset},0,0x80473e30);
    f.services->values->fields->factory={};ready=true;
    check(tail->resume().completed && selections==1 && polls==2 && tail->resume().completed && polls==2,
        "Pending string tail reselected its target or repeated a completed callback.");
    Fixture invalid;invalid.call.kind=IgbObjectInitServiceKind::object_virtual;invalid.call.vtable_word=0x8047693c;
    rejects([&]{invalid.select(0x2c)->resume();},"String tail ignored uninitialized physical vtable bytes.");
    for(const auto table:{0x8047693cu,0x804762fcu}){
        Fixture null;null.call.kind=IgbObjectInitServiceKind::object_virtual;null.call.vtable_word=table;
        igb_bootstrap_write_word({null.call.object.storage,null.call.object.offset},0,0);
        unsigned callbacks{};null.services->values->fields->factory=[&](const IgbFieldOperationCall&){++callbacks;return std::shared_ptr<IgbFieldOperation>{};};
        auto invocation=null.select(0x2c);
        rejects([&]{invocation->resume();},"Descriptor tail called a virtual through a represented null physical table.");
        check(callbacks==0,"Null physical table allowed a later descriptor callback.");
        rejects([&]{invocation->resume();},"Faulted null-table descriptor tail resumed.");
    }
}

void scalar_reflected_initialization(IgbMetaFieldKind kind){
    const auto table=igb_meta_field_profile(kind).vtable;
    Fixture f;auto type=std::make_shared<IgbRuntimeMetaType>(),base=std::make_shared<IgbRuntimeMetaType>();
    base->instance_count=0;f.call.object.storage->bytes->bytes.resize(0x44);f.call.object.storage->bytes->initialized.resize(0x44);
    type->type_word=table;type->native_size=0x34;type->allocation_prefix=0x10;type->factory_redirect=0;
    type->class_flags=4;type->flag_1a=0;type->instance_count=0;type->instances=std::shared_ptr<IgbObjectInstanceList>{};
    type->fields=std::make_shared<IgbDecodeFieldRegistry>();type->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    base->fields=std::make_shared<IgbDecodeFieldRegistry>();
    // Prepared inherited igMetaField descriptors isolate the original reflected
    // allocation and all three member passes on the supplied raw typed allocation.
    const auto add=[&](std::uint32_t table,std::uint32_t offset,unsigned size,std::uint32_t value){
        auto member=std::make_shared<IgbDecodeRuntimeField>();member->vtable_word=table;member->offset=offset;
        member->size=static_cast<std::uint16_t>(size);member->default_mode=0;
        auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.resize(size);bytes->initialized.assign(size,1);
        for(unsigned i=0;i<size;++i)bytes->bytes[i]=static_cast<std::uint8_t>(value>>(8*(size-i-1)));
        member->default_bytes=IgbRuntimeFieldDefaultSource{bytes,0};type->fields->backing->push_back(member);++type->fields->count;
    };
    add(0x80476e6c,8,4,0x12345678);add(0x80477054,0x14,2,4);add(0x80473f24,0x24,1,2);
    f.services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
    auto fields=std::make_shared<IgbMetaFieldFactoryServices>();f.services->registration->meta_fields=fields;
    auto canonical=std::make_shared<IgbRegistrationMetaSlot>();
    fields->metadata=kind==IgbMetaFieldKind::unsigned_int?canonical:std::make_shared<IgbRegistrationMetaSlot>(base);
    fields->metadata_slots[static_cast<std::size_t>(kind)]=kind==IgbMetaFieldKind::unsigned_int?
        std::make_shared<IgbRegistrationMetaSlot>(base):canonical;
    auto reflected=std::make_shared<IgbFileFactoryServices>();reflected->initializer=std::make_shared<IgbObjectInitServices>();
    auto init=reflected->initializer;init->runtime=std::make_shared<IgbRuntimeTypesServices>();
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->enabled=1;core->vtable_offset=0;core->field_initialization_enabled=1;
    init->runtime->singleton=core;init->object_metadata=base;
    unsigned allocations{},queries{};
    init->factory=make_native_igb_scalar_field_factory(make_native_igb_field_init_factory(f.services,[&](const IgbObjectInitCall& c){
        check(c.kind==IgbObjectInitServiceKind::allocation_pool && c.address==0x100000,
            "Scalar initializer changed the allocation-base lookup or invented a virtual.");
        auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[&]{++queries;return MessengerCallResult{true,0,{}};};return op;
    }));
    auto pool=std::make_shared<ModelSceneData>();
    reflected->factory=[&](const IgbFileFactoryCall& c){
        check(c.kind==IgbFileFactoryServiceKind::allocate && c.metadata==type && c.pool==pool && c.bytes==0x44,
            "Scalar reflected factory changed its original allocation arguments.");
        auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[&,out=op.get()]{++allocations;
            out->allocation_result=f.call.object.storage;return MessengerCallResult{true,0,{}};};return op;
    };
    IgbReflectedFactoryInvocation run(type,pool);
    check(!run.resume(reflected).completed && allocations==1 && queries==0 &&
        igb_buffer_word(f.call.object.storage->bytes,0x10)==table && f.call.object.field->reference_word==1 &&
        f.call.object.storage->bytes->initialized[0x18]==0,
        "Unknown canonical scalar metadata skipped header writes or executed later field work.");
    *canonical=type;
    check(run.resume(reflected).completed && allocations==1 && queries==1,"Scalar reflected initialization did not resume its retained allocation.");
    const auto result=run.result_instance();
    check(result.field==f.call.object.field && result.storage==f.call.object.storage && result.offset==0x10 &&
        result.field->vtable_word==table && result.field->offset==0x12345678 && result.field->size==4 && result.field->default_mode==2,
        "Scalar inherited descriptor defaults lost physical or canonical publication.");
    check(igb_buffer_word(result.storage->bytes,0x18)==0x12345678 && result.storage->bytes->bytes[0x25]==4 &&
        result.storage->bytes->bytes[0x34]==2 && result.storage->bytes->initialized[0x26]==0 &&
        result.storage->bytes->initialized[0xf]==0 && type->instance_count==1 && base->instance_count==0,
        "Scalar initializer changed padding, prefix, field bytes or final canonical instance count.");
    check(run.resume(reflected).completed && allocations==1 && queries==1 && type->instance_count==1,
        "Completed scalar initializer replayed allocation or fields.");
    f.call.kind=IgbObjectInitServiceKind::object_virtual;f.call.vtable_word=table;
    for(const auto slot:{0x08u,0x38u,0x40u,0x54u})check(!f.select(slot),"Scalar initializer claimed an unrecovered destructor or clone surface.");
}
}
int main(){try{reference_clear();gates_and_null_defaults();object_virtuals();string_and_byte_virtuals();scalar_reflected_initialization(IgbMetaFieldKind::unsigned_int);
    scalar_reflected_initialization(IgbMetaFieldKind::unsigned_short);
    std::cout<<"igb_field_init_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
