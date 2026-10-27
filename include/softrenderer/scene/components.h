#pragma once
#include "softrenderer/math/vec.h"

// ============================================================
// 渲染器组件定义（配合 EnTT 使用）
//
// 【文件职责】定义 ECS 场景管理所需的组件 struct。EnTT 提供框架
// （registry / view / each），本文件只提供数据。
//
// 【导出类型】全部位于 namespace sr：
//   sr::Position     世界坐标
//   sr::Velocity     线速度
//   sr::Rotation     yaw / pitch / roll 欧拉角
//   sr::Scale        缩放
//   sr::MeshRef      指向网格资源的下标（-1 = 未设置）
//   sr::MaterialRef  指向材质的 id
//   sr::Light        方向光（方向 + 颜色 + 强度）
//   sr::CameraTag    空标记组件，标识哪个 Entity 是相机
//
// 【为什么在 namespace sr 里】这 8 个名字是 ECS 领域的标准命名，
// 下游使用者几乎必然已定义过同名类型。本项目是 header-only 库，
// 符号直接进入使用者的编译单元，全局定义会导致**编译期重定义硬错误**。
// 实测本文件的引用面极小（仅本文件与命名空间哨兵），因此可以完整迁移
// 而不留全局别名 —— 别名桥并不能解决撞名（`using sr::Position;` 照样把
// Position 注入全局作用域），它只是分批提交的工具。
// 决策依据见 docs/adr/ADR-001-namespace-convergence.md。
//
// 用法：
// @code
//   #include <entt/entt.hpp>
//   #include "scene/components.h"
//
//   entt::registry registry;
//   auto e = registry.create();
//   registry.emplace<sr::Position>(e, Vec3f{1, 2, 3});
//   registry.emplace<sr::Velocity>(e, Vec3f{0, 1, 0});
//
//   registry.view<sr::Position, sr::Velocity>().each(
//       [](auto, sr::Position& p, sr::Velocity& v) { p.pos = p.pos + v.vel; });
// @endcode
//
// 注：Vec3f 目前仍在全局命名空间（属 ADR-001 的后续批次），
// 因此本文件内部与使用者都可以直接写 Vec3f，无需限定。
// ============================================================

namespace sr
{

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

// -1 表示「未设置」而非「第 0 个网格」：0 是合法下标，
// 用它做哨兵会让未初始化的组件静默指向首元素。
struct MeshRef
{
    int meshIndex = -1;
};

// 与 MeshRef 同一口径：-1 表示「未设置」。0 是合法材质下标，拿它做哨兵
// 会让默认构造的组件静默指向第一个材质，与 MeshRef 的语义相反。
struct MaterialRef
{
    int materialId = -1;
};

struct Light
{
    Vec3f direction{0, -1, 0};
    Vec3f color{1, 1, 1};
    float intensity = 1.0f;
};

// 空标记组件（tag component）：只用于 view<CameraTag> 过滤，不携带数据
struct CameraTag
{
};

} // namespace sr
