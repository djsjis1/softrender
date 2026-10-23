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

