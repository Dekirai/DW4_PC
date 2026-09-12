#pragma once

#include "dw4/camera.hpp"
#include "dw4/meshes.hpp"
#include "dw4/textures.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace dw4::viewer {
enum class MeshDrawMode { solid, wireframe, textured };
enum class MeshTextureWrap { repeat, clamp_to_edge, mirrored_repeat };
enum class MeshTextureFilter { nearest, linear };
struct MeshTextureOptions {
    MeshTextureWrap wrap_u = MeshTextureWrap::repeat;
    MeshTextureWrap wrap_v = MeshTextureWrap::repeat;
    MeshTextureFilter min_filter = MeshTextureFilter::linear;
    MeshTextureFilter mag_filter = MeshTextureFilter::linear;
    bool operator==(const MeshTextureOptions&) const = default;
};
struct MeshCanvasPoint { float x{}, y{}; };

struct MeshBatch {
    // Ordinary batches use one shared scene coordinate space. Prepared batches
    // retain source coordinates and receive a separate model-view for each draw.
    // Pointers are borrowed only during upload; no host data is retained.
    const MeshData* mesh{};
    const RgbaImage* texture{};
    MeshTextureOptions texture_options{};
    // The caller must explicitly label mixed-material inspection. Otherwise
    // Textured rejects any batch without a verified image and UV stream.
    bool allow_solid_fallback{};
};

// Requires the application's current OpenGL 3.3 context for upload/render/clear.
// Call shutdown before destroying that context. Construction performs no GL work.
class MeshRenderer {
public:
    MeshRenderer();
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;
    void upload(const MeshData& mesh);
    // Keep original source coordinates for an explicitly prepared draw. Unlike
    // ordinary mesh inspection, no center/radius normalization is baked in.
    void upload_prepared(const MeshData& mesh);
    // Upload all instances into one shared normalization, VBO/index buffer and
    // depth/MSAA target. Limits: 4,096 batches; 4,194,304 vertices and triangles;
    // 256 MiB of GPU image copies, with the existing 64 MiB per-image limit.
    // Shared image pointers with equal sampler options use one GPU texture.
    void upload_batches(std::span<const MeshBatch> batches);
    // Same bounded geometry/material upload, without source normalization.
    // Batch order is retained even when mesh or image pointers are shared.
    void upload_prepared_batches(std::span<const MeshBatch> batches);
    // Update a pose in source coordinates while keeping the initial camera
    // normalization (or raw prepared coordinates), UVs, indices and texture.
    // The vertex count must match.
    // Invalid input leaves the last valid pose available.
    void update_positions(std::span<const Vec3> positions);
    // Update one batch by its original upload order, using world positions for
    // scene uploads or raw source positions for prepared uploads, with exactly
    // that batch's original vertex count. Other batches, normalization, UVs,
    // textures and indices remain unchanged. All coordinates are checked before
    // GPU mutation. A single upload is batch zero.
    void update_batch_positions(std::size_t batch_index, std::span<const Vec3> positions);
    // Project a source-space point using the same fixed normalization/camera as
    // the mesh. Fractions are relative to the canvas's top-left corner. Points
    // outside the clip volume, including behind the camera, are omitted.
    // Prepared uploads require explicit matrix projection and reject here.
    std::optional<MeshCanvasPoint> project_to_canvas(const Vec3& point,
        const OrbitCamera& camera, float aspect) const;
    // Aim once at checked bounds in the uploaded scene's source coordinates.
    // Keeps orbit angles and renderer normalization; uses a padded bounding-
    // sphere distance and the camera's existing aspect/range handling.
    // Invalid bounds leave the supplied camera unchanged.
    // Prepared uploads require explicit camera placement and reject here.
    void focus_camera(OrbitCamera& camera, const MeshBounds& source_bounds) const;
    // Host source-to-inspection-space scale/translation for composing a raw
    // prepared pass with this scene's camera. Float GPU composition need not
    // be bit-identical to the ordinary per-vertex double normalization.
    std::array<float,16> source_normalization() const;
    // A textured upload requires an explicitly decoded UV pair per vertex.
    // Pixel data is copied to the GPU; no borrowed host pointers are retained.
    void set_texture(const RgbaImage& image, const MeshTextureOptions& options = {});
    void clear_texture();
    void clear();
    void shutdown();
    // Returns an RGBA GL texture. Display with vertically flipped UVs in ImGui.
    // Targets are limited to 4096 per axis and 8,388,608 pixels. MSAA is requested
    // independently for this target, clamped to the driver limit and 8 samples.
    // Optional host visibility aid: draw the chosen batch's occluded geometry
    // in opaque gold against the depth of preceding batches, before drawing its
    // normal visible surfaces. Place the player after the static scene batches.
    // The default retains ordinary scene rendering. Invalid indices reject
    // before changing any GPU state or the previous render target.
    std::uint32_t render(int width, int height, const OrbitCamera& camera,
        MeshDrawMode mode, int requested_samples = 0,
        std::optional<std::size_t> occluded_batch = std::nullopt);
    // Apply captured model-view once, then projection as a separate GPU stage.
    // Requires one prepared batch; does not apply the inspection OrbitCamera.
    std::uint32_t render_prepared(int width, int height,
        const std::array<float,16>& model_view, const std::array<float,16>& projection,
        MeshDrawMode mode, int requested_samples = 0);
    // Draw all prepared batches in upload order into one color/depth/MSAA
    // target, cleared once. Supply exactly one captured model-view per batch
    // and one shared projection. The complete pass is validated before target
    // mutation. Matrices are borrowed only for this call. The optional host
    // occlusion aid has the same semantics as render(), using this batch's view.
    std::uint32_t render_prepared_batches(int width, int height,
        std::span<const std::array<float,16>> model_views, const std::array<float,16>& projection,
        MeshDrawMode mode, int requested_samples = 0,
        std::optional<std::size_t> occluded_batch = std::nullopt);
    bool has_mesh() const noexcept;
    // True only when all uploaded batches have verified texture/UV data.
    bool has_texture() const noexcept;
    std::size_t batch_count() const noexcept;
    std::size_t textured_batch_count() const noexcept;
    std::size_t texture_count() const noexcept;
    std::size_t triangle_count() const noexcept;
    std::uint64_t rendered_frames() const noexcept;
    int samples() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    void upload_impl(const MeshData& mesh, bool prepared);
    void upload_batches_impl(std::span<const MeshBatch> batches, bool prepared);
    std::uint32_t render_matrices(int width, int height,
        std::span<const std::array<float,16>> model_views, const std::array<float,16>& projection,
        MeshDrawMode mode, int requested_samples, std::optional<std::size_t> occluded_batch);
};
} // namespace dw4::viewer
