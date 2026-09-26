#include "dw4/igb_field_reference.hpp"
#include "dw4/igb_bootstrap_object.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldReferenceError(message);}
}
struct IgbFieldReferenceAccess::Impl {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> physical;
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t allocation_address{};
    bool fault{};
    void validate()const{
        if(fault)fail("Packed field reference requires an unfaulted captured access.");
        if(!field)fail("Packed field reference dereferences a null descriptor.");
        if(field->construction_fault_)fail("Packed field reference reached a faulted descriptor.");
        if(!physical){
            if(field->physical_storage)fail("Packed field reference acquired a physical binding after canonical capture.");
            return;
        }
        if(!physical->storage || !field->physical_storage || physical->storage->faulted() ||
            field->physical_storage->storage.lock()!=physical->storage || field->physical_storage->offset!=physical->offset ||
            physical->storage->original_address!=allocation_address || physical->storage->bytes!=bytes || !bytes || bytes->released)
            fail("Packed field reference lost its captured physical allocation identity.");
        // Bootstrap synchronization reads the committed word through the shared
        // scratch-buffer contract. Reject an invalid owner before any store.
        if(bytes->initialized.size()!=bytes->bytes.size())
            fail("Packed field reference has an inconsistent physical knownness extent.");
        IgbObjectInstance object;object.storage=physical->storage;object.offset=physical->offset;object.field=field;
        igb_object_validate_instance(object);
    }
};
IgbFieldReferenceAccess::IgbFieldReferenceAccess(std::shared_ptr<IgbDecodeRuntimeField> field):impl_(std::make_unique<Impl>()){
    auto& s=*impl_;s.field=std::move(field);
    if(s.field && s.field->physical_storage){const auto& binding=*s.field->physical_storage;
        s.physical=IgbBootstrapPointer{binding.storage.lock(),binding.offset};
        if(s.physical->storage){s.bytes=s.physical->storage->bytes;s.allocation_address=s.physical->storage->original_address;}
    }
}
IgbFieldReferenceAccess::~IgbFieldReferenceAccess()=default;
bool IgbFieldReferenceAccess::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbFieldReferenceAccess::read(){
    auto& s=*impl_;try{s.validate();return s.physical?igb_bootstrap_read_word(*s.physical,4):s.field->reference_word;}
    catch(...){s.fault=true;throw;}
}
void IgbFieldReferenceAccess::write(std::uint32_t value){
    auto& s=*impl_;try{s.validate();if(s.physical)igb_bootstrap_write_word(*s.physical,4,value);else s.field->reference_word=value;}
    catch(...){s.fault=true;throw;}
}
} // namespace dw4
