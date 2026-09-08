#include "mesh_renderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()>& operation) {
    try { operation(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid renderer operation was accepted");
}
struct Context {
    SDL_Window* window{};
    SDL_GLContext context{};
    ~Context() {
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
std::vector<std::uint8_t> pixels(std::uint32_t texture, int width, int height) {
    GLint previous = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * height * 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    check(glGetError() == GL_NO_ERROR, "Reading the rendered texture failed");
    return image;
}
std::size_t coverage(const std::vector<std::uint8_t>& image) {
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < image.size(); offset += 4) {
        check(image[offset + 3] == 255, "Offscreen viewer output must be opaque");
        if (image[offset] > 16 || image[offset + 1] > 24 || image[offset + 2] > 30) ++count;
    }
    return count;
}
void saved_state() {
    check(glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST)
        && !glIsEnabled(GL_DEPTH_TEST), "Renderer did not restore caller capabilities");
    GLboolean depth_mask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    check(!depth_mask, "Renderer did not restore depth write mask");
    GLint viewport[4]{}; glGetIntegerv(GL_VIEWPORT, viewport);
    check(viewport[0] == 7 && viewport[1] == 9 && viewport[2] == 71 && viewport[3] == 53,
        "Renderer did not restore the caller viewport");
    GLdouble depth = 0; glGetDoublev(GL_DEPTH_CLEAR_VALUE, &depth);
    check(std::abs(depth - 0.4) < 1e-5, "Renderer did not restore depth clear value");
    GLint polygon[2]{}; glGetIntegerv(GL_POLYGON_MODE, polygon);
    // Core profile has one polygon mode; some drivers write only the first
    // value while compatibility implementations return front/back modes.
    check(polygon[0] == GL_LINE, "Renderer did not restore the core polygon mode");
    check(glGetError() == GL_NO_ERROR, "Renderer left an OpenGL error");
}

template<class T> T procedure(const char* name) {
    auto function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    check(function != nullptr, name);
    return function;
}

// Deliberately hostile caller state catches accidental dependence on ImGui's
// active unit, sampler or upload layout. The unpack buffer must never receive
// the host image pointer as a buffer offset.
struct TextureCallerState {
    PFNGLACTIVETEXTUREPROC active = procedure<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    PFNGLBINDSAMPLERPROC bind_sampler = procedure<PFNGLBINDSAMPLERPROC>("glBindSampler");
    PFNGLDELETESAMPLERSPROC delete_samplers = procedure<PFNGLDELETESAMPLERSPROC>("glDeleteSamplers");
    PFNGLBINDBUFFERPROC bind_buffer = procedure<PFNGLBINDBUFFERPROC>("glBindBuffer");
    PFNGLDELETEBUFFERSPROC delete_buffers = procedure<PFNGLDELETEBUFFERSPROC>("glDeleteBuffers");
    GLuint textures[2]{}, sampler{}, unpack{};
    TextureCallerState() {
        glGenTextures(2, textures);
        active(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, textures[0]);
        active(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, textures[1]);
        procedure<PFNGLGENSAMPLERSPROC>("glGenSamplers")(1, &sampler);
        const auto parameter = procedure<PFNGLSAMPLERPARAMETERIPROC>("glSamplerParameteri");
        parameter(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        parameter(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        bind_sampler(0, sampler);
        procedure<PFNGLGENBUFFERSPROC>("glGenBuffers")(1, &unpack);
        bind_buffer(GL_PIXEL_UNPACK_BUFFER, unpack);
        procedure<PFNGLBUFFERDATAPROC>("glBufferData")(GL_PIXEL_UNPACK_BUFFER, 64, nullptr, GL_STATIC_DRAW);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8); glPixelStorei(GL_UNPACK_ROW_LENGTH, 5);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 2); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
        procedure<PFNGLBLENDFUNCSEPARATEPROC>("glBlendFuncSeparate")(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        procedure<PFNGLBLENDEQUATIONSEPARATEPROC>("glBlendEquationSeparate")(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
        verify();
    }
    ~TextureCallerState() {
        bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0); delete_buffers(1, &unpack);
        bind_sampler(0, 0); delete_samplers(1, &sampler);
        active(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
        active(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(2, textures); active(GL_TEXTURE0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    }
    void verify() {
        auto equals = [](GLenum name, GLint expected) {
            GLint value = 0; glGetIntegerv(name, &value);
            check(value == expected, "Texture operation did not restore the caller's GL state");
        };
        equals(GL_ACTIVE_TEXTURE, GL_TEXTURE3); equals(GL_TEXTURE_BINDING_2D, textures[1]);
        active(GL_TEXTURE0); equals(GL_TEXTURE_BINDING_2D, textures[0]);
        GLint value = 0; glGetIntegerv(GL_SAMPLER_BINDING, &value); active(GL_TEXTURE3);
        check(value == static_cast<GLint>(sampler), "Texture operation did not restore sampler zero");
        equals(GL_PIXEL_UNPACK_BUFFER_BINDING, unpack); equals(GL_UNPACK_ALIGNMENT, 8);
        equals(GL_UNPACK_ROW_LENGTH, 5); equals(GL_UNPACK_SKIP_ROWS, 2); equals(GL_UNPACK_SKIP_PIXELS, 1);
        equals(GL_BLEND_SRC_RGB, GL_SRC_ALPHA); equals(GL_BLEND_DST_RGB, GL_ONE);
        equals(GL_BLEND_SRC_ALPHA, GL_ONE); equals(GL_BLEND_DST_ALPHA, GL_ZERO);
        equals(GL_BLEND_EQUATION_RGB, GL_FUNC_REVERSE_SUBTRACT); equals(GL_BLEND_EQUATION_ALPHA, GL_FUNC_ADD);
        saved_state();
    }
};

void texture_checks(dw4::viewer::MeshRenderer& renderer, const dw4::MeshData& tetra, const dw4::OrbitCamera& camera) {
    using namespace dw4::viewer;
    const dw4::RgbaImage atlas{2, 2, {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255}};
    renderer.upload(tetra);
    rejects([&] { renderer.set_texture(atlas); });
    rejects([&] { renderer.render(128, 128, camera, MeshDrawMode::textured); });
    auto mesh = tetra;
    mesh.texcoords.assign(mesh.positions.size(), {0.25f, 0.25f});
    renderer.upload(mesh);
    rejects([&] { renderer.render(128, 128, camera, MeshDrawMode::textured); });
    TextureCallerState caller;
    MeshTextureOptions options;
    options.min_filter = options.mag_filter = MeshTextureFilter::nearest;
    auto draw = [&](dw4::Vec2 uv, const dw4::RgbaImage& image) {
        mesh.texcoords.assign(mesh.positions.size(), uv);
        renderer.upload(mesh); caller.verify();
        renderer.set_texture(image, options); caller.verify();
        check(renderer.has_texture(), "Valid RGBA/UV upload did not create a texture");
        auto result = pixels(renderer.render(128, 128, camera, MeshDrawMode::textured), 128, 128);
        caller.verify();
        coverage(result); // Every output pixel must retain opaque viewport alpha.
        return result;
    };
    auto expect_color = [](const std::vector<std::uint8_t>& image, std::array<int, 3> expected) {
        std::size_t count = 0;
        for (std::size_t offset = 0; offset < image.size(); offset += 4) {
            bool equal = true;
            for (int channel = 0; channel < 3; ++channel)
                equal = equal && std::abs(int(image[offset + channel]) - expected[channel]) <= 1;
            if (equal) ++count;
        }
        check(count > 200, "Texture sampling produced incorrect colors, UV orientation or wrapping");
    };
    expect_color(draw({0.25f, 0.25f}, atlas), {255,0,0});
    expect_color(draw({0.25f, 0.75f}, atlas), {0,0,255});
    expect_color(draw({-0.25f, 1.25f}, atlas), {0,255,0});
    options.wrap_u = options.wrap_v = MeshTextureWrap::clamp_to_edge;
    expect_color(draw({-0.25f, 1.25f}, atlas), {0,0,255});
    options.wrap_u = options.wrap_v = MeshTextureWrap::mirrored_repeat;
    expect_color(draw({1.25f, 0.25f}, atlas), {0,255,0});
    options.min_filter = options.mag_filter = MeshTextureFilter::linear;
    expect_color(draw({0.5f, 0.5f}, atlas), {128,128,64});
    auto transparent = atlas;
    for (std::size_t i = 3; i < transparent.rgba.size(); i += 4) transparent.rgba[i] = 0;
    check(coverage(draw({0.25f, 0.25f}, transparent)) == 0, "Transparent texels must preserve the background");
    mesh.indices = {0,1,2};
    transparent.rgba[3] = 128;
    expect_color(draw({0.25f, 0.25f}, transparent), {132,7,9});
    auto malformed = atlas; malformed.rgba.pop_back();
    rejects([&] { renderer.set_texture(malformed); });
    check(!renderer.has_texture(), "Failed texture replacement retained a stale image");
    renderer.set_texture(atlas); renderer.clear_texture();
    check(renderer.has_mesh() && !renderer.has_texture(), "Clearing a texture must preserve geometry");
    rejects([&] { renderer.render(128, 128, camera, MeshDrawMode::textured); });
    mesh.texcoords.pop_back();
    rejects([&] { renderer.upload(mesh); });
    check(!renderer.has_mesh(), "Mismatched UV count retained stale geometry");
    mesh.texcoords.assign(mesh.positions.size(), {0, std::numeric_limits<float>::quiet_NaN()});
    rejects([&] { renderer.upload(mesh); });
    caller.verify();
}

void pose_checks(dw4::viewer::MeshRenderer& renderer, const dw4::MeshData& tetra, const dw4::OrbitCamera& camera) {
    using namespace dw4::viewer;
    renderer.clear();
    rejects([&] { renderer.update_positions(tetra.positions); });
    rejects([&] { renderer.update_batch_positions(0,tetra.positions); });
    check(!renderer.project_to_canvas({0,0,0}, camera, 1), "Empty renderer projected a stale mesh point");
    auto mesh = tetra;
    mesh.texcoords.assign(mesh.positions.size(), {0.25f, 0.25f});
    renderer.upload(mesh);
    const dw4::RgbaImage image{1, 1, {255,0,0,255}};
    TextureCallerState caller;
    renderer.set_texture(image);
    const auto target = renderer.render(128, 128, camera, MeshDrawMode::textured);
    const auto initial = pixels(target, 128, 128);
    const auto center = renderer.project_to_canvas({0,0,0}, camera, 1);
    check(center && std::abs(center->x - 0.5f) < 1e-5f && std::abs(center->y - 0.5f) < 1e-5f,
        "Canvas projection disagrees with the fitted mesh center");
    check(!renderer.project_to_canvas({1e6f,1e6f,1e6f}, camera, 1), "Out-of-view joint was not clipped");
    rejects([&] { renderer.project_to_canvas({0,std::numeric_limits<float>::infinity(),0}, camera, 1); });
    const auto initial_frames = renderer.rendered_frames();
    auto translated = tetra.positions;
    for (auto& point : translated) point.x += 0.8f;
    renderer.update_positions(translated); caller.verify();
    const auto shifted_center = renderer.project_to_canvas({0,0,0}, camera, 1);
    check(shifted_center && shifted_center->x == center->x && shifted_center->y == center->y,
        "Pose update moved the skeleton overlay's fixed reference normalization");
    check(renderer.has_texture() && renderer.triangle_count() == 4, "Pose update lost UVs, indices or texture");
    check(renderer.rendered_frames() == initial_frames, "Pose upload reset the successful-frame counter");
    check(renderer.render(128, 128, camera, MeshDrawMode::textured) == target, "Pose update replaced the render target");
    const auto moved = pixels(target, 128, 128);
    check(moved != initial && coverage(moved) > 200, "Pose update was ignored or camera normalization followed the moving mesh");
    auto invalid = translated; invalid.pop_back();
    rejects([&] { renderer.update_positions(invalid); });
    invalid = translated; invalid[0].y = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { renderer.update_positions(invalid); });
    check(pixels(renderer.render(128, 128, camera, MeshDrawMode::textured), 128, 128) == moved,
        "Rejected pose input changed the previous valid pose");
    renderer.update_positions(tetra.positions); caller.verify();
    check(pixels(renderer.render(128, 128, camera, MeshDrawMode::textured), 128, 128) == initial,
        "Restoring source positions did not reproduce the textured reference image");
    rejects([&] { renderer.update_batch_positions(1,tetra.positions); });
    renderer.update_batch_positions(0,translated); caller.verify();
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==moved,
        "Batch-zero update is not compatible with a single uploaded mesh");
    renderer.update_batch_positions(0,tetra.positions);
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==initial,
        "Batch-zero reset changed single-mesh UVs or source normalization");
    renderer.clear();
    check(!renderer.has_texture(), "Clearing a posed mesh retained its texture");
}

void batch_pose_checks(dw4::viewer::MeshRenderer& renderer) {
    using namespace dw4::viewer;
    auto quad=[](float x,float z,float extent) {
        dw4::MeshData mesh;
        mesh.positions={{x-extent,-extent,z},{x+extent,-extent,z},{x+extent,extent,z},{x-extent,extent,z}};
        mesh.indices={0,1,2,0,2,3};
        mesh.texcoords={{0,0},{1,0},{1,1},{0,1}};
        return mesh;
    };
    dw4::OrbitCamera camera; camera.orbit(-.65f,-.35f);
    auto left=quad(-1,0,.4f),actor=quad(0,0,.3f),right=quad(1,0,.4f);
    // A three-vertex static batch before the four-vertex actor catches incorrect
    // update offsets; a static batch after it catches an oversized update.
    left.positions.pop_back();left.texcoords.pop_back();left.indices={0,1,2};
    const dw4::RgbaImage red{1,1,{255,0,0,255}},blue{1,1,{0,0,255,255}};
    auto atlas=dw4::RgbaImage{2,2,{0,255,0,255,255,255,0,255,0,255,255,255,255,255,255,255}};
    MeshTextureOptions sampler;
    sampler.min_filter=sampler.mag_filter=MeshTextureFilter::nearest;
    sampler.wrap_u=sampler.wrap_v=MeshTextureWrap::clamp_to_edge;
    std::array<MeshBatch,3> batches{{{&left,&red},{&actor,&atlas,sampler},{&right,&blue}}};
    TextureCallerState caller;
    renderer.upload_batches(batches);caller.verify();
    const auto target=renderer.render(160,128,camera,MeshDrawMode::textured);
    const auto initial=pixels(target,160,128);
    const auto origin=renderer.project_to_canvas({0,0,0},camera,1.25f);
    check(origin.has_value(),"Actor scene origin was not projected");
    const auto initial_frames=renderer.rendered_frames();
    const auto original_positions=actor.positions;
    auto moved_positions=original_positions;
    for(auto& point:moved_positions) point.y+=.4f;
    // Pose updates must use retained immutable UVs, never borrowed host arrays.
    actor.positions.clear();actor.texcoords.clear();atlas.rgba.clear();
    renderer.update_batch_positions(1,moved_positions);caller.verify();
    check(renderer.batch_count()==3 && renderer.textured_batch_count()==3 && renderer.texture_count()==3 &&
        renderer.triangle_count()==5 && renderer.rendered_frames()==initial_frames,
        "Actor batch update changed scene identities, materials or frame count");
    const auto unchanged_origin=renderer.project_to_canvas({0,0,0},camera,1.25f);
    check(unchanged_origin && unchanged_origin->x==origin->x && unchanged_origin->y==origin->y,
        "Actor batch update changed the shared source normalization");
    check(renderer.render(160,128,camera,MeshDrawMode::textured)==target,"Actor update replaced the shared render target");
    const auto moved=pixels(target,160,128);
    check(moved!=initial && coverage(moved)>400,"Actor batch pose was ignored or erased the scene");
    std::size_t static_pixels{};
    for(std::size_t i=0;i<initial.size();i+=4) {
        auto is_static=[&](const auto& image) {
            return image[i+1]==0 && ((image[i]==255 && image[i+2]==0) || (image[i]==0 && image[i+2]==255));
        };
        check(is_static(initial)==is_static(moved),"Actor update moved an adjacent static batch or changed its texture");
        if(is_static(initial)) {
            ++static_pixels;
            check(std::equal(initial.begin()+i,initial.begin()+i+4,moved.begin()+i),"Static scene pixels changed during actor update");
        }
    }
    check(static_pixels>300,"Static scene isolation test did not cover enough pixels");
    rejects([&]{renderer.update_batch_positions(3,moved_positions);});
    rejects([&]{renderer.update_batch_positions(std::numeric_limits<std::size_t>::max(),moved_positions);});
    rejects([&]{renderer.update_batch_positions(1,{});});
    rejects([&]{renderer.update_batch_positions(0,moved_positions);});
    auto invalid=moved_positions;invalid.pop_back();
    rejects([&]{renderer.update_batch_positions(1,invalid);});
    invalid=moved_positions;invalid.front().x+=.1f;invalid.back().z=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{renderer.update_batch_positions(1,invalid);});
    invalid=moved_positions;invalid.back().y=std::numeric_limits<float>::infinity();
    rejects([&]{renderer.update_batch_positions(1,invalid);});
    caller.verify();
    check(pixels(renderer.render(160,128,camera,MeshDrawMode::textured),160,128)==moved,
        "Rejected actor batch input changed the last valid frame");
    renderer.update_batch_positions(1,original_positions);caller.verify();
    check(pixels(renderer.render(160,128,camera,MeshDrawMode::textured),160,128)==initial,
        "Actor reset did not restore exact scene pixels, UVs and textures");

    auto back=quad(0,-.5f,.9f),front=quad(0,.5f,.5f);
    std::array<MeshBatch,2> depth_batches{{{&back,&blue},{&front,&red}}};
    renderer.upload_batches(depth_batches);
    const auto depth_target=renderer.render(128,128,camera,MeshDrawMode::textured,8);
    const auto in_front=pixels(depth_target,128,128);
    const auto middle=(64u*128+64)*4;
    check(in_front[middle]==255 && in_front[middle+2]==0,"Initial actor failed to occlude static scene geometry");
    auto behind=front.positions;for(auto& point:behind) point.z-=2;
    renderer.update_batch_positions(1,behind);caller.verify();
    check(renderer.render(128,128,camera,MeshDrawMode::textured,8)==depth_target,"Actor pose discarded the shared MSAA target");
    const auto in_back=pixels(depth_target,128,128);
    check(in_back[middle]==0 && in_back[middle+2]==255,"Updated actor does not share depth with unchanged static batches");
    renderer.update_batch_positions(1,front.positions);
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured,8),128,128)==in_front,
        "Actor depth reset did not reproduce exact MSAA scene pixels");

    // Finite world coordinates may overflow float after a tiny scene's fixed
    // normalization. Reject them before changing any GPU vertex.
    for(auto& point:back.positions) {point.x*=1e-30f;point.y*=1e-30f;point.z*=1e-30f;}
    for(auto& point:front.positions) {point.x*=1e-30f;point.y*=1e-30f;point.z*=1e-30f;}
    renderer.upload_batches(depth_batches);
    const auto tiny=pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128);
    invalid=front.positions;invalid.back().x=std::numeric_limits<float>::max();
    rejects([&]{renderer.update_batch_positions(1,invalid);});
    caller.verify();
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==tiny,
        "Unrepresentable normalized actor coordinate corrupted the previous scene");
    renderer.clear();
    rejects([&]{renderer.update_batch_positions(0,front.positions);});
}

void focus_checks(dw4::viewer::MeshRenderer& renderer) {
    using namespace dw4::viewer;
    dw4::OrbitCamera camera;camera.orbit(.3f,-.1f);
    const dw4::MeshBounds actor_bounds{{49,-21,30},{51,-19,30}};
    renderer.clear();
    const auto empty_camera=camera.view_projection(1);
    rejects([&]{renderer.focus_camera(camera,actor_bounds);});
    check(camera.view_projection(1)==empty_camera,"Empty-renderer focus changed the camera");
    dw4::MeshData map,actor;
    map.positions={{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0}};
    actor.positions={{49,-21,30},{51,-21,30},{51,-19,30},{49,-19,30}};
    map.indices=actor.indices={0,1,2,0,2,3};
    map.texcoords.assign(4,{0,0});actor.texcoords=map.texcoords;
    const dw4::RgbaImage red{1,1,{255,0,0,255}},green{1,1,{0,255,0,255}};
    std::array<MeshBatch,2> batches{{{&map,&red},{&actor,&green}}};
    TextureCallerState caller;
    renderer.upload_batches(batches);
    const auto overview=camera;
    const auto overview_point=renderer.project_to_canvas({50,-20,30},overview,1);
    check(overview_point.has_value(),"Focus fixture actor was outside the overview projection");
    renderer.focus_camera(camera,actor_bounds);caller.verify();
    check(camera.distance()>=.05f && camera.distance()<overview.distance(),"Focusing a tiny scene actor did not approach its bounds");
    const auto fixed_camera=camera.view_projection(1);
    for(float aspect : {.2f,1.0f,16.0f/9}) {
        const auto point=renderer.project_to_canvas({50,-20,30},camera,aspect);
        check(point && std::abs(point->x-.5f)<2e-5f && std::abs(point->y-.5f)<2e-5f,
            "Source bounds center is not centered after scene camera focus");
        for(const auto& corner:actor.positions)
            check(renderer.project_to_canvas(corner,camera,aspect).has_value(),"Padded focus clipped a source bounds corner");
    }
    const auto target=renderer.render(128,128,camera,MeshDrawMode::textured);
    const auto focused=pixels(target,128,128);
    std::size_t actor_pixels{};
    for(std::size_t i=0;i<focused.size();i+=4)
        if(focused[i]==0 && focused[i+1]==255 && focused[i+2]==0) ++actor_pixels;
    check(actor_pixels>100,"Tiny actor remained too small after bounds focus");
    auto shifted=actor.positions;for(auto& point:shifted) point.x+=.3f;
    renderer.update_batch_positions(1,shifted);
    check(camera.view_projection(1)==fixed_camera,"Actor animation changed the one-time camera focus");
    const auto unchanged_overview=renderer.project_to_canvas({50,-20,30},overview,1);
    check(unchanged_overview && unchanged_overview->x==overview_point->x && unchanged_overview->y==overview_point->y,
        "Focusing or animating an actor changed the renderer's source normalization");
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)!=focused,
        "Actor animation did not remain visible through the focused camera");
    renderer.update_batch_positions(1,actor.positions);
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==focused,
        "Actor reset failed to reproduce the focused frame");
    auto invalid=actor_bounds;invalid.min.x=invalid.max.x+1;
    rejects([&]{renderer.focus_camera(camera,invalid);});
    invalid=actor_bounds;invalid.max.z=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{renderer.focus_camera(camera,invalid);});
    invalid=actor_bounds;invalid.min.y=-std::numeric_limits<float>::infinity();
    rejects([&]{renderer.focus_camera(camera,invalid);});
    invalid={{1e30f,1e30f,1e30f},{1e30f,1e30f,1e30f}};
    rejects([&]{renderer.focus_camera(camera,invalid);});
    check(camera.view_projection(1)==fixed_camera,"Rejected source focus bounds changed camera state");
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==focused,
        "Rejected source focus bounds changed the previous rendered scene");
    renderer.focus_camera(camera,{{50,-20,30},{50,-20,30}});
    const auto point_focus=renderer.project_to_canvas({50,-20,30},camera,1);
    check(camera.distance()==.05f && point_focus && std::abs(point_focus->x-.5f)<2e-5f,
        "Point bounds did not use the minimum centered focus distance");
    for(auto& point:map.positions) {point.x*=1e-30f;point.y*=1e-30f;point.z*=1e-30f;}
    for(auto& point:actor.positions) {point.x*=1e-30f;point.y*=1e-30f;point.z*=1e-30f;}
    renderer.upload_batches(batches);
    const auto before_overflow=camera.view_projection(1);
    const auto maximum=std::numeric_limits<float>::max();
    rejects([&]{renderer.focus_camera(camera,{{-maximum,-maximum,-maximum},{maximum,maximum,maximum}});});
    check(camera.view_projection(1)==before_overflow,"Unrepresentable focus distance changed the camera");
    caller.verify();renderer.clear();
}

void scene_checks(dw4::viewer::MeshRenderer& renderer) {
    using namespace dw4::viewer;
    auto quad = [](float x, float z, float extent) {
        dw4::MeshData mesh;
        mesh.positions = {{x-extent,-extent,z},{x+extent,-extent,z},{x+extent,extent,z},{x-extent,extent,z}};
        mesh.indices = {0,1,2,0,2,3}; mesh.texcoords.assign(4,{.25f,.25f});
        return mesh;
    };
    auto color_count = [](const std::vector<std::uint8_t>& image, std::array<std::uint8_t,3> color) {
        std::size_t count{};
        for (std::size_t i=0;i<image.size();i+=4)
            if (image[i]==color[0] && image[i+1]==color[1] && image[i+2]==color[2]) ++count;
        return count;
    };
    dw4::OrbitCamera camera; camera.orbit(-.65f,-.35f);
    auto left=quad(-1,0,.6f),right=quad(1,0,.6f);
    const dw4::RgbaImage red{1,1,{255,0,0,255}},green{1,1,{0,255,0,255}};
    std::array<MeshBatch,2> batches{{{&left,&red},{&right,&green}}};
    TextureCallerState caller;
    renderer.upload_batches(batches); caller.verify();
    check(renderer.batch_count()==2 && renderer.textured_batch_count()==2 && renderer.texture_count()==2
        && renderer.triangle_count()==4 && renderer.has_texture(),"Scene upload lost batches, textures or aggregate triangles");
    const auto target=renderer.render(128,128,camera,MeshDrawMode::textured);
    const auto separated=pixels(target,128,128); caller.verify();
    check(color_count(separated,{255,0,0})>400 && color_count(separated,{0,255,0})>400,
        "World-positioned batches were independently centered or lost their distinct materials");
    const auto left_center=renderer.project_to_canvas({-1,0,0},camera,1);
    const auto right_center=renderer.project_to_canvas({1,0,0},camera,1);
    check(left_center && right_center && left_center->x<.4f && right_center->x>.6f,
        "Scene batches did not use one shared source normalization");
    const auto rendered=renderer.rendered_frames();
    rejects([&]{renderer.update_positions(left.positions);});
    rejects([&]{renderer.set_texture(red);});
    check(renderer.rendered_frames()==rendered && renderer.texture_count()==2,
        "Unsupported single-mesh operation changed the static scene");
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==separated,
        "Rejected pose/material mutation corrupted the previous scene");
    auto disposable_left=left, disposable_right=right;
    auto disposable_red=red, disposable_green=green;
    batches={{{&disposable_left,&disposable_red},{&disposable_right,&disposable_green}}};
    renderer.upload_batches(batches);
    disposable_left.positions.clear(); disposable_right.positions.clear();
    disposable_red.rgba.clear(); disposable_green.rgba.clear();
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==separated,
        "Scene rendering retained borrowed geometry or image buffers after upload");

    auto front=quad(0,.5f,.5f),back=quad(0,-.5f,.9f);
    batches={{{&front,&red},{&back,&green}}}; renderer.upload_batches(batches);
    const auto ordered=pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128);
    check(color_count(ordered,{255,0,0})>400 && color_count(ordered,{0,255,0})>200,
        "Scene draw batches did not share their color/depth target");
    const auto middle=(64u*128+64)*4;
    check(ordered[middle]==255 && ordered[middle+1]==0,"Front scene geometry failed to occlude a later back batch");
    std::reverse(batches.begin(),batches.end()); renderer.upload_batches(batches);
    check(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128)==ordered,
        "Opaque scene visibility depends on batch order instead of shared depth");
    const auto msaa=renderer.render(160,96,camera,MeshDrawMode::textured,8);
    check(coverage(pixels(msaa,160,96))>500,"Shared scene target failed to resize or resolve MSAA");
    check(renderer.render(160,96,camera,MeshDrawMode::textured,8)==msaa,
        "Unchanged scene framebuffer request did not reuse its target");
    const auto solid=pixels(renderer.render(128,128,camera,MeshDrawMode::solid),128,128);
    const auto wire=pixels(renderer.render(128,128,camera,MeshDrawMode::wireframe),128,128);
    check(coverage(solid)>500 && coverage(wire)>40 && coverage(wire)<coverage(solid),
        "Global scene Solid/Wireframe overrides failed");
    caller.verify();

    const dw4::RgbaImage atlas{2,1,{255,0,0,255,0,255,0,255}};
    left.texcoords.assign(4,{1.25f,.25f});right.texcoords=left.texcoords;
    MeshTextureOptions repeat; repeat.min_filter=repeat.mag_filter=MeshTextureFilter::nearest;
    auto clamp=repeat;clamp.wrap_u=MeshTextureWrap::clamp_to_edge;
    batches={{{&left,&atlas,repeat},{&right,&atlas,clamp}}}; renderer.upload_batches(batches);
    const auto samplers=pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128);
    check(renderer.texture_count()==2 && color_count(samplers,{255,0,0})>400 && color_count(samplers,{0,255,0})>400,
        "Scene texture sharing incorrectly merged different per-batch sampler options");
    batches[1].texture_options=repeat; renderer.upload_batches(batches);
    check(renderer.texture_count()==1 && renderer.textured_batch_count()==2,
        "Repeated image identity and equal sampler were uploaded more than once");
    renderer.clear_texture(); caller.verify();
    check(renderer.has_mesh() && renderer.batch_count()==2 && renderer.texture_count()==0 && !renderer.has_texture(),
        "Clearing scene textures destroyed geometry or retained shared image handles");
    rejects([&]{renderer.render(128,128,camera,MeshDrawMode::textured);});

    batches={{{&left,&red},{&right}}}; renderer.upload_batches(batches);
    check(renderer.textured_batch_count()==1 && !renderer.has_texture(),"Incomplete texture coverage was reported as complete");
    rejects([&]{renderer.render(128,128,camera,MeshDrawMode::textured);});
    check(coverage(pixels(renderer.render(128,128,camera,MeshDrawMode::solid),128,128))>500,
        "Missing scene texture should leave explicit Solid inspection available");
    batches[1].allow_solid_fallback=true;renderer.upload_batches(batches);
    check(color_count(pixels(renderer.render(128,128,camera,MeshDrawMode::textured),128,128),{255,0,0})>400,
        "Caller-authorized mixed inspection lost the verified textured batch");
    auto malformed=left;malformed.indices[0]=4;
    batches[0].mesh=&malformed;
    rejects([&]{renderer.upload_batches(batches);});
    check(!renderer.has_mesh() && renderer.batch_count()==0 && renderer.texture_count()==0,
        "Failed scene replacement retained stale geometry or textures");
    malformed=left;malformed.texcoords.pop_back();rejects([&]{renderer.upload_batches(batches);});
    malformed=left;malformed.positions[0].x=std::numeric_limits<float>::infinity();rejects([&]{renderer.upload_batches(batches);});
    malformed=left;malformed.texcoords[0].u=std::numeric_limits<float>::quiet_NaN();rejects([&]{renderer.upload_batches(batches);});
    malformed=left;malformed.texcoords.clear();rejects([&]{renderer.upload_batches(batches);});
    auto bad_image=red;bad_image.rgba.pop_back();batches[0]={&left,&bad_image,{},true};
    rejects([&]{renderer.upload_batches(batches);}); // Invalid supplied images cannot silently become Solid.
    batches[0]={nullptr};rejects([&]{renderer.upload_batches(batches);});
    rejects([&]{renderer.upload_batches({});});
    std::vector<MeshBatch> excessive(4097,{&left});
    rejects([&]{renderer.upload_batches(excessive);});
    auto many_vertices=left;many_vertices.positions.resize(1025);many_vertices.texcoords.clear();
    excessive.assign(4096,{&many_vertices});rejects([&]{renderer.upload_batches(excessive);});
    auto many_triangles=left;many_triangles.indices.assign(1025*3,0);
    excessive.assign(4096,{&many_triangles});rejects([&]{renderer.upload_batches(excessive);});
    // Five distinct sampler variants would allocate 320 MiB. The aggregate
    // budget must reject this before any large image reaches the GPU.
    const dw4::RgbaImage large{4096,4096,std::vector<std::uint8_t>(64u*1024u*1024u,255)};
    excessive.assign(5,{&left,&large});
    excessive[1].texture_options.wrap_u=MeshTextureWrap::clamp_to_edge;
    excessive[2].texture_options.wrap_u=MeshTextureWrap::mirrored_repeat;
    excessive[3].texture_options.wrap_v=MeshTextureWrap::clamp_to_edge;
    excessive[4].texture_options.wrap_v=MeshTextureWrap::mirrored_repeat;
    rejects([&]{renderer.upload_batches(excessive);});
    caller.verify();renderer.clear();
}
}
int main() {
    Context context;
    try {
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        context.window = SDL_CreateWindow("DW4 native renderer validation", 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(context.window != nullptr, SDL_GetError());
        context.context = SDL_GL_CreateContext(context.window);
        check(context.context != nullptr, SDL_GetError());
        std::cout << "GPU: " << glGetString(GL_RENDERER) << '\n';
        {
            dw4::viewer::MeshRenderer renderer;
            dw4::OrbitCamera camera;
            dw4::MeshData tetra;
            tetra.positions = {{1,1,1}, {-1,-1,1}, {-1,1,-1}, {1,-1,-1}};
            tetra.indices = {0,1,2, 0,3,1, 0,2,3, 1,3,2};
            glViewport(7, 9, 71, 53);
            glEnable(GL_CULL_FACE); glEnable(GL_BLEND); glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, 1, 1); glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glClearDepth(0.4);
            saved_state();
            renderer.upload(tetra); saved_state();
            check(renderer.has_mesh() && renderer.triangle_count() == 4, "Tetrahedron upload lost geometry");
            const auto texture = renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::solid);
            saved_state();
            const auto solid = pixels(texture, 128, 128);
            const auto solid_coverage = coverage(solid);
            check(solid_coverage > 500 && solid_coverage < 12000, "Solid triangles did not produce a fitted visible shape");
            camera.orbit(0.8f, -0.35f);
            const auto orbited = pixels(renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::solid), 128, 128);
            check(orbited != solid && coverage(orbited) > 200, "Camera orbit did not change rendered geometry");
            camera.reset();
            const auto wire = pixels(renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::wireframe), 128, 128);
            check(coverage(wire) > 40 && coverage(wire) < solid_coverage, "Wireframe coverage is not distinct from solid rendering");
            GLint maximum_samples = 0; glGetIntegerv(GL_MAX_SAMPLES, &maximum_samples);
            const auto target = renderer.render(96, 80, camera, dw4::viewer::MeshDrawMode::solid, 8);
            const auto minimum_samples = maximum_samples >= 8 ? 8 : maximum_samples >= 4 ? 4 : maximum_samples >= 2 ? 2 : 0;
            check(renderer.samples() >= minimum_samples && renderer.samples() <= maximum_samples,
                "Offscreen MSAA is outside the supported request range");
            check(coverage(pixels(target, 96, 80)) > 200, "Resized/MSAA-resolved target is blank");
            check(renderer.render(96, 80, camera, dw4::viewer::MeshDrawMode::solid, 8) == target,
                "Unchanged framebuffer request should reuse its color texture");
            saved_state();
            check(renderer.rendered_frames() == 5, "Successful render frame counter is incorrect");
            rejects([&] { renderer.render(4097, 128, camera, dw4::viewer::MeshDrawMode::solid); });
            rejects([&] { renderer.render(4096, 4096, camera, dw4::viewer::MeshDrawMode::solid); });
            auto malformed = tetra; malformed.indices[0] = 99;
            rejects([&] { renderer.upload(malformed); });
            check(!renderer.has_mesh(), "Failed upload retained stale geometry");
            malformed = tetra; malformed.positions[0].x = std::numeric_limits<float>::infinity();
            rejects([&] { renderer.upload(malformed); });
            texture_checks(renderer, tetra, camera);
            pose_checks(renderer, tetra, camera);
            scene_checks(renderer);
            batch_pose_checks(renderer);
            focus_checks(renderer);
            renderer.upload(tetra); renderer.clear();
            check(!renderer.has_mesh() && renderer.triangle_count() == 0, "Clear retained geometry");
            rejects([&] { renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::solid); });
            renderer.shutdown(); renderer.shutdown();
            std::cout << "Renderer GPU checks passed: coverage, orbit, wireframe, resize, MSAA, textured UV/color/filter/wrap/alpha, stable pose updates, shared scene batches/depth/materials/normalization/budgets, isolated actor-batch poses and exact reset, source-bounds camera focus, state restoration, malformed upload and cleanup\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Renderer test failure: " << error.what() << '\n';
        return 1;
    }
}
