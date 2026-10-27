#pragma once
#include <algorithm>  // std::min / std::max / std::abs
#include <cassert>
#include <cmath>      // std::floor / std::ceil（包围盒取整）
#include <cstdint>
#include <vector>

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

    // 裸机路径：深度缓冲用调用方提供的外部存储，整个渲染
    // （外部存储 Framebuffer + 本构造 + drawMesh）不经任何堆分配。
    // storage 须容纳 width*height 个 float，生命周期覆盖本渲染器。
    Renderer(int width, int height, float* depthStorage)
        : width_(width), height_(height), zbuf_(width, height, depthStorage),
          viewport_(sr::transform::viewport(width, height))
    {
        assert(depthStorage != nullptr);
    }

    // 裸机路径（续）：顶点缓存同样用外部存储，drawMesh 内部零堆分配。
    // cache 须容纳 capacity 个 ClipVertex、stamps 须容纳 capacity 个 uint32_t，
    // capacity >= 所有将绘制网格的顶点数；不足时该次 drawMesh 回退到内部
    // vector（会分配堆）。设置时 stamps 会被清零。
    // 【与外部深度存储配合，构成完整的零堆渲染路径；见 docs/engineering/60】
    void setExternalVertexCache(ClipVertex* cache, uint32_t* stamps, int capacity)
    {
        extCache_ = cache;
        extStamps_ = stamps;
        extCapacity_ = capacity > 0 ? capacity : 0;
        if (extCapacity_ > 0)
            std::fill(extStamps_, extStamps_ + extCapacity_, 0u);
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
    //
    // 【顶点缓存】同一次 drawMesh 内按索引缓存顶点着色结果（GPU 的
    // post-transform cache 同款思想）：索引网格中每个共享顶点只执行一次
    // shader.vertex，而不是每三角形重复 3 次。由此推论两点契约：
    //   1. shader.vertex 必须只依赖 (顶点输入, 本次 draw 期间的 uniform 状态)，
    //      与「这是第几个三角形」无关——GPU 顶点着色同样没有三角形上下文；
    //   2. 顶点着色次数从 3×三角形数 变为 去重后的顶点数，依赖调用计数的
    //      着色器（如统计用途）看到的次数会与旧版本不同。
    // 缓存按调用分代（epoch），两次 drawMesh 之间不跨帧复用。
    template <class FB>
    void drawMesh(FB& fb, const Mesh& mesh, Shader& shader)
    {
        const int vertexCount = int(mesh.vertices.size());
        if (vertexCount <= 0 || mesh.triangleCount() <= 0)
            return;
        prepareVertexCache(vertexCount);

        for (int i = 0; i < mesh.triangleCount(); ++i)
        {
            // 索引越界的三角形直接丢弃。Mesh 是公共结构，可以被使用者手工构造，
            // 越界索引在这里不是“渲染结果差异”而是未定义行为（读越界内存）。
            const int i0 = mesh.indices[size_t(i) * 3 + 0];
            const int i1 = mesh.indices[size_t(i) * 3 + 1];
            const int i2 = mesh.indices[size_t(i) * 3 + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                continue;

            // 1. 顶点着色（命中缓存的顶点直接复用上次的输出）
            ClipVertex tri[3] = {fetchVertex(shader, mesh, i0), fetchVertex(shader, mesh, i1),
                                 fetchVertex(shader, mesh, i2)};

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
        const double invArea = 1.0 / double(coverage->area());

        // 主循环骨架与 MSAA 路径共用 sr::forEachCoveredPixel；行指针在行首取一次，
        // 包围盒已在屏幕内，逐像素免去边界检查。
        float* zRow = nullptr;
        Pixel* pixRow = nullptr;
        sr::forEachCoveredPixel(
            *coverage, bounds,
            [&](int y) {
                zRow = zbuf_.row(y);
                pixRow = fb.row(y);
            },
            [&](int x, int, const sr::TriangleCoverage::Values& coveragePixel) {
                if (!sr::TriangleCoverage::contains(coveragePixel))
                    return;
                // 7. 深度测试（行内直访）
                const Vec3f bc = sr::barycentricFromValues(*coverage, coveragePixel, invArea);
                const float z = bc.x * s0.z + bc.y * s1.z + bc.z * s2.z;
                if (!(z < zRow[x]))
                    return;
                // 8. 透视校正插值 + 片元着色
                Varyings frag = interpolateVaryings(a.var, b.var, c.var, bc, iw0, iw1, iw2);
                Vec3f color;
                // 仅在片元被接受时提交颜色与深度：被 discard 的片元不得占据深度
                if (shader.fragment(frag, color))
                {
                    zRow[x] = z;
                    pixRow[x] = Pixel::fromFloat(color.x, color.y, color.z);
                }
            });
    }

    // 顶点缓存：一次 drawMesh 内，同一索引的顶点只着色一次。
    // stamp 记录每个顶点最近一次被变换时的 epoch，不匹配即重算。
    // 存储二选一：外部（setExternalVertexCache，零堆）或内部 vector；
    // 每次调用把 cache_/stamps_/cacheCapacity_ 解析到实际使用的存储上。
    void prepareVertexCache(int vertexCount)
    {
        if (extCache_ && extCapacity_ >= vertexCount)
        {
            // 外部路径的 epoch 耗尽：外部 stamps 归零后从 1 重新计数。
            if (xformEpoch_ == 0xFFFFFFFFu)
            {
                std::fill(extStamps_, extStamps_ + extCapacity_, 0u);
                xformEpoch_ = 0;
            }
            cache_ = extCache_;
            stamps_ = extStamps_;
            cacheCapacity_ = extCapacity_;
        }
        else
        {
            if (int(xformStamp_.size()) < vertexCount)
            {
                xformStamp_.assign(size_t(vertexCount), 0);
                xformCache_.resize(size_t(vertexCount));
                xformEpoch_ = 0; // stamp 全量清零后 epoch 归零，本调用内必然全部重算
            }
            else if (xformEpoch_ == 0xFFFFFFFFu)
            {
                // epoch 耗尽：清空 stamp 再从 1 重新计数（约 40 亿次 drawMesh 才发生一次）。
                std::fill(xformStamp_.begin(), xformStamp_.end(), 0u);
                xformEpoch_ = 0;
            }
            cache_ = xformCache_.data();
            stamps_ = xformStamp_.data();
            cacheCapacity_ = int(xformCache_.size());
        }
        ++xformEpoch_;
    }

    ClipVertex& fetchVertex(Shader& shader, const Mesh& mesh, int index)
    {
        // index 已在 drawMesh 里通过 [0, vertexCount) 校验，且
        // vertexCount <= cacheCapacity_（prepareVertexCache 的解析保证）。
        ClipVertex& cv = cache_[size_t(index)];
        if (stamps_[size_t(index)] != xformEpoch_)
        {
            cv.pos = shader.vertex(mesh.vertices[size_t(index)], cv.var);
            stamps_[size_t(index)] = xformEpoch_;
        }
        return cv;
    }

    int width_, height_;
    DepthBuffer zbuf_;
    Mat4 viewport_;
    // 内部（自分配）顶点缓存；外部（非拥有）缓存经 prepareVertexCache
    // 解析到 cache_/stamps_/cacheCapacity_ 三个指针上
    std::vector<ClipVertex> xformCache_;
    std::vector<uint32_t> xformStamp_;
    ClipVertex* extCache_ = nullptr;
    uint32_t* extStamps_ = nullptr;
    int extCapacity_ = 0;
    ClipVertex* cache_ = nullptr;
    uint32_t* stamps_ = nullptr;
    int cacheCapacity_ = 0;
    uint32_t xformEpoch_ = 0;
};

}  // namespace sr
