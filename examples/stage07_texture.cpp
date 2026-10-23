// ============================================================
// 阶段 07：纹理映射 + 透视校正插值
//
// 【源码阅读】先读这些文件：
//   scene/texture.h        — Texture：2D 纹理采样 + 双线性过滤
//   raster/renderer.h      — Renderer 中的透视校正插值逻辑
//   raster/triangle.h      — 重心坐标 + varyings 插值
//   shader/shaders.h       — TextureShader：UV 采样 + 光照
//
// 【运行验证】生成 output/stage07_texture.tga（左右对比）：
//   左：仿射插值（纹理扭曲）vs 右：透视校正插值（纹理均匀）
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
#include "softrenderer/resource/texture.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture_generator.h"
#include "softrenderer/math/barycentric.h"

using namespace sr;

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(20, 22, 28));

    // 棋盘格纹理
    Texture tex(sr::texgen::checkerboard(256, 8));

    const float PI = 3.14159265f;
    // 一块很大的地面平面，UV 重复多次；相机低角度看向远方，
    // 这种"延伸到地平线"的视角最能暴露仿射插值的错误。
    // 平面整体平移到相机前方（z=-8），避免远角落到相机背后被剔除
    // （那属于近平面裁剪的范畴，留到阶段 7 解决）。
    Mesh ground = sr::meshgen::plane(20.0f, 12.0f);
    Mat4 model = Mat4::translate(0, 0, -8.0f);
    Mat4 view = sr::transform::lookAt({0, 2.5f, 3.5f}, {0, 0, -6}, {0, 1, 0});
    Mat4 proj = sr::transform::perspective(PI / 3.0f, float(W / 2) / H, 0.1f, 100.0f);
    Mat4 mvp = proj * view * model;

    // 渲染同一个平面两次：correct=false 左半，correct=true 右半
    auto renderHalf = [&](int offsetX, bool correct)
    {
        Mat4 vp = sr::transform::viewport(W / 2, H);
        DepthBuffer zbuf(W, H);

        for (int i = 0; i < ground.triangleCount(); ++i)
        {
            const Vertex &A = ground.vertices[ground.indices[i * 3 + 0]];
            const Vertex &B = ground.vertices[ground.indices[i * 3 + 1]];
            const Vertex &C = ground.vertices[ground.indices[i * 3 + 2]];

            Vec4f c0 = mvp * Vec4f(A.position, 1.0f);
            Vec4f c1 = mvp * Vec4f(B.position, 1.0f);
            Vec4f c2 = mvp * Vec4f(C.position, 1.0f);
            if (c0.w <= 0 || c1.w <= 0 || c2.w <= 0)
                continue;

            // 透视除法
            Vec3f n0{c0.x / c0.w, c0.y / c0.w, c0.z / c0.w};
            Vec3f n1{c1.x / c1.w, c1.y / c1.w, c1.z / c1.w};
            Vec3f n2{c2.x / c2.w, c2.y / c2.w, c2.z / c2.w};
            Vec3f s0 = vp.transformPoint(n0);
            Vec3f s1 = vp.transformPoint(n1);
            Vec3f s2 = vp.transformPoint(n2);

            Vec2f p0{s0.x, s0.y}, p1{s1.x, s1.y}, p2{s2.x, s2.y};
            // 1/w：透视校正的核心量
            float iw0 = 1.0f / c0.w, iw1 = 1.0f / c1.w, iw2 = 1.0f / c2.w;

            int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
            int maxX = std::min(W / 2 - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
            int minY = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
            int maxY = std::min(H - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));

            for (int y = minY; y <= maxY; ++y)
                for (int x = minX; x <= maxX; ++x)
                {
                    Vec3f bc = sr::barycentric(p0, p1, p2, {x + 0.5f, y + 0.5f});
                    if (bc.x < 0 || bc.y < 0 || bc.z < 0)
                        continue;
                    float z = bc.x * s0.z + bc.y * s1.z + bc.z * s2.z;
                    if (!zbuf.testAndSet(x + offsetX, y, z))
                        continue;

                    float u, v;
                    if (correct)
                    {
                        // 透视校正：插值 u/w、v/w、1/w 后再相除
                        float iw = bc.x * iw0 + bc.y * iw1 + bc.z * iw2;
                        float uw = bc.x * (A.uv.x * iw0) + bc.y * (B.uv.x * iw1) +
                                   bc.z * (C.uv.x * iw2);
                        float vw = bc.x * (A.uv.y * iw0) + bc.y * (B.uv.y * iw1) +
                                   bc.z * (C.uv.y * iw2);
                        u = uw / iw;
                        v = vw / iw;
                    }
                    else
                    {
                        // 仿射（错误）：屏幕空间直接线性插值 UV
                        u = bc.x * A.uv.x + bc.y * B.uv.x + bc.z * C.uv.x;
                        v = bc.x * A.uv.y + bc.y * B.uv.y + bc.z * C.uv.y;
                    }

                    Vec3f rgb = tex.sampleBilinear(u, v);
                    fb.setPixel(x + offsetX, y, Color::fromFloat(rgb.x, rgb.y, rgb.z));
                }
        }
    };

    renderHalf(0, false);    // 左：仿射
    renderHalf(W / 2, true); // 右：透视校正

    // 分隔线 + 标注色块
    for (int y = 0; y < H; ++y)
        fb.setPixel(W / 2, y, Color(255, 200, 60));

    std::filesystem::create_directories("output");
    const char *path = "output/stage07_texture.tga";
    if (!sr::io::writeTga(path, fb))
    {
        std::printf("写文件失败: %s\n", path);
        return 1;
    }
    std::printf("已生成 %s（左=仿射错误，右=透视校正）\n", path);
    return 0;
}
