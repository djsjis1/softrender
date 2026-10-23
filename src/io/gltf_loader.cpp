#include "softrenderer/io/gltf_loader.h"

#include <limits>
#include <utility>
#include <vector>

#include "gltf_accessor.h"
#include "gltf_convert.h"
#include "gltf_mesh.h"
#include "tiny_gltf.h"

// ============================================================
// glTF 加载实现（私有）
//
// 分两段：
//   1. convertModel：tinygltf::Model → sr::SceneAsset，逐项校验，
//      任一项失败都整体失败，绝不发布半成品（ADR-002）。
//   2. loadGltfScene：解析文件（.gltf / .glb）+ 调用转换。
//
// 这里的类型（tinygltf、AccessorView）只出现在本编译单元，
// 公共头 softrenderer/io/gltf_loader.h 只有自有类型。
// ============================================================

namespace sr::gltf
{

namespace
{

// 统一的失败结果：scene 一定为空
sr::io::SceneLoadResult fail(std::string message)
{
    sr::io::SceneLoadResult result;
    result.error = std::move(message);
    return result;
}

// 把 accessor 里的 MAT4 读成行主序 Mat4（glTF 按列主序存储）
bool readMatrix(const AccessorView &view, size_t index, Mat4 &out)
{
    if (view.type() != TINYGLTF_TYPE_MAT4 || view.hasStride())
        return false;
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            if (!view.readFloat(index, column * 4 + row, out.m[row][column]))
                return false;
    return true;
}

// 解码一个动画采样器：时间轴 + 关键帧值（CubicSpline 含切线）
bool decodeSampler(const tinygltf::Model &model, const tinygltf::AnimationSampler &source,
                   const AnimationChannel::Path path, AnimationSampler &out, std::string &error)
{
    const auto input = AccessorView::create(model, source.input);
    const auto output = AccessorView::create(model, source.output);
    const bool rotation = path == AnimationChannel::Path::Rotation;
    const bool cubic = source.interpolation == "CUBICSPLINE";
    if (!input || !output)
    {
        error = "animation: invalid accessor";
        return false;
    }
    if (input->hasStride() || output->hasStride() || input->type() != TINYGLTF_TYPE_SCALAR ||
        input->componentType() != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        output->type() != (rotation ? TINYGLTF_TYPE_VEC4 : TINYGLTF_TYPE_VEC3) ||
        output->normalized())
    {
        error = "animation: unsupported accessor layout";
        return false;
    }
    if (output->componentType() != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        error = "animation: unsupported component type";
        return false;
    }
    const size_t stride = cubic ? 3 : 1;
    if (output->count() % stride || output->count() / stride != input->count())
    {
        error = "animation: key count mismatch";
        return false;
    }

    out.times.clear();
    out.values.clear();
    out.times.reserve(input->count());
    out.values.reserve(output->count());

    float previous = -1.0f;
    for (size_t i = 0; i < input->count(); ++i)
    {
        float timestamp = 0.0f;
        if (!input->readFloat(i, 0, timestamp) || timestamp < 0.0f || timestamp <= previous)
        {
            error = "animation: non-monotonic or nonfinite timeline";
            return false;
        }
        previous = timestamp;
        out.times.push_back(timestamp);
    }

    const size_t components = rotation ? 4u : 3u;
    for (size_t i = 0; i < output->count(); ++i)
    {
        Vec4f value{0, 0, 0, 0};
        float raw[4]{};
        double lengthSquared = 0.0;
        for (size_t c = 0; c < components; ++c)
        {
            if (!output->readFloat(i, c, raw[c]))
            {
                error = "animation: nonfinite key value";
                return false;
            }
            lengthSquared += double(raw[c]) * raw[c];
        }
        value = {raw[0], raw[1], raw[2], components == 4 ? raw[3] : 0.0f};
        // 四元数关键帧不能是零向量（切线允许为零）
        if (rotation && (!cubic || i % 3 == 1) && lengthSquared == 0.0)
        {
            error = "animation: zero quaternion";
            return false;
        }
        out.values.push_back(value);
    }
    return true;
}

} // namespace

sr::io::SceneLoadResult convertModel(const tinygltf::Model &model)
{
    sr::io::SceneLoadResult result;

    // ---- 网格 ----
    for (const auto &source : model.meshes)
    {
        for (const auto &primitive : source.primitives)
        {
            Mesh mesh;
            std::string error;
            if (!readMeshPrimitive(model, primitive, mesh, error))
                return fail("mesh: " + error);
            result.scene.meshMaterialIndices.push_back(primitive.material);
            result.scene.meshes.push_back(std::move(mesh));
        }
    }
    if (result.scene.meshes.empty())
        return fail("model contains no supported meshes");

    // ---- 材质（只存纹理索引，不存指针）----
    for (const auto &mat : model.materials)
    {
        Material m;
        m.name = mat.name;
        m.doubleSided = mat.doubleSided;
        if (mat.pbrMetallicRoughness.baseColorFactor.size() >= 3)
        {
            m.baseColorFactor = {float(mat.pbrMetallicRoughness.baseColorFactor[0]),
                                 float(mat.pbrMetallicRoughness.baseColorFactor[1]),
                                 float(mat.pbrMetallicRoughness.baseColorFactor[2])};
        }
        m.metallicFactor = float(mat.pbrMetallicRoughness.metallicFactor);
        m.roughnessFactor = float(mat.pbrMetallicRoughness.roughnessFactor);
        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0)
            m.baseColorTextureIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
            m.metallicRoughnessTextureIndex = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
        if (mat.normalTexture.index >= 0)
            m.normalTextureIndex = mat.normalTexture.index;
        if (mat.emissiveTexture.index >= 0)
            m.emissiveTextureIndex = mat.emissiveTexture.index;
        if (mat.emissiveFactor.size() >= 3)
            m.emissiveFactor = {float(mat.emissiveFactor[0]), float(mat.emissiveFactor[1]), float(mat.emissiveFactor[2])};
        if (mat.alphaMode == "MASK")
            m.alphaMode = Material::AlphaMode::Mask;
        else if (mat.alphaMode == "BLEND")
            m.alphaMode = Material::AlphaMode::Blend;
        m.alphaCutoff = float(mat.alphaCutoff);
        result.scene.materials.push_back(std::move(m));
    }

    // ---- 节点 ----
    for (const auto &node : model.nodes)
    {
        SceneNode n;
        n.name = node.name;
        n.meshIndex = node.mesh;
        n.skinIndex = node.skin;
        n.children = node.children;
        if (node.translation.size() >= 3)
            n.translation = {float(node.translation[0]), float(node.translation[1]), float(node.translation[2])};
        if (node.scale.size() >= 3)
            n.scale = {float(node.scale[0]), float(node.scale[1]), float(node.scale[2])};
        if (node.rotation.size() >= 4)
            n.rotation = {float(node.rotation[0]), float(node.rotation[1]), float(node.rotation[2]),
                          float(node.rotation[3])};
        if (node.matrix.size() >= 16)
        {
            // glTF 规范中 matrix 按列主序存储，而 Mat4 是行主序，需转置写入
            n.hasMatrix = true;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    n.matrix.m[row][column] = float(node.matrix[size_t(column) * 4 + size_t(row)]);
        }
        result.scene.nodes.push_back(std::move(n));
    }

    // ---- 纹理：解码后的像素转成 sr::Image，再构造 Texture ----
    result.scene.textures.resize(model.textures.size());
    for (size_t ti = 0; ti < model.textures.size(); ++ti)
    {
        const auto &texture = model.textures[ti];
        if (texture.source < 0 || size_t(texture.source) >= model.images.size())
            return fail("texture: invalid source image");
        const auto &image = model.images[size_t(texture.source)];

        // TinyGLTF 已经把 image.image 解码成像素
        if (image.as_is || image.width <= 0 || image.height <= 0 || image.component < 1 || image.component > 4 ||
            image.bits != 8 || image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
            return fail("texture: only decoded 8-bit images are supported");
        const size_t width = size_t(image.width), height = size_t(image.height), channels = size_t(image.component);
        if (width > std::numeric_limits<size_t>::max() / height)
            return fail("texture size overflow");
        const size_t count = width * height;
        if (count > size_t(std::numeric_limits<int>::max()))
            return fail("texture exceeds renderer pixel limit");
        if (count > image.image.size() / channels)
            return fail("truncated decoded texture");
        sr::Image decoded;
        decoded.width = image.width;
        decoded.height = image.height;
        decoded.pixels.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            const auto *pixel = image.image.data() + i * channels;
            const auto r = pixel[0];
            const auto g = channels < 3 ? r : pixel[1];
            const auto b = channels < 3 ? r : pixel[2];
            const auto alpha = channels == 2 ? pixel[1] : channels == 4 ? pixel[3] : static_cast<unsigned char>(255);
            decoded.pixels[i] = Color(r, g, b, alpha);
        }
        result.scene.textures[ti] = Texture(std::move(decoded));
    }

    // 材质纹理索引必须落在 textures 范围内
    for (const auto &material : result.scene.materials)
    {
        for (int index :
             {material.baseColorTextureIndex, material.metallicRoughnessTextureIndex, material.normalTextureIndex,
              material.emissiveTextureIndex})
            if (index < -1 || (index >= 0 && size_t(index) >= result.scene.textures.size()))
                return fail("material: invalid texture index");
    }
    // 网格材质索引必须落在 materials 范围内
    for (int index : result.scene.meshMaterialIndices)
        if (index < -1 || (index >= 0 && size_t(index) >= result.scene.materials.size()))
            return fail("mesh: invalid material index");

    // ---- 蒙皮：逆绑定矩阵在此解码成 Mat4 ----
    for (const auto &source : model.skins)
    {
        Skin skin;
        skin.name = source.name;
        skin.joints = source.joints;
        skin.skeleton = source.skeleton;
        for (int joint : skin.joints)
            if (joint < 0 || size_t(joint) >= result.scene.nodes.size())
                return fail("skin: invalid joint index");
        if (source.inverseBindMatrices >= 0)
        {
            const auto matrices = AccessorView::create(model, source.inverseBindMatrices);
            if (!matrices || matrices->type() != TINYGLTF_TYPE_MAT4 || matrices->hasStride() ||
                matrices->count() != skin.joints.size())
                return fail("skin: invalid inverse bind matrices");
            skin.inverseBindMatrices.resize(matrices->count());
            for (size_t j = 0; j < matrices->count(); ++j)
                if (!readMatrix(*matrices, j, skin.inverseBindMatrices[j]))
                    return fail("skin: nonfinite inverse bind matrix");
        }
        result.scene.skins.push_back(std::move(skin));
    }
    // 节点的蒙皮索引必须有效
    for (const auto &node : result.scene.nodes)
        if (node.skinIndex < -1 || (node.skinIndex >= 0 && size_t(node.skinIndex) >= result.scene.skins.size()))
            return fail("node: invalid skin index");

    // ---- 动画：解码为 AnimationClip，运行时不再访问 accessor ----
    for (const auto &animation : model.animations)
    {
        AnimationClip clip;
        clip.name = animation.name;

        for (const auto &channel : animation.channels)
        {
            AnimationChannel decoded;
            decoded.samplerIndex = channel.sampler;
            decoded.targetNode = channel.target_node;
            if (channel.target_path == "translation")
                decoded.path = AnimationChannel::Path::Translation;
            else if (channel.target_path == "rotation")
                decoded.path = AnimationChannel::Path::Rotation;
            else if (channel.target_path == "scale")
                decoded.path = AnimationChannel::Path::Scale;
            else
                return fail("unsupported animation path: " + channel.target_path);
            if (decoded.samplerIndex < 0 || size_t(decoded.samplerIndex) >= animation.samplers.size())
                return fail("animation: invalid sampler index");
            if (decoded.targetNode < 0 || size_t(decoded.targetNode) >= result.scene.nodes.size())
                return fail("animation: invalid target node");
            clip.channels.push_back(decoded);
        }

        for (size_t si = 0; si < animation.samplers.size(); ++si)
        {
            const auto &source = animation.samplers[si];
            AnimationSampler decoded;
            // 采样器的插值方式随引用它的通道而定；未引用的采样器按 LINEAR 处理
            AnimationChannel::Path path = AnimationChannel::Path::Translation;
            for (const auto &channel : clip.channels)
                if (size_t(channel.samplerIndex) == si)
                    path = channel.path;

            if (source.interpolation == "STEP")
                decoded.interpolation = AnimationSampler::Interpolation::Step;
            else if (source.interpolation == "CUBICSPLINE")
                decoded.interpolation = AnimationSampler::Interpolation::CubicSpline;
            else if (source.interpolation != "LINEAR" && !source.interpolation.empty())
                return fail("unsupported interpolation: " + source.interpolation);

            std::string error;
            if (!decodeSampler(model, source, path, decoded, error))
                return fail(error);
            clip.samplers.push_back(std::move(decoded));
        }
        result.scene.animations.push_back(std::move(clip));
    }

    result.success = true;
    return result;
}

} // namespace sr::gltf

namespace sr::io
{

SceneLoadResult loadGltfScene(const std::string &path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string error, warning;
    const bool binary = path.size() >= 4 && path.compare(path.size() - 4, 4, ".glb") == 0;
    const bool loaded = binary ? loader.LoadBinaryFromFile(&model, &error, &warning, path)
                               : loader.LoadASCIIFromFile(&model, &error, &warning, path);
    if (!loaded)
    {
        SceneLoadResult result;
        result.error = error.empty() ? "unable to parse glTF file" : error;
        return result;
    }
    return sr::gltf::convertModel(model);
}

bool loadGltfMesh(const std::string &path, Mesh &out, std::string *error)
{
    SceneLoadResult loaded = loadGltfScene(path);
    if (!loaded.success || loaded.scene.meshes.empty())
    {
        if (error)
            *error = loaded.success ? "model contains no meshes" : loaded.error;
        return false;
    }
    out = std::move(loaded.scene.meshes.front());
    return true;
}

} // namespace sr::io
