// ============================================================
// 阶段 11：后处理管线（Bloom + ACES 色调映射）
//
// 生成 output/stage11_postprocess.tga（左右对比）：
//   左半屏：HDR 渲染 → 直接 clamp 到 [0,1]（无后处理）
//   右半屏：HDR 渲染 → Bloom（亮部提取+高斯模糊+合成）→ ACES 色调映射
//
// 场景：暗环境 + 几个高亮自发光球体，Bloom 让它们产生光晕。
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "core/float_framebuffer.h"
#include "core/framebuffer.h"
#include "core/tga.h"
#include "math/transform.h"
#include "raster/triangle.h"
#include "scene/mesh.h"

// 简易 HDR 渲染：画带自发光强度的球
static void renderHDRScene(FloatFramebuffer &hdrFB, int W, int H)
{
    hdrFB.clear(Vec3f{0.01f, 0.01f, 0.015f}); // 暗背景

    const float PI = 3.14159265f;
    Mat4 view = transform::lookAt({0, 0, 6}, {0, 0, 0}, {0, 1, 0});
    Mat4 proj = transform::perspective(PI / 4.0f, float(W) / H, 0.1f, 100.0f);
    Mat4 vp = transform::viewport(W, H);

    // 5 个球，不同颜色和自发光强度
    struct Emissive
    {
        Vec3f pos;
        Vec3f color;
        float intensity;
        float radius;
    };
    Emissive balls[] = {
        {{-2.0f, 0, 0}, {1, 0.3f, 0.1f}, 8.0f, 0.6f},
        {{-0.7f, 1.2f, 0}, {0.2f, 1, 0.3f}, 5.0f, 0.5f},
        {{0.8f, -0.5f, 0}, {0.1f, 0.4f, 1}, 10.0f, 0.7f},
        {{2.2f, 0.8f, 0}, {1, 1, 0.2f}, 6.0f, 0.4f},
        {{0, 0, 1.5f}, {1, 0.8f, 0.6f}, 3.0f, 0.9f},
    };

    for (auto &ball : balls)
    {
        Mesh sphere = meshgen::sphere(ball.radius, 32, 20);
        Mat4 model = Mat4::translate(ball.pos.x, ball.pos.y, ball.pos.z);
        Mat4 mvp = proj * view * model;

        for (int i = 0; i < sphere.triangleCount(); ++i)
        {
            Vec4f c0 = mvp * Vec4f(sphere.vertices[sphere.indices[i * 3 + 0]].position, 1);
            Vec4f c1 = mvp * Vec4f(sphere.vertices[sphere.indices[i * 3 + 1]].position, 1);
            Vec4f c2 = mvp * Vec4f(sphere.vertices[sphere.indices[i * 3 + 2]].position, 1);
            if (c0.w <= 0 || c1.w <= 0 || c2.w <= 0)
                continue;

            float iw0 = 1 / c0.w, iw1 = 1 / c1.w, iw2 = 1 / c2.w;
            Vec3f s0 = vp.transformPoint({c0.x * iw0, c0.y * iw0, c0.z * iw0});
            Vec3f s1 = vp.transformPoint({c1.x * iw1, c1.y * iw1, c1.z * iw1});
            Vec3f s2 = vp.transformPoint({c2.x * iw2, c2.y * iw2, c2.z * iw2});

            Vec2f p0{s0.x, s0.y}, p1{s1.x, s1.y}, p2{s2.x, s2.y};
            float area2 = cross2(p1 - p0, p2 - p0);
            if (area2 <= 0)
                continue;

            // 简单法线光照 + 自发光
            Vec3f n = sphere.vertices[sphere.indices[i * 3 + 0]].normal;
            float lit = std::max(0.15f, dot(n.normalized(), Vec3f{0.3f, 0.7f, 0.5f}.normalized()));
            Vec3f color = ball.color * (lit * 0.3f + ball.intensity * 0.7f);

            int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
            int maxX = std::min(W - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
            int minY = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
            int maxY = std::min(H - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));

            for (int y = minY; y <= maxY; ++y)
                for (int x = minX; x <= maxX; ++x)
                {
                    Vec3f bc = barycentric(p0, p1, p2, {x + 0.5f, y + 0.5f});
                    if (bc.x < 0 || bc.y < 0 || bc.z < 0)
                        continue;
                    float z = bc.x * s0.z + bc.y * s1.z + bc.z * s2.z;
                    if (!hdrFB.depth().testAndSet(x, y, z))
                        continue;
                    hdrFB.setPixel(x, y, color);
                }
        }
    }
}

int main()
{
    const int W = 800, H = 600;

    // 渲染 HDR 场景
    FloatFramebuffer hdr(W, H);
    renderHDRScene(hdr, W, H);

    // ---- 左半屏：直接 clamp（无后处理）----
    Framebuffer left(W / 2, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W / 2; ++x)
        {
            Vec3f c = hdr.getPixel(x, y);
            left.setPixel(x, y, Color::fromFloat(std::min(c.x, 1.0f), std::min(c.y, 1.0f), std::min(c.z, 1.0f)));
        }

    // ---- 右半屏：Bloom + ACES ----
    // 1. 亮部提取
    FloatFramebuffer bright(W, H), blurH(W, H), blurV(W, H);
    hdr.brightPass(bright, 1.0f);
    // 2. 两遍高斯模糊（水平 + 垂直）
    bright.gaussianBlur(blurH, true, 12);
    blurH.gaussianBlur(blurV, false, 12);
    // 3. 合成：HDR + bloom
    FloatFramebuffer combined(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            combined.setPixel(x, y, hdr.getPixel(x, y) + blurV.getPixel(x, y) * 0.8f);

    // 4. ACES 色调映射 + gamma
    Framebuffer right(W / 2, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W / 2; ++x)
        {
            Vec3f c = combined.getPixel(x + W / 2, y);
            Vec3f mapped = FloatFramebuffer::gammaCorrect(FloatFramebuffer::acesToneMap(c));
            right.setPixel(x, y, Color::fromFloat(mapped.x, mapped.y, mapped.z));
        }

    // 合并
    Framebuffer fb(W, H);
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W / 2; ++x)
            fb.setPixel(x, y, left.getPixel(x, y));
        for (int x = 0; x < W / 2; ++x)
            fb.setPixel(x + W / 2, y, right.getPixel(x, y));
    }
    for (int y = 0; y < H; ++y)
        fb.setPixel(W / 2, y, Color(255, 200, 0));

    std::filesystem::create_directories("output");
    tga::write("output/stage11_postprocess.tga", fb);
    std::printf("已生成 output/stage11_postprocess.tga（左=clamp, 右=Bloom+ACES）\n");
    return 0;
}
