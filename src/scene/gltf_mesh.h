#pragma once
#include <limits>

#include "scene/gltf_accessor.h"
#include "scene/mesh.h"

namespace sr::gltf {
inline bool readMeshPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, Mesh& out,
                              std::string& error)
{
    auto fail = [&](const char* message) {
        error = message;
        return false;
    };
    if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES)
        return fail("only triangle primitives are supported");
    if (primitive.material < -1 || (primitive.material >= 0 && size_t(primitive.material) >= model.materials.size()))
        return fail("invalid material index");
    const auto p = primitive.attributes.find("POSITION");
    if (p == primitive.attributes.end())
        return fail("missing POSITION");
    for (const auto& [name, index] : primitive.attributes)
    {
        if (name != "POSITION" && name != "NORMAL" && name != "TEXCOORD_0")
            continue;
        if (index < 0 || size_t(index) >= model.accessors.size())
            return fail("invalid attribute accessor");
        const auto& accessor = model.accessors[size_t(index)];
        if (accessor.bufferView < 0 || size_t(accessor.bufferView) >= model.bufferViews.size())
            return fail("invalid attribute buffer view");
        const auto& view = model.bufferViews[size_t(accessor.bufferView)];
        if (accessor.byteOffset % 4 || view.byteOffset % 4 || view.byteStride % 4)
            return fail("vertex attributes require four-byte alignment");
    }
    auto position = AccessorView::create(model, p->second, &error);
    if (!position)
        return false;
    if (position->type() != TINYGLTF_TYPE_VEC3 || position->componentType() != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        position->count() > size_t(std::numeric_limits<int>::max()))
        return fail("invalid POSITION format/count");
    std::optional<AccessorView> normal, uv;
    auto attribute = [&](const char* name, std::optional<AccessorView>& view, int type) {
        const auto it = primitive.attributes.find(name);
        if (it == primitive.attributes.end())
            return true;
        view = AccessorView::create(model, it->second, &error);
        if (!view)
            return false;
        if (view->count() > 1 && view->strideBytes() % 4)
            return fail("vertex stride requires four-byte alignment");
        if (view->type() != type || view->count() != position->count())
            return fail("attribute shape/count mismatch");
        return true;
    };
    if (!attribute("NORMAL", normal, TINYGLTF_TYPE_VEC3) || !attribute("TEXCOORD_0", uv, TINYGLTF_TYPE_VEC2))
        return false;
    if (normal && normal->componentType() != TINYGLTF_COMPONENT_TYPE_FLOAT)
        return fail("invalid NORMAL format");
    if (uv && uv->componentType() != TINYGLTF_COMPONENT_TYPE_FLOAT &&
        !(uv->normalized() && (uv->componentType() == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                               uv->componentType() == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)))
        return fail("invalid TEXCOORD_0 format");
    Mesh mesh;
    mesh.vertices.resize(position->count());
    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        auto& v = mesh.vertices[i];
        if (!position->readFloat(i, 0, v.position.x) || !position->readFloat(i, 1, v.position.y) ||
            !position->readFloat(i, 2, v.position.z))
            return fail("nonfinite POSITION");
        if (normal && (!normal->readFloat(i, 0, v.normal.x) || !normal->readFloat(i, 1, v.normal.y) ||
                       !normal->readFloat(i, 2, v.normal.z)))
            return fail("nonfinite NORMAL");
        if (uv && (!uv->readFloat(i, 0, v.uv.x) || !uv->readFloat(i, 1, v.uv.y)))
            return fail("nonfinite TEXCOORD_0");
    }
    if (primitive.indices < -1)
        return fail("invalid index accessor");
    if (primitive.indices >= 0)
    {
        auto indices = AccessorView::create(model, primitive.indices, &error);
        if (!indices)
            return false;
        if (indices->hasStride() || indices->count() % 3 || indices->count() > size_t(std::numeric_limits<int>::max()))
            return fail("invalid triangle index layout");
        mesh.indices.reserve(indices->count());
        for (size_t i = 0; i < indices->count(); ++i)
        {
            uint32_t index = 0;
            if (!indices->readUnsigned(i, index) || index >= position->count())
                return fail("invalid vertex index");
            mesh.indices.push_back(int(index));
        }
    }
    else
    {
        if (position->count() % 3)
            return fail("incomplete triangle");
        for (size_t i = 0; i < position->count(); ++i)
            mesh.indices.push_back(int(i));
    }
    if (!normal)
    {
        for (auto& v : mesh.vertices)
            v.normal = {0, 0, 0};
        for (size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            auto& a = mesh.vertices[size_t(mesh.indices[i])];
            auto& b = mesh.vertices[size_t(mesh.indices[i + 1])];
            auto& c = mesh.vertices[size_t(mesh.indices[i + 2])];
            Vec3f n = cross(b.position - a.position, c.position - a.position);
            if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z))
                return fail("normal calculation overflow");
            a.normal = a.normal + n;
            b.normal = b.normal + n;
            c.normal = c.normal + n;
        }
        for (auto& v : mesh.vertices)
        {
            const float length = v.normal.lengthSq();
            if (!std::isfinite(length))
                return fail("normal accumulation overflow");
            if (length > 1e-12f)
                v.normal = v.normal.normalized();
        }
    }
    out = std::move(mesh);
    error.clear();
    return true;
}
}  // namespace sr::gltf
