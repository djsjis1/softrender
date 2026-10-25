// ============================================================
// ECS 适配单元测试：EnTT registry 与 sr:: 组件
//
// 【命名空间哨兵】本文件在 ecsCanary 命名空间里声明与组件同名的类型，
// 证明下游可以自由使用这些通用名，而不会与 sr::Position 等冲突。
// ============================================================
#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <type_traits>

#include "softrenderer/scene/components.h"

using namespace sr;

// 与 sr 组件同名的"外部"类型：能同时存在本身就说明没有别名桥。
namespace ecsCanary
{
struct Position
{
    int canary = 0;
};
struct Velocity
{
    int canary = 0;
};
struct Rotation
{
    int canary = 0;
};
struct Scale
{
    int canary = 0;
};
struct MeshRef
{
    int canary = 0;
};
struct MaterialRef
{
    int canary = 0;
};
struct Light
{
    int canary = 0;
};
struct CameraTag
{
    int canary = 0;
};
}  // namespace ecsCanary

// MeshRef 与 MaterialRef 都是「资源下标」语义，未设置必须用 -1：
// 0 是合法下标，拿它做哨兵会让默认构造的组件静默指向第一个资源。
TEST(ECS, ReferenceComponentsDefaultToUnset)
{
    EXPECT_EQ(sr::MeshRef{}.meshIndex, -1);
    EXPECT_EQ(sr::MaterialRef{}.materialId, -1);
}

TEST(ECS, CreateAndDestroy)
{
    entt::registry registry;
    auto e1 = registry.create();
    auto e2 = registry.create();
    EXPECT_TRUE(registry.valid(e1));
    EXPECT_TRUE(registry.valid(e2));
    registry.destroy(e1);
    EXPECT_FALSE(registry.valid(e1));
    EXPECT_TRUE(registry.valid(e2));
}

TEST(ECS, EmplaceAndGet)
{
    entt::registry registry;
    auto e = registry.create();
    registry.emplace<sr::Position>(e, Vec3f{1, 2, 3});
    EXPECT_TRUE(registry.all_of<sr::Position>(e));
    EXPECT_FALSE(registry.all_of<sr::Velocity>(e));
    auto& pos = registry.get<sr::Position>(e);
    EXPECT_FLOAT_EQ(pos.pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.pos.y, 2.0f);
}

TEST(ECS, ViewSystem)
{
    entt::registry registry;
    for (int i = 0; i < 5; ++i)
    {
        auto e = registry.create();
        registry.emplace<sr::Position>(e, Vec3f{float(i), 0, 0});
        registry.emplace<sr::Velocity>(e, Vec3f{1, 0, 0});
    }
    // System: move
    registry.view<sr::Position, sr::Velocity>().each(
        [](sr::Position& p, sr::Velocity& v) { p.pos = p.pos + v.vel; });
    // Verify: each position should be initial + 1
    int count = 0;
    registry.view<sr::Position>().each([&](sr::Position& p) {
        // 初始值是 0,1,2,3,4，加 1 后应该是 1,2,3,4,5
        EXPECT_GE(p.pos.x, 1.0f);
        EXPECT_LE(p.pos.x, 5.0f);
        ++count;
    });
    EXPECT_EQ(count, 5);
}

// 验证哨兵类型与 sr:: 组件确实是**不同类型**，而非同一类型的两个名字。
// 若 components.h 退化成全局定义（或加了 using 别名），上面的哨兵声明
// 会先报重定义错误；本用例是第二道保险。

TEST(ECS, globalCanaryTypesAreDistinctFromSrComponents)
{
    EXPECT_FALSE((std::is_same<ecsCanary::Position, sr::Position>::value));
    EXPECT_FALSE((std::is_same<ecsCanary::Light, sr::Light>::value));
    EXPECT_FALSE((std::is_same<ecsCanary::CameraTag, sr::CameraTag>::value));

    // 哨兵类型可正常使用，证明 ecsCanary 作用域确实归使用者所有
    ecsCanary::Position canary;
    EXPECT_EQ(canary.canary, 0);

    // sr:: 组件也可正常使用，且成员语义未变
    sr::Light light;
    EXPECT_FLOAT_EQ(light.intensity, 1.0f);
    EXPECT_FLOAT_EQ(light.direction.y, -1.0f);

    sr::MeshRef ref;
    EXPECT_EQ(ref.meshIndex, -1);  // -1 = 未设置，不是「第 0 个网格」
}

// ==================== GBuffer ====================

