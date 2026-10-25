#pragma once
#include <cstddef>
#include <cmath>
#include <vector>
#include "softrenderer/math/vec.h"
#include "softrenderer/resource/vertex.h"

// ============================================================
// Mesh：三角形网格。索引式存储（顶点数组 + 索引数组），
// 相邻三角形共享顶点，省内存也方便法线平滑。
//
// Vertex 携带三种最常用的顶点属性：
//   position - 模型空间坐标
//   normal   - 法线（光照用，阶段 6）
//   uv       - 纹理坐标（阶段 5）
// ============================================================
namespace sr
{

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<int> indices; // 每 3 个构成一个三角形，逆时针为正面

    int triangleCount() const { return int(indices.size()) / 3; }
};

// ============================================================
// 计算切线（tangent）：法线贴图需要 TBN 坐标系。
//
// 切线 T 是"UV 中 u 增大的方向在模型空间的对应向量"。
// 对三角形，已知三个顶点位置差 (e1,e2) 与 UV 差 (Δuv1,Δuv2)，
// 满足：
//   e1 = Δu1 * T + Δv1 * B
//   e2 = Δu2 * T + Δv2 * B
// 解这个 2x2 线性方程组即得 T（B 为副切线）。
// 逐三角形累加到顶点再单位化，得到平滑切线。
// ============================================================
inline void computeTangents(Mesh &mesh)
{
    for (auto &v : mesh.vertices)
        v.tangent = {0, 0, 0};

    // 与法线正交的任意切向：零切向与“去完法线分量后接近零”两种情况都走这里，
    // 保证返回的 TBN 基不会退化。法线本身为零时回退 +Z。
    auto orthogonalTangent = [](const Vec3f &normal)
    {
        const Vec3f n = normal.normalizedOr({0, 0, 1});
        const Vec3f ref = std::abs(n.y) < 0.99f ? Vec3f{0, 1, 0} : Vec3f{1, 0, 0};
        return cross(ref, n).normalizedOr({1, 0, 0});
    };

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        // 索引是外部数据（手写 Mesh、解析结果）：越界就跳过整个三角形。
        // 否则 mesh.vertices[idx] 是越界写。drawMesh 也会丢弃这类三角形，
        // 但本函数是独立公共 API，不能依赖调用方先清理。
        const int i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || size_t(i0) >= mesh.vertices.size() ||
            size_t(i1) >= mesh.vertices.size() || size_t(i2) >= mesh.vertices.size())
            continue;

        Vertex &a = mesh.vertices[size_t(i0)];
        Vertex &b = mesh.vertices[size_t(i1)];
        Vertex &c = mesh.vertices[size_t(i2)];

        Vec3f e1 = b.position - a.position;
        Vec3f e2 = c.position - a.position;
        float du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y;
        float du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;

        float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-8f)
            continue;
        float r = 1.0f / det;
        // T = (dv2*e1 - dv1*e2) / det
        Vec3f t = (e1 * dv2 - e2 * dv1) * r;
        a.tangent = a.tangent + t;
        b.tangent = b.tangent + t;
        c.tangent = c.tangent + t;
    }

    // 单位化，并用 Gram-Schmidt 让切线与法线正交
    for (auto &v : mesh.vertices)
    {
        const Vec3f n = v.normal.normalizedOr({0, 0, 1});
        if (v.tangent.lengthSq() < 1e-12f)
        {
            v.tangent = orthogonalTangent(v.normal);
        }
        else
        {
            // 去掉法线分量。若切线与法线几乎平行，去完接近零向量 ——
            // 旧实现兜底成固定 (1,0,0)，当法线也接近 +X 时切线与法线仍然平行，
            // TBN 依旧退化。这里统一回退到正交构造。
            const Vec3f orthogonal = v.tangent - n * dot(n, v.tangent);
            v.tangent = orthogonal.lengthSq() < 1e-12f ? orthogonalTangent(v.normal) : orthogonal.normalized();
        }
    }
}

}  // namespace sr
