// ============================================================
// 阶段 06：深度缓冲 + 背面剔除
//
// 【源码阅读】先读这些文件：
//   core/depthbuffer.h     — DepthBuffer：每像素一个 float 深度
//   raster/renderer.h      — Renderer::drawMesh：深度测试 + 背面剔除
//   math/transform.h       — MVP 变换链
//   scene/mesh.h           — Mesh 数据结构
//   scene/camera.h         — Camera：lookAt + perspective
//
// 【运行验证】生成两张图对比：
//   output/stage06_depth.tga  — 正确遮挡的彩色场景
//   output/stage06_depth_zbuf.tga — 深度缓冲可视化（越亮越近）
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/render/raster/triangle.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/math/barycentric.h"

using namespace sr;

struct Scene
{
    Mesh mesh;
    Mat4 model;
    Color color;
};

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(24, 24, 32));
    DepthBuffer zbuf(W, H);

    const float PI = 3.14159265f;
    Mat4 view = sr::transform::lookAt({0, 1.2f, 4}, {0, 0, 0}, {0, 1, 0});
    Mat4 proj = sr::transform::perspective(PI / 4.0f, float(W) / H, 0.1f, 100.0f);
    Mat4 vp = sr::transform::viewport(W, H);

    // 平行光方向（指向光源），朗伯光照 = max(0, 法线·光向)
    Vec3f lightDir = Vec3f{0.5f, 0.8f, 0.6f}.normalized();

    // 场景：三个立方体 + 一个球，位置互相遮挡
    std::vector<Scene> scenes = {
        {sr::meshgen::cube(1.2f), Mat4::translate(-1.0f, 0, 0) * Mat4::rotateY(0.6f),
         Color(230, 100, 90)},
        {sr::meshgen::cube(1.2f), Mat4::translate(1.0f, 0, -0.5f) * Mat4::rotateY(-0.4f),
         Color(90, 170, 230)},
        {sr::meshgen::sphere(0.9f, 32, 24), Mat4::translate(0, 0.1f, 1.0f),
         Color(120, 220, 130)},
        {sr::meshgen::cube(0.6f), Mat4::translate(0.2f, 1.2f, 0.3f) * Mat4::rotateX(0.5f),
         Color(240, 210, 100)},
    };

    // 主渲染循环：遍历每个物体的每个三角形
    for (const auto &s : scenes)
    {
        // 法线变换用"模型矩阵逆转置"，这里模型矩阵含旋转无非等比缩放，
        // 逆转置即旋转本身；为通用起见仍按公式算
        Mat4 normalMat = s.model.inversed().transposed();
        Mat4 mvp = proj * view * s.model;

        for (int i = 0; i < s.mesh.triangleCount(); ++i)
        {
            const Vertex &V0 = s.mesh.vertices[s.mesh.indices[i * 3 + 0]];
            const Vertex &V1 = s.mesh.vertices[s.mesh.indices[i * 3 + 1]];
            const Vertex &V2 = s.mesh.vertices[s.mesh.indices[i * 3 + 2]];

            // 顶点 -> 裁剪空间
            Vec4f c0 = mvp * Vec4f(V0.position, 1.0f);
            Vec4f c1 = mvp * Vec4f(V1.position, 1.0f);
            Vec4f c2 = mvp * Vec4f(V2.position, 1.0f);

            // 简易近平面剔除：任一顶点在相机后方(w<=0)就跳过整个三角形
            // （正规的近平面裁剪留到阶段 7）
            if (c0.w <= 0 || c1.w <= 0 || c2.w <= 0)
                continue;

            // 透视除法 -> NDC
            Vec3f n0{c0.x / c0.w, c0.y / c0.w, c0.z / c0.w};
            Vec3f n1{c1.x / c1.w, c1.y / c1.w, c1.z / c1.w};
            Vec3f n2{c2.x / c2.w, c2.y / c2.w, c2.z / c2.w};

            // 视口变换 -> 屏幕坐标（含 [0,1] 深度）
            Vec3f s0 = vp.transformPoint(n0);
            Vec3f s1 = vp.transformPoint(n1);
            Vec3f s2 = vp.transformPoint(n2);

            // ---------- 背面剔除 ----------
            // 屏幕空间有向面积（2D 叉积）。我们的正面是逆时针，
            // 屏幕 y 向上时逆时针面积为正；<=0 视作背面丢弃
            Vec2f e1{s1.x - s0.x, s1.y - s0.y};
            Vec2f e2{s2.x - s0.x, s2.y - s0.y};
            float area2 = cross2(e1, e2);
            if (area2 <= 0)
                continue; // 背面或退化

            // 逐顶点朗伯光照（Flat 前的过渡：这里按顶点法线算再插值）
            Vec3f wn0 = normalMat.transformDir(V0.normal).normalized();
            Vec3f wn1 = normalMat.transformDir(V1.normal).normalized();
            Vec3f wn2 = normalMat.transformDir(V2.normal).normalized();
            float l0 = std::max(0.15f, dot(wn0, lightDir));
            float l1 = std::max(0.15f, dot(wn1, lightDir));
            float l2 = std::max(0.15f, dot(wn2, lightDir));

            // ---------- 光栅化 + 深度测试 ----------
            Vec2f p0{s0.x, s0.y}, p1{s1.x, s1.y}, p2{s2.x, s2.y};
            int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
            int maxX = std::min(W - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
            int minY = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
            int maxY = std::min(H - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    Vec3f bc = sr::barycentric(p0, p1, p2, {x + 0.5f, y + 0.5f});
                    if (bc.x < 0 || bc.y < 0 || bc.z < 0)
                        continue;
                    // 深度插值（屏幕空间线性插值 z 即可，z 已在 NDC 后）
                    float z = bc.x * s0.z + bc.y * s1.z + bc.z * s2.z;
                    if (!zbuf.testAndSet(x, y, z))
                        continue;
                    // 光照强度插值
                    float lit = bc.x * l0 + bc.y * l1 + bc.z * l2;
                    Color c = Color::fromFloat(s.color.r / 255.0f * lit,
                                               s.color.g / 255.0f * lit,
                                               s.color.b / 255.0f * lit);
                    fb.setPixel(x, y, c);
                }
            }
        }
    }

    std::filesystem::create_directories("output");
    sr::io::writeTga("output/stage06_depth.tga", fb);

    // 深度可视化：把 zbuffer 映射成灰度图（近亮远暗）
    // 注意：透视深度是"非线性"的，数值几乎全部挤在接近 1 的远端，
    // 直接用 1-z 会几乎全黑。所以先扫描出实际 [min,max] 再线性拉伸。
    Framebuffer zvis(W, H, sr::colors::black);
    float zmin = DepthBuffer::kFar, zmax = -DepthBuffer::kFar;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float z = zbuf.get(x, y);
            if (z >= DepthBuffer::kFar)
                continue;
            zmin = std::min(zmin, z);
            zmax = std::max(zmax, z);
        }
    float range = std::max(1e-6f, zmax - zmin);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float z = zbuf.get(x, y);
            if (z >= DepthBuffer::kFar)
                continue;                        // 没画到的地方留黑
            float g = 1.0f - (z - zmin) / range; // 拉伸到 [0,1]，近=1 亮
            zvis.setPixel(x, y, Color::fromFloat(g, g, g));
        }
    sr::io::writeTga("output/stage06_depth_zbuf.tga", zvis);

    std::printf("已生成 output/stage06_depth.tga 与 output/stage06_depth_zbuf.tga\n");
    return 0;
}
