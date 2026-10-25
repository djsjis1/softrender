#include "softrenderer/io/obj_loader.h"

#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

#include "tiny_obj_loader.h"

namespace sr::io {

// 加载 OBJ 文件到 Mesh。
//
// 【为何自己再查一次索引上界】tiny_obj_loader 对正索引只做 -1 偏移，越界仅
// 追加一条 warning 并返回成功（见 third/tiny_obj_loader.h 中 `Vertex indices
// out of bounds` 分支）。若直接把它导出的索引当有效索引用，`f 1 2 99` 这类
// 文件会在这里越界读 attrib 数组。因此包装层必须独立校验，不能依赖上游。
//
// 【失败不改 out】先在局部 Mesh 上构造，全部成功后才移动赋值：失败时调用方
// 传进来的旧数据保持原样，不会拿到半成品。
bool loadObjMesh(const std::string& path, Mesh& out)
{
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;  // 自动三角化多边形

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config))
    {
        return false;
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    // 三个独立数组必须能整除各自的元组大小，否则下面的除法计数不可信。
    if (attrib.vertices.size() % 3 != 0 || attrib.normals.size() % 3 != 0 || attrib.texcoords.size() % 2 != 0)
        return false;
    const int vertexCount = static_cast<int>(attrib.vertices.size() / 3);
    const int normalCount = static_cast<int>(attrib.normals.size() / 3);
    const int texcoordCount = static_cast<int>(attrib.texcoords.size() / 2);

    Mesh loaded;

    // OBJ 的索引把 位置/UV/法线 拆成三个独立数组，同一个 (v,vt,vn)
    // 组合可能被多个面共享。这里按组合键去重，既减少重复顶点，
    // 也让后面的平滑法线累加真正作用于共享顶点上。
    struct IndexKey
    {
        int v, t, n;
        bool operator==(const IndexKey& o) const { return v == o.v && t == o.t && n == o.n; }
    };
    struct IndexKeyHash
    {
        size_t operator()(const IndexKey& k) const
        {
            // 混合三个索引做哈希，-1 映射为特殊值避免与 0 冲突
            size_t h = static_cast<size_t>(static_cast<unsigned>(k.v));
            h ^= static_cast<size_t>(static_cast<unsigned>(k.t)) * 0x9E3779B1u + 0x9E3779B9u + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(static_cast<unsigned>(k.n)) * 0x85EBCA77u + 0x9E3779B9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<IndexKey, int, IndexKeyHash> indexCache;

    // 预留容量：50k 面 / 150k 索引的模型不预留会经历多次扩容与 rehash，
    // 实测这一项就占了解析耗时的可观部分。去重后顶点数必然 ≤ 索引数。
    size_t totalIndices = 0;
    for (const auto& shape : shapes)
        totalIndices += shape.mesh.indices.size();
    const size_t expectedVertices = std::min(totalIndices, size_t(vertexCount));
    indexCache.reserve(expectedVertices);
    loaded.indices.reserve(totalIndices);
    loaded.vertices.reserve(expectedVertices);

    for (const auto& shape : shapes)
    {
        const auto& mesh = shape.mesh;

        // 索引数量不是 3 的倍数时，后续按三角形访问会越界。
        if (mesh.indices.size() % 3 != 0)
            return false;

        // 逐索引去重后生成顶点
        for (size_t i = 0; i < mesh.indices.size(); ++i)
        {
            const auto& idx = mesh.indices[i];

            // 位置索引必需且必须在范围内；法线/UV 索引可为负（未提供）。
            if (idx.vertex_index < 0 || idx.vertex_index >= vertexCount || idx.normal_index >= normalCount ||
                idx.texcoord_index >= texcoordCount)
                return false;

            IndexKey key{idx.vertex_index, idx.texcoord_index, idx.normal_index};

            auto it = indexCache.find(key);
            if (it != indexCache.end())
            {
                loaded.indices.push_back(it->second);
                continue;
            }

            // 目标索引是 int：顶点数必须能落在 int 范围内。
            if (loaded.vertices.size() >= size_t(std::numeric_limits<int>::max()))
                return false;

            Vertex v;

            // 位置
            {
                const int vi = idx.vertex_index;
                v.position = {attrib.vertices[3 * vi + 0], attrib.vertices[3 * vi + 1], attrib.vertices[3 * vi + 2]};
            }

            // 法线
            if (idx.normal_index >= 0)
            {
                int ni = idx.normal_index;
                v.normal = {attrib.normals[3 * ni + 0], attrib.normals[3 * ni + 1], attrib.normals[3 * ni + 2]};
            }

            // UV
            if (idx.texcoord_index >= 0)
            {
                int ti = idx.texcoord_index;
                v.uv = {attrib.texcoords[2 * ti + 0], attrib.texcoords[2 * ti + 1]};
            }

            indexCache.emplace(key, static_cast<int>(loaded.vertices.size()));
            loaded.indices.push_back(static_cast<int>(loaded.vertices.size()));

            // 文本里的 nan / inf 能通过 strtod，会原样进入 Mesh：非法位置会让
            // 包围盒、平滑法线、蒙皮结果全部变 NaN，必须在入口拒绝。
            if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z) ||
                !std::isfinite(v.normal.x) || !std::isfinite(v.normal.y) || !std::isfinite(v.normal.z) ||
                !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y))
                return false;

            loaded.vertices.push_back(v);
        }
    }

    // 如果没有法线，计算平滑法线（顶点已去重，累加会在共享顶点上合并）
    if (attrib.normals.empty() && !loaded.vertices.empty())
    {
        for (auto& v : loaded.vertices)
            v.normal = {0, 0, 0};
        for (size_t i = 0; i + 2 < loaded.indices.size(); i += 3)
        {
            Vertex& a = loaded.vertices[loaded.indices[i]];
            Vertex& b = loaded.vertices[loaded.indices[i + 1]];
            Vertex& c = loaded.vertices[loaded.indices[i + 2]];
            Vec3f n = cross(b.position - a.position, c.position - a.position);
            a.normal = a.normal + n;
            b.normal = b.normal + n;
            c.normal = c.normal + n;
        }
        for (auto& v : loaded.vertices)
            if (v.normal.lengthSq() > 1e-12f)
                v.normal = v.normal.normalized();
    }

    computeTangents(loaded);  // 法线就绪后统一算切线

    if (loaded.vertices.empty())
        return false;

    out = std::move(loaded);
    return true;
}

}  // namespace objloader
