#pragma once
#include <algorithm>
#include <cmath>
#include "softrenderer/render/pipeline/surface_shader.h"

// ============================================================
// 三种经典光照着色器，展示"光照在管线哪个阶段计算"的差别：
//
//   Flat    ：逐三角形一个法线 → 整面同色，能看到棱面（最省）
//   Gouraud ：逐顶点算光照 → 面内插值颜色，平滑但高光会糊
//   Phong   ：逐片元算光照 → 插值法线后每像素算，高光锐利（最贵最好）
//
// 光照模型统一用 Blinn-Phong：
//   环境项 ambient + 漫反射 diffuse*max(0,N·L) + 高光 spec*max(0,N·H)^s
//   其中半程向量 H = normalize(L + V)，V 为指向相机方向。
// ============================================================

// 通用 Blinn-Phong 计算（世界空间法线 n、位置 p 已给出）
namespace sr
{

inline Vec3f blinnPhong(const SurfaceShader &s, const Vec3f &n, const Vec3f &p,
                        const Vec3f &albedo)
{
    Vec3f N = n.normalized();
    Vec3f L = s.lightDir; // 已单位化
    float diff = std::max(0.0f, dot(N, L));

    Vec3f V = (s.cameraPos - p).normalized(); // 指向相机
    Vec3f Hh = (L + V).normalized();          // 半程向量
    float spec = 0.0f;
    if (diff > 0.0f)
        spec = std::pow(std::max(0.0f, dot(N, Hh)), s.shininess);

    Vec3f color = albedo * s.ambient             // 环境
                  + albedo * s.lightColor * diff // 漫反射
                  + s.lightColor * spec * 0.4f;  // 高光（白色高光）
    return color;
}

// ---------------- Flat 着色 ----------------
struct FlatShader : SurfaceShader
{
    Vec3f flatColor_{0.5f, 0.5f, 0.5f}; // geometry() 算好，fragment() 直接用

    Vec4f vertex(const Vertex &in, Varyings &out) override
    {
        out.worldPos = model.transformPoint(in.position);
        out.normal = toWorldNormal(in.normal);
        out.uv = in.uv;
        return mvp * Vec4f(in.position, 1.0f);
    }

    // 用三顶点世界坐标算"面法线"，光照对整个三角形只算一次
    void geometry(const Varyings &a, const Varyings &b, const Varyings &c) override
    {
        Vec3f faceN = cross(b.worldPos - a.worldPos, c.worldPos - a.worldPos);
        if (faceN.lengthSq() > 1e-12f)
            faceN = faceN.normalized();
        Vec3f center = (a.worldPos + b.worldPos + c.worldPos) * (1.0f / 3.0f);
        Vec3f albedo = sampleAlbedo(a.uv);
        flatColor_ = blinnPhong(*this, faceN, center, albedo);
    }

    bool fragment(const Varyings &, Vec3f &color) override
    {
        color = flatColor_;
        return true;
    }
};

// ---------------- Gouraud 着色 ----------------
struct GouraudShader : SurfaceShader
{
    Vec4f vertex(const Vertex &in, Varyings &out) override
    {
        Vec3f wp = model.transformPoint(in.position);
        Vec3f wn = toWorldNormal(in.normal);
        out.worldPos = wp;
        out.normal = wn;
        out.uv = in.uv;
        // 逐顶点算光照，结果存进 color，交给光栅化器插值
        out.color = blinnPhong(*this, wn, wp, sampleAlbedo(in.uv));
        return mvp * Vec4f(in.position, 1.0f);
    }

    bool fragment(const Varyings &v, Vec3f &color) override
    {
        color = v.color; // 直接用插值后的顶点色
        return true;
    }
};

// ---------------- Phong 着色（逐片元光照）----------------
struct PhongShader : SurfaceShader
{
    Vec4f vertex(const Vertex &in, Varyings &out) override
    {
        out.worldPos = model.transformPoint(in.position);
        out.normal = toWorldNormal(in.normal);
        out.uv = in.uv;
        return mvp * Vec4f(in.position, 1.0f);
    }

    bool fragment(const Varyings &v, Vec3f &color) override
    {
        // 关键：法线经插值后长度会变，必须重新单位化再算光照
        color = blinnPhong(*this, v.normal, v.worldPos, sampleAlbedo(v.uv));
        return true;
    }
};

}  // namespace sr
