/** @file main.cpp
 *  @brief 真实下游程序：只用 sr 公共接口验证聚合用法。
 *
 *  刻意不包含任何第三方解析器类型（tinygltf / tinyobj / stbi / entt / json），
 *  证明公共头对下游是自洽的；加载失败路径同时验证 io 组件的链接与错误文本。
 */
#include <softrenderer/version.h>

#include <atomic>
#include <string>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/image.h"
#include "softrenderer/io/gltf_loader.h"
#include "softrenderer/io/image_write.h"
#include "softrenderer/io/obj_loader.h"
#include "softrenderer/math/barycentric.h"
#include "softrenderer/render/draw2d/renderer2d.h"
#include "softrenderer/render/pipeline/msaa_renderer.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/runtime/command_buffer.h"
#include "softrenderer/runtime/resource_cache.h"
#include "softrenderer/runtime/thread_pool.h"
#include "softrenderer/scene/animation_player.h"

using namespace sr;

static_assert(SOFTRENDERER_VERSION_MAJOR == 2);
static_assert(SOFTRENDERER_VERSION_MINOR >= 0);
static_assert(SOFTRENDERER_VERSION_PATCH >= 0);

int main()
{
    // ---- render：MSAA 解析 ----
    sr::MSAAFlatRenderer<4> msaa(1, 1);
    msaa.clear(sr::colors::black);
    msaa.rasterize({-2, -2}, {4, -2}, {-2, 4}, 0.1f, 0.2f, 0.3f, sr::colors::red);
    Framebuffer resolved(1, 1);
    msaa.resolveTo(resolved);
    if (resolved.getPixel(0, 0).r != 255)
        return 6;

    // ---- core：无文件 I/O 的 Image → Texture → 绘制 ----
    sr::Image image;
    image.width = 2;
    image.height = 2;
    image.pixels.assign(4, Color(10, 20, 30, 255));
    Texture texture(std::move(image));
    if (!texture.valid())
        return 7;
    Framebuffer canvas(8, 8);
    Renderer2D hud;
    hud.begin(canvas, sr::colors::black);
    hud.setColor(sr::colors::white);
    hud.fillRect(1, 1, 4, 4);
    if (canvas.getPixel(2, 2).r != 255)
        return 8;

    // ---- math：重心坐标 ----
    const Vec3f weights = sr::barycentric({0, 0}, {1, 0}, {0, 1}, {0.25f, 0.25f});
    if (std::abs(weights.x - 0.5f) > 1e-5f || std::abs(weights.y - 0.25f) > 1e-5f)
        return 9;

    // ---- scene：已解码动画片段采样 ----
    sr::AnimationClip clip;
    sr::AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {{0, 0, 0, 0}, {1, 2, 3, 0}};
    clip.samplers.push_back(sampler);
    sr::AnimationChannel channel;
    channel.samplerIndex = 0;
    channel.targetNode = 0;
    clip.channels.push_back(channel);
    std::vector<sr::SceneNode> nodes(1);
    const auto poses = sr::animation::sampleAnimation(nodes, clip, 0.5f);
    if (poses.size() != 1 || std::abs(poses[0].translation.y - 1.0f) > 1e-5f)
        return 10;

    // ---- io：公共加载接口的失败路径（不发布半成品） ----
    const auto missing = sr::io::loadGltfScene("does-not-exist.gltf");
    if (missing.success || missing.error.empty())
        return 11;
    Mesh unused;
    if (sr::io::loadObjMesh("does-not-exist.obj", unused))
        return 12;

    // ---- runtime：缓存 + 线程池 ----
    auto cached = sr::ResourceCache::instance().load<Texture>("canvas", [] {
        sr::Image pixels;
        pixels.width = 1;
        pixels.height = 1;
        pixels.pixels.assign(1, Color(1, 2, 3, 255));
        return std::make_shared<Texture>(std::move(pixels));
    });
    if (!cached || sr::ResourceCache::instance().size() != 1u)
        return 5;
    std::atomic<int> completed{0};
    sr::ThreadPool pool(2);
    pool.submit([&completed] { ++completed; });
    pool.waitAll();
    auto answer = pool.submitFuture([] { return 42; });
    if (answer.get() != 42)
        return 4;
    return completed == 1 ? 0 : 3;
}
