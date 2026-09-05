#pragma once
#include "math/vec.h"

// ============================================================
// 渲染器组件定义（配合 EnTT 使用）
//
// EnTT 提供 ECS 框架（registry, view, each 等），
// 这里只定义渲染器需要的组件 struct。
//
// 用法：
//   #include <entt/entt.hpp>
//   #include "scene/components.h"
//
//   entt::registry registry;
//   auto e = registry.create();
//   registry.emplace<Position>(e, Vec3f{1,2,3});
//   registry.emplace<Velocity>(e, Vec3f{0,1,0});
//
//   registry.view<Position, Velocity>().each([](auto, Position& p, Velocity& v) {
//       p.pos = p.pos + v.vel * dt;
//   });
// ============================================================

struct Position
{
    Vec3f pos{0, 0, 0};
};
struct Velocity
{
    Vec3f vel{0, 0, 0};
};
struct Rotation
{
    float yaw = 0, pitch = 0, roll = 0;
};
struct Scale
{
    Vec3f scale{1, 1, 1};
};
struct MeshRef
{
    int meshIndex = -1;
};
struct MaterialRef
{
    int materialId = 0;
};
struct Light
{
    Vec3f direction{0, -1, 0};
    Vec3f color{1, 1, 1};
    float intensity = 1.0f;
};
struct CameraTag
{
}; // 标记哪个 Entity 是相机
