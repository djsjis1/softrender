// ============================================================
// scene 模块单元测试：骨骼蒙皮、粒子系统与 2D 相机
//
// scene 不依赖加载器与窗口：动画解码相关的用例在 animation_test.cpp。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "softrenderer/math/mat4.h"
#include "softrenderer/math/vec.h"
#include "softrenderer/scene/camera2d.h"
#include "softrenderer/scene/particle_system.h"
#include "softrenderer/scene/skeleton.h"

using namespace sr;

TEST(Skeleton, SingleBoneIdentity)
{
    Skeleton skel;
    Bone b;
    b.name = "root";
    b.parent = -1;
    b.localTransform = Mat4::identity();
    b.inverseBindMatrix = Mat4::identity();
    skel.bones.push_back(b);
    auto skin = skel.computeSkinMatrices();
    EXPECT_EQ(skin.size(), 1u);
    // Identity * Identity = Identity
    Vec3f p = skin[0].transformPoint(Vec3f{1, 2, 3});
    EXPECT_NEAR(p.x, 1.0f, 1e-5f);
    EXPECT_NEAR(p.y, 2.0f, 1e-5f);
}

TEST(Skeleton, SkinPosition)
{
    VertexBoneWeights bw;
    bw.boneIndices[0] = 0;
    bw.weights[0] = 1.0f;
    std::vector<Mat4> skinMats = {Mat4::translate(5, 0, 0)};
    Vec3f result = skinPosition(Vec3f{1, 0, 0}, bw, skinMats);
    EXPECT_NEAR(result.x, 6.0f, 1e-5f);
}

// 绑定姿态不变量：当骨骼局部变换等于绑定姿态、逆绑定矩阵是其逆时，
// 蒙皮矩阵应为单位矩阵，顶点不得移动。
// 一旦把 global * inverseBind 写成 inverseBind * global，这个用例会立刻失败。
TEST(Skeleton, BindPoseLeavesVerticesUnchanged)
{
    Skeleton skel;
    skel.bones.resize(1);
    skel.bones[0].parent = -1;
    // 绑定时骨骼在 (0,1,0)，当前姿态与之一致
    skel.bones[0].localTransform = Mat4::translate(0.0f, 1.0f, 0.0f);
    skel.bones[0].inverseBindMatrix = Mat4::translate(0.0f, -1.0f, 0.0f);

    const auto skin = skel.computeSkinMatrices();
    ASSERT_EQ(skin.size(), 1u);
    // skin[0] == translate(0,1,0) * translate(0,-1,0) == I
    const Vec3f p = skin[0].transformPoint({0, 0, 0});
    EXPECT_NEAR(p.x, 0.0f, 1e-5f);
    EXPECT_NEAR(p.y, 0.0f, 1e-5f);
    EXPECT_NEAR(p.z, 0.0f, 1e-5f);

    VertexBoneWeights bw;
    bw.boneIndices[0] = 0;
    bw.weights[0] = 1.0f;
    const Vec3f skinned = skinPosition(Vec3f{0.5f, 0.25f, -0.5f}, bw, skin);
    EXPECT_NEAR(skinned.x, 0.5f, 1e-5f);
    EXPECT_NEAR(skinned.y, 0.25f, 1e-5f);
    EXPECT_NEAR(skinned.z, -0.5f, 1e-5f);
}

// 全局变换必须与骨骼在数组中的顺序无关（旧实现隐含父索引 < 子索引）。
TEST(Skeleton, GlobalTransformsIndependentOfBoneOrder)
{
    Skeleton skel;
    Bone child;
    child.parent = 1; // 先放子、后放父
    child.localTransform = Mat4::translate(0, 2, 0);
    Bone root;
    root.parent = -1;
    root.localTransform = Mat4::translate(10, 0, 0);
    skel.bones.push_back(child);
    skel.bones.push_back(root);

    const auto global = skel.computeGlobalTransforms();
    ASSERT_EQ(global.size(), 2u);
    const Vec3f p = global[0].transformPoint(Vec3f{0, 0, 0});
    EXPECT_NEAR(p.x, 10.0f, 1e-5f);  // 父平移 10 + 子平移 2
    EXPECT_NEAR(p.y, 2.0f, 1e-5f);
}

// 父索引越界与成环必须显式失败（返回空），不能返回错矩阵。
TEST(Skeleton, InvalidHierarchyReturnsEmpty)
{
    Skeleton outOfRange;
    Bone bad;
    bad.parent = 5;
    outOfRange.bones.push_back(bad);
    EXPECT_TRUE(outOfRange.computeGlobalTransforms().empty());
    EXPECT_TRUE(outOfRange.computeSkinMatrices().empty());

    Skeleton cyclic;
    Bone a;
    a.parent = 1;
    Bone b;
    b.parent = 0;
    cyclic.bones.push_back(a);
    cyclic.bones.push_back(b);
    EXPECT_TRUE(cyclic.computeGlobalTransforms().empty());
}

// 越界骨骼索引的权重被忽略；全部无效时位置退回原值而不是零向量。
TEST(Skeleton, SkinningIgnoresInvalidBoneIndex)
{
    VertexBoneWeights bw;
    bw.boneIndices[0] = 7;
    bw.weights[0] = 1.0f;
    const std::vector<Mat4> skinMats = {Mat4::translate(5, 0, 0)};
    const Vec3f pos{1, 2, 3};
    const Vec3f result = skinPosition(pos, bw, skinMats);
    EXPECT_NEAR(result.x, pos.x, 1e-5f);
    EXPECT_NEAR(result.y, pos.y, 1e-5f);
    EXPECT_NEAR(result.z, pos.z, 1e-5f);
}

// 无有效权重时法线不得返回 NaN。
TEST(Skeleton, SkinNormalWithoutWeightsKeepsNormal)
{
    const VertexBoneWeights bw{};
    const std::vector<Mat4> skinMats = {Mat4::identity()};
    const Vec3f normal = Vec3f{0, 1, 0};
    const Vec3f result = skinNormal(normal, bw, skinMats);
    EXPECT_NEAR(result.y, 1.0f, 1e-5f);
}

// ==================== 粒子发射器 ====================

// sizeMin 必须生效：旧实现无论配置如何都用 sizeMax 作为出生尺寸。
TEST(ParticleSystem, EmittedSizeHonoursSizeMin)
{
    EmitterConfig cfg;
    cfg.sizeMin = 0.42f;
    cfg.sizeMax = 0.42f;
    cfg.rate = 200.0f;
    cfg.lifeMin = 1000.0f;
    cfg.lifeMax = 1000.0f;
    cfg.maxParticles = 16;
    ParticleSystem ps(cfg);
    ps.update(0.05f);

    const auto alive = ps.getAlive();
    ASSERT_FALSE(alive.empty());
    for (const Particle* p : alive)
        EXPECT_NEAR(p->size, 0.42f, 0.01f);
}

// 运行期调大 maxParticles 必须真实扩容（旧实现只在构造时 resize）。
TEST(ParticleSystem, RuntimeCapacityChangeTakesEffect)
{
    EmitterConfig cfg;
    cfg.rate = 200.0f;
    cfg.maxParticles = 2;
    ParticleSystem ps(cfg);

    ps.config().maxParticles = 10;
    ps.update(0.02f);  // 期望发射 4 个，容量放得下
    EXPECT_EQ(ps.getAlive().size(), 4u);
}

// dt 非有限或负值不得发射，也不得扰动已有状态。
TEST(ParticleSystem, NonFiniteDeltaTimeIsIgnored)
{
    EmitterConfig cfg;
    cfg.rate = 1000.0f;
    ParticleSystem ps(cfg);
    ps.update(0.0f);
    ps.update(-1.0f);
    EXPECT_EQ(ps.getAlive().size(), 0u);
}

// 发射量受剩余空槽限制：超出容量不得越界重写或堆成负剩余。
TEST(ParticleSystem, EmissionIsCappedByCapacity)
{
    EmitterConfig cfg;
    cfg.maxParticles = 100;
    cfg.rate = 1e9f;  // 单帧请求远超容量
    cfg.lifeMin = 10.0f;
    cfg.lifeMax = 10.0f;
    ParticleSystem ps(cfg);

    ps.update(1.0f / 60.0f);
    EXPECT_EQ(ps.getAlive().size(), 100u);

    ps.update(1.0f / 60.0f);
    EXPECT_EQ(ps.getAlive().size(), 100u);
}

// 槽位满后继续发射不得重复占用或超量。
TEST(ParticleSystem, FullPoolDoesNotOverEmit)
{
    EmitterConfig cfg;
    cfg.maxParticles = 8;
    cfg.rate = 1000.0f;
    cfg.lifeMin = 10.0f;
    cfg.lifeMax = 10.0f;
    ParticleSystem ps(cfg);
    for (int i = 0; i < 10; ++i)
        ps.update(1.0f / 60.0f);
    EXPECT_EQ(ps.getAlive().size(), 8u);
}

// ==================== Benchmark ====================

TEST(Particle, EmitAndUpdate)
{
    EmitterConfig cfg;
    cfg.rate = 100;
    cfg.maxParticles = 50;
    cfg.gravity = {0, 0, 0};
    ParticleSystem ps(cfg);
    ps.update(0.1f);  // 应发射 10 个
    EXPECT_EQ(ps.aliveCount(), 10);
    ps.update(1.0f);  // 再发射 100，但上限 50
    EXPECT_LE(ps.aliveCount(), 50);
}

TEST(Particle, Lifetime)
{
    EmitterConfig cfg;
    cfg.rate = 100;
    cfg.lifeMin = 0.1f;
    cfg.lifeMax = 0.1f;
    cfg.maxParticles = 50;
    ParticleSystem ps(cfg);
    ps.update(0.05f);  // rate*dt = 5，发射 5 个
    int before = ps.aliveCount();
    EXPECT_GT(before, 0);
    ps.update(0.2f);  // 超过生命周期 0.1s
    EXPECT_EQ(ps.aliveCount(), 0);
}

// ==================== 资源缓存 ====================

TEST(Camera2D, WorldScreenRoundTrip)
{
    Camera2D cam(800, 600);
    cam.position = {10.0f, -5.0f};
    cam.zoom = 2.0f;
    cam.rotation = 0.3f;
    Vec2f w{3.0f, 7.0f};
    Vec2f back = cam.screenToWorld(cam.worldToScreen(w));
    EXPECT_NEAR(back.x, w.x, 1e-3f);
    EXPECT_NEAR(back.y, w.y, 1e-3f);
}

TEST(Camera2D, CenterMapsToScreenCenter)
{
    Camera2D cam(800, 600);
    cam.position = {4.0f, 9.0f};
    Vec2f s = cam.worldToScreen(cam.position);
    EXPECT_NEAR(s.x, 400.0f, 1e-3f);
    EXPECT_NEAR(s.y, 300.0f, 1e-3f);
}

// ==================== 2D 图元 ====================

