#include "dw4/messenger.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <type_traits>
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

void MessengerBuffer::check_mutation() const {
    if(clearing_) throw MessengerError("Messenger buffer mutation cannot be reentered during object release.");
    if(const auto suspended=suspended_.lock(); suspended && *suspended)
        throw MessengerError("The suspended deferred update owns this messenger packet.");
}
void MessengerBuffer::set_enabled_only(std::uint8_t value) { check_mutation(); enabled_only_=value; }
void MessengerBuffer::clear() {
    check_mutation();
    struct Guard { bool& clearing; explicit Guard(bool& value):clearing(value) { clearing=true; } ~Guard() { clearing=false; } } guard(clearing_);
    enabled_only_=0; entries_=bytes_=0;
    booleans_.clear(); integers_.clear(); floats_.clear(); integers64_.clear();
    vectors_.clear(); matrices_.clear(); objects_.clear(); strings_.clear();
}
void MessengerBuffer::check_append(std::size_t bytes) const {
    check_mutation();
    if(entries_>=32768 || bytes>text_limit-bytes_)
        throw MessengerError("Messenger shared argument/result storage exceeds its host bound.");
}
void MessengerBuffer::clear_integers() {
    check_mutation();
    entries_-=integers_.size(); bytes_-=integers_.size()*4;
    integers_.clear();
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
    if(value && !value->valid()) throw MessengerError("Controller object mixes native and serialized identity fields.");
    constexpr auto size=sizeof(MessengerObject);
    check_append(size);
    if(value) objects_.emplace_back(*value); else objects_.emplace_back(std::monostate{});
    ++entries_; bytes_+=size;
}
void MessengerBuffer::add_box(MessengerBox value) {
    if(!value) throw MessengerError("A retained AABox object must not be null; append a null object explicitly.");
    for(const auto number:value->minimum) finite(number);
    for(const auto number:value->maximum) finite(number);
    constexpr auto size=sizeof(MessengerObject)+sizeof(CollisionBox);
    check_append(size); objects_.emplace_back(std::move(value)); ++entries_; bytes_+=size;
}
void MessengerBuffer::add_data_list(MessengerDataList value) {
    if(!value) throw MessengerError("A retained game-data list must not be null; append a null object explicitly.");
    constexpr auto size=sizeof(MessengerObject);
    check_append(size);objects_.emplace_back(std::move(value));++entries_;bytes_+=size;
}
void MessengerBuffer::add_database(MessengerDatabase value) {
    if(!value) throw MessengerError("A retained database must not be null; append a null object explicitly.");
    constexpr auto size=sizeof(MessengerObject);
    check_append(size);objects_.emplace_back(std::move(value));++entries_;bytes_+=size;
}
void MessengerBuffer::add_game_object(GameDataObject value) {
    std::visit([&](const auto& object) {
        using T=std::decay_t<decltype(object)>;
        if constexpr(std::is_same_v<T,std::monostate>)add_object({});
        else if constexpr(std::is_same_v<T,ControllerObjectId>)add_object(object);
        else if constexpr(std::is_same_v<T,MessengerBox>)add_box(object);
        else if constexpr(std::is_same_v<T,MessengerDataList>)add_data_list(object);
        else if constexpr(std::is_same_v<T,MessengerDatabase>)add_database(object);
        else {
            if(!object)throw MessengerError("A retained typed game object must not be null.");
            constexpr auto size=sizeof(MessengerObject);
            check_append(size);objects_.emplace_back(object);++entries_;bytes_+=size;
        }
    },value);
}
void MessengerBuffer::add_string(std::optional<std::string> value) {
    text_check(value);
    const auto size=sizeof(value)+(value ? value->size() : 0);
    check_append(size); strings_.push_back(std::move(value)); ++entries_; bytes_+=size;
}

namespace {
struct DeferredRecord {
    bool active{}, keys_prepared{};
    std::uint64_t deadline{};
    MessengerMessage message;
    std::shared_ptr<MessengerBuffer> arguments=std::make_shared<MessengerBuffer>();
};
bool deadline_before(std::uint64_t left,std::uint64_t right) {
    return std::bit_cast<std::int64_t>(left)<std::bit_cast<std::int64_t>(right);
}
}
struct MessengerDeferredUpdate::Impl {
    std::weak_ptr<NativeMessenger::Impl> owner;
    std::uint64_t generation{}, id{}, time{};
    std::size_t budget{}, iterations{}, messages{}, recipients{};
    std::shared_ptr<DeferredRecord> record;
    std::optional<MessengerDispatch> dispatch;
    bool executing{}, done{}, faulted{}, paused{};
    std::string diagnostic;
};

struct NativeMessenger::Impl {
    struct Row { ControllerObjectId recipient; std::int32_t index{}; };
    struct Registration { Row row; std::string category; std::optional<std::string> name; };
    struct Cache { MessengerMessage message; std::vector<Row> rows; bool valid{}; };
    std::vector<Registration> registrations;
    std::size_t registration_text{}, handles{}, depth{};
    std::array<Cache,64> cache;
    std::array<std::size_t,64> order;
    std::array<std::shared_ptr<MessengerBuffer>,5> buffers;
    std::shared_ptr<bool> packets_suspended;
    std::vector<std::shared_ptr<DeferredRecord>> deferred;
    std::shared_ptr<MessengerDeferredUpdate::Impl> update;
    std::uint64_t pause_anchor{};
    std::array<std::size_t,4> buffer_pool{1,2,3,4};
    std::size_t current_buffer{}, buffer_depth{};
    bool changing_buffer{};
    std::uint64_t generation{}, next_dispatch=1;
    bool prepared{};
    Impl() { std::iota(order.begin(),order.end(),std::size_t{}); }
    MessengerBuffer& buffer() { return *buffers[current_buffer]; }
    void prepare() {
        if(prepared) return;
        auto suspension=std::make_shared<bool>(false);
        std::array<std::shared_ptr<MessengerBuffer>,5> packets;
        for(auto& packet:packets) {
            packet=std::make_shared<MessengerBuffer>(); packet->suspended_=suspension;
        }
        std::vector<std::shared_ptr<DeferredRecord>> records;
        records.reserve(32);
        for(std::size_t index=0;index<32;++index) {
            auto record=std::make_shared<DeferredRecord>(); record->arguments->suspended_=suspension;
            records.push_back(std::move(record));
        }
        packets_suspended=std::move(suspension); buffers=std::move(packets); deferred=std::move(records); prepared=true;
    }
    void check_buffer_change() const {
        if(changing_buffer) throw MessengerError("Messenger buffer mutation cannot be reentered during object release.");
        for(const auto& buffer:buffers) if(buffer && buffer->clearing_)
            throw MessengerError("Messenger buffer mutation cannot be reentered during object release.");
        for(const auto& record:deferred) if(record->arguments->clearing_)
            throw MessengerError("Messenger buffer mutation cannot be reentered during object release.");
    }
    void check_deferred_mutation() const {
        check_buffer_change();
        if(update && !update->executing)
            throw MessengerError("The unfinished deferred update owns the current messenger packet until resume or reset.");
    }
    struct BufferChange {
        Impl& state;
        explicit BufferChange(Impl& state):state(state) { state.check_buffer_change(); state.changing_buffer=true; }
        ~BufferChange() { state.changing_buffer=false; }
    };
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
MessengerDeferredUpdate::MessengerDeferredUpdate(std::shared_ptr<Impl> impl):impl_(std::move(impl)) {}
MessengerDeferredUpdate::MessengerDeferredUpdate(MessengerDeferredUpdate&&) noexcept=default;
MessengerDeferredUpdate& MessengerDeferredUpdate::operator=(MessengerDeferredUpdate&&) noexcept=default;
MessengerDeferredUpdate::~MessengerDeferredUpdate()=default;
std::uint64_t MessengerDeferredUpdate::id() const noexcept { return impl_ ? impl_->id : 0; }
std::size_t MessengerDeferredUpdate::messages_completed() const noexcept { return impl_ ? impl_->messages : 0; }
bool MessengerDeferredUpdate::completed() const noexcept { return impl_ && impl_->done; }
NativeMessenger::NativeMessenger():impl_(std::make_shared<Impl>()) {}
NativeMessenger::NativeMessenger(NativeMessenger&&) noexcept=default;
NativeMessenger& NativeMessenger::operator=(NativeMessenger&&) noexcept=default;
NativeMessenger::~NativeMessenger()=default;

void NativeMessenger::register_recipient(ControllerObjectId recipient,std::string category,
    std::optional<std::string> name,std::int32_t index) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    if(!recipient.valid()) throw MessengerError("Controller recipient mixes native and serialized identity fields.");
    impl_->check_buffer_change();
    text_check(std::string_view(category)); text_check(name);
    const auto size=category.size()+(name ? name->size() : 0);
    if(impl_->registrations.size()>=65536 || size>text_limit-impl_->registration_text)
        throw MessengerError("Messenger registration storage exceeds its host bound.");
    impl_->registrations.push_back({{recipient,index},std::move(category),std::move(name)});
    impl_->registration_text+=size;
    // 803050A8 appends rows without invalidating any existing selected-message cache.
}
void NativeMessenger::register_recipients(std::span<const MessengerRegistration> registrations) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    impl_->check_buffer_change();
    if(registrations.size()>65536-impl_->registrations.size())
        throw MessengerError("Messenger registration rows exceed their host bound.");
    std::size_t added_text{};
    std::vector<Impl::Registration> pending;pending.reserve(registrations.size());
    for(const auto& row:registrations) {
        if(!row.recipient.valid()) throw MessengerError("Controller recipient mixes native and serialized identity fields.");
        text_check(std::string_view(row.category));text_check(row.name);
        const auto size=row.category.size()+(row.name ? row.name->size() : 0);
        if(size>text_limit-impl_->registration_text-added_text)
            throw MessengerError("Messenger registration text exceeds its host bound.");
        pending.push_back({{row.recipient,row.index},row.category,row.name});added_text+=size;
    }
    // All validation and allocation precede publication. Moving these owned
    // rows cannot invoke user callbacks or invalidate existing live cache rows.
    impl_->registrations.reserve(impl_->registrations.size()+pending.size());
    for(auto& row:pending)impl_->registrations.push_back(std::move(row));
    impl_->registration_text+=added_text;
}
void NativeMessenger::select(std::string category,std::optional<std::string> name,std::int32_t index) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    impl_->check_deferred_mutation();
    text_check(std::string_view(category)); text_check(name);
    auto& state=*impl_;
    state.prepare();
    { Impl::BufferChange guard(state); state.buffer().clear(); }
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
    impl_->check_deferred_mutation();
    return impl_->buffer();
}
const MessengerBuffer& NativeMessenger::buffer() const {
    if(!impl_ || !impl_->prepared) throw MessengerError("Messenger selection buffer has not been prepared.");
    impl_->check_buffer_change();
    return impl_->buffer();
}
std::shared_ptr<const MessengerBuffer> NativeMessenger::buffer_owner() const {
    buffer();
    return impl_->buffers[impl_->current_buffer];
}
void NativeMessenger::push_buffer() {
    buffer();
    auto& state=*impl_;
    if(state.buffer_depth==state.buffer_pool.size())
        throw MessengerError("Messenger buffer stack exceeds four nested pushes.");
    const auto previous=state.current_buffer;
    state.current_buffer=state.buffer_pool.back();
    std::move_backward(state.buffer_pool.begin(),state.buffer_pool.end()-1,state.buffer_pool.end());
    state.buffer_pool.front()=previous; ++state.buffer_depth;
}
void NativeMessenger::pop_buffer() {
    buffer();
    auto& state=*impl_;
    if(!state.buffer_depth) throw MessengerError("Messenger buffer stack cannot pop below zero.");
    Impl::BufferChange guard(state);
    state.buffer().clear();
    const auto previous=state.current_buffer;
    state.current_buffer=state.buffer_pool.front();
    std::move(state.buffer_pool.begin()+1,state.buffer_pool.end(),state.buffer_pool.begin());
    state.buffer_pool.back()=previous; --state.buffer_depth;
}
std::size_t NativeMessenger::buffer_depth() const noexcept { return impl_ ? impl_->buffer_depth : 0; }
const MessengerMessage& NativeMessenger::selected() const {
    if(!impl_ || !impl_->prepared || !impl_->cache[impl_->order.front()].valid)
        throw MessengerError("Messenger has no selected message.");
    impl_->check_buffer_change();
    return impl_->cache[impl_->order.front()].message;
}
std::size_t NativeMessenger::registration_count() const noexcept { return impl_ ? impl_->registrations.size() : 0; }
bool NativeMessenger::has_category(std::string_view category) const noexcept {
    return impl_ && std::any_of(impl_->registrations.begin(),impl_->registrations.end(),
        [&](const auto& registration) { return registration.category==category; });
}
std::size_t NativeMessenger::registration_text_bytes() const noexcept { return impl_ ? impl_->registration_text : 0; }
MessengerDispatch NativeMessenger::begin_send(std::string command) {
    selected(); text_check(std::string_view(command));
    impl_->check_deferred_mutation();
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
    impl_->check_deferred_mutation();
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
            state->buffer().add_bool(count!=0); state->buffer().add_int(count);
            frame.phase=Phase::completed;
            return result(MessengerStop::completed);
        }
        if(frame.command=="FIND_SPACE") {
            for(;frame.cursor<frame.count;++frame.cursor) {
                frame.recipient=row(frame.cursor).recipient;
                if(!services.active) return result(MessengerStop::service_required,"Live recipient activity service is required.");
                const auto active=services.active(*frame.recipient);
                if(!active) return result(MessengerStop::service_required,"Live recipient activity is unavailable.");
                if(*active==0) { state->buffer().add_object(frame.recipient); break; }
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
                if(index==-2 || state->buffer().enabled_only()==1) {
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
void NativeMessenger::prepare_deferred(std::string category,std::optional<std::string> name,std::int32_t index) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    impl_->check_deferred_mutation();
    text_check(std::string_view(category)); text_check(name);
    auto& state=*impl_;
    state.prepare();
    { Impl::BufferChange guard(state); state.buffer().clear(); }
    if(state.deferred.back()->active) {
        if(state.deferred.size()>=65536)
            throw MessengerError("Deferred messenger records exceed their 65,536-record host bound.");
        auto record=std::make_shared<DeferredRecord>(); record->arguments->suspended_=state.packets_suspended;
        state.deferred.push_back(std::move(record));
    }
    auto& tail=*state.deferred.back();
    tail.message.category=std::move(category); tail.message.name=std::move(name); tail.message.index=index;
    tail.keys_prepared=true;
}
void NativeMessenger::schedule_deferred(std::string command,std::uint64_t delay,std::uint64_t context_time) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    impl_->check_deferred_mutation(); text_check(std::string_view(command));
    auto& state=*impl_;
    if(!state.prepared || state.deferred.empty() || state.deferred.back()->active || !state.deferred.back()->keys_prepared)
        throw MessengerError("Deferred commit requires a prepared inactive tail record.");
    const auto deadline=context_time+delay*std::uint64_t{16666666};
    if(state.update && state.update->record && deadline_before(deadline,state.update->record->deadline))
        throw MessengerError("Deferred insertion before the currently delivered live head is outside the safe owned profile.");
    auto tail=state.deferred.back();
    tail->deadline=deadline;
    std::swap(state.buffers[state.current_buffer],tail->arguments);
    tail->message.command=std::move(command); tail->active=true; tail->keys_prepared=false;
    state.deferred.pop_back();
    const auto position=std::find_if(state.deferred.begin(),state.deferred.end(),[&](const auto& row) {
        return !row->active || deadline_before(deadline,row->deadline);
    });
    // Removal leaves capacity for this insertion; shared owner moves cannot throw.
    state.deferred.insert(position,std::move(tail));
}
void NativeMessenger::defer_to(std::string category,std::optional<std::string> name,std::int32_t index,
    std::string command,std::uint64_t delay,std::uint64_t context_time) {
    text_check(std::string_view(command));
    prepare_deferred(std::move(category),std::move(name),index);
    schedule_deferred(std::move(command),delay,context_time);
}
void NativeMessenger::set_deferred_pause(std::uint32_t raw_value,std::uint64_t context_time) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    impl_->check_deferred_mutation();
    impl_->pause_anchor=(raw_value&255u)!=0 ? context_time : 0;
}
std::uint64_t NativeMessenger::deferred_pause_anchor() const noexcept { return impl_ ? impl_->pause_anchor : 0; }
std::vector<MessengerDeferredRecord> NativeMessenger::deferred_snapshot() const {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    impl_->check_buffer_change();
    std::vector<MessengerDeferredRecord> result;
    result.reserve(impl_->deferred.size());
    for(const auto& record:impl_->deferred)
        result.push_back({record->active,record->deadline,record->message,record->arguments});
    return result;
}
MessengerDeferredUpdate NativeMessenger::begin_deferred_update(std::uint64_t context_time) {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    auto& state=*impl_;
    state.check_buffer_change();
    if(state.update || state.depth)
        throw MessengerError("A deferred messenger update cannot overlap or recursively enter another delivery.");
    if(!state.next_dispatch) throw MessengerError("Messenger update identifiers exceed their host bound.");
    auto frame=std::make_shared<MessengerDeferredUpdate::Impl>();
    frame->owner=impl_; frame->generation=state.generation; frame->id=state.next_dispatch++;
    frame->time=context_time; frame->budget=state.deferred.size(); frame->paused=state.pause_anchor!=0;
    frame->done=!state.prepared || state.deferred.empty();
    if(!frame->done) { state.update=frame; *state.packets_suspended=true; }
    return MessengerDeferredUpdate(std::move(frame));
}
MessengerResult NativeMessenger::resume(MessengerDeferredUpdate& update,const MessengerServices& services) {
    if(!update.impl_ || !impl_ || update.impl_->owner.lock()!=impl_)
        throw MessengerError("Deferred update belongs to another or expired messenger context.");
    auto frame=update.impl_;
    auto result=[&](MessengerStop stop,std::string diagnostic={}) {
        return MessengerResult{stop,frame->recipients,{},std::move(diagnostic)};
    };
    impl_->check_buffer_change();
    if(frame->generation!=impl_->generation) {
        frame->faulted=true; frame->done=false; frame->diagnostic="Messenger reset invalidated this deferred update.";
    }
    if(frame->faulted) return result(MessengerStop::faulted,frame->diagnostic);
    if(frame->done) return result(MessengerStop::completed);
    if(frame->executing || impl_->depth || impl_->update!=frame)
        throw MessengerError("Deferred update requires its exclusive original manager invocation.");
    auto state=impl_;
    struct Guard {
        bool& executing;
        Impl& state;
        Guard(bool& value,Impl& owner):executing(value),state(owner) { executing=true; *state.packets_suspended=false; }
        ~Guard() { executing=false; *state.packets_suspended=state.update!=nullptr; }
    } guard(frame->executing,*state);
    try {
        if(frame->paused) {
            const auto delta=frame->time-state->pause_anchor;
            for(auto& row:state->deferred) {
                if(!row->active) break;
                row->deadline+=delta;
            }
            state->pause_anchor=frame->time;
        } else {
            while(frame->iterations<frame->budget) {
                if(!frame->record) {
                    const auto& head=state->deferred.front();
                    if(!head->active || deadline_before(frame->time,head->deadline)) break;
                    frame->record=head;
                    select(head->message.category,head->message.name,head->message.index);
                    std::swap(state->buffers[state->current_buffer],head->arguments);
                    if(!head->message.command) throw MessengerError("An active deferred record has no command.");
                    frame->dispatch.emplace(begin_send(*head->message.command));
                }
                auto sent=resume(*frame->dispatch,services);
                if(sent.stop!=MessengerStop::completed) {
                    sent.recipients_completed+=frame->recipients;
                    if(sent.stop==MessengerStop::faulted) {
                        frame->faulted=true; frame->diagnostic=sent.diagnostic;
                    }
                    return sent;
                }
                frame->recipients+=sent.recipients_completed;
                if(state->deferred.front()!=frame->record)
                    throw MessengerError("Nested deferred mutation displaced the original retained live head.");
                auto record=frame->record;
                record->active=false; record->keys_prepared=false; record->deadline=0; record->message={};
                { Impl::BufferChange clearing(*state); record->arguments->clear(); }
                state->deferred.erase(state->deferred.begin());
                state->deferred.push_back(std::move(record));
                frame->dispatch.reset(); frame->record.reset(); ++frame->iterations; ++frame->messages;
            }
        }
        { Impl::BufferChange clearing(*state); state->buffer().clear(); }
        frame->done=true; state->update.reset();
        return result(MessengerStop::completed);
    } catch(const std::exception& error) {
        frame->faulted=true; frame->diagnostic=error.what();
    } catch(...) {
        frame->faulted=true; frame->diagnostic="Deferred messenger service threw an unknown exception.";
    }
    return result(MessengerStop::faulted,frame->diagnostic);
}
void NativeMessenger::reset() {
    if(!impl_) throw MessengerError("Moved-from messenger has no context.");
    impl_->check_buffer_change();
    if(impl_->depth || (impl_->update && impl_->update->executing))
        throw MessengerError("Messenger cleanup during an active callback is outside the owned profile.");
    Impl::BufferChange guard(*impl_);
    if(impl_->packets_suspended) *impl_->packets_suspended=false;
    // Release owners in the original cleanup order. A caller-retained packet
    // survives with its contents; cleanup does not mutate a separately owned one.
    impl_->buffers[impl_->current_buffer].reset();
    impl_->registrations.clear(); impl_->registration_text=0;
    for(auto& record:impl_->cache) { record.message={}; record.rows.clear(); record.valid=false; }
    std::iota(impl_->order.begin(),impl_->order.end(),std::size_t{});
    impl_->deferred.clear(); impl_->update.reset();
    for(auto& buffer:impl_->buffers) buffer.reset();
    impl_->packets_suspended.reset();
    impl_->current_buffer=impl_->buffer_depth=0; impl_->buffer_pool={1,2,3,4};
    impl_->pause_anchor=0; impl_->prepared=false; ++impl_->generation;
}
} // namespace dw4
