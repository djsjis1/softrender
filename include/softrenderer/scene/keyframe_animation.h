#pragma once
#include <string>
#include <vector>

#include "softrenderer/math/mat4.h"
#include "softrenderer/math/vec.h"

namespace sr::keyframe
{

// ---- 动画关键帧 ----
struct Keyframe
{
    float time;
    Vec3f translation{0, 0, 0};
    Vec3f rotation{0, 0, 0}; // 欧拉角（简化，完整应用四元数）
    Vec3f scale{1, 1, 1};
};

struct AnimationChannel
{
    int boneIndex;
    std::vector<Keyframe> keyframes; // 按时间排序
};

struct Animation
{
    std::string name;
    float duration;
    std::vector<AnimationChannel> channels;
};

// 辅助：从关键帧构造变换矩阵
inline Mat4 makeTransform(const Keyframe &kf)
{
    return Mat4::translate(kf.translation.x, kf.translation.y, kf.translation.z) *
           Mat4::rotateY(kf.rotation.y) *
           Mat4::rotateX(kf.rotation.x) *
           Mat4::rotateZ(kf.rotation.z) *
           Mat4::scale(kf.scale.x, kf.scale.y, kf.scale.z);
}

// 在给定时间插值骨骼的局部变换
inline Mat4 sampleAnimation(const AnimationChannel &channel, float time)
{
    const auto &kf = channel.keyframes;
    if (kf.empty())
        return Mat4::identity();
    if (time <= kf.front().time)
        return makeTransform(kf.front());
    if (time >= kf.back().time)
        return makeTransform(kf.back());

    // 二分查找
    int lo = 0, hi = int(kf.size()) - 1;
    while (lo + 1 < hi)
    {
        int mid = (lo + hi) / 2;
        if (kf[mid].time <= time)
            lo = mid;
        else
            hi = mid;
    }
    float t = (time - kf[lo].time) / (kf[hi].time - kf[lo].time);
    // 线性插值
    Keyframe interp;
    interp.translation = kf[lo].translation + (kf[hi].translation - kf[lo].translation) * t;
    interp.rotation = kf[lo].rotation + (kf[hi].rotation - kf[lo].rotation) * t;
    interp.scale = kf[lo].scale + (kf[hi].scale - kf[lo].scale) * t;
    return makeTransform(interp);
}

} // namespace sr::keyframe
