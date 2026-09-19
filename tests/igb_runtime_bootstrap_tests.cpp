#include "dw4/igb_runtime_bootstrap.hpp"

#include <iostream>
#include <set>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const IgbRuntimeTypesError&) {return;}throw std::runtime_error(reason);
}
std::shared_ptr<IgbRuntimeMetaType> find(const IgbRuntimeBootstrap& profile,const char* name) {
    IgbRuntimeTypeLookupInvocation call(std::make_shared<const std::string>(name));
    check(call.resume(profile.type_services()).completed,"Bootstrap lookup remained unavailable.");return call.result();
}
void inheritance_and_layout() {
    auto profile=make_original_ndmw_runtime_bootstrap();
    auto root=find(profile,"igObject"),named=find(profile,"igNamedObject"),dir=find(profile,"igDirEntry");
    auto object_dir=find(profile,"igObjectDirEntry"),memory_dir=find(profile,"igMemoryDirEntry");
    auto external=find(profile,"igExternalDirEntry"),image=find(profile,"igExternalImageEntry");
    check(root && root->major==1 && root->minor==0 && root->class_flags==7 && root->flag_1a==0,
        "Concrete metadata constructor/registration defaults changed.");
    check(igb_runtime_slot_count(root)==2 && igb_runtime_slot_count(named)==3 && igb_runtime_slot_count(dir)==7,
        "Original root/parent field registration counts changed.");
    check(igb_runtime_slot_count(object_dir)==13 && igb_runtime_slot_count(memory_dir)==14 && igb_runtime_slot_count(image)==11,
        "Original derived append/replacement counts changed.");
    const auto count=igb_runtime_field_at(root,0),pool=igb_runtime_field_at(root,1);
    check(count!=pool && count->offset==4 && pool->offset==4 && !count->flag_26 && !pool->flag_26,
        "The two distinct overlapping original root fields were merged or made serializable.");
    check(igb_runtime_field_at(named,0)==count && igb_runtime_field_at(memory_dir,0)==count,
        "Inherited fields lost their original physical identity.");
    const auto raw=igb_runtime_field_at(dir,6),reference=igb_runtime_field_at(object_dir,6),external_ref=igb_runtime_field_at(external,6);
    check(raw!=reference && reference!=external_ref && raw!=external_ref && reference->offset==24 && external_ref->offset==24,
        "Derived reference-field replacements reused the inherited allocation.");
    check(*profile.field_metadata(raw)->name=="igRawRefMetaField" && *profile.field_metadata(reference)->name=="igObjectRefMetaField",
        "Reference replacement retained the wrong concrete metadata identity.");
    check(igb_runtime_field_at(image,6)==external_ref && igb_runtime_field_at(memory_dir,6)==raw,
        "Unmodified inherited reference identities were replaced.");
    check(igb_runtime_field_at(object_dir,11)->offset==48 && igb_runtime_field_at(object_dir,12)->offset==44,
        "Nonmonotonic original directory offsets were sorted or inferred from slots.");
    const auto ref_counted=igb_runtime_field_at(memory_dir,11);
    check(ref_counted->offset==52 && ref_counted->size==1 && memory_dir->native_size==56 && memory_dir->native_alignment==4,
        "Boolean width or registered object size/alignment changed.");
    auto data=find(profile,"igDataList"),objects=find(profile,"igObjectList"),afs_list=find(profile,"igAdxAfsFileList");
    check(igb_runtime_field_at(data,4)!=igb_runtime_field_at(objects,4) && igb_runtime_field_at(objects,4)==igb_runtime_field_at(afs_list,4),
        "Object-list clone or derived list sharing changed.");
    check(igb_runtime_slot_count(data)==5 && igb_runtime_slot_count(objects)==5 && igb_runtime_slot_count(afs_list)==5,
        "Replacing list element metadata appended a spurious runtime slot.");
    auto afs=find(profile,"igAdxAfsFile"),setup=find(profile,"beNDMWAfsSetupInfo");
    check(igb_runtime_field_at(afs,4)->offset==16 && igb_runtime_field_at(afs,5)->offset==18 &&
        igb_runtime_field_at(afs,4)->size==2 && afs->native_size==20,"AFS short fields were widened to pointer slots.");
    check(igb_runtime_slot_count(setup)==7 && igb_runtime_field_at(setup,5)->offset==20 && igb_runtime_field_at(setup,6)->offset==24,
        "Actual setup-info fields were replaced by serialized own-field counts.");
    check(igb_runtime_local_field_count(setup)==7,"Represented field prefix flags changed.");
    check(!find(profile,"igVertexBlendShaderCapsInfoMetaField") && !find(profile,"UnregisteredType"),
        "An unsupported serialized class was fabricated as registered.");
}
void metadata_identity_boundaries() {
    auto profile=make_original_ndmw_runtime_bootstrap();const auto registry=profile.field_registry();
    check(registry && registry->count==47 && registry->backing && registry->backing->size()==47,
        "Recovered field registry subset changed.");
    std::set<std::string> names;
    for(const auto& field:*registry->backing) {
        const auto type=profile.field_metadata(field);
        check(type && type->name && field->owner && *type->name==field->owner->type(),"Concrete field virtual+58 returned another type.");
        check(type==find(profile,type->name->c_str()),"Field and object registries have duplicate metadata identities.");
        check(!type->fields,"Unrecovered field-class schema was fabricated as an empty completed layout.");
        check(names.insert(*type->name).second,"Field registry repeated a class instead of preserving distinct registrations.");
    }
    const auto field=registry->backing->front();const auto original=field->owner;
    field->owner=std::make_shared<ModelSceneData>(original->type());
    rejects([&]{profile.field_metadata(field);},"Same-name replacement acquired another owner's virtual binding.");
    field->owner=original;check(bool(profile.field_metadata(field)),"Restored exact field owner lost its binding.");
    auto copied=std::make_shared<IgbDecodeRuntimeField>(*field);
    rejects([&]{profile.field_metadata(copied);},"An unregistered wrapper copied a live field binding.");
    rejects([&]{profile.field_metadata({});},"Null field metadata callback succeeded.");
    auto second=make_original_ndmw_runtime_bootstrap();
    rejects([&]{second.field_metadata(field);},"Independent runtime bootstrap shared field instances.");
    check(find(profile,"igObject")!=find(second,"igObject"),"Independent runtime bootstrap reused global mutable metadata.");
    rejects([&]{igb_runtime_slot_count(find(profile,"igIntMetaField"));},"Unrecovered field-class schema produced a fabricated count.");
}
}
int main(int argc,char**) {
    try {
        if(argc!=1)throw std::runtime_error("Usage: igb_runtime_bootstrap_tests");
        inheritance_and_layout();metadata_identity_boundaries();
        std::cout<<"igb_runtime_bootstrap_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
