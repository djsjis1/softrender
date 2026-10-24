#pragma once
#include <cstddef>
#include <vector>

#include "softrenderer/resource/mesh.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/scene/animation_clip.h"
#include "softrenderer/scene/material.h"
#include "softrenderer/scene/scene_node.h"
#include "softrenderer/scene/skin.h"

// ============================================================
// SceneAsset：一次加载得到的完整场景资源
//
// 持有网格、材质、纹理、节点、蒙皮与动画片段，是加载层与使用层之间
// 唯一的数据契约：
//   - 不出现任何文件格式概念（URI / accessor / bufferView）
//   - 不出现任何解析器类型（tinygltf / tinyobj / stbi）
//   - 资源之间用索引互相引用，因此整体可安全移动或复制
//
// 用法：
//   auto result = sr::io::loadGltfScene("model.glb");
//   if (!result.success) { /* result.error 是给用户看的文本 */ }
//   const sr::SceneAsset& scene = result.scene;
//   const sr::Material* mat = scene.materialForMesh(0);
//   const sr::Texture* albedo = scene.texture(mat ? mat->baseColorTextureIndex : -1);
// ============================================================

namespace sr
{

struct SceneAsset
{
    std::vector<Mesh> meshes;
    std::vector<int> meshMaterialIndices; // 与 meshes 等长，-1 = 无材质
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<SceneNode> nodes;
    std::vector<Skin> skins;
    std::vector<AnimationClip> animations;

    // 越界或负数索引返回 nullptr（不抛异常，调用方按"无纹理"处理）
    const Texture *texture(int index) const
    {
        if (index < 0 || size_t(index) >= textures.size())
            return nullptr;
        return &textures[size_t(index)];
    }

    const Material *material(int index) const
    {
        if (index < 0 || size_t(index) >= materials.size())
            return nullptr;
        return &materials[size_t(index)];
    }

    // 某个网格对应的材质（无则 nullptr）
    const Material *materialForMesh(int meshIndex) const
    {
        if (meshIndex < 0 || size_t(meshIndex) >= meshMaterialIndices.size())
            return nullptr;
        return material(meshMaterialIndices[size_t(meshIndex)]);
    }

    bool empty() const { return meshes.empty(); }
};

} // namespace sr
