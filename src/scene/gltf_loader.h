#pragma once
#include <string>
#include <vector>

#include "math/vec.h"
#include "scene/animation_player.h"
#include "scene/gltf_mesh.h"
#include "scene/gltf_types.h"

// ============================================================
// glTF 模型加载器 —— 使用 tinygltf
//
// glTF 是 Khronos 定义的 3D 场景传输格式（"3D 的 JPEG"），
// 支持网格、材质、骨骼、动画、相机等完整场景描述。
// tinygltf 是单头文件的 C++ 解析器，性能极好。
//
// 源码：third/tinygltf/tiny_gltf.h
// 实现：src/scene/gltf_loader_impl.cpp
// ============================================================

// tinygltf 头文件（实现在 src/scene/gltf_loader_impl.cpp）
// 不定义任何 TINYGLTF_NO_* 宏，让头文件正常声明所有函数
#include "tiny_gltf.h"

namespace gltfloader {

// 加载结果：可能包含多个 mesh（glTF 场景可有多个节点）
// 注意：纹理存储在本结构的 textures 成员中，材质里是裸指针。
// 结构可移动（vector 移动不重新分配，指针保持有效），但禁止拷贝。
struct LoadResult
{
    std::vector<Mesh> meshes;
    std::vector<int> meshMaterialIndices;  // 每个 mesh 对应的材质索引
    std::vector<GltfMaterial> materials;
    std::vector<GltfNode> nodes;
    std::vector<GltfAnimation> animations;
    std::vector<Texture> textures;  // 纹理生命周期随结果对象
    bool success = false;
    std::string error;

    LoadResult() = default;
    LoadResult(LoadResult&&) = default;
    LoadResult& operator=(LoadResult&&) = default;
    LoadResult(const LoadResult&) = delete;
    LoadResult& operator=(const LoadResult&) = delete;
};

}  // namespace gltfloader

namespace sr::gltf {
// Convert an already parsed model. Failure never exposes partially converted resources.
inline gltfloader::LoadResult convertModel(const tinygltf::Model& model)
{
    gltfloader::LoadResult result;
    auto fail = [](std::string message) {
        gltfloader::LoadResult failed;
        failed.error = std::move(message);
        return failed;
    };
    for (const auto& source : model.meshes)
    {
        for (const auto& primitive : source.primitives)
        {
            Mesh mesh;
            std::string error;
            if (!readMeshPrimitive(model, primitive, mesh, error))
                return fail("mesh: " + error);
            result.meshMaterialIndices.push_back(primitive.material);
            result.meshes.push_back(std::move(mesh));
        }
    }
    // 解析材质
    for (const auto& mat : model.materials)
    {
        GltfMaterial m;
        m.name = mat.name;
        m.doubleSided = mat.doubleSided;

        // PBR metallic-roughness
        if (mat.pbrMetallicRoughness.baseColorFactor.size() >= 3)
        {
            m.baseColorFactor = {(float)mat.pbrMetallicRoughness.baseColorFactor[0],
                                 (float)mat.pbrMetallicRoughness.baseColorFactor[1],
                                 (float)mat.pbrMetallicRoughness.baseColorFactor[2]};
        }
        m.metallicFactor = (float)mat.pbrMetallicRoughness.metallicFactor;
        m.roughnessFactor = (float)mat.pbrMetallicRoughness.roughnessFactor;

        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0)
            m.baseColorTextureIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
            m.metallicRoughnessTextureIndex = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
        if (mat.normalTexture.index >= 0)
            m.normalTextureIndex = mat.normalTexture.index;
        if (mat.emissiveTexture.index >= 0)
            m.emissiveTextureIndex = mat.emissiveTexture.index;

        if (mat.emissiveFactor.size() >= 3)
        {
            m.emissiveFactor = {(float)mat.emissiveFactor[0], (float)mat.emissiveFactor[1],
                                (float)mat.emissiveFactor[2]};
        }

        if (mat.alphaMode == "MASK")
            m.alphaMode = GltfMaterial::AlphaMode::Mask;
        else if (mat.alphaMode == "BLEND")
            m.alphaMode = GltfMaterial::AlphaMode::Blend;
        m.alphaCutoff = (float)mat.alphaCutoff;

        result.materials.push_back(std::move(m));
    }

    // 解析节点
    for (const auto& node : model.nodes)
    {
        GltfNode n;
        n.name = node.name;
        n.meshIndex = node.mesh;
        n.skinIndex = node.skin;
        n.children = node.children;
        if (node.translation.size() >= 3)
            n.translation = {(float)node.translation[0], (float)node.translation[1], (float)node.translation[2]};
        if (node.scale.size() >= 3)
            n.scale = {(float)node.scale[0], (float)node.scale[1], (float)node.scale[2]};
        if (node.rotation.size() >= 4)
        {
            n.rotation[0] = (float)node.rotation[0];
            n.rotation[1] = (float)node.rotation[1];
            n.rotation[2] = (float)node.rotation[2];
            n.rotation[3] = (float)node.rotation[3];
        }
        if (node.matrix.size() >= 16)
        {
            // glTF 规范中 matrix 按列主序存储，而 Mat4 是行主序，需转置写入
            n.hasMatrix = true;
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    n.matrix.m[row][col] = (float)node.matrix[col * 4 + row];
        }
        result.nodes.push_back(std::move(n));
    }

    // 解析纹理：加载 images 并创建 Texture 对象
    // 纹理存入 result.textures，生命周期随结果对象（避免静态存储导致
    // 二次加载时旧结果中的材质纹理指针悬空）
    result.textures.resize(model.textures.size());

    for (size_t ti = 0; ti < model.textures.size(); ++ti)
    {
        const auto& tex = model.textures[ti];
        if (tex.source < 0 || size_t(tex.source) >= model.images.size())
            return fail("texture: invalid source image");
        const auto& img = model.images[tex.source];

        // TinyGLTF has already decoded image.image into pixels.
        if (img.as_is || img.width <= 0 || img.height <= 0 || img.component < 1 || img.component > 4 || img.bits != 8 ||
            img.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
            return fail("texture: only decoded 8-bit images are supported");
        const size_t width = size_t(img.width), height = size_t(img.height), channels = size_t(img.component);
        if (width > std::numeric_limits<size_t>::max() / height)
            return fail("texture size overflow");
        const size_t count = width * height;
        if (count > size_t(std::numeric_limits<int>::max()))
            return fail("texture exceeds renderer pixel limit");
        if (count > img.image.size() / channels)
            return fail("truncated decoded texture");
        tga::Image image;
        image.width = img.width;
        image.height = img.height;
        image.pixels.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            const auto* pixel = img.image.data() + i * channels;
            const auto r = pixel[0];
            const auto g = channels < 3 ? r : pixel[1];
            const auto b = channels < 3 ? r : pixel[2];
            const auto alpha = channels == 2 ? pixel[1] : channels == 4 ? pixel[3] : static_cast<unsigned char>(255);
            image.pixels[i] = Color(r, g, b, alpha);
        }
        result.textures[ti] = Texture(std::move(image));
    }
    // 填充材质纹理指针
    for (auto& mat : result.materials)
    {
        for (int index : {mat.baseColorTextureIndex, mat.metallicRoughnessTextureIndex, mat.normalTextureIndex,
                          mat.emissiveTextureIndex})
            if (index < -1 || (index >= 0 && size_t(index) >= result.textures.size()))
                return fail("material: invalid texture index");
        if (mat.baseColorTextureIndex >= 0 && mat.baseColorTextureIndex < (int)result.textures.size())
            mat.baseColorTexture = &result.textures[mat.baseColorTextureIndex];
        if (mat.metallicRoughnessTextureIndex >= 0 && mat.metallicRoughnessTextureIndex < (int)result.textures.size())
            mat.metallicRoughnessTexture = &result.textures[mat.metallicRoughnessTextureIndex];
        if (mat.normalTextureIndex >= 0 && mat.normalTextureIndex < (int)result.textures.size())
            mat.normalTexture = &result.textures[mat.normalTextureIndex];
    }

    // 解析动画
    for (const auto& anim : model.animations)
    {
        GltfAnimation ga;
        ga.name = anim.name;
        for (const auto& ch : anim.channels)
        {
            GltfAnimation::Channel gc;
            gc.samplerIndex = ch.sampler;
            gc.targetNode = ch.target_node;
            if (ch.target_path == "translation")
                gc.path = GltfAnimation::Channel::Path::Translation;
            else if (ch.target_path == "rotation")
                gc.path = GltfAnimation::Channel::Path::Rotation;
            else if (ch.target_path == "scale")
                gc.path = GltfAnimation::Channel::Path::Scale;
            else
                return fail("unsupported animation path: " + ch.target_path);
            ga.channels.push_back(gc);
        }
        for (const auto& s : anim.samplers)
        {
            GltfAnimation::Sampler gs;
            gs.input = s.input;
            gs.output = s.output;
            if (s.interpolation == "STEP")
                gs.interpolation = GltfAnimation::Sampler::Interpolation::Step;
            else if (s.interpolation == "CUBICSPLINE")
                gs.interpolation = GltfAnimation::Sampler::Interpolation::CubicSpline;
            else if (s.interpolation != "LINEAR" && !s.interpolation.empty())
                return fail("unsupported interpolation");
            ga.samplers.push_back(gs);
        }
        for (const auto& channel : ga.channels)
        {
            AnimSample sample;
            if (!sampleChannel(model, ga, channel, 0, sample))
                return fail("invalid animation channel");
            const auto& sampler = ga.samplers[size_t(channel.samplerIndex)];
            const auto values = AccessorView::create(model, sampler.output);
            const bool rotation = channel.path == GltfAnimation::Channel::Path::Rotation;
            const bool cubic = sampler.interpolation == GltfAnimation::Sampler::Interpolation::CubicSpline;
            for (size_t i = 0; i < values->count(); ++i)
            {
                double lengthSquared = 0;
                for (size_t c = 0; c < (rotation ? 4u : 3u); ++c)
                {
                    float value = 0;
                    if (!values->readFloat(i, c, value))
                        return fail("nonfinite animation output");
                    lengthSquared += double(value) * value;
                }
                if (rotation && (!cubic || i % 3 == 1) && lengthSquared == 0)
                    return fail("zero animation quaternion");
            }
        }
        result.animations.push_back(std::move(ga));
    }

    if (result.meshes.empty())
        return fail("model contains no supported meshes");
    result.success = true;
    return result;
}

}  // namespace sr::gltf

namespace gltfloader {
inline LoadResult load(const std::string& path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string error, warning;
    const bool binary = path.size() >= 4 && path.substr(path.size() - 4) == ".glb";
    const bool loaded = binary ? loader.LoadBinaryFromFile(&model, &error, &warning, path)
                               : loader.LoadASCIIFromFile(&model, &error, &warning, path);
    if (!loaded)
    {
        LoadResult result;
        result.error = error.empty() ? "unable to parse glTF file" : error;
        return result;
    }
    return sr::gltf::convertModel(model);
}
// 便捷接口：加载第一个 mesh
inline bool loadFirstMesh(const std::string& path, Mesh& out)
{
    auto result = load(path);
    if (!result.success)
        return false;
    out = std::move(result.meshes[0]);
    return true;
}

}  // namespace gltfloader
