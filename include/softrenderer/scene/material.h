#pragma once
#include <cstdint>
#include <string>

#include "softrenderer/math/vec.h"

// ============================================================
// Material：PBR metallic-roughness 材质（运行时数据）
//
// 与文件格式无关：不包含 URI、accessor、bufferView 等格式细节，
// 也不持有 Texture 指针——纹理用索引引用，由 SceneAsset 统一持有。
//
// 【为什么用索引而不是指针】材质随 SceneAsset 一起移动/复制；
// 一旦内部存 `Texture*`，移动后指向旧地址即为悬垂指针。索引不会失效，
// 调用方在绑定 Shader 时用 SceneAsset::texture(index) 解析一次即可。
//
// 字段语义对齐 glTF 2.0 的 material 对象：
//   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials
// ============================================================

namespace sr
{

struct Material
{
    // ---- PBR metallic-roughness ----
    Vec3f baseColorFactor{1, 1, 1};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    Vec3f emissiveFactor{0, 0, 0};

    // ---- 纹理索引（-1 = 无纹理）----
    int baseColorTextureIndex = -1;
    int metallicRoughnessTextureIndex = -1;
    int normalTextureIndex = -1;
    int emissiveTextureIndex = -1;

    // ---- Alpha 模式 ----
    enum class AlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Blend
    };
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;

    bool doubleSided = false;

    std::string name;
};

} // namespace sr
