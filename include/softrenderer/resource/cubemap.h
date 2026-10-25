#pragma once
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <vector>

#include "softrenderer/math/vec.h"
#include "softrenderer/resource/texture.h"

// ============================================================
// Cubemap：六面环境贴图
//
// 存储 6 张 face 纹理（+X, -X, +Y, -Y, +Z, -Z），
// 用方向向量采样。用于 IBL（Image-Based Lighting）：
//   - 镜面反射：用反射向量 R 采样 cubemap
//   - 漫反射辐照度：用预滤波的 irradiance map（半球积分近似）
//
// 参考：LearnOpenGL IBL、OpenGL Cubemap 教程。
// ============================================================

namespace sr
{

enum class CubeFace : int
{
    PosX = 0,
    NegX,
    PosY,
    NegY,
    PosZ,
    NegZ
};

class Cubemap
{
   public:
    Cubemap() = default;

    // 从 6 张 sr::Image 构建
    void setFaces(const sr::Image faces[6])
    {
        for (int i = 0; i < 6; ++i)
            faces_[i] = Texture(faces[i]);
    }

    // 六面必须全部有效：只检查第一面会让缺失面的采样落到默认 Texture（无 mip）。
    bool valid() const
    {
        for (const Texture& face : faces_)
            if (!face.valid())
                return false;
        return true;
    }

    // 用方向向量采样（双线性）
    Vec3f sample(const Vec3f& dir) const
    {
        if (!valid())
            return {0, 0, 0};
        int face = 0;
        float u = 0.0f, v = 0.0f;
        if (!faceFor(dir, face, u, v))
            return {0, 0, 0};
        return faces_[face].sampleBilinear(u, v);
    }

    // 镜面反射采样：按粗糙度选 mip 层。
    //
    // 每面 Texture 已携带 box filter mip 链，用级别近似预滤波：层号越高，
    // 采样范围越宽，粗糙表面得到更模糊的反射。这只是盒式滤波近似，
    // 与真正的 GGX 预滤波（按 NDF 卷积）仍有差异，但不再是“忽略 roughness”。
    Vec3f sampleReflection(const Vec3f& R, float roughness) const
    {
        if (!valid())
            return {0, 0, 0};
        int face = 0;
        float u = 0.0f, v = 0.0f;
        if (!faceFor(R, face, u, v))
            return {0, 0, 0};
        const Texture& tex = faces_[face];
        const float k = std::isfinite(roughness) ? std::clamp(roughness, 0.0f, 1.0f) : 0.0f;
        const int maxLevel = std::max(0, tex.mipLevels() - 1);
        const int level = int(std::lround(k * float(maxLevel)));
        return tex.sampleLevel(u, v, level);
    }

    // 漫反射辐照度近似（半球采样 N 次取平均）
    Vec3f sampleIrradiance(const Vec3f& N, int samples = 32) const
    {
        if (!valid() || samples <= 0)
            return {0, 0, 0};
        const float normalLenSq = N.lengthSq();
        if (!(normalLenSq > 1e-20f) || !std::isfinite(normalLenSq))
            return {0, 0, 0};
        Vec3f n = N * (1.0f / std::sqrt(normalLenSq));
        Vec3f up = std::abs(n.y) < 0.999f ? Vec3f{0, 1, 0} : Vec3f{1, 0, 0};
        Vec3f T = cross(up, n).normalized();
        Vec3f B = cross(n, T);

        Vec3f irradiance{0, 0, 0};
        float totalWeight = 0;
        const float PI = 3.14159265f;
        // 均匀半球采样
        for (int i = 0; i < samples; ++i)
        {
            // Fibonacci 半球
            float fi = (i + 0.5f) / samples;
            float cosTheta = fi;  // [0,1] → 从顶到底
            float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
            float phi = PI * (1.0f + std::sqrt(5.0f)) * i;  // 黄金角
            Vec3f H = T * (sinTheta * std::cos(phi)) + B * (sinTheta * std::sin(phi)) + n * cosTheta;
            float weight = cosTheta;  // cos 加权
            irradiance = irradiance + sample(H) * weight;
            totalWeight += weight;
        }
        return irradiance * (1.0f / totalWeight);
    }

   private:
    // 方向向量 → 面索引 + [0,1] UV。零向量与非有限输入返回 false，
    // 避免下游用 1/0 选面与求 UV。
    bool faceFor(const Vec3f& dir, int& faceIndex, float& outU, float& outV) const
    {
        const float lenSq = dir.lengthSq();
        if (!(lenSq > 1e-20f) || !std::isfinite(lenSq))
            return false;
        const Vec3f d = dir * (1.0f / std::sqrt(lenSq));
        const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);

        CubeFace face;
        float u, v;
        if (ax >= ay && ax >= az)
        {
            face = d.x > 0 ? CubeFace::PosX : CubeFace::NegX;
            const float inv = 1.0f / ax;
            u = d.x > 0 ? (-d.z * inv) : (d.z * inv);
            v = -d.y * inv;
        }
        else if (ay >= ax && ay >= az)
        {
            face = d.y > 0 ? CubeFace::PosY : CubeFace::NegY;
            const float inv = 1.0f / ay;
            u = d.x * inv;
            v = d.y > 0 ? (d.z * inv) : (-d.z * inv);
        }
        else
        {
            face = d.z > 0 ? CubeFace::PosZ : CubeFace::NegZ;
            const float inv = 1.0f / az;
            u = d.z > 0 ? (d.x * inv) : (-d.x * inv);
            v = -d.y * inv;
        }
        faceIndex = int(face);
        outU = u * 0.5f + 0.5f;
        outV = v * 0.5f + 0.5f;
        return true;
    }

    Texture faces_[6];
};

// 程序化生成简单渐变 cubemap（天空模拟）
inline void generateSkyCubemap(Cubemap& out, int size = 64, Vec3f zenith = {0.2f, 0.4f, 0.8f},
                               Vec3f horizon = {0.7f, 0.8f, 0.9f})
{
    sr::Image faces[6];
    if (size <= 0)
    {
        out.setFaces(faces); // 六张空图：Cubemap 变为无效而非崩溃
        return;
    }
    for (int f = 0; f < 6; ++f)
    {
        faces[f].width = size;
        faces[f].height = size;
        faces[f].pixels.resize(size_t(size) * size);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                // 从像素坐标反算方向
                float u = (2.0f * (x + 0.5f) / size) - 1.0f;
                float v = (2.0f * (y + 0.5f) / size) - 1.0f;
                Vec3f dir;
                switch (CubeFace(f))
                {
                    case CubeFace::PosX:
                        dir = {1, -v, -u};
                        break;
                    case CubeFace::NegX:
                        dir = {-1, -v, u};
                        break;
                    case CubeFace::PosY:
                        dir = {u, 1, v};
                        break;
                    case CubeFace::NegY:
                        dir = {u, -1, -v};
                        break;
                    case CubeFace::PosZ:
                        dir = {u, -v, 1};
                        break;
                    case CubeFace::NegZ:
                        dir = {-u, -v, -1};
                        break;
                }
                dir = dir.normalized();
                float t = std::max(dir.y, 0.0f);  // 0=地平线, 1=天顶
                Vec3f color = horizon * (1.0f - t) + zenith * t;
                faces[f].pixels[size_t(y) * size + x] = Color::fromFloat(color.x, color.y, color.z);
            }
        }
    }
    out.setFaces(faces);
}

}  // namespace sr
