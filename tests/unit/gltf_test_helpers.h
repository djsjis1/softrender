/** @file gltf_test_helpers.h
 *  @brief 生成小端 glTF 测试模型；所有数据在内存中，使用非对称分量。
 */
#pragma once
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

#include "tiny_gltf.h"

namespace sr::tests::gltfFixtures {
inline int addBytes(tinygltf::Model& model, const std::vector<unsigned char>& bytes, int type, int componentType,
                    size_t count, size_t stride = 0)
{
    tinygltf::Buffer buffer;
    buffer.data = bytes;
    tinygltf::BufferView view;
    view.buffer = static_cast<int>(model.buffers.size());
    view.byteLength = bytes.size();
    view.byteStride = stride;
    model.buffers.push_back(std::move(buffer));
    tinygltf::Accessor accessor;
    accessor.bufferView = static_cast<int>(model.bufferViews.size());
    accessor.type = type;
    accessor.componentType = componentType;
    accessor.count = count;
    model.bufferViews.push_back(view);
    model.accessors.push_back(accessor);
    return static_cast<int>(model.accessors.size() - 1);
}

inline int addFloats(tinygltf::Model& model, const std::vector<float>& values, int type, size_t count,
                     size_t stride = 0)
{
    std::vector<unsigned char> bytes;
    for (float value : values)
    {
        const auto bits = std::bit_cast<uint32_t>(value);
        for (unsigned int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<unsigned char>((bits >> shift) & 0xff));
    }
    return addBytes(model, bytes, type, TINYGLTF_COMPONENT_TYPE_FLOAT, count, stride);
}
}  // namespace sr::tests::gltfFixtures
