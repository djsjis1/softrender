/** @file animation_test.cpp
 *  @brief 动画采样的独立数值期望、非法输入和输出原子性。
 */
#include <gtest/gtest.h>

#include "gltf_test_helpers.h"
#include "scene/animation_player.h"

namespace sr::tests {
TEST(GltfAnimationSafety, sampleChannel_cubicTriplets_interpolatesValuesAndTangents)
{
    tinygltf::Model model;
    model.nodes.resize(1);
    GltfAnimation animation;
    GltfAnimation::Sampler sampler;
    sampler.input = gltfFixtures::addFloats(model, {0, 2}, TINYGLTF_TYPE_SCALAR, 2);
    sampler.output =
        gltfFixtures::addFloats(model, {0, 0, 0, 1, 2, 3, 4, 0, 0, 0, 0, 0, 5, 6, 7, 0, 0, 0}, TINYGLTF_TYPE_VEC3, 6);
    sampler.interpolation = GltfAnimation::Sampler::Interpolation::CubicSpline;
    animation.samplers.push_back(sampler);
    GltfAnimation::Channel channel;
    channel.targetNode = 0;
    channel.samplerIndex = 0;
    AnimSample result;
    ASSERT_TRUE(sampleChannel(model, animation, channel, 1.0f, result));
    EXPECT_TRUE(result.hasTranslation);
    EXPECT_FLOAT_EQ(result.translation.x, 4.0f);
    EXPECT_FLOAT_EQ(result.translation.y, 4.0f);
    EXPECT_FLOAT_EQ(result.translation.z, 5.0f);
}
TEST(GltfAnimationSafety, sampleChannel_invalidInputs_preservesOutput)
{
    for (int variant = 0; variant < 9; ++variant)
    {
        tinygltf::Model model;
        model.nodes.resize(1);
        GltfAnimation animation;
        GltfAnimation::Sampler sampler;
        sampler.input = gltfFixtures::addFloats(model,
                                                variant == 0   ? std::vector<float>{1, 1}
                                                : variant == 1 ? std::vector<float>{2, 1}
                                                : variant == 2 ? std::vector<float>{-1, 1}
                                                               : std::vector<float>{0, 1},
                                                TINYGLTF_TYPE_SCALAR, 2);
        sampler.output = gltfFixtures::addFloats(model, {0, 0, 0, 1, 2, 3}, TINYGLTF_TYPE_VEC3, 2);
        GltfAnimation::Channel channel;
        channel.targetNode = 0;
        channel.samplerIndex = 0;
        if (variant == 3)
            sampler.output = 99;
        if (variant == 4)
            model.accessors[1].count = 1;
        if (variant == 5)
            channel.targetNode = -1;
        if (variant == 6)
            model.bufferViews[1].byteLength--;
        if (variant == 7)
            model.accessors[1].type = TINYGLTF_TYPE_VEC2;
        animation.samplers.push_back(sampler);
        animation.channels.push_back(channel);
        AnimSample result;
        result.nodeIndex = 42;
        result.translation = {8, 9, 10};
        EXPECT_FALSE(sampleChannel(model, animation, channel, variant == 8 ? NAN : 0.5f, result)) << variant;
        EXPECT_EQ(result.nodeIndex, 42);
        EXPECT_FLOAT_EQ(result.translation.x, 8);
        auto poses = sampleAnimation(model, animation, variant == 8 ? NAN : 0.5f);
        ASSERT_EQ(poses.size(), 1u);
        EXPECT_FALSE(poses[0].hasTranslation);
    }
}
TEST(GltfAnimationSafety, sampleAnimation_linearStepAndClamp_retainsBindPoseAndFlags)
{
    tinygltf::Model model;
    model.nodes.resize(1);
    model.nodes[0].scale = {2, 3, 4};
    GltfAnimation animation;
    GltfAnimation::Sampler sampler;
    sampler.input = gltfFixtures::addFloats(model, {1, 3}, TINYGLTF_TYPE_SCALAR, 2);
    sampler.output = gltfFixtures::addFloats(model, {2, 0, 0, 6, 0, 0}, TINYGLTF_TYPE_VEC3, 2);
    animation.samplers.push_back(sampler);
    GltfAnimation::Channel channel;
    channel.targetNode = 0;
    channel.samplerIndex = 0;
    animation.channels.push_back(channel);
    auto poses = sampleAnimation(model, animation, 2);
    EXPECT_FLOAT_EQ(poses[0].translation.x, 4);
    EXPECT_TRUE(poses[0].hasTranslation);
    EXPECT_FLOAT_EQ(poses[0].scale.y, 3);
    EXPECT_FALSE(poses[0].hasScale);
    EXPECT_FLOAT_EQ(sampleAnimation(model, animation, -1)[0].translation.x, 2);
    EXPECT_FLOAT_EQ(sampleAnimation(model, animation, 5)[0].translation.x, 6);
    animation.samplers[0].interpolation = GltfAnimation::Sampler::Interpolation::Step;
    EXPECT_FLOAT_EQ(sampleAnimation(model, animation, 2)[0].translation.x, 2);
    EXPECT_FLOAT_EQ(sampleAnimation(model, animation, 3)[0].translation.x, 6);
}
TEST(GltfAnimationSafety, sampleChannel_rotation_normalizesAndRejectsZero)
{
    tinygltf::Model model;
    model.nodes.resize(1);
    GltfAnimation animation;
    GltfAnimation::Sampler sampler;
    sampler.input = gltfFixtures::addFloats(model, {0, 1}, TINYGLTF_TYPE_SCALAR, 2);
    sampler.output = gltfFixtures::addFloats(model, {0, 0, 0, 1, 0, 0, 1, 0}, TINYGLTF_TYPE_VEC4, 2);
    animation.samplers.push_back(sampler);
    GltfAnimation::Channel channel;
    channel.targetNode = 0;
    channel.samplerIndex = 0;
    channel.path = GltfAnimation::Channel::Path::Rotation;
    AnimSample result;
    ASSERT_TRUE(sampleChannel(model, animation, channel, 0.5f, result));
    EXPECT_NEAR(result.rotation.z, std::sqrt(0.5f), 1e-6f);
    EXPECT_NEAR(result.rotation.length(), 1, 1e-6f);
    animation.samplers[0].output = gltfFixtures::addFloats(model, {0, 0, 0, 0, 0, 0, 1, 0}, TINYGLTF_TYPE_VEC4, 2);
    EXPECT_FALSE(sampleChannel(model, animation, channel, 0.5f, result));
}
TEST(GltfAnimationSafety, sampleChannel_cubicQuaternion_normalizesHermiteResult)
{
    tinygltf::Model model;
    model.nodes.resize(1);
    GltfAnimation animation;
    GltfAnimation::Sampler sampler;
    sampler.input = gltfFixtures::addFloats(model, {0, 2}, TINYGLTF_TYPE_SCALAR, 2);
    sampler.output = gltfFixtures::addFloats(
        model, {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}, TINYGLTF_TYPE_VEC4, 6);
    sampler.interpolation = GltfAnimation::Sampler::Interpolation::CubicSpline;
    animation.samplers.push_back(sampler);
    GltfAnimation::Channel channel;
    channel.targetNode = 0;
    channel.samplerIndex = 0;
    channel.path = GltfAnimation::Channel::Path::Rotation;
    AnimSample result;
    ASSERT_TRUE(sampleChannel(model, animation, channel, 1, result));
    EXPECT_NEAR(result.rotation.z, std::sqrt(0.5f), 1e-6f);
    EXPECT_NEAR(result.rotation.w, std::sqrt(0.5f), 1e-6f);
    ASSERT_TRUE(sampleChannel(model, animation, channel, 3, result));
    EXPECT_FLOAT_EQ(result.rotation.z, 1);
}
}  // namespace sr::tests
