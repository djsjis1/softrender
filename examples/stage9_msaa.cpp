// ============================================================
// 阶段 9：MSAA 抗锯齿对比
//
// 生成 output/stage9_msaa.tga（左右对比）：
//   左半屏：无抗锯齿（1 样本/像素，像素中心判定）
//   右半屏：4x MSAA（4 个子采样点独立做覆盖+深度测试，
//           片元着色只算 1 次，写入所有被覆盖的样本槽）
//
// 场景刻意用斜线多的物体（旋转立方体线框 + 球体），
// 让锯齿在左半屏清晰可见，右半屏平滑。
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>
#include "core/framebuffer.h"
#include "core/msaa_framebuffer.h"
#include "core/tga.h"
#include "math/transform.h"
#include "raster/triangle.h"
#include "scene/mesh.h"
#include "shader/shaders.h"

// 简易渲染器：支持 MSAA 模式
template <int Samples>
struct MiniRenderer
{
    int W, H;
    MSAAFramebuffer<Samples> msaaFB;
    std::vector<float> centerDepth; // 像素中心深度（MSAA 的着色只跑一次）

    MiniRenderer(int w, int h) : W(w), H(h), msaaFB(w, h), centerDepth(w * h, 1e9f) {}

    void clear(Color bg)
    {
        msaaFB.clear(bg);
        std::fill(centerDepth.begin(), centerDepth.end(), 1e9f);
    }

    // 光栅化一个三角形（裁剪空间已变换到屏幕坐标）
    void rasterize(const Vec2f &p0, const Vec2f &p1, const Vec2f &p2,
                   float z0, float z1, float z2, Color color)
    {
        int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
        int maxX = std::min(W - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
        int minY = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
        int maxY = std::min(H - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));

        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                // 像素中心的重心坐标（用于着色 + 中心深度）
                Vec3f bcC = barycentric(p0, p1, p2, {x + 0.5f, y + 0.5f});
                bool centerInside = (bcC.x >= 0 && bcC.y >= 0 && bcC.z >= 0);
                float zCenter = centerInside ? bcC.x * z0 + bcC.y * z1 + bcC.z * z2 : 1e9f;

                // 对每个样本做覆盖+深度测试
                bool anyCovered = false;
                for (int s = 0; s < Samples; ++s)
                {
                    float sx, sy;
                    if constexpr (Samples == 1)
                    {
                        sx = x + 0.5f;
                        sy = y + 0.5f;
                    }
                    else
                    {
                        sx = x + 0.5f + kMSAA4Offsets[s].first;
                        sy = y + 0.5f + kMSAA4Offsets[s].second;
                    }
                    Vec3f bc = barycentric(p0, p1, p2, {sx, sy});
                    if (bc.x < 0 || bc.y < 0 || bc.z < 0)
                        continue;
                    float zS = bc.x * z0 + bc.y * z1 + bc.z * z2;
                    if (msaaFB.testDepth(x, y, s, zS))
                    {
                        // 此样本被覆盖且深度通过
                        anyCovered = true;
                    }
                }

                // MSAA 核心优化：片元着色只跑一次（用中心深度做代表）
                // 如果任一样本被覆盖，且中心深度通过了"像素级"竞争，
                // 就把颜色写入所有被覆盖的样本槽。
                if (anyCovered)
                {
                    // 简化：用中心深度做像素级竞争（避免同像素多次着色）
                    float &cd = centerDepth[size_t(y) * W + x];
                    if (zCenter < cd)
                    {
                        cd = zCenter;
                        // 回填颜色到所有被覆盖的样本
                        for (int s = 0; s < Samples; ++s)
                        {
                            float sx, sy;
                            if constexpr (Samples == 1)
                            {
                                sx = x + 0.5f;
                                sy = y + 0.5f;
                            }
                            else
                            {
                                sx = x + 0.5f + kMSAA4Offsets[s].first;
                                sy = y + 0.5f + kMSAA4Offsets[s].second;
                            }
                            Vec3f bc = barycentric(p0, p1, p2, {sx, sy});
                            if (bc.x >= 0 && bc.y >= 0 && bc.z >= 0)
                            {
                                msaaFB.setSample(x, y, s, color);
                            }
                        }
                    }
                }
            }
        }
    }

    void resolveTo(Framebuffer &dst)
    {
        std::vector<Color> pixels;
        msaaFB.resolve(pixels);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                dst.setPixel(x, y, pixels[size_t(y) * W + x]);
    }
};

int main()
{
    const int halfW = 400, H = 600;
    Framebuffer fb(halfW * 2, H, Color(30, 30, 40));

    // 场景：旋转的立方体 + 球（斜线多，锯齿明显）
    const float PI = 3.14159265f;
    Mesh cube = meshgen::cube(1.4f);
    Mesh sphere = meshgen::sphere(0.8f, 24, 16);

    Mat4 modelCube = Mat4::translate(-0.8f, 0, 0) * Mat4::rotateY(0.7f) * Mat4::rotateX(0.4f);
    Mat4 modelSphere = Mat4::translate(0.9f, 0, 0);
    Mat4 view = transform::lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
    Mat4 proj = transform::perspective(PI / 4.0f, float(halfW) / H, 0.1f, 100.0f);

    auto project = [&](const Vec3f &p, const Mat4 &mvp) -> std::pair<Vec2f, float>
    {
        Vec4f clip = mvp * Vec4f(p, 1.0f);
        if (clip.w <= 0)
            return {{-999, -999}, 1e9f};
        Vec3f ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
        float sx = (ndc.x * 0.5f + 0.5f) * halfW;
        float sy = (ndc.y * 0.5f + 0.5f) * H;
        float sz = ndc.z * 0.5f + 0.5f;
        return {{sx, sy}, sz};
    };

    auto renderScene = [&](auto &renderer, int offsetX)
    {
        renderer.clear(Color(30, 30, 40));
        Mat4 mvpC = proj * view * modelCube;
        Mat4 mvpS = proj * view * modelSphere;

        // 画立方体（每面一个颜色）
        Color cubeColors[6] = {
            Color(220, 80, 80), Color(80, 220, 80), Color(80, 80, 220),
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
            renderer.rasterize(s0, s1, s2, z0, z1, z2,
                               Color::fromFloat(0.3f * lit, 0.6f * lit, 0.9f * lit));
        }
    };

    // 左：无 AA
    MiniRenderer<1> noAA(halfW, H);
    renderScene(noAA, 0);
    noAA.resolveTo(fb);

    // 右：4x MSAA
    MiniRenderer<4> msaa(halfW, H);
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
    tga::write("output/stage9_msaa.tga", fb);
    std::printf("已生成 output/stage9_msaa.tga（左=无AA，右=4xMSAA）\n");
    return 0;
}
