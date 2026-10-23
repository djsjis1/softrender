#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/mesh_generator.h"

using namespace sr;

namespace sr::benchmark {
// 固定输入与线程数，整帧计时包含 clear/draw，不包含模型创建和哈希。
template <class ShaderType>
void run(const char* name)
{
    constexpr int width = 640, height = 360, warmup = 5, frames = 31;
    const Mesh mesh = sr::meshgen::sphere(1.0f, 64, 32);
    Renderer renderer(width, height);
    Framebuffer fb(width, height);
    ShaderType shader;
    shader.model = Mat4::rotateY(0.37f) * Mat4::rotateX(0.21f);
    shader.cameraPos = {0, 0, 3};
    shader.mvp = sr::transform::perspective(0.9f, float(width) / height, 0.1f, 100.0f) *
                 sr::transform::lookAt(shader.cameraPos, {0, 0, 0}, {0, 1, 0}) * shader.model;
    auto frame = [&] {
        renderer.clear(fb, Color(20, 30, 40));
        renderer.drawMesh(fb, mesh, shader);
    };
    for (int i = 0; i < warmup; ++i)
        frame();
    std::vector<double> times;
    times.reserve(frames);
    for (int i = 0; i < frames; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        frame();
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(times.begin(), times.end());
    uint64_t hash = 14695981039346656037ull;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto color = fb.getPixel(x, y);
            for (auto channel : {color.r, color.g, color.b, color.a})
            {
                hash ^= channel;
                hash *= 1099511628211ull;
            }
        }
    std::printf("{\"scene\":\"%s\",\"width\":%d,\"height\":%d,\"threads\":1,\"triangles\":%d,"
                "\"warmup\":%d,\"frames\":%d,\"median_ms\":%.6f,\"p95_ms\":%.6f,\"min_ms\":%.6f,\"max_ms\":%.6f,"
                "\"rgba_fnv1a64\":\"%016llx\"}\n",
                name, width, height, mesh.triangleCount(), warmup, frames, times[frames / 2], times[29], times.front(),
                times.back(), static_cast<unsigned long long>(hash));
}
}  // namespace sr::benchmark
int main()
{
    // 基准只链接 render/core，不引入 runtime 的 Profiler：
    // 计时由 run() 内的 steady_clock 完成，Profiler 在这里本来也没有被激活。
    sr::benchmark::run<FlatShader>("sphere_flat");
    sr::benchmark::run<PhongShader>("sphere_phong");
}
