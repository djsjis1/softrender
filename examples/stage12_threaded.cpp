// ============================================================
// 阶段 12：多线程并行光栅化
//
// 生成 output/stage12_threaded.tga：
//   用 std::thread 把屏幕分成 N 条水平带，每条带一个线程
//   独立光栅化（各自拥有局部深度缓冲），最后合并深度。
//
// 同时输出单线程耗时 vs 多线程耗时，直观感受并行加速。
//
// 原理：光栅化是"embarrassingly parallel"的——每个像素的
// 着色互不依赖。唯一的竞争点是深度缓冲：两个线程可能同时
// 写同一像素的深度。解法：每条带维护自己的局部深度缓冲，
// 渲染完后按像素取深度最小者合并（类似 GPU 的 tile-based 合并）。
// ============================================================
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>
#include "core/framebuffer.h"
#include "core/tga.h"
#include "math/transform.h"
#include "raster/triangle.h"
#include "scene/mesh.h"
#include "shader/shaders.h"

// 一条水平带的局部缓冲
struct StripBuffer
{
    int W, yStart, yEnd; // [yStart, yEnd)
    std::vector<Color> color;
    std::vector<float> depth;

    StripBuffer(int w, int ys, int ye)
        : W(w), yStart(ys), yEnd(ye),
          color(size_t(w) * (ye - ys), Color(30, 34, 44)),
          depth(size_t(w) * (ye - ys), 1e9f) {}

    bool testAndSet(int x, int y, float z)
    {
        if (x < 0 || x >= W || y < yStart || y >= yEnd)
            return false;
        float &slot = depth[size_t(y - yStart) * W + x];
        if (z < slot)
        {
            slot = z;
            return true;
        }
        return false;
    }
    void setColor(int x, int y, Color c)
    {
        if (x < 0 || x >= W || y < yStart || y >= yEnd)
            return;
        color[size_t(y - yStart) * W + x] = c;
    }
};

// 渲染一条带
static void renderStrip(StripBuffer &strip, const std::vector<std::pair<Mesh, Mat4>> &objects,
                        const Mat4 &view, const Mat4 &proj, int W, int H)
{
    PhongShader shader;
    shader.view = view;
    shader.proj = proj;
    shader.lightDir = Vec3f{0.4f, 0.8f, 0.5f}.normalized();
    shader.ambient = {0.15f, 0.15f, 0.18f};
    shader.cameraPos = {0, 3, 8};
    Mat4 vp = transform::viewport(W, H);

    for (auto &[mesh, model] : objects)
    {
        shader.model = model;
        shader.mvp = proj * view * model;
        shader.normalMatrix = model.inversed().transposed();
        shader.baseColor = {0.7f, 0.5f, 0.3f};
        shader.texture = nullptr;

        for (int i = 0; i < mesh.triangleCount(); ++i)
        {
            Vec4f c0 = shader.mvp * Vec4f(mesh.vertices[mesh.indices[i * 3 + 0]].position, 1);
            Vec4f c1 = shader.mvp * Vec4f(mesh.vertices[mesh.indices[i * 3 + 1]].position, 1);
            Vec4f c2 = shader.mvp * Vec4f(mesh.vertices[mesh.indices[i * 3 + 2]].position, 1);
            if (c0.w <= 0 || c1.w <= 0 || c2.w <= 0)
                continue;

            float iw0 = 1 / c0.w, iw1 = 1 / c1.w, iw2 = 1 / c2.w;
            Vec3f s0 = vp.transformPoint({c0.x * iw0, c0.y * iw0, c0.z * iw0});
            Vec3f s1 = vp.transformPoint({c1.x * iw1, c1.y * iw1, c1.z * iw1});
            Vec3f s2 = vp.transformPoint({c2.x * iw2, c2.y * iw2, c2.z * iw2});
            Vec2f p0{s0.x, s0.y}, p1{s1.x, s1.y}, p2{s2.x, s2.y};
            if (cross2(p1 - p0, p2 - p0) <= 0)
                continue;

            // 法线光照
            Vec3f wn = shader.normalMatrix.transformDir(
                                              mesh.vertices[mesh.indices[i * 3 + 0]].normal)
                           .normalized();
            float lit = std::max(0.15f, dot(wn, shader.lightDir));
            Color col = Color::fromFloat(0.7f * lit, 0.5f * lit, 0.3f * lit);

            int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
            int maxX = std::min(W - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
            int minY = std::max(strip.yStart, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
            int maxY = std::min(strip.yEnd - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));

            for (int y = minY; y <= maxY; ++y)
                for (int x = minX; x <= maxX; ++x)
                {
                    Vec3f bc = barycentric(p0, p1, p2, {x + 0.5f, y + 0.5f});
                    if (bc.x < 0 || bc.y < 0 || bc.z < 0)
                        continue;
                    float z = bc.x * s0.z + bc.y * s1.z + bc.z * s2.z;
                    if (!strip.testAndSet(x, y, z))
                        continue;
                    strip.setColor(x, y, col);
                }
        }
    }
}

int main()
{
    const int W = 800, H = 600;
    const int numThreads = std::max(1u, std::thread::hardware_concurrency());

    // 场景：一堆球体
    std::vector<std::pair<Mesh, Mat4>> objects;
    Mesh sphere = meshgen::sphere(0.6f, 32, 24);
    for (int i = 0; i < 20; ++i)
    {
        float x = (i % 5 - 2) * 2.0f;
        float y = (i / 5 - 2) * 1.8f;
        float z = -i * 0.3f;
        objects.push_back({sphere, Mat4::translate(x, y, z)});
    }

    Mat4 view = transform::lookAt({0, 3, 8}, {0, 0, 0}, {0, 1, 0});
    Mat4 proj = transform::perspective(3.14159f / 3.0f, float(W) / H, 0.1f, 100.0f);

    // ---- 多线程渲染 ----
    auto t0 = std::chrono::high_resolution_clock::now();

    int stripH = (H + numThreads - 1) / numThreads;
    std::vector<StripBuffer> strips;
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t)
    {
        int ys = t * stripH;
        int ye = std::min(H, ys + stripH);
        if (ys >= H)
            break;
        strips.emplace_back(W, ys, ye);
    }
    for (auto &strip : strips)
        threads.emplace_back(renderStrip, std::ref(strip), std::cref(objects),
                             std::cref(view), std::cref(proj), W, H);
    for (auto &th : threads)
        th.join();

    auto t1 = std::chrono::high_resolution_clock::now();
    float msMulti = std::chrono::duration<float, std::milli>(t1 - t0).count();

    // 合并 strips 到最终 framebuffer
    Framebuffer fb(W, H, Color(30, 34, 44));
    for (auto &strip : strips)
    {
        for (int y = strip.yStart; y < strip.yEnd; ++y)
            for (int x = 0; x < W; ++x)
            {
                size_t idx = size_t(y - strip.yStart) * W + x;
                if (strip.depth[idx] < 1e8f)
                    fb.setPixel(x, y, strip.color[idx]);
            }
    }

    // ---- 单线程对比 ----
    auto t2 = std::chrono::high_resolution_clock::now();
    StripBuffer single(W, 0, H);
    renderStrip(single, objects, view, proj, W, H);
    auto t3 = std::chrono::high_resolution_clock::now();
    float msSingle = std::chrono::duration<float, std::milli>(t3 - t2).count();

    std::filesystem::create_directories("output");
    tga::write("output/stage12_threaded.tga", fb);
    std::printf("已生成 output/stage12_threaded.tga\n");
    std::printf("  线程数: %d\n", numThreads);
    std::printf("  单线程: %.1f ms\n", msSingle);
    std::printf("  多线程: %.1f ms\n", msMulti);
    std::printf("  加速比: %.2fx\n", msSingle / msMulti);
    return 0;
}
