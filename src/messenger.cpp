#include "dw4/messenger.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t text_limit=8u*1024u*1024u;
void text_check(std::string_view value) {
    if(value.size()>65535 || value.find('\0')!=std::string_view::npos)
        throw MessengerError("Messenger strings require at most 65,535 bytes without embedded NULs.");
}
void text_check(const std::optional<std::string>& value) { if(value) text_check(std::string_view(*value)); }
void finite(float value) {
    if(!std::isfinite(value)) throw MessengerError("Messenger numeric payload must be finite.");
}
}

void MessengerBuffer::clear() noexcept {
    enabled_only_=0; entries_=bytes_=0;
    booleans_.clear(); integers_.clear(); floats_.clear(); integers64_.clear();
    vectors_.clear(); matrices_.clear(); objects_.clear(); strings_.clear();
}
void MessengerBuffer::check_append(std::size_t bytes) const {
    if(entries_>=32768 || bytes>text_limit-bytes_)
        throw MessengerError("Messenger shared argument/result storage exceeds its host bound.");
}
void MessengerBuffer::add_bool(std::uint8_t value) {
    check_append(1); booleans_.push_back(value); ++entries_; ++bytes_;
}
void MessengerBuffer::add_int(std::int32_t value) {
    check_append(4); integers_.push_back(value); ++entries_; bytes_+=4;
}
void MessengerBuffer::add_float(float value) {
    finite(value); check_append(4); floats_.push_back(value); ++entries_; bytes_+=4;
}
void MessengerBuffer::add_int64(std::int64_t value) {
    check_append(8); integers64_.push_back(value); ++entries_; bytes_+=8;
}
void MessengerBuffer::add_vector(std::array<float,3> value) {
    for(auto item:value) finite(item);
    check_append(12); vectors_.push_back(value); ++entries_; bytes_+=12;
}
void MessengerBuffer::add_matrix(std::array<float,16> value) {
    for(auto& item:value) {
        finite(item);
        const auto word=std::bit_cast<std::uint32_t>(item);
        // 80306D68 -> 80128CF4 uses GQR0 PSQ stores, including on untouched fields.
        if((word&0x7f800000u)==0) item=std::bit_cast<float>(word&0x80000000u);
    }
    check_append(64); matrices_.push_back(value); ++entries_; bytes_+=64;
}
void MessengerBuffer::add_object(std::optional<ControllerObjectId> value) {
    constexpr auto size=sizeof(value);
    check_append(size); objects_.push_back(value); ++entries_; bytes_+=size;
}
void MessengerBuffer::add_string(std::optional<std::string> value) {
    text_check(value);
    const auto size=sizeof(value)+(value ? value->size() : 0);
    check_append(size); strings_.push_back(std::move(value)); ++entries_; bytes_+=size;
}

struct NativeMessenger::Impl {
    struct Row { ControllerObjectId recipient; std::int32_t index{}; };
    struct Registration { Row row; std::string category; std::optional<std::string> name; };
    struct Cache { MessengerMessage message; std::vector<Row> rows; bool valid{}; };
    std::vector<Registration> registrations;
    std::size_t registration_text{}, handles{}, depth{};
    std::array<Cache,64> cache;
    std::array<std::size_t,64> order;
    MessengerBuffer buffer;
    std::uint64_t generation{}, next_dispatch=1;
    bool prepared{};
    Impl() { std::iota(order.begin(),order.end(),std::size_t{}); }
};
struct MessengerDispatch::Impl {
    enum class Phase { row, manager, structured, fallback, completed, faulted };
    std::weak_ptr<NativeMessenger::Impl> owner;
    std::uint64_t generation{}, id{};
    std::size_t record{}, count{}, cursor{}, completed_count{};
    std::string command, diagnostic;
    std::optional<ControllerObjectId> recipient;
    std::optional<MessengerManager> manager;
    Phase phase=Phase::row;
    bool executing{};
    ~Impl() { if(auto state=owner.lock()) --state->handles; }
};
MessengerDispatch::MessengerDispatch(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
MessengerDispatch::MessengerDispatch(MessengerDispatch&&) noexcept=default;
MessengerDispatch& MessengerDispatch::operator=(MessengerDispatch&&) noexcept=default;
MessengerDispatch::~MessengerDispatch()=default;
std::uint64_t MessengerDispatch::id() const noexcept { return impl_ ? impl_->id : 0; }
bool MessengerDispatch::completed() const noexcept { return impl_ && impl_->phase==Impl::Phase::completed; }
NativeMessenger::NativeMessenger():impl_(std::make_shared<Impl>()) {}
NativeMessenger::NativeMessenger(NativeMessenger&&) noexcept=default;
NativeMessenger& NativeMessenger::operator=(NativeMessenger&&) noexcept=default;
NativeMessenger::~NativeMessenger()=default;

void NativeMessenger::register_recipient(ControllerObjectId recipient,std::string category,
    std::optional<std::string> name,std::int32_t index) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    text_check(std::string_view(category)); text_check(name);
    const auto size=category.size()+(name ? name->size() : 0);
    if(impl_->registrations.size()>=65536 || size>text_limit-impl_->registration_text)
        throw MessengerError("Messenger registration storage exceeds its host bound.");
    impl_->registrations.push_back({{recipient,index},std::move(category),std::move(name)});
    impl_->registration_text+=size;
    // 803050A8 appends rows without invalidating any existing selected-message cache.
}
void NativeMessenger::select(std::string category,std::optional<std::string> name,std::int32_t index) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    text_check(std::string_view(category)); text_check(name);
    auto& state=*impl_;
    state.buffer.clear(); state.prepared=true;
    std::size_t position=0;
    for(;position<state.order.size();++position) {
        const auto& record=state.cache[state.order[position]];
        if(record.valid && record.message.category==category && record.message.name==name) break;
    }
    const bool hit=position<state.order.size();
    if(!hit) position=state.order.size()-1;
    const auto record_id=state.order[position];
    auto& record=state.cache[record_id];
    if(!hit) {
        record.message.category=std::move(category); record.message.name=std::move(name);
        record.rows.clear(); record.valid=true;
        // Selection replaces keys/rows, but only the sender writes command +14.
    }
    record.message.index=index;
    std::rotate(state.order.begin(),state.order.begin()+position,state.order.begin()+position+1);
    if(record.rows.empty()) {
        for(const auto& registration:state.registrations)
            if(registration.category==record.message.category
                && (!registration.name || !record.message.name || registration.name==record.message.name))
                record.rows.push_back(registration.row);
    }
}
MessengerBuffer& NativeMessenger::buffer() {
    if(!impl_ || !impl_->prepared) throw MessengerError("Messenger selection buffer has not been prepared.");
    return impl_->buffer;
}
const MessengerBuffer& NativeMessenger::buffer() const {
    if(!impl_ || !impl_->prepared) throw MessengerError("Messenger selection buffer has not been prepared.");
    return impl_->buffer;
}
const MessengerMessage& NativeMessenger::selected() const {
    if(!impl_ || !impl_->prepared || !impl_->cache[impl_->order.front()].valid)
        throw MessengerError("Messenger has no selected message.");
    return impl_->cache[impl_->order.front()].message;
}
std::size_t NativeMessenger::registration_count() const noexcept { return impl_ ? impl_->registrations.size() : 0; }
std::size_t NativeMessenger::registration_text_bytes() const noexcept { return impl_ ? impl_->registration_text : 0; }
MessengerDispatch NativeMessenger::begin_send(std::string command) {
    selected(); text_check(std::string_view(command));
    auto& state=*impl_;
    if(state.handles>=256 || state.next_dispatch==0)
        throw MessengerError("Messenger dispatch handles exceed their host bound.");
    auto frame=std::make_unique<MessengerDispatch::Impl>();
    state.cache[state.order.front()].message.command=command;
    frame->owner=impl_; ++state.handles;
    frame->generation=state.generation; frame->id=state.next_dispatch++;
    frame->record=state.order.front(); frame->count=state.cache[frame->record].rows.size();
    frame->command=std::move(command);
    return MessengerDispatch(std::move(frame));
}
MessengerDispatch NativeMessenger::begin_send_to(std::string category,std::optional<std::string> name,
    std::int32_t index,std::string command) {
    text_check(std::string_view(command));
    select(std::move(category),std::move(name),index);
    return begin_send(std::move(command));
}
MessengerResult NativeMessenger::resume(MessengerDispatch& dispatch,const MessengerServices& services) {
    using Phase=MessengerDispatch::Impl::Phase;
    if(!dispatch.impl_ || !impl_ || dispatch.impl_->owner.lock()!=impl_)
        throw MessengerError("Messenger dispatch belongs to another or expired context.");
    auto& frame=*dispatch.impl_;
    auto result=[&](MessengerStop stop,std::string diagnostic={}) {
        return MessengerResult{stop,frame.completed_count,frame.recipient,std::move(diagnostic)};
    };
    if(frame.phase==Phase::faulted) return result(MessengerStop::faulted,frame.diagnostic);
    if(frame.generation!=impl_->generation) {
        frame.phase=Phase::faulted; frame.diagnostic="Messenger reset invalidated this dispatch.";
        return result(MessengerStop::faulted,frame.diagnostic);
    }
    if(frame.phase==Phase::completed) return result(MessengerStop::completed);
    if(frame.executing) throw MessengerError("A messenger dispatch cannot recursively resume itself.");
    if(impl_->depth>=64) return result(MessengerStop::service_required,"Messenger nested delivery exceeds 64 calls.");
    auto state=impl_; // Stable during callbacks; moving/resetting an executing owner is unsupported.
    struct Guard {
        Impl& state; MessengerDispatch::Impl& frame;
        Guard(Impl& state,MessengerDispatch::Impl& frame):state(state),frame(frame) { ++state.depth; frame.executing=true; }
        ~Guard() { --state.depth; frame.executing=false; }
    } guard(*state,frame);
    auto& record=state->cache[frame.record];
    const auto row=[&](std::size_t index) {
        if(index>=record.rows.size()) throw MessengerError("Nested cache replacement invalidated an original retained row access.");
        return record.rows[index];
    };
    try {
        if(frame.command=="SEARCH") {
            std::int32_t count=0;
            for(std::size_t index=0;index<frame.count;++index) {
                const auto key=row(index).index;
                if(record.message.index==-1 || key==-1 || key==record.message.index) ++count;
            }
            state->buffer.add_bool(count!=0); state->buffer.add_int(count);
            frame.phase=Phase::completed;
            return result(MessengerStop::completed);
        }
        if(frame.command=="FIND_SPACE") {
            for(;frame.cursor<frame.count;++frame.cursor) {
                frame.recipient=row(frame.cursor).recipient;
                if(!services.active) return result(MessengerStop::service_required,"Live recipient activity service is required.");
                const auto active=services.active(*frame.recipient);
                if(!active) return result(MessengerStop::service_required,"Live recipient activity is unavailable.");
                if(*active==0) { state->buffer.add_object(frame.recipient); break; }
            }
            frame.recipient.reset(); frame.phase=Phase::completed;
            return result(MessengerStop::completed);
        }
        const auto finish_recipient=[&] {
            ++frame.completed_count; ++frame.cursor;
            frame.recipient.reset(); frame.manager.reset();
            // 80305DDC reloads the live request index after both callbacks.
            frame.phase=record.message.index==-2 ? Phase::completed : Phase::row;
        };
        for(;;) {
            if(frame.phase==Phase::completed) return result(MessengerStop::completed);
            if(frame.phase==Phase::row) {
                if(frame.cursor>=frame.count) { frame.phase=Phase::completed; continue; }
                const auto current=row(frame.cursor);
                const auto index=record.message.index;
                if(index==-2 || state->buffer.enabled_only()==1) {
                    frame.recipient=current.recipient;
                    if(!services.active) return result(MessengerStop::service_required,"Live recipient activity service is required.");
                    const auto live=services.active(current.recipient);
                    if(!live) return result(MessengerStop::service_required,"Live recipient activity is unavailable.");
                    const bool active=*live!=0;
                    if((index==-2 && active) || (index!=-2 && !active)) {
                        frame.recipient.reset(); ++frame.cursor; continue;
                    }
                }
                if(index!=-2 && index!=-1 && current.index!=-1 && index!=current.index) {
                    frame.recipient.reset(); ++frame.cursor; continue;
                }
                frame.recipient=current.recipient; frame.phase=Phase::manager;
            }
            if(frame.phase==Phase::manager) {
                if(!services.manager) return result(MessengerStop::service_required,"Recipient parent-manager service is required.");
                auto manager=services.manager(*frame.recipient);
                if(!manager.structured) return result(MessengerStop::service_required,"Recipient parent-manager resolution is unavailable.");
                frame.manager=std::move(manager); frame.phase=Phase::structured;
            }
            const MessengerDelivery delivery{frame.id,frame.cursor,*frame.recipient,record.message};
            if(frame.phase==Phase::structured) {
                if(!frame.manager->structured) return result(MessengerStop::service_required,"Captured manager structured hook is unavailable.");
                const auto called=frame.manager->structured(*this,delivery);
                if(!called.completed) return result(MessengerStop::service_required,called.diagnostic);
                if((called.value&255u)!=0) { finish_recipient(); continue; }
                frame.phase=Phase::fallback;
            }
            if(!frame.manager->fallback) return result(MessengerStop::service_required,"Captured manager command fallback is unavailable.");
            const auto called=frame.manager->fallback(*this,delivery,frame.command);
            if(!called.completed) return result(MessengerStop::service_required,called.diagnostic);
            finish_recipient();
        }
    } catch(const std::exception& error) {
        frame.phase=Phase::faulted; frame.diagnostic=error.what();
    } catch(...) {
        frame.phase=Phase::faulted; frame.diagnostic="Messenger service threw an unknown exception.";
    }
    return result(MessengerStop::faulted,frame.diagnostic);
}
void NativeMessenger::reset() {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    if(impl_->depth) throw MessengerError("Messenger cleanup during an active callback is outside the owned profile.");
    impl_->registrations.clear(); impl_->registration_text=0;
    for(auto& record:impl_->cache) { record.message={}; record.rows.clear(); record.valid=false; }
    std::iota(impl_->order.begin(),impl_->order.end(),std::size_t{});
    impl_->buffer.clear(); impl_->prepared=false; ++impl_->generation;
}
} // namespace dw4
