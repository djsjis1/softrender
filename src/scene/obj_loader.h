#pragma once
#include <string>
#include <unordered_map>

#include "scene/mesh.h"

// ============================================================
// OBJ 模型加载器 —— 使用 tinyobjloader
//
// tinyobjloader 是单头文件的 Wavefront OBJ 解析器，
// 支持 v/vt/vn/f、mtllib、groups、负索引、多边形三角化等。
// 比自己写的解析器更快、更完整、更健壮。
//
// 源码：third/tiny_obj_loader.h
// 实现：third/tiny_obj_loader_impl.cpp
// ============================================================

// tinyobjloader 头文件（实现在 third/tiny_obj_loader_impl.cpp）
#include "tiny_obj_loader.h"

namespace objloader {

// 加载 OBJ 文件到 Mesh。接口保持不变。
inline bool load(const std::string& path, Mesh& out)
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

    out.vertices.clear();
    out.indices.clear();

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

    for (const auto& shape : shapes)
    {
        const auto& mesh = shape.mesh;

        // 逐索引去重后生成顶点
        for (size_t i = 0; i < mesh.indices.size(); ++i)
        {
            const auto& idx = mesh.indices[i];
            IndexKey key{idx.vertex_index, idx.texcoord_index, idx.normal_index};

            auto it = indexCache.find(key);
            if (it != indexCache.end())
            {
                out.indices.push_back(it->second);
                continue;
            }

            Vertex v;

            // 位置
            if (idx.vertex_index >= 0)
            {
                int vi = idx.vertex_index;
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

            indexCache.emplace(key, static_cast<int>(out.vertices.size()));
            out.indices.push_back(static_cast<int>(out.vertices.size()));
            out.vertices.push_back(v);
        }
    }

    // 如果没有法线，计算平滑法线（顶点已去重，累加会在共享顶点上合并）
    if (attrib.normals.empty() && !out.vertices.empty())
    {
        for (auto& v : out.vertices)
            v.normal = {0, 0, 0};
        for (size_t i = 0; i + 2 < out.indices.size(); i += 3)
        {
            Vertex& a = out.vertices[out.indices[i]];
            Vertex& b = out.vertices[out.indices[i + 1]];
            Vertex& c = out.vertices[out.indices[i + 2]];
            Vec3f n = cross(b.position - a.position, c.position - a.position);
            a.normal = a.normal + n;
            b.normal = b.normal + n;
            c.normal = c.normal + n;
        }
        for (auto& v : out.vertices)
            if (v.normal.lengthSq() > 1e-12f)
                v.normal = v.normal.normalized();
    }

    computeTangents(out);  // 法线就绪后统一算切线

    return !out.vertices.empty();
}

}  // namespace objloader
