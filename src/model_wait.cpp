#include "dw4/model_wait.hpp"

#include "dw4/pad.hpp"

#include <stdexcept>
#include <utility>

namespace dw4 {
void ModelWaitRuntime::require_usable() const {
    if(processing_) throw std::logic_error("Recursive model wait operation is unsupported.");
    if(faulted_) throw std::logic_error("Model wait owner has an uncertain service outcome; automatic replay is forbidden.");
}
void ModelWaitRuntime::transfer(ModelWaitRuntime&& other) {
    if(other.processing_) throw std::logic_error("Cannot move an executing model wait owner.");
    state_=other.state_; phase_=other.phase_; query_=other.query_;
    held_=other.held_; faulted_=other.faulted_;
    other.state_=nullptr; other.phase_=Phase::none; other.faulted_=false;
}
ModelWaitRuntime::ModelWaitRuntime(ModelWaitRuntime&& other) { transfer(std::move(other)); }
ModelWaitRuntime& ModelWaitRuntime::operator=(ModelWaitRuntime&& other) {
    require_usable();
    if(this==&other) return *this;
    if(pending()) throw std::logic_error("Cannot replace a pending model wait continuation.");
    transfer(std::move(other)); return *this;
}
ModelWaitResult ModelWaitRuntime::finish() noexcept {
    phase_=Phase::none; state_=nullptr; processing_=false; return {};
}
ModelWaitResult ModelWaitRuntime::update(ModelWaitState& state,const MotionChannels& channels,
    const ModelWaitServices& services) {
    require_usable();
    if(pending()) throw std::logic_error("Resume the pending model wait pass before updating.");
    if(!state.name) return {}; // 8030CFD0 caller excludes null before 8030BDB8.
    state_=&state; held_=0;
    const auto& motion=channels.states();
    const bool y=channels.allocated() && motion[1].active;
    const bool xyz=channels.allocated() && (motion[0].active || motion[1].active || motion[2].active);
    const bool rotation=channels.allocated() && motion[3].active;
    if(state.name=="TurnY" || state.name=="Landing" || state.name=="Moving" || state.name=="Rotation") {
        const bool active=state.name=="Moving" ? xyz : state.name=="Rotation" ? rotation : y;
        if(active) return finish();
        state.name.reset(); phase_=Phase::commands;
    } else if(state.name=="Action") {
        query_=Query::common; phase_=Phase::action_query;
    } else {
        // 8034D2FC..D318 samples pad BEFORE comparing any player wait name,
        // including unknown names and attack states which will not progress.
        phase_=state.control_enabled!=0 ? Phase::player_pad : Phase::player_branch;
    }
    return process(services);
}
ModelWaitResult ModelWaitRuntime::resume(ModelWaitState& state,const ModelWaitServices& services) {
    require_usable();
    if(pending() && state_!=&state) throw std::logic_error("Model wait continuation requires the same stable state owner.");
    return process(services);
}
ModelWaitResult ModelWaitRuntime::process(const ModelWaitServices& services) {
    processing_=true;
    try {
        for(;;) {
            switch(phase_) {
            case Phase::none: return finish();
            case Phase::player_pad: {
                if(!services.held_buttons) {processing_=false;return {ModelWaitStop::held_buttons_required};}
                const auto buttons=services.held_buttons();
                if(!buttons) {processing_=false;return {ModelWaitStop::held_buttons_required};}
                held_=*buttons; phase_=Phase::player_branch; break;
            }
            case Phase::player_branch:
                if(state_->name=="Atck01") {
                    if(state_->attack_state!=0 && state_->attack_state!=1) return finish();
                    if(state_->attack_state==0) {state_->attack_mode=0;state_->attack_state=1;}
                    if((held_&pad_button::a)==0) state_->attack_mode=1;
                    query_=Query::attack_one; phase_=Phase::action_query;
                } else if(state_->name=="Atck02") {
                    query_=Query::attack_two;
                    phase_=(held_&pad_button::a)==0 ? Phase::sequence_then_query : Phase::action_query;
                } else if(state_->name=="Atck03" && (held_&pad_button::a)==0) {
                    state_->name.reset(); phase_=Phase::commands;
                } else return finish();
                break;
            case Phase::action_query: {
                if(!services.action_complete) {processing_=false;return {ModelWaitStop::action_completion_required};}
                const auto complete=services.action_complete();
                if(!complete) {processing_=false;return {ModelWaitStop::action_completion_required};}
                if(!*complete) return finish();
                // The messenger or nested sequence callback may have changed
                // this work. Clear the LIVE name, then inspect the LIVE mode.
                state_->name.reset();
                if(query_==Query::attack_one) {
                    state_->attack_state=0;
                    phase_=state_->attack_mode==0 ? Phase::commands : Phase::sequence;
                } else phase_=Phase::commands;
                break;
            }
            case Phase::commands:
                if(!services.resume_commands || !services.resume_commands()) {
                    processing_=false;return {ModelWaitStop::commands_required};
                }
                return finish(); // Preserve any new wait installed by the command runner.
            case Phase::sequence_then_query:
                if(!services.dispatch_sequence || !services.dispatch_sequence("SQ_WAIT")) {
                    processing_=false;return {ModelWaitStop::sequence_required};
                }
                phase_=Phase::action_query; break; // Atck02 still queries after the nested send.
            case Phase::sequence:
                if(!services.dispatch_sequence || !services.dispatch_sequence("SQ_WAIT")) {
                    processing_=false;return {ModelWaitStop::sequence_required};
                }
                return finish();
            }
        }
    } catch(...) {
        faulted_=true; processing_=false; throw;
    }
}
} // namespace dw4
