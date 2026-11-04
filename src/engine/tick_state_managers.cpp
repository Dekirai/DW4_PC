// The tick state's manager sections (docs/SEAMLESS.md 6.13.2, round J): each
// manager's Manager::tick_state (its mutable fields; pointers into its own
// per-info data as indexes, scene nodes and RAMs as ids), owned_nodes (the
// nodes it made itself) and tick_rams (its RAMs in a fixed order). What a
// manager builds at its stage's load and never changes (infos, sequences,
// tables, RAM lists, rows) is topology: the reader has it already.
#include "dw4/engine/collision.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/tick_state.hpp"

#include <algorithm>

namespace dw4::engine {

void serialize(StateArchive& a, ItemDevice& v); // save_state.cpp
void serialize_cri(StateArchive& a, CriStreams& cri); // managers_core.cpp

void serialize(StateArchive& a, NodeTimer& v) {
    a.io(v.scale);
    a.io(v.bias);
    a.io(v.end);
    a.io(v.saved_world);
    a.io(v.cached_frames);
    a.io(v.previous);
}
void serialize(StateArchive& a, FrameCamera& v) {
    a.io(v.view);
    a.io(v.projection);
}
void serialize(StateArchive& a, StateClock& v) {
    a.io(v.speed);
    a.io(v.base);
}
void serialize(StateArchive& a, MotionState& v) {
    a.io(v.clip);
    a.io(v.clock);
    a.io(v.fresh);
    a.io(v.blend);
    a.io(v.blend_start);
    a.io(v.previous_clip);
    a.io(v.previous_clock);
    a.io(v.previous_fresh);
    a.io(v.previous_pose);
    a.io(v.hidden);
}

} // namespace dw4::engine

namespace dw4 {
void serialize(engine::StateArchive& a, MotionTimer& v) {
    a.io(v.time_scale);
    a.io(v.time_bias);
    a.io(v.end_time);
    a.io(v.saved_world_time);
    a.io(v.previous_time);
    a.io(v.cached_frames);
}
void serialize(engine::StateArchive& a, MotionState& v) {
    a.io(v.active);
    a.io(v.end_time);
    a.io(v.mode);
    a.io(v.value);
    a.io(v.acceleration);
    a.io(v.speed);
    a.io(v.stop_on_sequence);
    a.io(v.stop_value);
    a.io(v.stop_frames);
}
} // namespace dw4

namespace dw4::engine {

namespace {

template <class R> void ram_list(std::vector<const Ram*>& out, const std::vector<std::unique_ptr<R>>& rams) {
    for (const auto& r : rams) out.push_back(r.get());
}

// The base of every RAM: the active flag (the rest is the info's).
void ram_base(TickArchive& a, Ram& r) {
    a.same(r.info_name, "RAM info");
    a.io(r.active);
}

template <class R> void base_rams(TickArchive& a, std::vector<std::unique_ptr<R>>& rams) {
    a.same(std::uint32_t(rams.size()), "RAM count");
    for (auto& r : rams) ram_base(a, *r);
}

// An index into a vector for a pointer to one of its elements (-1 null).
template <class T> std::int32_t index_of(const std::vector<T>* list, const T* item) {
    if (!item || !list) return -1;
    for (std::size_t i = 0; i < list->size(); ++i)
        if (&(*list)[i] == item) return std::int32_t(i);
    throw TickStateError("ids", "a pointer outside its list");
}
template <class T> void element(TickArchive& a, const std::vector<T>* list, const T*& item) {
    std::int32_t index = a.reading() ? -1 : index_of(list, item);
    a.io(index);
    if (a.reading()) {
        if (index < 0 || !list) item = nullptr;
        else if (std::size_t(index) < list->size()) item = &(*list)[std::size_t(index)];
        else throw TickStateError("ids", "an index beyond its list");
    }
}

void motion_channels(TickArchive& a, dw4::MotionChannels& channels) {
    auto list = channels.list_owner();
    bool has = list != nullptr;
    a.io(has);
    if (!has) return;
    if (a.reading() && !list) throw TickStateError("topology", "motion channels without a list");
    auto& entries = list->mutable_entries();
    std::uint32_t count = std::uint32_t(entries.size());
    a.io(count);
    if (a.reading()) {
        if (count > 4096) throw TickStateError("models", "implausible motion channels");
        entries.clear();
        for (std::uint32_t i = 0; i < count; ++i) entries.push_back(std::make_shared<dw4::MotionState>());
    }
    for (auto& e : entries) a.io(*e);
}

void actor_pose(TickArchive& a, ActorPose& pose, const Skeleton* skeleton) {
    bool has = pose.skeleton != nullptr;
    a.io(has);
    if (a.reading()) pose.skeleton = has ? skeleton : nullptr;
    a.io(pose.bone_world);
    a.io(pose.bind_pose);
}

template <class T> void optional_struct(TickArchive& a, std::optional<T>& value) {
    bool has = value.has_value();
    a.io(has);
    if (a.reading()) {
        if (has && !value) value.emplace();
        if (!has) value.reset();
    }
}

} // namespace

// ---- CollisionWorld
void CollisionWorld::tick_state(TickArchive& a) {
    std::uint32_t count = std::uint32_t(groups_.size());
    a.io(count);
    if (a.reading()) {
        if (count > 1u << 20) throw TickStateError("collision", "implausible groups");
        groups_.assign(count, true);
    }
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        bool on = groups_[i];
        a.io(on);
        if (a.reading()) groups_[i] = on;
    }
    a.same(std::uint32_t(dynamic_.size()), "collision dynamics");
    for (auto& d : dynamic_) {
        a.node(d.node);
        a.node(d.mask_switch);
        a.io(d.bit);
        a.io(d.grouped);
        a.io(d.group);
    }
    if (a.reading()) {
        forget_answers();
        still_stale_ = true;
        for (auto& d : dynamic_) d.space_valid = false;
    }
}

// ---- LAYER
void LayerManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    a.io(groups_);
    a.io(ordered_);
}
void LayerManager::owned_nodes(std::vector<Node*>& out) const {
    if (layer_switch_) out.push_back(layer_switch_.get());
    for (const auto& n : group_nodes_) out.push_back(n.get());
}
void LayerManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- BKCOLOR
void BkColorManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    a.io(color_);
}
void BkColorManager::owned_nodes(std::vector<Node*>&) const {}
void BkColorManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- CAMERA
void CameraManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "camera RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.same(r->box.has_value(), "camera box data");
        if (r->box) {
            a.node(r->box->pivot);
            a.nodes(r->box->eyes);
            a.nodes(r->box->targets);
        }
        optional_struct(a, r->shake);
        if (r->shake) {
            auto& s = *r->shake;
            a.io(s.pattern);
            a.io(s.amplitude);
            a.io(s.step);
            a.io(s.total);
            a.io(s.index);
            a.io(s.fresh);
            a.io(s.eye);
            a.io(s.target);
            a.io(s.one);
            a.io(s.all);
        }
        a.io(r->demo_data_duration);
        a.io(r->state);
        a.io(r->named_path);
        a.io(r->named_start);
        a.io(r->named_duration);
        a.io(r->named_active);
        a.io(r->hokan);
    }
    optional_struct(a, viewer_);
    if (viewer_) {
        // The viewer's RAM by index, its path by index in that RAM's paths
        // (-2: the RAM's demo data path).
        std::int32_t ram = -1, path = -1;
        if (!a.reading()) {
            for (std::size_t i = 0; i < rams_.size(); ++i)
                if (rams_[i].get() == viewer_->ram) ram = std::int32_t(i);
            if (viewer_->ram && ram < 0) throw TickStateError("ids", "the camera viewer's RAM");
            if (ram >= 0) {
                const auto& r = *rams_[std::size_t(ram)];
                for (std::size_t p = 0; p < r.paths.size(); ++p)
                    if (&r.paths[p] == viewer_->path) path = std::int32_t(p);
                if (r.demo_data && &*r.demo_data == viewer_->path) path = -2;
                if (viewer_->path && path == -1) throw TickStateError("ids", "the camera viewer's path");
            }
        }
        a.io(ram);
        a.io(path);
        if (a.reading()) {
            viewer_->ram = ram >= 0 && std::size_t(ram) < rams_.size() ? rams_[std::size_t(ram)].get() : nullptr;
            viewer_->path = nullptr;
            if (viewer_->ram) {
                const auto& r = *viewer_->ram;
                if (path == -2 && r.demo_data) viewer_->path = &*r.demo_data;
                else if (path >= 0 && std::size_t(path) < r.paths.size()) viewer_->path = &r.paths[std::size_t(path)];
            }
        }
        a.io(viewer_->time);
        a.io(viewer_->duration);
        a.io(viewer_->last);
        a.io(viewer_->finished);
    }
    a.io(centre_);
    a.io(centre_moved_);
    a.io(blend_);
    a.io(box_);
    a.io(previous_box_);
    a.io(saved_eye_);
    a.io(saved_target_);
    a.io(frame_);
    for (auto& b : player_box_) {
        a.io(b.started);
        a.io(b.centre);
        a.io(b.centre_moved);
        a.io(b.blend);
        a.io(b.box);
        a.io(b.previous_box);
        a.io(b.saved_eye);
        a.io(b.saved_target);
        a.io(b.pivot);
        a.io(b.eye);
        a.io(b.target);
        a.io(b.hokan);
        a.io(b.shake_eye);
        a.io(b.shake_target);
        a.io(b.box_event);
    }
    a.io(viewer_events_);
    a.io(demo_event_);
    a.io(player_frame_);
}
void CameraManager::owned_nodes(std::vector<Node*>&) const {}
void CameraManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- STAGECTL
void StageCtlManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    a.io(reset_state_);
    a.io(reset_time_);
}
void StageCtlManager::owned_nodes(std::vector<Node*>&) const {}
void StageCtlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- SWITCHCTRL
void SwitchCtrlManager::tick_state(TickArchive& a) { base_rams(a, rams_); }
void SwitchCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void SwitchCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- SOUND (the game's side; the SNDF driver follows in the sound lane)
void SoundManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    for (auto& s : streams_) {
        a.io(s.name);
        a.io(s.file);
        a.io(s.serial);
        a.io(s.priority);
        a.io(s.type);
        a.io(s.volume);
        a.io(s.owner);
        a.io(s.out_volume);
        a.io(s.fade.running);
        a.io(s.fade.from);
        a.io(s.fade.to);
        a.io(s.fade.position);
        a.io(s.fade.increment);
    }
    a.io(serial_);
    a.io(pause_next_);
    a.io(se_recent_);
    a.io(common_loaded_);
    for (auto& e : sfp_) {
        a.io(e.serial);
        a.io(e.owner);
        a.io(e.type);
        a.io(e.priority);
        a.io(e.name);
        a.io(e.seq);
        a.io(e.request);
        a.io(e.seen);
        a.io(e.volume);
        a.io(e.fade.running);
        a.io(e.fade.from);
        a.io(e.fade.to);
        a.io(e.fade.position);
        a.io(e.fade.increment);
    }
    a.io(driver_frame_);
    a.io(random_);
    a.io(current_bgm_);
    a.io(se_volume_);
    a.io(base_volume_);
    a.io(fx_due_);
    a.io(fx_type_a_);
    a.io(fx_type_b_);
    a.io(fx_bank_);
    a.io(fx_preset_);
    a.io(fx_bank_select_);
    a.io(aux_a_callback_);
    // (audio_delta_ and the mirror's recorded master and reserve are the
    // audio output's: each machine keeps its own.)
    a.io(last_update_ticks_);
    a.io(updated_);
    serialize_cri(a, cri_);
    // The SNDF as the game sees it, its banks by their place in the list.
    // (A bank by its file name and kind: the session's common banks may
    // stand elsewhere in another engine's list.)
    auto bank_io = [&](std::shared_ptr<const sndf::BankData>& bank) {
        std::string name;
        std::uint8_t kind = 0; // 0 none, 1 stage, 2 common
        if (!a.reading() && bank) {
            for (const auto& b : banks_)
                if (b && b->data == bank) {
                    name = b->name;
                    kind = b->common ? 2 : 1;
                }
            if (!kind) throw TickStateError("sound", "a driver bank outside the bank list");
        }
        a.io(kind);
        if (kind) a.io(name);
        if (a.reading()) {
            bank = nullptr;
            for (const auto& b : banks_)
                if (b && b->name == name && (b->common ? 2 : 1) == kind) bank = b->data;
            if (kind && !bank) throw TickStateError("topology", "the sound bank " + name + " is not loaded");
        }
    };
    auto bank_bytes = [](const std::shared_ptr<const sndf::BankData>& bank) {
        return bank && bank->bank ? bank->bank->bytes() : std::span<const std::uint8_t>{};
    };
    driver_.tick_io(a, bank_io, bank_bytes);
}
void SoundManager::owned_nodes(std::vector<Node*>&) const {}
void SoundManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- TEXTURECTRL
void TextureCtrlManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    a.io(pending_);
}
void TextureCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void TextureCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- PARTICLECTRL
void ParticleCtrlManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "particle RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.io(r->running);
        a.io(r->state);
        a.io(r->start);
        a.node(r->transform);
        a.node(r->timer);
        a.io(r->owner);
        a.io(r->owner_index);
        a.io(r->offset);
    }
}
void ParticleCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void ParticleCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- WATERPLAIN (the surface's simulation; its geometry is presentation)
void WaterPlainManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(waters_.size()), "waters");
    for (auto& w : waters_) {
        a.io(w.c_sum);
        a.io(w.c_cur);
        a.io(w.buffers);
        a.io(w.current);
        a.io(w.period);
        a.io(w.move_start);
        a.io(w.move_index);
        std::uint32_t drops = std::uint32_t(w.drops.size());
        a.io(drops);
        if (a.reading()) {
            if (drops > 1u << 16) throw TickStateError("water", "implausible drops");
            w.drops.resize(drops);
        }
        for (auto& d : w.drops) {
            a.io(d.time);
            a.io(d.strength);
            a.io(d.points);
        }
        a.io(w.state);
        a.io(w.last_update);
        a.io(w.searched);
    }
}
void WaterPlainManager::owned_nodes(std::vector<Node*>&) const {}
void WaterPlainManager::tick_rams(std::vector<const Ram*>&) const {}

// ---- LIGHTCTRL (its colours are the attributes section's)
void LightCtrlManager::tick_state(TickArchive& a) { base_rams(a, rams_); }
void LightCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void LightCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- SHADOW
void ShadowManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(casters_.size()), "shadow casters");
    a.io(shader_);
    a.io(factor_);
    a.io(width_);
    a.io(height_);
    a.io(top_);
    a.io(bottom_);
    a.io(right_);
    a.io(left_);
    a.io(near_);
    a.io(far_);
    a.io(up_);
    a.io(direction_);
    a.io(light_);
    a.io(player_lights_);
    a.io(player_cameras_);
}
void ShadowManager::owned_nodes(std::vector<Node*>&) const {}
void ShadowManager::tick_rams(std::vector<const Ram*>&) const {}

// ---- PLAYERPOINT
void PlayerPointManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    a.io(point_no_);
}
void PlayerPointManager::owned_nodes(std::vector<Node*>&) const {}
void PlayerPointManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- HITLANDMODEL
void HitLandManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    a.same(std::uint32_t(deliveries_.size()), "deliveries");
    for (auto& r : deliveries_) {
        ram_base(a, *r);
        a.node(r->node);
        a.io(r->flags);
        a.io(r->width);
        a.io(r->up);
        a.io(r->down);
        a.io(r->position);
        a.io(r->previous);
        a.io(r->land);
        a.io(r->land_valid);
    }
    a.same(std::uint32_t(children_.size()), "hit children");
    for (auto& c : children_) {
        ram_base(a, *c);
        a.io(c->group);
    }
    a.same(std::uint32_t(followers_.size()), "followers");
    for (auto& f : followers_) {
        a.io(f.id);
        a.io(f.bit);
        a.io(f.resolved);
    }
    world_.tick_state(a);
}
void HitLandManager::owned_nodes(std::vector<Node*>&) const {}
void HitLandManager::tick_rams(std::vector<const Ram*>& out) const {
    ram_list(out, rams_);
    ram_list(out, deliveries_);
    ram_list(out, children_);
}

// ---- CHANGEPOS
void ChangePosManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "changepos RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.node(r->transform);
        a.io(r->state);
        a.io(r->last);
    }
}
void ChangePosManager::owned_nodes(std::vector<Node*>&) const {}
void ChangePosManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- TEXTCTL (the lines; their texts follow in the presentation lane)
void TextCtlManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    a.same(std::uint32_t(lines_.size()), "text lines");
    for (auto& l : lines_) {
        a.node(l.node);
        a.io(l.max_width);
        a.io(l.spacing);
        a.io(l.align);
        a.io(l.style);
        a.io(l.slot);
        // The shown text (raw form only): laid out again on the reader, for
        // the joiner's picture.
        a.shown(l.text);
        if (a.reading() && l.text) set_text(a.engine(), l, *l.text);
    }
}
void TextCtlManager::owned_nodes(std::vector<Node*>&) const {}
void TextCtlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- SELECTCTRL
void SelectCtrlManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "select RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.io(r->running);
        a.io(r->trigger);
        a.io(r->state);
        a.io(r->sub);
        a.io(r->counter);
        a.io(r->busy);
        a.io(r->cursor);
        a.io(r->items);
        a.io(r->vertical);
        a.io(r->cancel);
        a.io(r->position);
        a.io(r->enabled);
        a.io(r->sequence);
    }
}
void SelectCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void SelectCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- NUMBERCTRL
void NumberCtrlManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "number RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.node(r->geometry);
        a.io(r->step);
        a.io(r->maximum);
        a.io(r->blank_leading);
        a.io(r->places);
        a.io(r->shown);
        a.io(r->target);
        a.io(r->rate);
        a.io(r->drawn);
        // (The digits on the reader's picture: presentation only.)
        if (a.reading() && r->drawn) draw(a.engine(), *r, r->shown);
    }
}
void NumberCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void NumberCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- METERCTRL
void MeterCtrlManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "meter RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.node(r->node);
        a.io(r->maximum);
        a.io(r->target);
        a.io(r->shown);
        a.io(r->low_time);
        a.io(r->high_time);
        a.io(r->continuous);
        a.io(r->dirty);
        a.io(r->integer);
        a.io(r->model_name);
        a.io(r->model_index);
    }
}
void MeterCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void MeterCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- GENERATER
void GeneraterManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "generator RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.io(r->started);
        a.io(r->done);
        a.io(r->state);
        a.io(r->work);
        a.io(r->freeze);
        a.io(r->system_freeze);
        a.io(r->player_count);
        a.io(r->remaining);
        a.io(r->enemies);
        a.io(r->point);
        a.io(r->enemy_name);
        a.io(r->enemy_record);
        a.io(r->enemy_max_hp);
        a.io(r->boot_timer);
        a.io(r->gene_timer);
        a.io(r->clears);
        a.io(r->enemy_models);
        a.io(r->shuffled);
        a.io(r->wave_model);
        a.io(r->draws);
        std::uint32_t watch = std::uint32_t(r->watch.size());
        a.io(watch);
        if (a.reading()) {
            if (watch > 4096) throw TickStateError("generators", "implausible watch list");
            r->watch.resize(watch);
        }
        for (auto& w : r->watch) {
            a.io(w.spawn_y);
            a.io(w.airborne);
        }
    }
    a.io(shuffle_ready_);
    a.io(pool_);
}
void GeneraterManager::owned_nodes(std::vector<Node*>&) const {}
void GeneraterManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- MATCTRL
void MatCtrlManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "mat RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.io(r->running);
        a.io(r->initialized);
        a.node(r->visuals);
        a.same(std::uint32_t(r->mats.size()), "mats");
        for (auto& m : r->mats) {
            a.io(m.enabled);
            a.io(m.current);
            a.io(m.previous);
            a.io(m.exit);
        }
    }
    if (context_) throw TickStateError("mats", "a mat context between steps");
    a.io(pending_run_);
    a.io(live_);
}
void MatCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void MatCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- PASSIVE
void PassiveManager::tick_state(TickArchive& a) { base_rams(a, rams_); }
void PassiveManager::owned_nodes(std::vector<Node*>&) const {}
void PassiveManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- MODELCTRL
namespace {
void hitboxes(TickArchive& a, std::vector<ModelCtrlManager::ModelRam::Hitbox>& boxes) {
    std::uint32_t count = std::uint32_t(boxes.size());
    a.io(count);
    if (a.reading()) {
        if (count > 4096) throw TickStateError("models", "implausible hitboxes");
        boxes.resize(count);
    }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        auto& b = boxes[i];
        a.io(b.kind);
        a.io(b.delay);
        a.io(b.end);
        a.io(b.elapsed);
        a.io(b.offset);
        a.io(b.size);
        a.io(b.started);
        a.io(b.tag);
        a.io(b.aimed);
        a.io(b.yaw);
        a.io(b.shift);
        a.io(b.repeat);
        a.io(b.max_targets);
        a.io(b.struck);
        // The shared struck list (one Art strike): the first box that holds
        // it, -1 none, -2 this box (its contents follow).
        std::int32_t share = -1;
        if (!a.reading() && b.shared_struck) {
            share = -2;
            for (std::size_t j = 0; j < i; ++j)
                if (boxes[j].shared_struck == b.shared_struck) {
                    share = std::int32_t(j);
                    break;
                }
        }
        a.io(share);
        if (share == -2) {
            if (a.reading()) b.shared_struck = std::make_shared<std::vector<std::pair<std::string, int>>>();
            a.io(*b.shared_struck);
        } else if (a.reading()) {
            b.shared_struck = share >= 0 && std::size_t(share) < i ? boxes[std::size_t(share)].shared_struck : nullptr;
        }
    }
}
} // namespace

void ModelCtrlManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "model RAMs");
    for (auto& rp : rams_) {
        auto& r = *rp;
        ram_base(a, r);
        a.io(r.category);
        a.node(r.node);
        a.io(r.generated);
        a.io(r.seq_name);
        element(a, static_cast<const std::vector<Sequence>*>(r.sequences), r.seq);
        a.io(r.cursor);
        a.io(r.label_passed);
        a.io(r.sequence_serial);
        a.io(r.wait_name);
        a.io(r.hp);
        a.io(r.max_hp);
        a.io(r.base_matrix);
        motion_channels(a, r.channels);
        a.io(r.ctrl_mode);
        a.io(r.ctrl_phase);
        a.io(r.pdmove);
        a.io(r.pdmove_set);
        a.io(r.fall_check);
        a.io(r.wall_check);
        a.io(r.floor_check);
        optional_struct(a, r.ride);
        if (r.ride) {
            auto& d = *r.ride;
            a.io(d.kind);
            a.io(d.type);
            a.io(d.name);
            a.node(d.node);
            a.io(d.own);
            a.io(d.last);
            a.io(d.point);
            a.io(d.velocity);
            a.io(d.speed_far);
            a.io(d.speed_near);
            a.io(d.radius_near);
            a.io(d.radius_far);
            a.io(d.rate);
            a.io(d.rest);
            a.io(d.state);
        }
        a.io(r.applied);
        a.io(r.desired);
        a.io(r.turn_rate_y);
        a.io(r.turn_rate_x);
        a.io(r.aim_height);
        a.io(r.is_player);
        a.io(r.control_enabled);
        a.io(r.key_input);
        a.io(r.on_ground);
        a.io(r.item_table);
        a.io(r.item_record);
        a.io(r.item_device);
        a.io(r.item_label);
        a.io(r.item_lit);
        a.io(r.item_color_timer);
        a.io(r.label);
        a.io(r.labels);
        hitboxes(a, r.hitboxes);
        hitboxes(a, r.port_hitboxes);
        std::uint32_t effects = std::uint32_t(r.effects.size());
        a.io(effects);
        if (a.reading()) {
            if (effects > 4096) throw TickStateError("models", "implausible effects");
            r.effects.resize(effects);
        }
        for (auto& e : r.effects) {
            a.io(e.model);
            a.io(e.sequence);
            a.io(e.offset);
            a.io(e.rotation);
            a.io(e.delay);
            a.io(e.elapsed);
            a.io(e.started);
            a.io(e.sequence_dead);
            a.io(e.lock);
            a.io(e.ram);
        }
        std::uint32_t sounds = std::uint32_t(r.sounds.size());
        a.io(sounds);
        if (a.reading()) {
            if (sounds > 4096) throw TickStateError("models", "implausible sounds");
            r.sounds.resize(sounds);
        }
        for (auto& s : r.sounds) {
            a.io(s.name);
            a.io(s.delay);
            a.io(s.elapsed);
            a.io(s.started);
            a.io(s.id);
        }
        a.io(r.attach_name);
        a.io(r.attach_index);
        a.io(r.attach_offset);
        a.io(r.attach_rotation);
        a.node(r.attach_node);
        a.io(r.pos_lock);
        a.io(r.translated);
        a.io(r.owner_name);
        a.io(r.owner_index);
        a.io(r.hitbox_generation);
        a.io(r.muteki);
        a.io(r.keika);
        a.io(r.record);
        a.io(r.attack_delay);
        a.io(r.combo_phase);
        a.io(r.combo_released);
        a.io(r.panel_open);
        a.io(r.frozen);
        a.io(r.lod_high_saved);
        a.io(r.previous_live);
        a.io(r.lua_bhit);
        a.io(r.lua_whit);
        a.io(r.lua_fhit);
        a.io(r.spin);
        a.io(r.slam_state);
        a.io(r.slam_direction);
        a.io(r.slam_timer);
        a.io(r.capsule_on);
        a.io(r.capsule_age);
        a.io(r.ai_map);
        a.io(r.ai_timer);
        a.io(r.ai_request);
        a.io(r.ai_world_target);
        a.io(r.homing_state);
        a.io(r.homing_steps);
        a.io(r.follow_model);
        a.io(r.follow_bone);
        a.node(r.trace);
        a.node(r.follow_trace);
        optional_struct(a, r.status);
        if (r.status) {
            auto& s = *r.status;
            a.io(s.icon);
            a.io(s.place);
            a.io(s.timer);
            a.io(s.start);
            a.io(s.poison_damage);
            a.io(s.gedoku);
            a.io(s.down);
            a.io(s.up);
            a.io(s.down_list);
            a.io(s.up_list);
        }
        a.io(r.named_timers);
    }
    a.io(item_labels_);
}
void ModelCtrlManager::owned_nodes(std::vector<Node*>&) const {}
void ModelCtrlManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- WEAPON (the bone selects it hangs on the players' actors; a player's
// GENERATE during play makes them, so the structure pass makes them again)
void WeaponManager::tick_structure(TickArchive& a) {
    std::uint32_t players = std::uint32_t(attached_.size());
    a.io(players);
    if (a.reading()) {
        if (players > 4) throw TickStateError("weapons", "implausible attachments");
        for (int p = 0; p < 4; ++p) remove(p);
    }
    auto it = attached_.begin();
    for (std::uint32_t i = 0; i < players; ++i) {
        std::int32_t player = a.reading() ? 0 : it->first;
        a.io(player);
        auto& attached = attached_[player];
        a.nodes(attached.actors);
        std::uint32_t parts = std::uint32_t(attached.nodes.size());
        a.io(parts);
        if (a.reading()) {
            if (parts > 64) throw TickStateError("weapons", "implausible parts");
            for (std::uint32_t k = 0; k < parts; ++k) {
                auto select = std::make_unique<Node>();
                select->kind = NodeKind::bone_select;
                attached.nodes.push_back(std::move(select));
            }
        }
        for (auto& n : attached.nodes) {
            a.io(n->name);
            a.io(n->bone_index);
            a.nodes(n->children);
        }
        if (a.reading())
            for (auto* actor : attached.actors)
                if (actor)
                    for (const auto& n : attached.nodes) actor->children.push_back(n.get());
        if (!a.reading()) ++it;
    }
}
void WeaponManager::tick_state(TickArchive& a) { base_rams(a, rams_); }
void WeaponManager::owned_nodes(std::vector<Node*>& out) const {
    for (const auto& [player, attached] : attached_)
        for (const auto& n : attached.nodes) out.push_back(n.get());
}
void WeaponManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- SUBWND (no program runs at a state-safe tick yet; the shop stocks stay)
void SubWindowManager::tick_state(TickArchive& a) {
    base_rams(a, rams_);
    if (window_open()) throw TickStateError("windows", "a window program at a state-safe tick");
    a.io(stocks_);
    a.io(junk_counter_);
    auto program = [&](Program& p) {
        a.io(p.state);
        a.io(p.program);
        a.io(p.player);
        a.io(p.shown);
        a.io(p.shown_hp);
        a.io(p.shown_mp);
        a.io(p.left_hp);
        a.io(p.left_mp);
    };
    program(main_);
    for (auto& p : own_) program(p);
}
void SubWindowManager::owned_nodes(std::vector<Node*>&) const {}
void SubWindowManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- NDMWPANEL
void PanelManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "panel RAMs");
    for (auto& r : rams_) {
        ram_base(a, *r);
        a.io(r->running);
        a.io(r->reopen);
        a.io(r->cursor_shown);
        a.io(r->decided);
        a.io(r->state);
        a.io(r->fire);
        a.io(r->counter);
        a.io(r->kind);
        a.io(r->fire_timer);
        a.io(r->emgheal_timer);
        a.io(r->label);
        for (auto& l : r->lists) {
            a.io(l.kind);
            a.io(l.cursor);
            a.io(l.scroll);
            a.io(l.line_mode);
            a.io(l.count);
            a.io(l.fire_request);
            a.io(l.rows);
            a.io(l.colors);
        }
        a.io(r->no_gate_disk);
        a.io(r->no_healing);
        a.io(r->no_para_up);
        a.io(r->no_cureling);
        a.io(r->line_text);
        a.io(r->line_color);
    }
}
void PanelManager::owned_nodes(std::vector<Node*>&) const {}
void PanelManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- ACTIONSTARTER
void ActionStarterManager::tick_state(TickArchive& a) { base_rams(a, rams_); }
void ActionStarterManager::owned_nodes(std::vector<Node*>&) const {}
void ActionStarterManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

// ---- ACTION2
void ActionManager::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rams_.size()), "action RAMs");
    for (auto& rp : rams_) {
        auto& r = *rp;
        ram_base(a, r);
        a.node(r.node);
        a.node(r.switch_node);
        a.io(r.switch_child);
        element(a, static_cast<const std::vector<Action>*>(r.actions), r.current);
        a.io(r.cursor);
        a.io(r.action_active);
        a.io(r.visible);
        a.io(r.frozen);
        a.io(r.motion_immediate);
        a.io(r.motion_work);
        a.io(r.joint_state);
        a.io(r.fade_state);
        a.io(r.wait_state);
        a.io(r.flash_state);
        a.io(r.joint_start);
        a.io(r.joint_end);
        a.io(r.joint_ended);
        a.io(r.fade_limit);
        a.io(r.fade_add);
        a.io(r.fade_value);
        a.io(r.color);
        a.io(r.blend);
        a.io(r.texture);
        a.io(r.lighting);
        a.io(r.timers);
        a.io(r.node_clock);
        a.nodes(r.actors);
        a.io(r.motion);
        const Skeleton* skeleton = r.database ? &r.database->skeleton() : nullptr;
        actor_pose(a, r.pose, skeleton);
        a.io(r.pose_time);
        a.io(r.locals);
        a.io(r.combiner_cache);
        actor_pose(a, r.traversed, skeleton);
        a.io(r.traversed_time);
    }
    a.io(random_wait_min_);
    // MOTIONSET's loop flags, by database (in the order of their keys' ids).
    std::vector<std::pair<std::uint64_t, AnimationDatabase*>> databases;
    for (auto& [key, database] : databases_)
        databases.push_back(
            {(std::uint64_t(a.ids().document(static_cast<const IgbDocument*>(key.first))) << 32) | key.second, database.get()});
    std::sort(databases.begin(), databases.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    a.same(std::uint32_t(databases.size()), "animation databases");
    for (auto& [id, database] : databases) {
        a.same(id, "animation database");
        a.io(database->loops());
    }
}
void ActionManager::owned_nodes(std::vector<Node*>&) const {}
void ActionManager::tick_rams(std::vector<const Ram*>& out) const { ram_list(out, rams_); }

} // namespace dw4::engine
