/** @file animation_test.cpp
 *  @brief 动画采样的独立数值期望、非法输入和输出原子性。
 *
 *  这些用例只消费已解码的 AnimationClip（时间轴 + 关键帧值 + 切线），
 *  期望值由插值公式手工推导，刻意使用互不相等的分量与非对称采样点，
 *  避免"两个实现互相验证"式的伪证明。
 */
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "softrenderer/scene/animation_player.h"

using namespace sr;

namespace sr::tests {

namespace {

AnimationClip makeTranslationClip(AnimationSampler::Interpolation interpolation, std::vector<float> times,
                                 std::vector<Vec4f> values)
{
    AnimationClip clip;
    AnimationSampler sampler;
    sampler.interpolation = interpolation;
    sampler.times = std::move(times);
    sampler.values = std::move(values);
    clip.samplers.push_back(std::move(sampler));
    AnimationChannel channel;
    channel.samplerIndex = 0;
    channel.targetNode = 0;
    channel.path = AnimationChannel::Path::Translation;
    clip.channels.push_back(channel);
    return clip;
}

std::vector<SceneNode> oneNode()
{
    return std::vector<SceneNode>(1);
}

} // namespace

TEST(AnimationSampling, cubicTriplets_interpolateValuesAndTangents)
{
    // 关键帧 0: 值(0,0,0) 出切线(2,3,4)；关键帧 1: 值(5,6,7) 入切线(0,0,0)
    // t=1 时 α=0.5，duration=2：
    //   h00=0.5  h10=0.125  h01=0.5  h11=-0.125
    //   x = 0.5*0 + 0.125*2*2 + 0.5*5 + (-0.125*2*0) = 3.0
    //   y = 0.5*0 + 0.125*2*3 + 0.5*6 = 3.75
    //   z = 0.5*0 + 0.125*2*4 + 0.5*7 = 4.5
    auto clip = makeTranslationClip(AnimationSampler::Interpolation::CubicSpline, {0.0f, 2.0f},
                                    {{0, 0, 0, 0}, {0, 0, 0, 0}, {2, 3, 4, 0}, {0, 0, 0, 0}, {5, 6, 7, 0}, {0, 0, 0, 0}});
    animation::NodePose pose;
    ASSERT_TRUE(animation::sampleChannel(clip, clip.channels[0], 1.0f, pose));
    EXPECT_TRUE(pose.hasTranslation);
    EXPECT_NEAR(pose.translation.x, 3.0f, 1e-6f);
    EXPECT_NEAR(pose.translation.y, 3.75f, 1e-6f);
    EXPECT_NEAR(pose.translation.z, 4.5f, 1e-6f);
}

TEST(AnimationSampling, invalidInputs_preserveOutput)
{
    for (int variant = 0; variant < 8; ++variant)
    {
        auto clip = makeTranslationClip(AnimationSampler::Interpolation::Linear, {0.0f, 1.0f},
                                        {{0, 0, 0, 0}, {1, 2, 3, 0}});
        AnimationChannel channel = clip.channels[0];
        float time = 0.5f;
        switch (variant)
        {
            case 0: // 时间轴重复
                clip.samplers[0].times = {1.0f, 1.0f};
                break;
            case 1: // 时间轴递减
                clip.samplers[0].times = {2.0f, 1.0f};
                break;
            case 2: // 负时间戳
                clip.samplers[0].times = {-1.0f, 1.0f};
                break;
            case 3: // 关键帧数量与时间轴不匹配
                clip.samplers[0].values.push_back({9, 9, 9, 0});
                break;
            case 4: // 采样器索引越界
                channel.samplerIndex = 7;
                break;
            case 5: // 目标节点未设置
                channel.targetNode = -1;
                break;
            case 6: // 非法路径枚举
                channel.path = static_cast<AnimationChannel::Path>(7);
                break;
            case 7: // 非有限时间
                time = std::numeric_limits<float>::quiet_NaN();
                break;
        }
        animation::NodePose pose;
        pose.nodeIndex = 42;
        pose.translation = {8, 9, 10};
        EXPECT_FALSE(animation::sampleChannel(clip, channel, time, pose)) << variant;
        EXPECT_EQ(pose.nodeIndex, 42) << variant;
        EXPECT_FLOAT_EQ(pose.translation.x, 8) << variant;

        // sampleAnimation 读的是片段里的通道，确保它看到同样的非法状态
        clip.channels[0] = channel;
        const auto poses = animation::sampleAnimation(oneNode(), clip, time);
        ASSERT_EQ(poses.size(), 1u);
        EXPECT_FALSE(poses[0].hasTranslation) << variant;
    }
}

TEST(AnimationSampling, linearStepAndClamp_retainsBindPoseAndFlags)
{
    std::vector<SceneNode> nodes = oneNode();
    nodes[0].scale = {2, 3, 4};

    auto clip = makeTranslationClip(AnimationSampler::Interpolation::Linear, {1.0f, 3.0f},
                                    {{2, 0, 0, 0}, {6, 0, 0, 0}});

    auto poses = animation::sampleAnimation(nodes, clip, 2.0f);
    EXPECT_FLOAT_EQ(poses[0].translation.x, 4);
    EXPECT_TRUE(poses[0].hasTranslation);
    // 未被通道覆盖的 scale 保持 bind pose，且标志位为 false
    EXPECT_FLOAT_EQ(poses[0].scale.y, 3);
    EXPECT_FALSE(poses[0].hasScale);
    EXPECT_FLOAT_EQ(animation::sampleAnimation(nodes, clip, -1.0f)[0].translation.x, 2);
    EXPECT_FLOAT_EQ(animation::sampleAnimation(nodes, clip, 5.0f)[0].translation.x, 6);

    clip.samplers[0].interpolation = AnimationSampler::Interpolation::Step;
    EXPECT_FLOAT_EQ(animation::sampleAnimation(nodes, clip, 2.0f)[0].translation.x, 2);
    EXPECT_FLOAT_EQ(animation::sampleAnimation(nodes, clip, 3.0f)[0].translation.x, 6);
}

TEST(AnimationSampling, rotation_normalizesAndRejectsZero)
{
    auto clip = makeTranslationClip(AnimationSampler::Interpolation::Linear, {0.0f, 1.0f},
                                    {{0, 0, 0, 1}, {0, 0, 1, 0}});
    clip.channels[0].path = AnimationChannel::Path::Rotation;

    animation::NodePose pose;
    ASSERT_TRUE(animation::sampleChannel(clip, clip.channels[0], 0.5f, pose));
    EXPECT_NEAR(pose.rotation.z, std::sqrt(0.5f), 1e-6f);
    EXPECT_NEAR(pose.rotation.length(), 1, 1e-6f);

    // 零四元数关键帧视为非法
    clip.samplers[0].values = {{0, 0, 0, 0}, {0, 0, 1, 0}};
    EXPECT_FALSE(animation::sampleChannel(clip, clip.channels[0], 0.5f, pose));
}

TEST(AnimationSampling, cubicQuaternion_normalizesHermiteResult)
{
    // 两个单位四元数之间做 Hermite 插值，中点结果 (0,0,0.5,0.5)
    auto clip = makeTranslationClip(
        AnimationSampler::Interpolation::CubicSpline, {0.0f, 2.0f},
        {{0, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}});
    clip.channels[0].path = AnimationChannel::Path::Rotation;

    animation::NodePose pose;
    ASSERT_TRUE(animation::sampleChannel(clip, clip.channels[0], 1.0f, pose));
    EXPECT_NEAR(pose.rotation.z, std::sqrt(0.5f), 1e-6f);
    EXPECT_NEAR(pose.rotation.w, std::sqrt(0.5f), 1e-6f);
    ASSERT_TRUE(animation::sampleChannel(clip, clip.channels[0], 3.0f, pose));
    EXPECT_FLOAT_EQ(pose.rotation.z, 1);
}

TEST(AnimationSampling, clipDuration_reportsLatestKeyframe)
{
    auto clip = makeTranslationClip(AnimationSampler::Interpolation::Linear, {0.5f, 2.5f},
                                    {{0, 0, 0, 0}, {1, 0, 0, 0}});
    EXPECT_FLOAT_EQ(clip.duration(), 2.5f);
    EXPECT_TRUE(AnimationClip{}.duration() == 0.0f);
}

} // namespace sr::tests
