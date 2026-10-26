// ============================================================
// 裸机零堆证明（smoke）
//
// 【验证什么】ADR-007 的裸机承诺：core+render 的整帧渲染路径
// （外部存储 Framebuffer + 外部深度 + 外部顶点缓存 + drawMesh）
// 不做任何堆分配。方法：全局 operator new 熔断守卫——守卫开启期间
// 任何分配请求立即置违规计数并抛 bad_alloc，由 main 捕获判定失败。
//
// 两个阶段：① 裸 Renderer（drawMesh 级）；② ForwardPipeline 稳态
// （验证门面声明的“渲染调用路径零分配”确实成立，而不是只写在注释里）。
//
// 【边界】Mesh 用 std::vector 存顶点，构建期必然分配——构建在守卫
// 开启前完成；被证明的是**渲染阶段**零分配（裸机场景网格来自静态
// 数据，等价成立）。本程序只链 SoftRenderer::core + SoftRenderer::render，
// 兼作组件边界验证。
// ============================================================
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/render/pipeline/clip.h"
#include "softrenderer/render/pipeline/forward_pipeline.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/mesh_generator.h"

namespace
{
std::atomic<bool> g_heapForbidden{false};
std::atomic<int> g_violations{0};
} // namespace

// 全量替换抛异常形态（含数组与 sized-delete）；nothrow/aligned 形态保留库默认。
// 渲染路径中的类型（Vec3f/Color/ClipVertex/Mat4）均为普通对齐，不会走 aligned new。
void* operator new(std::size_t size)
{
    if (g_heapForbidden.load(std::memory_order_relaxed))
    {
        ++g_violations;
        throw std::bad_alloc();
    }
    if (size == 0)
        ++size;
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    constexpr int W = 64, H = 48;
    // 全部渲染目标放在静态存储（裸机上等价于指定 RAM 段）
    static sr::Color g_pixels[size_t(W) * size_t(H)];
    static float g_depth[size_t(W) * size_t(H)];
    constexpr int kMaxVerts = 2048;
    static sr::ClipVertex g_vcache[kMaxVerts];
    static uint32_t g_vstamps[kMaxVerts];

    // 网格构建（允许分配）：守卫开启前完成
    const sr::Mesh mesh = sr::meshgen::sphere(1.0f, 24, 16);
    const int vertexCount = int(mesh.vertices.size());
    if (vertexCount > kMaxVerts)
    {
        std::printf("[FAIL] 测试网格顶点数 %d 超出静态缓存 %d\n", vertexCount, kMaxVerts);
        return 1;
    }

    sr::BasicFramebuffer<sr::Color> fb(W, H, g_pixels);
    sr::Renderer renderer(W, H, g_depth);
    renderer.setExternalVertexCache(g_vcache, g_vstamps, kMaxVerts);

    sr::FlatShader shader;
    shader.model = sr::Mat4::rotateY(0.37f) * sr::Mat4::rotateX(0.21f);
    shader.mvp = sr::transform::perspective(0.9f, float(W) / float(H), 0.1f, 100.0f) *
                 sr::transform::lookAt(sr::Vec3f{0, 0, 3}, {0, 0, 0}, {0, 1, 0}) * shader.model;
    shader.cameraPos = {0, 0, 3};

    // 预热一帧（守卫关闭）：任何惰性初始化留在这里
    renderer.clear(fb, sr::colors::black);
    renderer.drawMesh(fb, mesh, shader);

    // ---- 守卫开启：连续两帧完整渲染（第二次覆盖跨 drawMesh 的 epoch 递增路径）----
    g_heapForbidden.store(true, std::memory_order_relaxed);
    bool badAlloc = false;
    try
    {
        for (int frame = 0; frame < 2; ++frame)
        {
            renderer.clear(fb, sr::colors::black);
            renderer.drawMesh(fb, mesh, shader);
        }
    }
    catch (const std::bad_alloc&)
    {
        badAlloc = true;
    }
    g_heapForbidden.store(false, std::memory_order_relaxed);

    // ---- 阶段二：ForwardPipeline 稳态零分配 ----
    // 门面的承诺是「渲染调用路径稳态零分配」，不包含构造期与首次渲染的惰性
    // 分配（Renderer 对象、队列/顶点缓存容量）。这里先把那些跑一遍，再开守卫。
    sr::ForwardPipeline pipeline(W, H);
    pipeline.clearColor = sr::colors::black;
    sr::ForwardDraw draw;
    draw.mesh = &mesh;
    draw.shader = &shader;
    pipeline.submit(draw);

    pipeline.render(fb);  // 预热：draws_/queue_/positions_/顶点缓存的首次分配在此

    g_heapForbidden.store(true, std::memory_order_relaxed);
    try
    {
        for (int frame = 0; frame < 2; ++frame)
            pipeline.render(fb);
    }
    catch (const std::bad_alloc&)
    {
        badAlloc = true;
    }
    g_heapForbidden.store(false, std::memory_order_relaxed);

    // 渲染确实发生：画面中必须有非黑像素（否则“零分配”可能只是什么都没画）
    long lit = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (fb.getPixel(x, y).r + fb.getPixel(x, y).g + fb.getPixel(x, y).b > 0)
                ++lit;

    const int violations = g_violations.load(std::memory_order_relaxed);
    if (badAlloc || violations > 0 || lit < 100)
    {
        std::printf("[FAIL] baremetal_smoke: badAlloc=%d violations=%d litPixels=%ld\n", int(badAlloc),
                    violations, lit);
        return 1;
    }
    std::printf("[OK ] baremetal_smoke: %dx%d 两帧零堆分配，着色像素 %ld，顶点 %d\n", W, H, lit,
                vertexCount);
    return 0;
}
