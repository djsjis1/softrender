#pragma once
#include <algorithm>

#include "core/depthbuffer.h"
#include "core/framebuffer.h"
#include "core/profiler.h"
#include "math/transform.h"
#include "raster/clip.h"
#include "raster/triangle.h"
#include "shader/shader.h"

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
// ============================================================
class Renderer
{
   public:
    Renderer(int width, int height)
        : width_(width), height_(height), zbuf_(width, height), viewport_(transform::viewport(width, height))
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }

    void clear(Framebuffer& fb, Color bg)
    {
        fb.clear(bg);
        zbuf_.clear();
    }

    DepthBuffer& depth() { return zbuf_; }

    bool cullBackface = true;  // 关闭后可渲染双面片（如单面地面）
    bool nearClip = true;      // 阶段 7：关闭则退化为"整三角形剔除"以作对比

    void drawMesh(Framebuffer& fb, const Mesh& mesh, Shader& shader)
    {
        SR_PROFILE_SCOPE("drawMesh");
        for (int i = 0; i < mesh.triangleCount(); ++i)
        {
            // 1. 顶点着色
            ClipVertex tri[3];
            for (int k = 0; k < 3; ++k)
                tri[k].pos = shader.vertex(mesh.vertices[mesh.indices[i * 3 + k]], tri[k].var);

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
    // 使用增量式边函数：每像素 3 次加法代替 3 次叉积
    void rasterTriangle(Framebuffer& fb, Shader& shader, const ClipVertex& a, const ClipVertex& b, const ClipVertex& c)
    {
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

        // 6. 光栅化包围盒（同时 clamp 到帧缓冲尺寸，保证后续行指针直写安全）
        int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
        int maxX = std::min({width_ - 1, fb.width() - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x}))});
        int minY = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
        int maxY = std::min({height_ - 1, fb.height() - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y}))});
        if (minX > maxX || minY > maxY)
            return;

        // ---- 增量式边函数预计算 ----
        // 左上角像素中心的边函数值
        float px0 = minX + 0.5f, py0 = minY + 0.5f;
        float e0 = (p1.x - p0.x) * (py0 - p0.y) - (p1.y - p0.y) * (px0 - p0.x);
        float e1 = (p2.x - p1.x) * (py0 - p1.y) - (p2.y - p1.y) * (px0 - p1.x);
        float e2 = (p0.x - p2.x) * (py0 - p2.y) - (p0.y - p2.y) * (px0 - p2.x);

        // 顺时针三角形 area2<0，边函数在内部为负，翻转符号使判定统一为 >=0
        float sign = 1.0f;
        if (area2 < 0)
        {
            sign = -1.0f;
            e0 = -e0;
            e1 = -e1;
            e2 = -e2;
        }

        // 增量（已乘 sign）
        float de0_dx = -(p1.y - p0.y) * sign, de0_dy = (p1.x - p0.x) * sign;
        float de1_dx = -(p2.y - p1.y) * sign, de1_dy = (p2.x - p1.x) * sign;
        float de2_dx = -(p0.y - p2.y) * sign, de2_dy = (p0.x - p2.x) * sign;

        // 深度增量：z = (e1*s0.z + e2*s1.z + e0*s2.z) * invArea
        // 因为 bc.x(对应s0) = e1*invArea, bc.y(对应s1) = e2*invArea, bc.z(对应s2) = e0*invArea
        float invArea = 1.0f / std::abs(area2);
        float dz_dx = (de1_dx * s0.z + de2_dx * s1.z + de0_dx * s2.z) * invArea;
        float dz_dy = (de1_dy * s0.z + de2_dy * s1.z + de0_dy * s2.z) * invArea;

        // 透视校正插值增量 (u/w, v/w 等)
        // 对每个 Varyings 字段做增量太复杂，这里用混合策略：
        // 边函数做内外判定（快），重心坐标只在内部像素算（少）
        float e0_row = e0, e1_row = e1, e2_row = e2;
        float z_row = (e1 * s0.z + e2 * s1.z + e0 * s2.z) * invArea;

        for (int y = minY; y <= maxY; ++y)
        {
            // 行指针快速路径：包围盒已在屏幕内，免去逐像素边界检查
            float* zRow = zbuf_.row(y);
            Color* pixRow = fb.row(y);

            float e0_x = e0_row, e1_x = e1_row, e2_x = e2_row;
            float z_x = z_row;
            for (int x = minX; x <= maxX; ++x)
            {
                // 增量边函数判定：全 >= 0 则在三角形内
                if (e0_x >= 0 && e1_x >= 0 && e2_x >= 0)
                {
                    // 7. 深度测试（行内直访）
                    if (z_x < zRow[x])
                    {
                        zRow[x] = z_x;

                        // 从边函数值算重心坐标（只在内部像素做 2 次除法）
                        // 边函数与重心坐标的对应关系：
                        // E0 = edge(v0->v1) 对应 v2 的权重
                        // E1 = edge(v1->v2) 对应 v0 的权重
                        // E2 = edge(v2->v0) 对应 v1 的权重
                        Vec3f bc{e1_x * invArea, e2_x * invArea, e0_x * invArea};

                        // 8. 透视校正插值 + 片元着色
                        Varyings frag = interpolateVaryings(a.var, b.var, c.var, bc, iw0, iw1, iw2);
                        Vec3f color;
                        if (shader.fragment(frag, color))
                            pixRow[x] = Color::fromFloat(color.x, color.y, color.z);
                    }
                }
                e0_x += de0_dx;
                e1_x += de1_dx;
                e2_x += de2_dx;
                z_x += dz_dx;
            }
            e0_row += de0_dy;
            e1_row += de1_dy;
            e2_row += de2_dy;
            z_row += dz_dy;
        }
    }

    int width_, height_;
    DepthBuffer zbuf_;
    Mat4 viewport_;
};
