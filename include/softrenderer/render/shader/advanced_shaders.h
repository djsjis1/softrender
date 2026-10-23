#pragma once
#include <algorithm>
#include <cmath>
#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/render/shader/shaders.h"

// ============================================================
// 阶段 8 高级着色器：阴影贴图 + 法线贴图
// ============================================================

// ---------------- 深度着色器（阴影 Pass 1 用）----------------
// 从光源视角渲染场景，只关心深度。fragment 输出什么颜色无所谓，
// 渲染完后我们取用它的 DepthBuffer 作为"阴影贴图"。
namespace sr
{

struct DepthShader : SurfaceShader
{
    Vec4f vertex(const Vertex &in, Varyings &out) override
    {
        out.worldPos = model.transformPoint(in.position);
        return mvp * Vec4f(in.position, 1.0f); // mvp = lightProj * lightView * model
    }
    bool fragment(const Varyings &, Vec3f &color) override
    {
        color = {1, 1, 1};
        return true;
    }
};

// ---------------- 最终着色器（Pass 2）----------------
// Blinn-Phong 光照 + 可选法线贴图 + 可选阴影贴图（含 3x3 PCF 软化）。
// 三个可选项为 null 时自动退化，方便单独观察每个特性。
struct FinalShader : SurfaceShader
{
    // 法线贴图（切线空间）
    const Texture *normalMap = nullptr;

    // 阴影贴图相关
    const DepthBuffer *shadowMap = nullptr;
    Mat4 lightVP = Mat4::identity(); // lightProj * lightView（不含 model）
    int shadowW = 0, shadowH = 0;
    float shadowBias = 0.0025f;

    Vec4f vertex(const Vertex &in, Varyings &out) override
    {
        out.worldPos = model.transformPoint(in.position);
        out.normal = toWorldNormal(in.normal);
        // 切线用 model 的线性部分变换（无缩放时与逆转置一致）
        out.tangent = model.transformDir(in.tangent).normalized();
        out.uv = in.uv;
        return mvp * Vec4f(in.position, 1.0f);
    }

    bool fragment(const Varyings &v, Vec3f &color) override
    {
        // ---- 1. 确定着色法线（可选法线贴图）----
        Vec3f N = v.normal.normalized();
        if (normalMap && normalMap->valid())
        {
            // 构建 TBN：把切线空间法线转到世界空间
            Vec3f T = v.tangent;
            T = (T - N * dot(N, T)).normalized(); // Gram-Schmidt 正交化
            Vec3f B = cross(N, T);
            Vec3f enc = normalMap->sampleBilinear(v.uv.x, v.uv.y);
            // 解码 [0,1] -> [-1,1]
            Vec3f nt{enc.x * 2.0f - 1.0f, enc.y * 2.0f - 1.0f, enc.z * 2.0f - 1.0f};
            N = (T * nt.x + B * nt.y + N * nt.z).normalized();
        }

        // ---- 2. Blinn-Phong 各项 ----
        Vec3f albedo = sampleAlbedo(v.uv);
        Vec3f L = lightDir;
        float diff = std::max(0.0f, dot(N, L));
        Vec3f View = (cameraPos - v.worldPos).normalized();
        Vec3f Hh = (L + View).normalized();
        float spec = (diff > 0.0f)
                         ? std::pow(std::max(0.0f, dot(N, Hh)), shininess)
                         : 0.0f;

        // ---- 3. 阴影因子 ----
        float shadow = 1.0f; // 1 = 全亮，0 = 全阴影
        if (shadowMap)
            shadow = computeShadow(v.worldPos, N, L);

        // ---- 4. 合成：环境光不受阴影影响，漫反射/高光受阴影调制 ----
        Vec3f lit = albedo * ambient +
                    (albedo * lightColor * diff + lightColor * spec * 0.5f) * shadow;
        color = lit;
        return true;
    }

private:
    // 返回 [0,1] 的可见度（PCF 平均）：1 全亮，0 全遮挡
    float computeShadow(const Vec3f &worldPos, const Vec3f &N, const Vec3f &L) const
    {
        // 世界坐标 -> 光源裁剪空间 -> NDC
        Vec4f lc = lightVP * Vec4f(worldPos, 1.0f);
        if (lc.w <= 0.0f)
            return 1.0f;
        Vec3f ndc{lc.x / lc.w, lc.y / lc.w, lc.z / lc.w};
        // 超出光锥范围就当作全亮（不投阴影）
        if (ndc.x < -1 || ndc.x > 1 || ndc.y < -1 || ndc.y > 1)
            return 1.0f;

        // NDC -> 阴影图像素坐标 + 深度 [0,1]
        float sx = (ndc.x * 0.5f + 0.5f) * shadowW;
        float sy = (ndc.y * 0.5f + 0.5f) * shadowH;
        float curDepth = ndc.z * 0.5f + 0.5f;

        // 斜率相关偏移：表面越斜（N·L 越小）越容易自阴影(acne)，加大 bias
        float bias = std::max(shadowBias, shadowBias * 4.0f * (1.0f - dot(N, L)));

        // 3x3 PCF：采样周围 9 个纹素，统计有多少比当前片元近 -> 软化边缘
        int lit = 0, total = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                int px = int(sx) + dx, py = int(sy) + dy;
                if (px < 0 || px >= shadowW || py < 0 || py >= shadowH)
                {
                    lit++;
                    total++;
                    continue;
                }
                float stored = shadowMap->get(px, py);
                if (stored >= DepthBuffer::kFar) // 光看不到这里，视为无遮挡
                    lit++;
                else if (curDepth - bias <= stored) // 当前片元没被更近的东西挡住
                    lit++;
                total++;
            }
        return total ? float(lit) / total : 1.0f;
    }
};

}  // namespace sr
