/** @file namespace_sentinel.cpp
 *  @brief 命名空间冲突哨兵：先占用一批"下游很可能已经定义过"的通用名，
 *         再包含**全部**公共头。
 *
 *  本文件是 ADR-004「公共类统一归 sr」的编译期门禁：
 *    - 若库把任何一个类型放回全局命名空间，下面的同名声明会直接编译失败；
 *    - 若有人用 `using sr::X;` 别名桥把名字重新注入全局，同样失败。
 *
 *  本文件不产生运行期行为，只为编译关卡（arches：namespace_sentinel OBJECT 库）。
 */
#include <softrenderer/version.h>

// ------------------------------------------------------------
// 全局作用域：这些名字必须归使用者所有
// ------------------------------------------------------------
struct Color
{
    int canary = 0;
};
struct Image
{
    int canary = 0;
};
struct Framebuffer
{
    int canary = 0;
};
struct DepthBuffer
{
    int canary = 0;
};
struct FloatFramebuffer
{
    int canary = 0;
};
struct MSAAFramebuffer
{
    int canary = 0;
};
struct GBuffer
{
    int canary = 0;
};
struct BlendMode
{
    int canary = 0;
};
struct Mesh
{
    int canary = 0;
};
struct MeshGenerator
{
    int canary = 0;
};
struct Texture
{
    int canary = 0;
};
struct Cubemap
{
    int canary = 0;
};
struct Vertex
{
    int canary = 0;
};
struct Varyings
{
    int canary = 0;
};
struct Shader
{
    int canary = 0;
};
struct SurfaceShader
{
    int canary = 0;
};
struct Renderer
{
    int canary = 0;
};
struct Renderer2D
{
    int canary = 0;
};
struct RenderQueue
{
    int canary = 0;
};
struct RenderLayer
{
    int canary = 0;
};
struct Camera
{
    int canary = 0;
};
struct Camera2D
{
    int canary = 0;
};
struct Material
{
    int canary = 0;
};
struct SceneNode
{
    int canary = 0;
};
struct SceneAsset
{
    int canary = 0;
};
struct Skin
{
    int canary = 0;
};
struct Skeleton
{
    int canary = 0;
};
struct Bone
{
    int canary = 0;
};
struct Particle
{
    int canary = 0;
};
struct ParticleSystem
{
    int canary = 0;
};
struct Animation
{
    int canary = 0;
};
struct AnimationChannel
{
    int canary = 0;
};
struct AnimationClip
{
    int canary = 0;
};
struct AnimationSampler
{
    int canary = 0;
};
struct Keyframe
{
    int canary = 0;
};
struct NodePose
{
    int canary = 0;
};
struct Sampler
{
    int canary = 0;
};
struct Position
{
    int canary = 0;
};
struct Velocity
{
    int canary = 0;
};
struct Light
{
    int canary = 0;
};
struct CameraTag
{
    int canary = 0;
};
struct Rect
{
    int canary = 0;
};
struct Quat
{
    int canary = 0;
};
struct Mat3
{
    int canary = 0;
};
struct Mat4
{
    int canary = 0;
};
struct Vec3f
{
    int canary = 0;
};
struct Vec2f
{
    int canary = 0;
};
struct Log
{
    int canary = 0;
};
struct LogLevel
{
    int canary = 0;
};
struct ThreadPool
{
    int canary = 0;
};
struct CommandBuffer
{
    int canary = 0;
};
struct ResourceCache
{
    int canary = 0;
};
struct Profiler
{
    int canary = 0;
};
struct ArenaAllocator
{
    int canary = 0;
};
struct ClipVertex
{
    int canary = 0;
};
struct TriangleCoverage
{
    int canary = 0;
};
struct SceneLoadResult
{
    int canary = 0;
};

// ------------------------------------------------------------
// 再包含全部公共头：库内同名符号必须位于 sr::（或其子命名空间）
// ------------------------------------------------------------
#include <softrenderer/core/arena.h>
#include <softrenderer/core/blend.h>
#include <softrenderer/core/color.h>
#include <softrenderer/core/depthbuffer.h>
#include <softrenderer/core/float_framebuffer.h>
#include <softrenderer/core/framebuffer.h>
#include <softrenderer/core/gbuffer.h>
#include <softrenderer/core/image.h>
#include <softrenderer/core/msaa_framebuffer.h>
#include <softrenderer/core/namespace.h>
#include <softrenderer/io/gltf_loader.h>
#include <softrenderer/io/image_loader.h>
#include <softrenderer/io/image_write.h>
#include <softrenderer/io/obj_loader.h>
#include <softrenderer/io/tga.h>
#include <softrenderer/math/barycentric.h>
#include <softrenderer/math/mat3.h>
#include <softrenderer/math/mat4.h>
#include <softrenderer/math/quat.h>
#include <softrenderer/math/rect.h>
#include <softrenderer/math/transform.h>
#include <softrenderer/math/transform2d.h>
#include <softrenderer/math/vec.h>
#include <softrenderer/render/draw2d/blit2d.h>
#include <softrenderer/render/draw2d/renderer2d.h>
#include <softrenderer/render/draw2d/shape2d.h>
#include <softrenderer/render/pipeline/clip.h>
#include <softrenderer/render/pipeline/msaa_renderer.h>
#include <softrenderer/render/pipeline/render_queue.h>
#include <softrenderer/render/pipeline/renderer.h>
#include <softrenderer/render/pipeline/shader.h>
#include <softrenderer/render/pipeline/surface_shader.h>
#include <softrenderer/render/pipeline/varyings.h>
#include <softrenderer/render/postprocess/post_processing.h>
#include <softrenderer/render/raster/coverage.h>
#include <softrenderer/render/raster/rasterizer_fast.h>
#include <softrenderer/render/raster/triangle.h>
#include <softrenderer/render/shader/advanced_shaders.h>
#include <softrenderer/render/shader/deferred_lighting.h>
#include <softrenderer/render/shader/pbr_shader.h>
#include <softrenderer/render/shader/shaders.h>
#include <softrenderer/resource/cubemap.h>
#include <softrenderer/resource/mesh.h>
#include <softrenderer/resource/mesh_generator.h>
#include <softrenderer/resource/texture.h>
#include <softrenderer/resource/texture_generator.h>
#include <softrenderer/resource/vertex.h>
#include <softrenderer/runtime/command_buffer.h>
#include <softrenderer/runtime/log.h>
#include <softrenderer/runtime/profiler.h>
#include <softrenderer/runtime/resource_cache.h>
#include <softrenderer/runtime/thread_pool.h>
#include <softrenderer/scene/animation_clip.h>
#include <softrenderer/scene/animation_player.h>
#include <softrenderer/scene/camera.h>
#include <softrenderer/scene/camera2d.h>
#include <softrenderer/scene/components.h>
#include <softrenderer/scene/keyframe_animation.h>
#include <softrenderer/scene/material.h>
#include <softrenderer/scene/particle_system.h>
#include <softrenderer/scene/scene_asset.h>
#include <softrenderer/scene/scene_node.h>
#include <softrenderer/scene/skeleton.h>
#include <softrenderer/scene/skin.h>

// 库内符号确实位于 sr::（而非全局）：此行不成立即编译失败。
static_assert(sizeof(sr::Framebuffer) > 0, "Framebuffer must live in sr");
static_assert(sizeof(sr::Mesh) > 0, "Mesh must live in sr");
static_assert(sizeof(sr::Texture) > 0, "Texture must live in sr");

int sr_namespace_sentinel_anchor = 0;
