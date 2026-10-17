/** @file main.cpp
 *  @brief 真实下游程序：只链接导出目标，编译公共头并使用解析器和线程符号。
 */
#include <softrenderer/version.h>

#include <atomic>
#include <entt/entt.hpp>
#include <json.hpp>

#include "core/command_buffer.h"
#include "core/image_write.h"
#include "raster/msaa_renderer.h"
#include "scene/animation_player.h"
#include "scene/gltf_loader.h"
#include "scene/obj_loader.h"

static_assert(SOFTRENDERER_VERSION_MAJOR == 1);
static_assert(SOFTRENDERER_VERSION_MINOR >= 0);
static_assert(SOFTRENDERER_VERSION_PATCH >= 0);

int main()
{
    sr::MSAAFlatRenderer<4> msaa(1, 1);
    msaa.clear(colors::black);
    msaa.rasterize({-2, -2}, {4, -2}, {-2, 4}, 0.1f, 0.2f, 0.3f, colors::red);
    Framebuffer resolved(1, 1);
    msaa.resolveTo(resolved);
    if (resolved.getPixel(0, 0).r != 255)
        return 6;
    entt::registry registry;
    const auto entity = registry.create();
    std::atomic<int> completed{0};
    sr::ThreadPool pool(2);
    pool.submit([&completed] { ++completed; });
    pool.waitAll();
    auto answer = pool.submitFuture([] { return 42; });
    if (answer.get() != 42)
        return 4;

    tinygltf::Model model;
    if (sr::gltf::AccessorView::create(model, 0))
        return 5;
    tinygltf::TinyGLTF loader;
    std::string error, warning;
    const std::string input = R"({"asset":{"version":"2.0"}})";
    if (!loader.LoadASCIIFromString(&model, &error, &warning, input.data(), static_cast<unsigned int>(input.size()),
                                    ""))
        return 1;
    tinyobj::ObjReader obj;
    if (!obj.ParseFromString("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", ""))
        return 2;
    const auto json = nlohmann::json::parse(input);
    return completed == 1 && registry.valid(entity) && json.contains("asset") ? 0 : 3;
}
