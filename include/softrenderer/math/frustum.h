#pragma once
#include "softrenderer/math/mat4.h"
#include "softrenderer/math/vec.h"

// ============================================================
// 视锥剔除：判断模型局部空间的轴对齐包围盒（AABB）是否可能与视锥相交。
//
// 【为什么是保守测试】只做「明确整块都在外 → 剔除」，其余一律保留。
// 两个方向的代价不对称：误剔 = 物体凭空消失（用户一眼能看见的错误），
// 漏剔 = 白算几个三角形（只是慢一点）。所以宁可放过，不可误杀。
//
// 【坐标约定】与 render/pipeline/clip.h 完全一致（OpenGL 风格半空间，
// 近平面内侧为 z + w >= 0）。两处若各推一套约定，会出现
// 「包围盒判可见、裁剪又判在外」的裂缝，所以约定写死在这里。
//
// 【NaN 处理】d < 0 为假时即视为「不在外侧」，因此 NaN 输入不会导致剔除，
// 交回给后续管线去暴露问题——剔除器不该成为掩盖错误的那个环节。
// ============================================================
namespace sr
{

struct Frustum
{
    // mvp = proj * view * model：把模型局部坐标直接送到裁剪空间。
    Mat4 mvp = Mat4::identity();

    static Frustum fromMVP(const Mat4 &m)
    {
        Frustum f;
        f.mvp = m;
        return f;
    }

    // 局部 AABB 是否可能可见。返回 false 表示可以安全跳过整块几何。
    bool intersectsAabb(const Vec3f &boundsMin, const Vec3f &boundsMax) const
    {
        Vec4f corners[8];
        for (int i = 0; i < 8; ++i)
        {
            const Vec3f p{(i & 1) ? boundsMax.x : boundsMin.x, (i & 2) ? boundsMax.y : boundsMin.y,
                          (i & 4) ? boundsMax.z : boundsMin.z};
            corners[i] = mvp * Vec4f(p, 1.0f);
        }

        // 六个半空间：w±x、w±y、w±z 都非负才可能相交。
        for (int plane = 0; plane < 6; ++plane)
        {
            bool allOutside = true;
            for (int i = 0; i < 8; ++i)
            {
                const Vec4f &v = corners[i];
                float d = 0.0f;
                switch (plane)
                {
                case 0: d = v.w + v.x; break; // 左
                case 1: d = v.w - v.x; break; // 右
                case 2: d = v.w + v.y; break; // 下
                case 3: d = v.w - v.y; break; // 上
                case 4: d = v.w + v.z; break; // 近
                default: d = v.w - v.z; break; // 远
                }
                // 用 !(d < 0) 而不是 d >= 0：NaN 比较恒假，这样 NaN 归到"不在外侧"
                if (!(d < 0.0f))
                {
                    allOutside = false;
                    break;
                }
            }
            if (allOutside)
                return false;
        }
        return true;
    }
};

} // namespace sr
