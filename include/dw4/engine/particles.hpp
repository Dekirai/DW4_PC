#pragma once

// Particle arrays (Gap::Gfx::igParticleArray update 800E0910 and the
// Gap::Bec::ParticleArray override 8031A340). Every particle of the 180-byte
// records is a quadratic in time:
//   +0x00 position, +0x0C velocity, +0x18 acceleration (p0 + v t + a t^2)
//   +0x24/+0x34/+0x44 RGBA terms, +0x54/+0x5C/+0x64 width/height terms
//   +0x6C/+0x74 texture rectangle corners, +0x94..+0xB0 their terms (Bec)
//   +0x7C/+0x80/+0x84 rotation terms (t^2, t, constant), +0x90 time offset (Bec)
// The base update draws every particle at the array's own time; the Bec one
// (continuous time or texture coordinates) phases particles by their offset
// and snaps the rectangle to the texture's image grid.
#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <cstdint>

namespace dw4::engine {

// Builds camera-facing quads (world space) for particles first..first+count
// of an igParticleAttr's array at `time` seconds of its timer. Returns false
// while the array is outside its time range.
bool build_particles(const ObjectView& particle_attr, float time, const Mat4& model, const Mat4& view,
                     GeometryData& out);

} // namespace dw4::engine
