#pragma once
#include <string>
#include <vector>

#include "math/vec.h"
#include "scene/gltf_types.h"
#include "scene/mesh.h"

// ============================================================
// glTF 模型加载器 —— 使用 tinygltf
//
// glTF 是 Khronos 定义的 3D 场景传输格式（"3D 的 JPEG"），
// 支持网格、材质、骨骼、动画、相机等完整场景描述。
// tinygltf 是单头文件的 C++ 解析器，性能极好。
//
// 源码：third/tinygltf/tiny_gltf.h
// 实现：third/tinygltf/tiny_gltf_impl.cpp
// ============================================================

// tinygltf 头文件（实现在 third/tinygltf/tiny_gltf_impl.cpp）
// 不定义任何 TINYGLTF_NO_* 宏，让头文件正常声明所有函数
#include "tiny_gltf.h"

// stb_image 函数声明（实现在 third_party_impl 中）
extern "C" {
unsigned char* stbi_load_from_memory(const unsigned char* buffer, int len, int* x, int* y, int* channels_in_file,
                                     int desired_channels);
void stbi_image_free(void* retval_from_stbi_load);
}

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

// 加载 glTF/glb 文件
inline LoadResult load(const std::string& path)
{
    LoadResult result;
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = false;
    // 根据扩展名判断 .glb 还是 .gltf
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb")
    {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    }
    else
    {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }

    if (!ret)
    {
        result.error = err;
        return result;
    }

    // 遍历所有 mesh
    for (const auto& gltfMesh : model.meshes)
    {
        for (const auto& primitive : gltfMesh.primitives)
        {
            Mesh mesh;

            // 获取 position accessor
            auto posIt = primitive.attributes.find("POSITION");
            if (posIt == primitive.attributes.end())
                continue;
            const auto& posAccessor = model.accessors[posIt->second];
            const auto& posBufferView = model.bufferViews[posAccessor.bufferView];
            const auto& posBuffer = model.buffers[posBufferView.buffer];
            const float* posData = reinterpret_cast<const float*>(posBuffer.data.data() + posBufferView.byteOffset +
                                                                  posAccessor.byteOffset);
            int vertexCount = static_cast<int>(posAccessor.count);

            // 获取 normal accessor（可选）
            const float* normData = nullptr;
            auto normIt = primitive.attributes.find("NORMAL");
            if (normIt != primitive.attributes.end())
            {
                const auto& nAcc = model.accessors[normIt->second];
                const auto& nBV = model.bufferViews[nAcc.bufferView];
                const auto& nBuf = model.buffers[nBV.buffer];
                normData = reinterpret_cast<const float*>(nBuf.data.data() + nBV.byteOffset + nAcc.byteOffset);
            }

            // 获取 texcoord accessor（可选）
            const float* uvData = nullptr;
            auto uvIt = primitive.attributes.find("TEXCOORD_0");
            if (uvIt != primitive.attributes.end())
            {
                const auto& uvAcc = model.accessors[uvIt->second];
                const auto& uvBV = model.bufferViews[uvAcc.bufferView];
                const auto& uvBuf = model.buffers[uvBV.buffer];
                uvData = reinterpret_cast<const float*>(uvBuf.data.data() + uvBV.byteOffset + uvAcc.byteOffset);
            }

            // 构建顶点
            mesh.vertices.resize(vertexCount);
            for (int i = 0; i < vertexCount; ++i)
            {
                mesh.vertices[i].position = {posData[i * 3 + 0], posData[i * 3 + 1], posData[i * 3 + 2]};
                if (normData)
                    mesh.vertices[i].normal = {normData[i * 3 + 0], normData[i * 3 + 1], normData[i * 3 + 2]};
                if (uvData)
                    mesh.vertices[i].uv = {uvData[i * 2 + 0], uvData[i * 2 + 1]};
            }

            // 索引
            if (primitive.indices >= 0)
            {
                const auto& idxAccessor = model.accessors[primitive.indices];
                const auto& idxBV = model.bufferViews[idxAccessor.bufferView];
                const auto& idxBuf = model.buffers[idxBV.buffer];
                const uint8_t* idxBase = idxBuf.data.data() + idxBV.byteOffset + idxAccessor.byteOffset;

                int indexCount = static_cast<int>(idxAccessor.count);
                mesh.indices.resize(indexCount);
                if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                {
                    const uint16_t* idx16 = reinterpret_cast<const uint16_t*>(idxBase);
                    for (int i = 0; i < indexCount; ++i)
                        mesh.indices[i] = idx16[i];
                }
                else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                {
                    const uint32_t* idx32 = reinterpret_cast<const uint32_t*>(idxBase);
                    for (int i = 0; i < indexCount; ++i)
                        mesh.indices[i] = static_cast<int>(idx32[i]);
                }
                else
                {
                    const uint8_t* idx8 = idxBase;
                    for (int i = 0; i < indexCount; ++i)
                        mesh.indices[i] = idx8[i];
                }
            }
            else
            {
                // 无索引：每 3 个顶点一个三角形
                mesh.indices.resize(vertexCount);
                for (int i = 0; i < vertexCount; ++i)
                    mesh.indices[i] = i;
            }

            // 如果没有法线，计算
            if (!normData && !mesh.vertices.empty())
            {
                for (auto& v : mesh.vertices)
                    v.normal = {0, 0, 0};
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                {
                    auto& a = mesh.vertices[mesh.indices[i]];
                    auto& b = mesh.vertices[mesh.indices[i + 1]];
                    auto& c = mesh.vertices[mesh.indices[i + 2]];
                    Vec3f n = cross(b.position - a.position, c.position - a.position);
                    a.normal = a.normal + n;
                    b.normal = b.normal + n;
                    c.normal = c.normal + n;
                }
                for (auto& v : mesh.vertices)
                    if (v.normal.lengthSq() > 1e-12f)
                        v.normal = v.normal.normalized();
            }

            // 材质索引
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
        if (tex.source < 0 || tex.source >= (int)model.images.size())
            continue;
        const auto& img = model.images[tex.source];

        tga::Image tgaImg;
        bool ok = false;

        // 尝试从 buffer 加载（内嵌 base64 或外部 bin）
        if (!img.image.empty())
        {
            // 用 stb_image 从内存加载
            int w, h, ch;
            unsigned char* data = stbi_load_from_memory(img.image.data(), (int)img.image.size(), &w, &h, &ch, 4);
            if (data)
            {
                tgaImg.width = w;
                tgaImg.height = h;
                tgaImg.pixels.resize(size_t(w) * h);
                for (int i = 0; i < w * h; ++i)
                {
                    tgaImg.pixels[i] = Color(data[i * 4 + 0], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]);
                }
                stbi_image_free(data);
                ok = true;
            }
        }

        if (ok)
        {
            result.textures[ti] = Texture(std::move(tgaImg));
        }
    }

    // 填充材质纹理指针
    for (auto& mat : result.materials)
    {
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
            ga.samplers.push_back(gs);
        }
        result.animations.push_back(std::move(ga));
    }

    result.success = !result.meshes.empty();
    return result;
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
