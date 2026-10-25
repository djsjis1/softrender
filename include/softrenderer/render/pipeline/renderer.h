#pragma once
#include <algorithm>  // std::min / std::max / std::abs
#include <cmath>      // std::floor / std::ceil（包围盒取整）

#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/render/pipeline/clip.h"
#include "softrenderer/render/raster/coverage.h"
#include "softrenderer/render/raster/triangle.h"
#include "softrenderer/render/pipeline/shader.h"

// ============================================================
// Renderer：把"可编程着色管线"完整串起来的光栅化渲染器。
//
// 一次 drawMesh 对每个三角形执行以下固定流水线（GPU 也是这套）：
//   1. 顶点着色：shader.vertex 得到裁剪空间坐标 + Varyings
//   2. 近平面裁剪：沿近平面切开，可能变成 3~4 顶点，再三角扇拆分
//   3. 透视除法：clip.xyz / clip.w -> NDC
//   4. 视口变换：NDC -> 屏幕像素 + [0,1] 深度
//   5. 背面剔除：屏幕空间有向面积 <=0 丢弃
//   6. 光栅化：包围盒遍历 + 重心坐标
//   7. 深度测试：z 更近才通过
//   8. 透视校正插值 Varyings -> shader.fragment -> 写像素
//
// 【帧缓冲格式无关】drawMesh / rasterTriangle 的帧缓冲参数是模板参数，
// 而不是固定在 Framebuffer 上：着色器始终输出 Vec3f（高精度运算域），
// 只有最后一步 `Pixel::fromFloat` 把结果量化进目标缓冲的存储格式。
// 因此 `renderer.drawMesh(fb8888, ...)` 与 `renderer.drawMesh(fb565, ...)`
// 走同一条代码路径、同一套覆盖规则，差别仅一行量化。
// 用模板成员函数而非模板类，是为了让既有调用点（22 个示例、测试、
// 压测）一行都不用改——实参类型自动推导。
// ============================================================
namespace sr
{

class Renderer
{
   public:
    Renderer(int width, int height)
        : width_(width), height_(height), zbuf_(width, height), viewport_(sr::transform::viewport(width, height))
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // 清屏：同时清深度。fb 可为任意格式的帧缓冲（见 drawMesh 的说明）。
    template <class FB>
    void clear(FB& fb, const Color& bg)
    {
        fb.clear(bg);
        zbuf_.clear();
    }

    DepthBuffer& depth() { return zbuf_; }

    bool cullBackface = true;  // 关闭后可渲染双面片（如单面地面）
    bool nearClip = true;      // 阶段 7：关闭则退化为"整三角形剔除"以作对比

    // 本函数不做宿主计时：ProfileScope 属于 runtime，由调用方在示例/基准里
    // 包住 drawMesh，避免渲染头依赖时钟、线程与文件写出。
    //
    // FB 只需满足 BasicFramebuffer 的接口（width/height/row），
    // 像素格式由 FB::Pixel 决定。
    template <class FB>
    void drawMesh(FB& fb, const Mesh& mesh, Shader& shader)
    {
        const int vertexCount = int(mesh.vertices.size());
        for (int i = 0; i < mesh.triangleCount(); ++i)
        {
            // 索引越界的三角形直接丢弃。Mesh 是公共结构，可以被使用者手工构造，
            // 越界索引在这里不是“渲染结果差异”而是未定义行为（读越界内存）。
            const int i0 = mesh.indices[size_t(i) * 3 + 0];
            const int i1 = mesh.indices[size_t(i) * 3 + 1];
            const int i2 = mesh.indices[size_t(i) * 3 + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                continue;

            // 1. 顶点着色
            ClipVertex tri[3];
            tri[0].pos = shader.vertex(mesh.vertices[size_t(i0)], tri[0].var);
            tri[1].pos = shader.vertex(mesh.vertices[size_t(i1)], tri[1].var);
            tri[2].pos = shader.vertex(mesh.vertices[size_t(i2)], tri[2].var);

            if (nearClip)
            {
                // 2. 完整 6 面视锥裁剪 -> 三角扇
                ClipVertex poly[kMaxClipVerts];
                int n = clipTriangleFullFrustum(tri, poly);
                for (int t = 1; t + 1 < n; ++t)
                    rasterTriangle(fb, shader, poly[0], poly[t], poly[t + 1]);
            }
            else
            {
                // 对比用：任一顶点在近平面外则整块丢弃（旧的粗暴做法）
                auto outside = [](const ClipVertex& v) { return v.pos.z + v.pos.w < 0 || v.pos.w <= 1e-6f; };
                if (outside(tri[0]) || outside(tri[1]) || outside(tri[2]))
                    continue;
                rasterTriangle(fb, shader, tri[0], tri[1], tri[2]);
            }
        }
    }

   private:
    // 光栅化一个（已裁剪的）裁剪空间三角形
    //
    // 内外判定与覆盖归属统一交给 sr::TriangleCoverage 的整数 top-left 规则，
    // 与基础填充（triangle.h）、标量/SSE2 与 MSAA 路径共用同一套规则，
    // 从而消除共享边被两个三角形重复覆盖的问题（ADR-003）。
    // 顶点属性仍用浮点重心坐标做透视校正插值。
    //
    // 【与格式无关】Pixel 由调用方传入的帧缓冲类型决定；
    // fromFloat 是唯一把运算域颜色量化成存储格式的位置。
    template <class FB>
    void rasterTriangle(FB& fb, Shader& shader, const ClipVertex& a, const ClipVertex& b, const ClipVertex& c)
    {
        using Pixel = typename FB::Pixel;
        // 3. 透视除法
        float iw0 = 1.0f / a.pos.w, iw1 = 1.0f / b.pos.w, iw2 = 1.0f / c.pos.w;
        Vec3f n0{a.pos.x * iw0, a.pos.y * iw0, a.pos.z * iw0};
        Vec3f n1{b.pos.x * iw1, b.pos.y * iw1, b.pos.z * iw1};
        Vec3f n2{c.pos.x * iw2, c.pos.y * iw2, c.pos.z * iw2};

        // 4. 视口变换
        Vec3f s0 = viewport_.transformPoint(n0);
        Vec3f s1 = viewport_.transformPoint(n1);
        Vec3f s2 = viewport_.transformPoint(n2);

        // 5. 背面剔除
        Vec2f p0{s0.x, s0.y}, p1{s1.x, s1.y}, p2{s2.x, s2.y};
        float area2 = cross2(p1 - p0, p2 - p0);
        if (cullBackface && area2 <= 0)
            return;
        if (std::abs(area2) < 1e-8f)
            return;

        shader.geometry(a.var, b.var, c.var);

        // 6. 覆盖规则 + 包围盒
        // bounds 内部已 clamp 到 [0, width-1] × [0, height-1]，这里取渲染器与
        // 帧缓冲尺寸的较小者，保证下面行指针直写落在两者的合法范围内。
        auto coverage = sr::TriangleCoverage::create(p0, p1, p2);
        if (!coverage)
            return;
        const auto bounds = coverage->bounds(std::min(width_, fb.width()), std::min(height_, fb.height()));
        if (bounds.empty())
            return;

        // 边函数值与重心坐标的对应关系（边下标同 sr::TriangleCoverage）：
        // E0 = edge(v0->v1) 对应 v2 的权重
        // E1 = edge(v1->v2) 对应 v0 的权重
        // E2 = edge(v2->v0) 对应 v1 的权重
        // 三边无偏边函数值之和恒等于三角形面积（量化单位），逐像素不变，故只求一次倒数。
        // 分母取无偏面积：coverage 的偏置只服务于覆盖归属，不参与归一化。
        auto coverageRow = coverage->atPixel(bounds.minX, bounds.minY);
        const double invArea = 1.0 / double(coverage->area());

        for (int y = bounds.minY; y <= bounds.maxY; ++y)
        {
            // 行指针快速路径：包围盒已在屏幕内，免去逐像素边界检查
            float* zRow = zbuf_.row(y);
            Pixel* pixRow = fb.row(y);

            auto coveragePixel = coverageRow;
            for (int x = bounds.minX; x <= bounds.maxX; ++x)
            {
                if (sr::TriangleCoverage::contains(coveragePixel))
                {
                    // 从整数边函数值算重心坐标；用 double 相乘避免大三角形丢精度。
                    // 先扣除 top-left 偏置：覆盖判定需要它，深度/UV 插值不能要它。
                    const auto raw = coverage->unbiased(coveragePixel);
                    Vec3f bc{float(double(raw[1]) * invArea), float(double(raw[2]) * invArea),
                             float(double(raw[0]) * invArea)};

                    // 7. 深度测试（行内直访）
                    const float z = bc.x * s0.z + bc.y * s1.z + bc.z * s2.z;
                    if (z < zRow[x])
                    {
                        // 8. 透视校正插值 + 片元着色
                        Varyings frag = interpolateVaryings(a.var, b.var, c.var, bc, iw0, iw1, iw2);
                        Vec3f color;
                        // 仅在片元被接受时提交颜色与深度：被 discard 的片元不得占据深度
                        if (shader.fragment(frag, color))
                        {
                            zRow[x] = z;
                            pixRow[x] = Pixel::fromFloat(color.x, color.y, color.z);
                        }
                    }
                }
                coverage->advanceX(coveragePixel);
            }
            coverage->advanceY(coverageRow);
        }
    }

    int width_, height_;
    DepthBuffer zbuf_;
    Mat4 viewport_;
};

}  // namespace sr
