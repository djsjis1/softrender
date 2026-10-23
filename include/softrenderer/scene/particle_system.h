#pragma once
#include <vector>
#include <cmath>
#include <random>
#include "softrenderer/math/vec.h"
#include "softrenderer/core/color.h"

// ============================================================
// 粒子系统：Billboard 粒子 + 发射器
//
// 每个粒子是一个面向相机的四边形（billboard），用加法混合渲染。
// 发射器控制粒子的生成速率、初始速度、生命周期等。
//
// 参考：GPU Gems 粒子系统章节、Unity Particle System 设计。
// ============================================================

namespace sr
{

struct Particle
{
    Vec3f position;
    Vec3f velocity;
    Color color;
    float size;
    float life; // 剩余生命 [0, maxLife]
    float maxLife;
    bool alive;
};

struct EmitterConfig
{
    Vec3f position{0, 0, 0};
    Vec3f direction{0, 1, 0};       // 发射方向
    float spread = 0.5f;            // 扩散角（弧度）
    float speed = 2.0f;             // 初始速度
    float speedVariance = 0.5f;     // 速度随机范围
    float rate = 50.0f;             // 每秒发射数
    float lifeMin = 1.0f;           // 最短生命
    float lifeMax = 3.0f;           // 最长生命
    float sizeMin = 0.05f;          // 最小尺寸
    float sizeMax = 0.15f;          // 最大尺寸
    Color colorStart{255, 200, 50}; // 初始颜色
    Color colorEnd{255, 50, 0, 0};  // 结束颜色（alpha=0 淡出）
    Vec3f gravity{0, -2.0f, 0};     // 重力
    int maxParticles = 1000;        // 最大粒子数
};

class ParticleSystem
{
public:
    explicit ParticleSystem(const EmitterConfig &cfg = {}) : config_(cfg)
    {
        particles_.resize(config_.maxParticles);
        for (auto &p : particles_)
            p.alive = false;
    }

    // 更新所有粒子（dt = 帧时间）
    void update(float dt)
    {
        emitAccum_ += config_.rate * dt;
        int toEmit = static_cast<int>(emitAccum_);
        emitAccum_ -= toEmit;

        // 发射新粒子
        for (int i = 0; i < toEmit; ++i)
            emit();

        // 更新存活粒子
        for (auto &p : particles_)
        {
            if (!p.alive)
                continue;
            p.life -= dt;
            if (p.life <= 0)
            {
                p.alive = false;
                continue;
            }
            p.velocity = p.velocity + config_.gravity * dt;
            p.position = p.position + p.velocity * dt;
            // 颜色插值
            float t = 1.0f - p.life / p.maxLife;
            p.color = Color::lerp(config_.colorStart, config_.colorEnd, t);
            // 尺寸随生命缩小
            p.size = config_.sizeMax * (p.life / p.maxLife);
        }
    }

    // 获取存活粒子列表（用于渲染）
    std::vector<const Particle *> getAlive() const
    {
        std::vector<const Particle *> alive;
        for (const auto &p : particles_)
            if (p.alive)
                alive.push_back(&p);
        return alive;
    }

    int aliveCount() const
    {
        int c = 0;
        for (const auto &p : particles_)
            if (p.alive)
                ++c;
        return c;
    }

    const EmitterConfig &config() const { return config_; }
    EmitterConfig &config() { return config_; }

private:
    void emit()
    {
        // 找一个空闲槽
        for (auto &p : particles_)
        {
            if (p.alive)
                continue;
            p.alive = true;
            p.position = config_.position;
            // 随机方向（锥形扩散）
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            Vec3f dir = config_.direction.normalized();
            Vec3f right = std::abs(dir.y) < 0.99f ? cross(dir, Vec3f{0, 1, 0}).normalized() : cross(dir, Vec3f{1, 0, 0}).normalized();
            Vec3f up = cross(right, dir);
            float angle = dist(rng_) * config_.spread;
            float angle2 = dist(rng_) * config_.spread;
            Vec3f vel = dir + right * std::sin(angle) + up * std::sin(angle2);
            vel = vel.normalized();
            float spd = config_.speed + dist(rng_) * config_.speedVariance;
            p.velocity = vel * spd;
            p.maxLife = config_.lifeMin + (config_.lifeMax - config_.lifeMin) * (dist(rng_) * 0.5f + 0.5f);
            p.life = p.maxLife;
            p.size = config_.sizeMax;
            p.color = config_.colorStart;
            return;
        }
    }

    EmitterConfig config_;
    std::vector<Particle> particles_;
    float emitAccum_ = 0;
    std::mt19937 rng_{42};
};

}  // namespace sr
