#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "tiny_gltf.h"

namespace sr::gltf {
// Non-owning: the model and its buffers must remain alive and unchanged.
class AccessorView
{
   public:
    static std::optional<AccessorView> create(const tinygltf::Model& model, int index, std::string* error = nullptr)
    {
        auto fail = [&](const char* message) -> std::optional<AccessorView> {
            if (error)
                *error = message;
            return std::nullopt;
        };
        if (index < 0 || size_t(index) >= model.accessors.size())
            return fail("invalid accessor index");
        const auto& a = model.accessors[size_t(index)];
        if (a.sparse.isSparse)
            return fail("sparse accessors are unsupported");
        if (!a.count)
            return fail("empty accessor");
        size_t components = 0;
        switch (a.type)
        {
            case TINYGLTF_TYPE_SCALAR:
                components = 1;
                break;
            case TINYGLTF_TYPE_VEC2:
                components = 2;
                break;
            case TINYGLTF_TYPE_VEC3:
                components = 3;
                break;
            case TINYGLTF_TYPE_VEC4:
                components = 4;
                break;
            case TINYGLTF_TYPE_MAT4:
                if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                    components = 16;
                break;
            default:
                break;
        }
        if (!components)
            return fail("unsupported accessor shape");
        size_t bytes = 0;
        switch (a.componentType)
        {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                bytes = 1;
                break;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                bytes = 2;
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                bytes = 4;
                break;
            default:
                return fail("unsupported component type");
        }
        if (a.normalized && (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT ||
                             a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
            return fail("invalid normalized component type");
        if (a.bufferView < 0 || size_t(a.bufferView) >= model.bufferViews.size())
            return fail("invalid buffer view");
        const auto& v = model.bufferViews[size_t(a.bufferView)];
        if (v.buffer < 0 || size_t(v.buffer) >= model.buffers.size())
            return fail("invalid buffer index");
        const auto& data = model.buffers[size_t(v.buffer)].data;
        if (v.byteOffset > data.size() || v.byteLength > data.size() - v.byteOffset)
            return fail("buffer view exceeds buffer");
        if (a.byteOffset % bytes || v.byteOffset % bytes)
            return fail("misaligned accessor");
        const size_t element = components * bytes;
        const size_t stride = v.byteStride ? v.byteStride : element;
        if (v.byteStride && (stride < 4 || stride > 252 || stride % bytes || stride < element))
            return fail("invalid byte stride");
        if (a.byteOffset > v.byteLength || element > v.byteLength - a.byteOffset ||
            a.count - 1 > (v.byteLength - a.byteOffset - element) / stride)
            return fail("accessor exceeds buffer view");
        if (error)
            error->clear();
        return AccessorView(data.data() + v.byteOffset + a.byteOffset, a.count, stride, bytes, components, a.type,
                            a.componentType, a.normalized, v.byteStride != 0);
    }
    size_t count() const { return count_; }
    int type() const { return type_; }
    int componentType() const { return componentType_; }
    bool normalized() const { return normalized_; }
    bool hasStride() const { return hasStride_; }
    size_t strideBytes() const { return stride_; }
    bool readFloat(size_t index, size_t component, float& out) const
    {
        if (index >= count_ || component >= components_)
            return false;
        const uint32_t u = bits(index, component);
        float value = 0;
        switch (componentType_)
        {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                value = std::bit_cast<float>(u);
                break;
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                value = float(u < 128 ? int(u) : int(u) - 256);
                if (normalized_)
                    value = std::max(value / 127.0f, -1.0f);
                break;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                value = float(u < 32768 ? int(u) : int(u) - 65536);
                if (normalized_)
                    value = std::max(value / 32767.0f, -1.0f);
                break;
            default:
                value = float(u);
                if (normalized_)
                    value /= bytes_ == 1 ? 255.0f : 65535.0f;
                break;
        }
        if (!std::isfinite(value))
            return false;
        out = value;
        return true;
    }
    bool readUnsigned(size_t index, uint32_t& out) const
    {
        if (index >= count_ || type_ != TINYGLTF_TYPE_SCALAR || normalized_ ||
            (componentType_ != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
             componentType_ != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
             componentType_ != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
            return false;
        out = bits(index, 0);
        return true;
    }

   private:
    AccessorView(const unsigned char* data, size_t count, size_t stride, size_t bytes, size_t components, int type,
                 int componentType, bool normalized, bool hasStride)
        : data_(data),
          count_(count),
          stride_(stride),
          bytes_(bytes),
          components_(components),
          type_(type),
          componentType_(componentType),
          normalized_(normalized),
          hasStride_(hasStride)
    {
    }
    uint32_t bits(size_t index, size_t component) const
    {
        const auto* p = data_ + index * stride_ + component * bytes_;
        uint32_t value = 0;
        for (size_t b = 0; b < bytes_; ++b)
            value |= uint32_t(p[b]) << (8 * b);
        return value;
    }
    const unsigned char* data_;
    size_t count_, stride_, bytes_, components_;
    int type_, componentType_;
    bool normalized_, hasStride_;
};
}  // namespace sr::gltf
