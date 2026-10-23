#pragma once
#include <algorithm>
#include <cmath>
#include "softrenderer/math/mat3.h"
#include "softrenderer/math/rect.h"
#include "softrenderer/math/transform.h"

// ============================================================
// Camera：FPS 风格自由相机。
//
// 用 yaw（偏航，绕世界 Y 轴）和 pitch（俯仰，绕相机右轴）两个角
// 描述朝向，从中算出 forward 向量；配合位置 position 得到 View 矩阵。
//
// 约定与 sr::transform::lookAt 一致：右手系，看向 forward 方向。
// pitch 夹在 ±89° 防止万向节翻转（看到头顶正上方时朝向会突变）。
// ============================================================
namespace sr
{

class Camera
{
public:
    Vec3f position{0, 1.5f, 5.0f};
    float yaw = -3.14159265f * 0.5f; // 初始朝 -Z 看
    float pitch = 0.0f;

    float fovY = 3.14159265f / 3.0f; // 60°
    float aspect = 4.0f / 3.0f;
    float nearZ = 0.1f;
    float farZ = 100.0f;

    // 由 yaw/pitch 算出的前方向（世界空间）
    Vec3f forward() const
    {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        return Vec3f{std::cos(yaw) * cp, sp, std::sin(yaw) * cp}.normalized();
    }
    Vec3f right() const
    {
        // 前方向与世界上方向叉积得到右方向
        return cross(forward(), Vec3f{0, 1, 0}).normalized();
    }

    Mat4 viewMatrix() const
    {
        return sr::transform::lookAt(position, position + forward(), {0, 1, 0});
    }
    Mat4 projMatrix() const
    {
        return sr::transform::perspective(fovY, aspect, nearZ, farZ);
    }

    // ---- 交互 ----

    // 鼠标移动改变朝向（dx,dy 为像素位移）
    void addYawPitch(float dxRad, float dyRad)
    {
        yaw += dxRad;
        pitch += dyRad;
        const float lim = 3.14159265f * 0.49f;
        pitch = std::max(-lim, std::min(lim, pitch));
    }

    // 沿前/右/上方向平移（WASD/QE）
    void moveForward(float d) { position = position + forward() * d; }
    void moveRight(float d) { position = position + right() * d; }
    void moveUp(float d) { position = position + Vec3f{0, 1, 0} * d; }

    // ---- 轨道模式 ----
    // 让相机以 target 为中心、distance 为半径，按给定 yaw/pitch 环绕，
    // 并自动朝向 target。适合看模型（拖动即绕物体转圈）。
    void orbit(const Vec3f &target, float distance, float orbitYaw, float orbitPitch)
    {
        float cp = std::cos(orbitPitch), sp = std::sin(orbitPitch);
        Vec3f dir{std::cos(orbitYaw) * cp, sp, std::sin(orbitYaw) * cp};
        position = target - dir * distance; // 站在 target 后方 distance 处
        // 反推 yaw/pitch，使 forward() 指向 target
        yaw = orbitYaw;
        pitch = orbitPitch;
    }
};

}  // namespace sr
