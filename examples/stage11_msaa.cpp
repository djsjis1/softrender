// ============================================================
// 阶段 11：MSAA 多重采样抗锯齿
//
// 【源码阅读】先读这些文件：
//   core/msaa_framebuffer.h  — MSAA 帧缓冲：每像素 N 个样本槽
//   raster/msaa_renderer.h   — MSAAFlatRenderer：逐样本覆盖+深度
//   raster/coverage.h        — 子像素覆盖规则
//
// 【运行验证】生成 output/stage11_msaa.tga（左右对比）：
//   左：无抗锯齿 vs 右：4x MSAA（斜线平滑）
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/msaa_framebuffer.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/render/pipeline/msaa_renderer.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/mesh_generator.h"

// 本库公共符号正逐步迁入 namespace sr（见 docs/adr/ADR-001）。
using namespace sr;

int main()
{
    const int halfW = 400, H = 600;
    Framebuffer fb(halfW * 2, H, Color(30, 30, 40));

    // 场景：旋转的立方体 + 球（斜线多，锯齿明显）
    const float PI = 3.14159265f;
    Mesh cube = sr::meshgen::cube(1.4f);
    Mesh sphere = sr::meshgen::sphere(0.8f, 24, 16);

    Mat4 modelCube = Mat4::translate(-0.8f, 0, 0) * Mat4::rotateY(0.7f) * Mat4::rotateX(0.4f);
    Mat4 modelSphere = Mat4::translate(0.9f, 0, 0);
    Mat4 view = sr::transform::lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
    Mat4 proj = sr::transform::perspective(PI / 4.0f, float(halfW) / H, 0.1f, 100.0f);

    auto project = [&](const Vec3f &p, const Mat4 &mvp) -> std::pair<Vec2f, float> {
        Vec4f clip = mvp * Vec4f(p, 1.0f);
        if (clip.w <= 0)
            return {{-999, -999}, 1e9f};
        Vec3f ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
        float sx = (ndc.x * 0.5f + 0.5f) * halfW;
        float sy = (ndc.y * 0.5f + 0.5f) * H;
        float sz = ndc.z * 0.5f + 0.5f;
        return {{sx, sy}, sz};
    };

    auto renderScene = [&](auto &renderer, int offsetX) {
        renderer.clear(Color(30, 30, 40));
        Mat4 mvpC = proj * view * modelCube;
        Mat4 mvpS = proj * view * modelSphere;

        // 画立方体（每面一个颜色）
        Color cubeColors[6] = {Color(220, 80, 80),  Color(80, 220, 80),  Color(80, 80, 220),
                               Color(220, 220, 80), Color(220, 80, 220), Color(80, 220, 220)};
        for (int i = 0; i < cube.triangleCount(); ++i)
        {
            int face = i / 2;
            auto [s0, z0] = project(cube.vertices[cube.indices[i * 3 + 0]].position, mvpC);
            auto [s1, z1] = project(cube.vertices[cube.indices[i * 3 + 1]].position, mvpC);
            auto [s2, z2] = project(cube.vertices[cube.indices[i * 3 + 2]].position, mvpC);
            s0.x += offsetX;
            s1.x += offsetX;
            s2.x += offsetX;
            renderer.rasterize(s0, s1, s2, z0, z1, z2, cubeColors[face]);
        }
        // 画球
        for (int i = 0; i < sphere.triangleCount(); ++i)
        {
            auto [s0, z0] = project(sphere.vertices[sphere.indices[i * 3 + 0]].position, mvpS);
            auto [s1, z1] = project(sphere.vertices[sphere.indices[i * 3 + 1]].position, mvpS);
            auto [s2, z2] = project(sphere.vertices[sphere.indices[i * 3 + 2]].position, mvpS);
            s0.x += offsetX;
            s1.x += offsetX;
            s2.x += offsetX;
            // 简单法线着色
            Vec3f n = sphere.vertices[sphere.indices[i * 3 + 0]].normal;
            float lit = std::max(0.2f, dot(n.normalized(), Vec3f{0.5f, 0.7f, 0.5f}.normalized()));
            renderer.rasterize(s0, s1, s2, z0, z1, z2, Color::fromFloat(0.3f * lit, 0.6f * lit, 0.9f * lit));
        }
    };

    // 左：无 AA
    MSAAFlatRenderer<1> noAA(halfW, H);
    renderScene(noAA, 0);
    noAA.resolveTo(fb);

    // 右：4x MSAA
    MSAAFlatRenderer<4> msaa(halfW, H);
    renderScene(msaa, 0);
    // 把 MSAA 结果贴到右半屏
    Framebuffer tmpR(halfW, H);
    msaa.resolveTo(tmpR);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < halfW; ++x)
            fb.setPixel(x + halfW, y, tmpR.getPixel(x, y));

    // 分隔线
    for (int y = 0; y < H; ++y)
        fb.setPixel(halfW, y, Color(255, 200, 0));

    std::filesystem::create_directories("output");
    sr::io::writeTga("output/stage11_msaa.tga", fb);
    std::printf("已生成 output/stage11_msaa.tga（左=无AA，右=4xMSAA）\n");
    return 0;
}
