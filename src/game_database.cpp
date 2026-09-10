#include "dw4/game_database.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/database.hpp"

namespace dw4 {
struct GameDatabaseQuery::Impl {
    std::string name;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::optional<MessengerDispatch> dispatch;
    MessengerDatabase value;
    bool complete{},fault{},executing{};
};
GameDatabaseQuery::GameDatabaseQuery(std::string name):impl_(std::make_unique<Impl>()) {
    if(name.empty() || name.size()>4096 || name.find('\0')!=std::string::npos)
        throw DatabaseError("A game database query requires its exact bounded database name.");
    impl_->name=std::move(name);
}
GameDatabaseQuery::~GameDatabaseQuery()=default;
GameDatabaseQuery::GameDatabaseQuery(GameDatabaseQuery&&) noexcept=default;
GameDatabaseQuery& GameDatabaseQuery::operator=(GameDatabaseQuery&&) noexcept=default;
bool GameDatabaseQuery::completed() const noexcept {return impl_ && impl_->complete;}
bool GameDatabaseQuery::faulted() const noexcept {return !impl_ || impl_->fault;}
const MessengerDatabase& GameDatabaseQuery::database() const {
    if(!completed())throw DatabaseError("A game database query has no completed result.");
    return impl_->value;
}
MessengerCallResult GameDatabaseQuery::resume(NativeMessenger& messenger,ActionEventRuntime& events) {
    if(!impl_ || impl_->fault)throw DatabaseError("An uncertain game database query cannot be replayed.");
    auto& state=*impl_;
    if(state.executing || (state.messenger && (state.messenger!=&messenger || state.events!=&events)))
        throw DatabaseError("A retained game database query requires the same non-recursive owners.");
    if(state.complete)return {true,1,{}};
    state.messenger=&messenger;state.events=&events;
    struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(state.executing);
    try {
        if(!state.dispatch) {
            messenger.select("DATABASE",state.name,0);
            state.dispatch.emplace(messenger.begin_send("GETDATABASE"));
        }
        const auto result=events.resume(*state.dispatch);
        if(result.stop==MessengerStop::faulted)throw DatabaseError("Original database message delivery faulted.");
        if(result.stop!=MessengerStop::completed)return {false,0,result.diagnostic};
        const auto& objects=messenger.buffer().objects();
        const auto* value=objects.empty()?nullptr:std::get_if<MessengerDatabase>(&objects[0]);
        if(!value || !*value)throw DatabaseError("Original database query requires a non-null database at current object zero.");
        state.value=*value;state.complete=true;state.dispatch.reset();
        return {true,1,{}};
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
