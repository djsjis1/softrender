#pragma once
#include <cmath>
#include <vector>
#include "math/vec.h"

// ============================================================
// Mesh：三角形网格。索引式存储（顶点数组 + 索引数组），
// 相邻三角形共享顶点，省内存也方便法线平滑。
//
// Vertex 携带三种最常用的顶点属性：
//   position - 模型空间坐标
//   normal   - 法线（光照用，阶段 6）
//   uv       - 纹理坐标（阶段 5）
// ============================================================
struct Vertex
{
    Vec3f position;
    Vec3f normal;
    Vec2f uv;
    Vec3f tangent; // 切线方向（沿 U 增大的方向），法线贴图 TBN 用
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<int> indices; // 每 3 个构成一个三角形，逆时针为正面

    int triangleCount() const { return int(indices.size()) / 3; }
};

// ============================================================
// 程序化生成网格：不依赖外部文件也能测试渲染器
// ============================================================
namespace meshgen
{

    // 立方体：边长 size，中心在原点。
    // 24 个顶点（每面独立 4 个顶点而非共享 8 个）——因为每个面的
    // 法线和 UV 不同，共享顶点会让属性无法区分（"硬边"必须拆顶点）
    inline Mesh cube(float size = 1.0f)
    {
        Mesh mesh;
        const float h = size * 0.5f;
        // 六个面：法线、面上的两个切向轴
        const Vec3f normals[6] = {
            {0, 0, 1},
            {0, 0, -1}, // 前(+Z) 后(-Z)
            {1, 0, 0},
            {-1, 0, 0}, // 右(+X) 左(-X)
            {0, 1, 0},
            {0, -1, 0}, // 上(+Y) 下(-Y)
        };
        const Vec3f tangentsU[6] = {
            {1, 0, 0},
            {-1, 0, 0},
            {0, 0, -1},
            {0, 0, 1},
            {1, 0, 0},
            {1, 0, 0},
        };
        const Vec3f tangentsV[6] = {
            {0, 1, 0},
            {0, 1, 0},
            {0, 1, 0},
            {0, 1, 0},
            {0, 0, -1},
            {0, 0, 1},
        };
        for (int f = 0; f < 6; ++f)
        {
            Vec3f n = normals[f], tu = tangentsU[f], tv = tangentsV[f];
            int base = int(mesh.vertices.size());
            // 面上四个角：(-1,-1) (1,-1) (1,1) (-1,1)
            const float us[4] = {-1, 1, 1, -1};
            const float vs[4] = {-1, -1, 1, 1};
            for (int i = 0; i < 4; ++i)
            {
                Vertex v;
                v.position = n * h + tu * (us[i] * h) + tv * (vs[i] * h);
                v.normal = n;
                v.uv = {(us[i] + 1) * 0.5f, (vs[i] + 1) * 0.5f};
                mesh.vertices.push_back(v);
            }
            // 两个三角形，逆时针（从法线方向看）
            mesh.indices.insert(mesh.indices.end(),
                                {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        return mesh;
    }

    // UV 球：经纬线细分。segments 经线数、rings 纬线数
    inline Mesh sphere(float radius = 1.0f, int segments = 32, int rings = 16)
    {
        Mesh mesh;
        const float PI = 3.14159265358979f;
        // 顶点：从北极 (v=1) 到南极 (v=0) 逐圈生成
        for (int r = 0; r <= rings; ++r)
        {
            float phi = PI * r / rings; // 极角 [0, π]
            for (int s = 0; s <= segments; ++s)
            {                                        // 多一列缝合 UV 接缝
                float theta = 2 * PI * s / segments; // 方位角 [0, 2π]
                Vec3f p{std::sin(phi) * std::cos(theta),
                        std::cos(phi),
                        std::sin(phi) * std::sin(theta)};
                Vertex v;
                v.position = p * radius;
                v.normal = p; // 球心在原点：法线就是单位化的位置
                v.uv = {float(s) / segments, 1.0f - float(r) / rings};
                mesh.vertices.push_back(v);
            }
        }
        // 索引：每个网格单元两三角形
        for (int r = 0; r < rings; ++r)
        {
            for (int s = 0; s < segments; ++s)
            {
                int a = r * (segments + 1) + s;
                int b = a + segments + 1;
                mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        }
        return mesh;
    }

    // 平面：xz 平面上 size×size，法线朝 +Y（做地面用）
    inline Mesh plane(float size = 1.0f, float uvRepeat = 1.0f)
    {
        Mesh mesh;
        const float h = size * 0.5f;
        const float px[4] = {-h, h, h, -h};
        const float pz[4] = {h, h, -h, -h};
        const float pu[4] = {0, 1, 1, 0};
        const float pv[4] = {0, 0, 1, 1};
        for (int i = 0; i < 4; ++i)
        {
            Vertex v;
            v.position = {px[i], 0, pz[i]};
            v.normal = {0, 1, 0};
            v.uv = {pu[i] * uvRepeat, pv[i] * uvRepeat};
            mesh.vertices.push_back(v);
        }
        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    }

    // 细分地面：xz 平面 size×size，划成 div×div 个小格。
    // 相比单个大四边形，细分后靠近相机的三角形被逐个剔除时，
    // 前方大片地面仍能保留（在没有真正近平面裁剪的阶段 6 用它）。
    inline Mesh planeGrid(float size = 1.0f, int div = 20, float uvRepeat = 1.0f)
    {
        Mesh mesh;
        const float h = size * 0.5f;
        for (int z = 0; z <= div; ++z)
            for (int x = 0; x <= div; ++x)
            {
                float fx = float(x) / div, fz = float(z) / div;
                Vertex v;
                v.position = {-h + fx * size, 0, -h + fz * size};
                v.normal = {0, 1, 0};
                v.uv = {fx * uvRepeat, fz * uvRepeat};
                mesh.vertices.push_back(v);
            }
        int stride = div + 1;
        for (int z = 0; z < div; ++z)
            for (int x = 0; x < div; ++x)
            {
                int a = z * stride + x;
                int b = a + 1;
                int c = a + stride;
                int d = c + 1;
                // 绕序使面法线朝 +Y（从上方看为逆时针）：
                // cross(c-a, b-a) = +Y。与顶点法线一致，Flat 着色才正确
                mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
            }
        return mesh;
    }

} // namespace meshgen

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

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        Vertex &a = mesh.vertices[mesh.indices[i]];
        Vertex &b = mesh.vertices[mesh.indices[i + 1]];
        Vertex &c = mesh.vertices[mesh.indices[i + 2]];

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
        if (v.tangent.lengthSq() < 1e-12f)
        {
            // 退化：造一个与法线正交的任意切线
            Vec3f n = v.normal;
            Vec3f ref = std::abs(n.y) < 0.99f ? Vec3f{0, 1, 0} : Vec3f{1, 0, 0};
            v.tangent = cross(ref, n).normalized();
        }
        else
        {
            Vec3f n = v.normal.normalized();
            Vec3f t = v.tangent;
            v.tangent = (t - n * dot(n, t)).normalized(); // 去掉法线分量
        }
    }
}
