#pragma once

#include "dw4/engine/scene_graph.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dw4::game {

struct Viewport {
    int x{}, y{}, width{}, height{};
};

// OpenGL 3.3 renderer for engine draw lists. Requires a current context.
class GlRenderer {
public:
    GlRenderer();
    ~GlRenderer();
    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

    // `screen` is the 4:3 game area inside the window (GL window coordinates).
    void render(const std::vector<engine::DrawItem>& items, const engine::FrameCamera& camera,
                const engine::Vec4f& clear_color, const Viewport& screen, int window_width, int window_height);
    // A movie picture (RGBA rows, top first) over the whole 4:3 screen.
    void draw_movie(const std::uint8_t* rgba, int width, int height, const Viewport& screen, int window_width,
                    int window_height);
    // The projected shadow drawn with the next render (null: none).
    void set_shadow(const engine::ShadowFrame* shadow) { shadow_ = shadow; }
    // Drop all GPU resources (call when the engine changed stage).
    void clear_cache();
    std::size_t texture_count() const { return textures_.size(); }
    std::size_t mesh_count() const { return meshes_.size(); }
    struct Impl;

private:
    struct Mesh {
        unsigned vao{}, vbo{}, ibo{};
        int index_count{};
        bool has_colors{}, has_uvs{}, has_normals{};
        std::uint64_t serial{};       // GeometryData instance the buffers hold
        std::uint64_t last_frame{};   // eviction of unused runtime geometry
    };
    void release(Mesh& mesh);
    std::uint64_t frame_{};
    struct Texture {
        unsigned id{};
        bool alpha{};
    };
    const Mesh* mesh(const engine::GeometryData* geometry);
    const Texture* texture(const engine::Attribute* binding);
    void render_shadow_map();
    void render_shadow_receivers(const engine::FrameCamera& camera);

    std::unique_ptr<Impl> gl_;
    unsigned program_{};
    std::unordered_map<const engine::GeometryData*, Mesh> meshes_;
    std::map<std::pair<const void*, std::uint32_t>, Texture> textures_;
    unsigned white_texture_{};
    const engine::ShadowFrame* shadow_{};
    unsigned movie_program_{}, movie_texture_{}, movie_vao_{};
    int movie_width_{}, movie_height_{};
    unsigned shadow_fbo_{}, shadow_texture_{};
    int shadow_size_{};
    bool shadow_ready_{};
};

} // namespace dw4::game
