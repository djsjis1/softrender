#include <gtest/gtest.h>

#include <limits>

#include "gltf_accessor.h"
#include "gltf_convert.h"
#include "gltf_mesh.h"
#include "gltf_test_helpers.h"
#include "softrenderer/io/gltf_loader.h"

using namespace sr;

namespace sr::tests {
using namespace gltfFixtures;
static tinygltf::Model triangle()
{
    tinygltf::Model model;
    tinygltf::Primitive primitive;
    primitive.attributes["POSITION"] = addFloats(model, {0, 0, 0, 1, 0, 0, 0, 1, 0}, TINYGLTF_TYPE_VEC3, 3);
    tinygltf::Mesh mesh;
    mesh.primitives.push_back(primitive);
    model.meshes.push_back(mesh);
    return model;
}
TEST(GltfAccessorSafety, create_invalidChain_returnsError)
{
    auto model = triangle();
    std::string error;
    EXPECT_FALSE(gltf::AccessorView::create(model, -1, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(gltf::AccessorView::create(model, 1));
    model.accessors[0].bufferView = 9;
    EXPECT_FALSE(gltf::AccessorView::create(model, 0));
    model.accessors[0].bufferView = 0;
    model.bufferViews[0].buffer = -1;
    EXPECT_FALSE(gltf::AccessorView::create(model, 0));
}
TEST(GltfAccessorSafety, create_truncatedAndOverflowingRanges_rejectsBeforeRead)
{
    for (int variant = 0; variant < 6; ++variant)
    {
        auto model = triangle();
        auto& a = model.accessors[0];
        auto& v = model.bufferViews[0];
        switch (variant)
        {
            case 0:
                v.byteLength--;
                break;
            case 1:
                v.byteOffset = std::numeric_limits<size_t>::max();
                break;
            case 2:
                a.byteOffset = std::numeric_limits<size_t>::max();
                break;
            case 3:
                a.count = std::numeric_limits<size_t>::max();
                break;
            case 4:
                a.byteOffset = 1;
                break;
            case 5:
                a.count = 0;
                break;
        }
        EXPECT_FALSE(gltf::AccessorView::create(model, 0)) << variant;
    }
}
TEST(GltfAccessorSafety, create_invalidStrideAndSparse_rejectsExplicitly)
{
    for (size_t stride : {size_t(2), size_t(8), size_t(13), size_t(256)})
    {
        auto model = triangle();
        model.bufferViews[0].byteStride = stride;
        EXPECT_FALSE(gltf::AccessorView::create(model, 0));
    }
    auto model = triangle();
    model.accessors[0].sparse.isSparse = true;
    std::string error;
    EXPECT_FALSE(gltf::AccessorView::create(model, 0, &error));
    EXPECT_NE(error.find("sparse"), std::string::npos);
}
TEST(GltfAccessorSafety, read_normalizedAndUnsigned_preservesValues)
{
    tinygltf::Model model;
    const int signedIndex = addBytes(model, {128, 127}, TINYGLTF_TYPE_SCALAR, TINYGLTF_COMPONENT_TYPE_BYTE, 2);
    model.accessors[size_t(signedIndex)].normalized = true;
    auto signedView = gltf::AccessorView::create(model, signedIndex);
    ASSERT_TRUE(signedView);
    float value = 9;
    ASSERT_TRUE(signedView->readFloat(0, 0, value));
    EXPECT_FLOAT_EQ(value, -1);
    ASSERT_TRUE(signedView->readFloat(1, 0, value));
    EXPECT_FLOAT_EQ(value, 1);
    EXPECT_FALSE(signedView->readFloat(2, 0, value));
    EXPECT_FLOAT_EQ(value, 1);
    const int index =
        addBytes(model, {255, 255, 255, 255}, TINYGLTF_TYPE_SCALAR, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, 1);
    auto view = gltf::AccessorView::create(model, index);
    ASSERT_TRUE(view);
    uint32_t result = 0;
    ASSERT_TRUE(view->readUnsigned(0, result));
    EXPECT_EQ(result, UINT32_MAX);
}
TEST(GltfMeshSafety, convert_interleavedVerticesAndNormalizedUV_readsStride)
{
    auto model = triangle();
    const int p = addFloats(model, {0, 0, 0, 99, 1, 0, 0, 98, 0, 1, 0, 97}, TINYGLTF_TYPE_VEC3, 3, 16);
    const int uv = addBytes(model, {0, 255, 0, 0, 255, 0, 0, 0, 128, 64, 0, 0}, TINYGLTF_TYPE_VEC2,
                            TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, 3, 4);
    model.accessors[size_t(uv)].normalized = true;
    auto& primitive = model.meshes[0].primitives[0];
    primitive.attributes["POSITION"] = p;
    primitive.attributes["TEXCOORD_0"] = uv;
    auto result = gltf::convertModel(model);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FLOAT_EQ(result.scene.meshes[0].vertices[1].position.x, 1);
    EXPECT_FLOAT_EQ(result.scene.meshes[0].vertices[0].uv.y, 1);
    EXPECT_FLOAT_EQ(result.scene.meshes[0].vertices[2].uv.x, 128.0f / 255);
    EXPECT_FLOAT_EQ(result.scene.meshes[0].vertices[0].normal.z, 1);
}
TEST(GltfMeshSafety, convert_invalidIndex_doesNotPublishPartialMeshes)
{
    auto model = triangle();
    auto invalid = model.meshes[0].primitives[0];
    invalid.indices = addBytes(model, {0, 1, 3}, TINYGLTF_TYPE_SCALAR, TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, 3);
    model.meshes[0].primitives.push_back(invalid);
    auto result = gltf::convertModel(model);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.scene.meshes.empty());
}
TEST(GltfMeshSafety, convert_invalidAttributesTopologyAndNonfinite_rejects)
{
    for (int variant = 0; variant < 5; ++variant)
    {
        auto model = triangle();
        auto& primitive = model.meshes[0].primitives[0];
        switch (variant)
        {
            case 0:
                primitive.mode = TINYGLTF_MODE_LINE;
                break;
            case 1:
                primitive.material = 0;
                break;
            case 2:
                primitive.attributes["NORMAL"] = addFloats(model, {0, 0, 1}, TINYGLTF_TYPE_VEC3, 1);
                break;
            case 3:
                primitive.attributes["POSITION"] = addFloats(model, {0, 0, 0, 1, 0, 0}, TINYGLTF_TYPE_VEC3, 2);
                break;
            case 4:
                primitive.attributes["POSITION"] =
                    addFloats(model, {INFINITY, 0, 0, 1, 0, 0, 0, 1, 0}, TINYGLTF_TYPE_VEC3, 3);
                break;
        }
        EXPECT_FALSE(gltf::convertModel(model).success) << variant;
    }
}
TEST(GltfTextureSafety, convert_decodedPixelsAndMove_keepsMaterialTextureAlive)
{
    auto model = triangle();
    tinygltf::Image image;
    image.width = 1;
    image.height = 1;
    image.component = 4;
    image.bits = 8;
    image.pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image.image = {255, 0, 0, 255};
    model.images.push_back(image);
    tinygltf::Texture texture;
    texture.source = 0;
    model.textures.push_back(texture);
    tinygltf::Material material;
    material.pbrMetallicRoughness.baseColorTexture.index = 0;
    model.materials.push_back(material);
    auto loaded = gltf::convertModel(model);
    ASSERT_TRUE(loaded.success) << loaded.error;
    auto moved = std::move(loaded);
    // 这个 primitive 没引材质，直接按索引取材质验证纹理引用
    const sr::Material* pbr = moved.scene.material(0);
    ASSERT_NE(pbr, nullptr);
    const Texture* albedo = moved.scene.texture(pbr->baseColorTextureIndex);
    ASSERT_NE(albedo, nullptr);
    EXPECT_TRUE(albedo->valid());
    const auto pixel = albedo->sample(0.5f, 0.5f);
    EXPECT_FLOAT_EQ(pixel.x, 1);
    EXPECT_FLOAT_EQ(pixel.y, 0);
    EXPECT_FLOAT_EQ(pixel.z, 0);
    // 索引引用在移动后仍然有效（不存在悬垂指针）
    EXPECT_EQ(albedo, &moved.scene.textures[0]);
    model.images[0].as_is = true;
    EXPECT_FALSE(gltf::convertModel(model).success);
    model.images[0].as_is = false;
    model.images[0].image.pop_back();
    EXPECT_FALSE(gltf::convertModel(model).success);
}
TEST(GltfAccessorSafety, read_matrixAndOffsets_usesColumnMajorComponentOrder)
{
    tinygltf::Model model;
    const int index = addFloats(model, {99, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1}, TINYGLTF_TYPE_MAT4, 1);
    model.accessors[size_t(index)].byteOffset = 4;
    auto view = gltf::AccessorView::create(model, index);
    ASSERT_TRUE(view);
    float value = 0;
    ASSERT_TRUE(view->readFloat(0, 12, value));
    EXPECT_FLOAT_EQ(value, 3);
    model.accessors[size_t(index)].type = TINYGLTF_TYPE_MAT3;
    EXPECT_FALSE(gltf::AccessorView::create(model, index));
}
TEST(GltfMeshSafety, convert_unsignedIndexWidths_readsLittleEndian)
{
    for (int type : {TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT,
                     TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT})
    {
        auto model = triangle();
        const size_t width = type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE    ? 1
                             : type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2
                                                                              : 4;
        std::vector<unsigned char> bytes(3 * width, 0);
        bytes[width] = 1;
        bytes[2 * width] = 2;
        model.meshes[0].primitives[0].indices = addBytes(model, bytes, TINYGLTF_TYPE_SCALAR, type, 3);
        auto result = gltf::convertModel(model);
        ASSERT_TRUE(result.success) << result.error;
        EXPECT_EQ(result.scene.meshes[0].indices, (std::vector<int>{0, 1, 2}));
    }
}
TEST(GltfAnimationSafety, convert_laterNonfiniteKey_rejectsWholeModel)
{
    auto model = triangle();
    model.nodes.resize(1);
    tinygltf::AnimationSampler sampler;
    sampler.input = addFloats(model, {0, 1}, TINYGLTF_TYPE_SCALAR, 2);
    sampler.output = addFloats(model, {0, 0, 0, NAN, 0, 0}, TINYGLTF_TYPE_VEC3, 2);
    tinygltf::AnimationChannel channel;
    channel.sampler = 0;
    channel.target_node = 0;
    channel.target_path = "translation";
    tinygltf::Animation animation;
    animation.samplers.push_back(sampler);
    animation.channels.push_back(channel);
    model.animations.push_back(animation);
    auto result = gltf::convertModel(model);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.scene.meshes.empty());
}

// 一个源 mesh 含两个 primitive 时，节点必须引用展平后的两个网格。
// 旧实现直接保存原始 node.mesh 索引，第二个 primitive 会丢失。
TEST(GltfMeshMapping, convert_multiPrimitiveNodeMapsAllFlattenedMeshes)
{
    tinygltf::Model model;
    tinygltf::Primitive first;
    first.attributes["POSITION"] = addFloats(model, {0, 0, 0, 1, 0, 0, 0, 1, 0}, TINYGLTF_TYPE_VEC3, 3);
    tinygltf::Primitive second;
    second.attributes["POSITION"] = addFloats(model, {2, 0, 0, 3, 0, 0, 2, 1, 0}, TINYGLTF_TYPE_VEC3, 3);
    tinygltf::Mesh mesh;
    mesh.primitives.push_back(first);
    mesh.primitives.push_back(second);
    model.meshes.push_back(mesh);

    // 第二个源 mesh 只有一个 primitive，用于验证索引偏移不会错位
    tinygltf::Primitive third;
    third.attributes["POSITION"] = addFloats(model, {5, 0, 0, 6, 0, 0, 5, 1, 0}, TINYGLTF_TYPE_VEC3, 3);
    tinygltf::Mesh other;
    other.primitives.push_back(third);
    model.meshes.push_back(other);

    model.nodes.resize(2);
    model.nodes[0].mesh = 0;
    model.nodes[1].mesh = 1;

    const auto result = gltf::convertModel(model);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.scene.meshes.size(), 3u);

    ASSERT_EQ(result.scene.nodes[0].meshIndices.size(), 2u);
    EXPECT_EQ(result.scene.nodes[0].meshIndices[0], 0);
    EXPECT_EQ(result.scene.nodes[0].meshIndices[1], 1);
    EXPECT_EQ(result.scene.nodes[0].meshIndex, 0);  // 首元素快捷值

    ASSERT_EQ(result.scene.nodes[1].meshIndices.size(), 1u);
    EXPECT_EQ(result.scene.nodes[1].meshIndices[0], 2);
    EXPECT_EQ(result.scene.nodes[1].meshIndex, 2);
}

// 节点引用不存在的 mesh 必须在转换阶段就失败。
TEST(GltfMeshMapping, convert_nodeWithInvalidMeshIndexFails)
{
    auto model = triangle();
    model.nodes.resize(1);
    model.nodes[0].mesh = 99;
    const auto result = gltf::convertModel(model);
    EXPECT_FALSE(result.success);
}

// 没有 mesh 的节点不应凭空得到 meshIndex。
TEST(GltfMeshMapping, convert_nodeWithoutMeshHasNegativeIndex)
{
    auto model = triangle();
    model.nodes.resize(1);  // mesh 默认为 -1
    const auto result = gltf::convertModel(model);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.scene.nodes[0].meshIndices.empty());
    EXPECT_EQ(result.scene.nodes[0].meshIndex, -1);
}
}  // namespace sr::tests
