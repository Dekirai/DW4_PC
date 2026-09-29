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
    };
    struct Texture {
        unsigned id{};
        bool alpha{};
    };
    const Mesh* mesh(const engine::GeometryData* geometry);
    const Texture* texture(const engine::Attribute* binding);

    std::unique_ptr<Impl> gl_;
    unsigned program_{};
    std::unordered_map<const engine::GeometryData*, Mesh> meshes_;
    std::map<std::pair<const void*, std::uint32_t>, Texture> textures_;
    unsigned white_texture_{};
};

} // namespace dw4::game
