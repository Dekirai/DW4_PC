#pragma once

// Particle arrays (Gap::Gfx::igParticleArray update 800E0910 and the
// Gap::Bec::ParticleArray override 8031A340). Every particle record (the
// array's stride apart: 180 bytes, or 160 without the Bec terms) is a
// quadratic in time:
//   +0x00 position, +0x0C velocity, +0x18 acceleration (p0 + v t + a t^2)
//   +0x24/+0x34/+0x44 RGBA terms, +0x54/+0x5C/+0x64 width/height terms
//   +0x6C/+0x74 texture rectangle corners, +0x94..+0xB0 their terms (Bec)
//   +0x7C/+0x80/+0x84 rotation terms (t^2, t, constant), +0x90 time offset (Bec)
//   +0x88/+0x8C per-particle time scale/offset (800E0858, only with time
//   terms at format +0x2C; none of the 2717 arrays in the data has them)
// The base update draws every particle at the array's own time; the Bec one
// (continuous time or texture coordinates) phases particles by their offset
// and snaps the rectangle to the texture's image grid.
#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <cstdint>
#include <span>

namespace dw4::engine {

// Record `index` of a particle array's data at `stride` bytes per record: the
// 0xB4 bytes the updates read, fewer at the end of the data (the missing
// fields read as 0), empty past it.
std::span<const std::uint8_t> particle_record(std::span<const std::uint8_t> data, std::uint32_t index, std::uint32_t stride);

// Builds camera-facing quads (world space) for particles first..first+count
// of an igParticleAttr's array at `time` seconds of its timer. Returns false
// while the array is outside its time range.
bool build_particles(const ObjectView& particle_attr, float time, const Mat4& model, const Mat4& view,
                     GeometryData& out);

} // namespace dw4::engine
