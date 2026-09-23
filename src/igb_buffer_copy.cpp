#include "dw4/igb_buffer_copy.hpp"
#include "dw4/igb_stream.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw IgbBufferCopyError(message); }
MessengerCallResult pending(const char* message) { return {false, 0, message}; }
struct Running {
    bool& value;
    explicit Running(bool& flag) : value(flag) { value = true; }
    ~Running() { value = false; }
};
// Resolving a pointer does not read its bytes or revive a released allocation.
bool resolve(IgbRuntimeFieldDefaultSource& pointer, std::uint32_t address,
             const std::shared_ptr<IgbBufferServices>& services) {
    if (pointer.buffer) return true;
    if (!address) fail("Buffer copy dereferences a null pointer.");
    if (!services || !services->values) return false;
    bool found = false;
    for (const auto& binding : services->values->buffers) {
        if (!binding.buffer) continue;
        const auto offset = address - binding.base_address;
        if (std::uint64_t(offset) >= binding.buffer->bytes.size()) continue;
        if (found && (pointer.buffer != binding.buffer || pointer.offset != offset))
            fail("Buffer copy has conflicting arithmetic allocation bindings.");
        pointer = {binding.buffer, offset};
        found = true;
    }
    return found;
}
std::size_t reached_byte(const IgbRuntimeFieldDefaultSource& pointer,
                         std::uint32_t address, std::uint32_t relative) {
    if (address + relative == 0) fail("Buffer copy dereferences a null byte address.");
    const auto at = std::uint64_t(pointer.offset) + relative;
    if (!pointer.buffer || pointer.buffer->released || at >= pointer.buffer->bytes.size() ||
        at >= pointer.buffer->initialized.size())
        fail("Buffer copy accesses unavailable allocation bytes.");
    return static_cast<std::size_t>(at);
}
} // namespace

struct IgbBufferCopyInvocation::Impl {
    enum class Phase {
        entry, owner_for_allocation, first_size_owner, size_virtual,
        allocate, second_size_owner, free_virtual, copy_read, copy_write, call, done
    };
    IgbBufferFunction function;
    IgbRuntimeFieldDefaultSource source, destination;
    std::uint32_t source_address{}, destination_address{}, size{}, copied{}, result{};
    std::uint8_t byte{};
    bool backwards{}, second_size{}, running{}, fault{};
    std::shared_ptr<IgbBufferServices> owner;
    std::shared_ptr<ModelSceneObject> allocation_pool, current_pool;
    IgbBufferCall request;
    IgbBufferFactory factory;
    std::shared_ptr<IgbBufferOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase{Phase::entry}, after{Phase::done};
};

IgbBufferCopyInvocation::IgbBufferCopyInvocation(
    IgbBufferFunction function, IgbRuntimeFieldDefaultSource source, std::uint32_t address)
    : impl_(std::make_unique<Impl>()) {
    impl_->function = function;
    impl_->source = std::move(source);
    impl_->source_address = address;
}
IgbBufferCopyInvocation::~IgbBufferCopyInvocation() {
    auto& s = *impl_;
    if ((!completed() || faulted()) && s.owner) {
        const auto retain = [&](const auto& value) { if (value) s.owner->unfinished.push_back(value); };
        retain(s.source.buffer);
        retain(s.destination.buffer);
        retain(s.allocation_pool);
        retain(s.current_pool);
        retain(s.operation);
    }
}
bool IgbBufferCopyInvocation::completed() const noexcept { return impl_->phase == Impl::Phase::done; }
bool IgbBufferCopyInvocation::faulted() const noexcept { return impl_->fault; }
IgbRuntimeFieldDefaultSource IgbBufferCopyInvocation::buffer_result() const {
    if (!completed() || faulted() || impl_->function != IgbBufferFunction::copy)
        fail("Incomplete buffer copy has no allocation result.");
    return impl_->destination;
}
std::uint32_t IgbBufferCopyInvocation::buffer_address() const {
    if (!completed() || faulted() || impl_->function != IgbBufferFunction::copy)
        fail("Incomplete buffer copy has no allocation address.");
    return impl_->destination_address;
}
MessengerCallResult IgbBufferCopyInvocation::resume(std::shared_ptr<IgbBufferServices> services) {
    auto& s = *impl_;
    using P = Impl::Phase;
    using K = IgbBufferServiceKind;
    if (s.running || s.fault) {
        s.fault = true;
        fail("Buffer copy requires an unfaulted nonrecursive invocation.");
    }
    if (completed()) return {true, s.result, {}};
    Running running(s.running);
    if (!s.owner && services) s.owner = services;
    if (!services) services = s.owner;
    const auto clear_call = [&] {
        s.callback = {};
        s.operation.reset();
        s.factory = {};
        s.request = {};
        if (s.fault) fail("Buffer helper operation teardown invalidated its invocation.");
    };
    const auto select = [&](K kind, P after, std::uint32_t slot = 0) {
        clear_call();
        s.request = {};
        s.request.kind = kind;
        s.request.virtual_slot = slot;
        if (slot == 0xd8) {
            // This virtual receives only the byte count, not the source pointer.
            s.request.pool = s.allocation_pool;
            s.request.bytes = s.size;
        } else {
            s.request.pool = kind == K::pool_virtual ? s.current_pool : nullptr;
            s.request.buffer = s.source;
            s.request.address = s.source_address;
        }
        if (kind == K::pool_virtual && !s.request.pool)
            fail("Buffer helper dereferences a null owner pool.");
        s.after = after;
        s.phase = P::call;
    };
    try {
        for (;;) {
            switch (s.phase) {
            case P::entry:
                switch (s.function) {
                case IgbBufferFunction::size: select(K::allocation_pool, P::size_virtual); break;
                case IgbBufferFunction::free: select(K::allocation_pool, P::free_virtual); break;
                case IgbBufferFunction::copy: select(K::allocation_pool, P::owner_for_allocation); break;
                default: fail("Unknown original buffer helper.");
                }
                break;
            case P::owner_for_allocation:
                s.allocation_pool = s.operation->pool_result;
                clear_call();
                s.phase = P::first_size_owner;
                break;
            case P::first_size_owner:
                select(K::allocation_pool, P::size_virtual);
                break;
            case P::size_virtual:
                s.current_pool = s.operation->pool_result;
                select(K::pool_virtual, s.function == IgbBufferFunction::size ? P::done :
                       s.second_size ? P::copy_read : P::allocate, 0x150);
                break;
            case P::allocate:
                s.size = s.result;
                select(K::pool_virtual, P::second_size_owner, 0xd8);
                break;
            case P::second_size_owner:
                s.destination = s.operation->buffer_result;
                s.destination_address = s.operation->buffer_address;
                s.second_size = true;
                select(K::allocation_pool, P::size_virtual);
                break;
            case P::free_virtual:
                s.current_pool = s.operation->pool_result;
                select(K::pool_virtual, P::done, 0x108);
                break;
            case P::call:
                if (!s.factory && services) s.factory = services->factory;
                if (!s.factory) return pending("Buffer helper requires its selected native pool service.");
                if (!s.operation) s.operation = s.factory(s.request);
                if (s.fault) fail("Buffer helper factory invalidated its invocation.");
                if (!s.operation) return pending("Buffer helper reached an unbound native pool operation.");
                if (!s.callback) s.callback = s.operation->resume;
                if (!s.callback) return pending("Buffer helper requires its retained pool continuation.");
                {
                    const auto status = s.callback();
                    if (s.fault) fail("Buffer helper continuation invalidated its invocation.");
                    if (!status.completed) return status;
                    s.result = status.value;
                }
                if (s.after == P::copy_read) {
                    s.size = s.result;
                    s.backwards = s.source_address < s.destination_address;
                    clear_call();
                }
                s.phase = s.after;
                break;
            case P::copy_read:
                if (s.copied == s.size) {
                    s.result = s.destination_address;
                    s.phase = P::done;
                    break;
                }
                if (!resolve(s.source, s.source_address, services))
                    return pending("Buffer copy requires its captured source allocation binding.");
                {
                    const auto relative = s.backwards ? s.size - 1u - s.copied : s.copied;
                    const auto at = reached_byte(s.source, s.source_address, relative);
                    if (!s.source.buffer->initialized[at]) fail("Buffer copy reads an unknown source byte.");
                    s.byte = s.source.buffer->bytes[at];
                }
                s.phase = P::copy_write;
                break;
            case P::copy_write:
                if (!resolve(s.destination, s.destination_address, services))
                    return pending("Buffer copy requires its captured destination allocation binding.");
                {
                    const auto relative = s.backwards ? s.size - 1u - s.copied : s.copied;
                    const auto at = reached_byte(s.destination, s.destination_address, relative);
                    s.destination.buffer->bytes[at] = s.byte;
                    s.destination.buffer->initialized[at] = 1;
                }
                ++s.copied;
                s.phase = P::copy_read;
                break;
            case P::done:
                clear_call();
                return {true, s.result, {}};
            }
        }
    } catch (...) {
        s.fault = true;
        throw;
    }
}
} // namespace dw4
