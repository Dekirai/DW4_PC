#pragma once

#include "dw4/camera.hpp"
#include "dw4/meshes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace dw4::viewer {
enum class MeshDrawMode { solid, wireframe };

// Requires the application's current OpenGL 3.3 context for upload/render/clear.
// Call shutdown before destroying that context. Construction performs no GL work.
class MeshRenderer {
public:
    MeshRenderer();
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;
    void upload(const MeshData& mesh);
    void clear();
    void shutdown();
    // Returns an RGBA GL texture. Display with vertically flipped UVs in ImGui.
    // Targets are limited to 4096 per axis and 8,388,608 pixels. MSAA is requested
    // independently for this target, clamped to the driver limit and 8 samples.
    std::uint32_t render(int width, int height, const OrbitCamera& camera,
        MeshDrawMode mode, int requested_samples = 0);
    bool has_mesh() const noexcept;
    std::size_t triangle_count() const noexcept;
    std::uint64_t rendered_frames() const noexcept;
    int samples() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4::viewer
