#pragma once
#include <cmath>
#include "math/mat4.h"

// ============================================================
// 观察与投影变换：一个顶点从模型空间到屏幕像素的完整旅程
//
//   模型空间 --Model--> 世界空间 --View--> 相机空间
//     --Projection--> 裁剪空间 --透视除法(÷w)--> NDC
//     --Viewport--> 屏幕空间
//
// 约定：右手系，相机在原点看向 -Z（OpenGL 惯例）。
// NDC（规范化设备坐标）：三轴都在 [-1, 1] 的立方体。
// ============================================================
namespace transform
{

    // ---------- View 矩阵（lookAt）----------
    // 思路：与其"移动相机"，不如把整个世界反向变换，让相机回到
    // 原点朝 -Z。构造相机的正交基 (right, up', forward)：
    //   forward = normalize(eye - target)   注意指向 +Z（看向的是 -forward）
    //   right   = normalize(cross(up, forward))
    //   up'     = cross(forward, right)
    // View = [基向量按行排] * translate(-eye)
    // （正交矩阵的逆是转置，所以基向量按"行"放就是旋转的逆）
    inline Mat4 lookAt(const Vec3f &eye, const Vec3f &target, const Vec3f &up)
    {
        Vec3f f = (eye - target).normalized(); // 相机 +Z（背向观察方向）
        Vec3f r = cross(up, f).normalized();   // 相机 +X
        Vec3f u = cross(f, r);                 // 相机 +Y（已单位化）

        Mat4 view = Mat4::identity();
        // 旋转部分：世界坐标投影到相机基向量上（点积 = 每行放一个基向量）
        view.m[0][0] = r.x;
        view.m[0][1] = r.y;
        view.m[0][2] = r.z;
        view.m[1][0] = u.x;
        view.m[1][1] = u.y;
        view.m[1][2] = u.z;
        view.m[2][0] = f.x;
        view.m[2][1] = f.y;
        view.m[2][2] = f.z;
        // 平移部分：先把 eye 挪回原点再旋转，合并后 = -基·eye
        view.m[0][3] = -dot(r, eye);
        view.m[1][3] = -dot(u, eye);
        view.m[2][3] = -dot(f, eye);
        return view;
    }

    // ---------- 透视投影矩阵 ----------
    // 核心思想：近大远小 = "x、y 除以距离"。齐次坐标的妙处在于
    // 不能在矩阵里做除法，但可以把 -z 塞进 w，让后续的
    // "透视除法 (÷w)" 完成这件事。
    //
    // 参数：fovY 垂直视场角（弧度），aspect = 宽/高，
    //       n/f 近/远平面距离（正数）。
    // 推导要点：
    //   近平面上半高 t = n*tan(fovY/2)，半宽 = t*aspect
    //   x_ndc = x / (z*tan*aspect)，y 同理 → 矩阵前两行
    //   z 行：设 z' = (A*z+B)/-z，要求 z=-n → -1, z=-f → +1，
    //   解得 A=-(f+n)/(f-n), B=-2fn/(f-n)
    //   第 4 行 (0,0,-1,0)：把 -z 复制进 w —— 全场最关键的一行
    inline Mat4 perspective(float fovY, float aspect, float n, float f)
    {
        float t = std::tan(fovY * 0.5f);
        Mat4 p; // 全零开始
        p.m[0][0] = 1.0f / (aspect * t);
        p.m[1][1] = 1.0f / t;
        p.m[2][2] = -(f + n) / (f - n);
        p.m[2][3] = -2.0f * f * n / (f - n);
        p.m[3][2] = -1.0f; // w' = -z：距离越远 w 越大，除完就越小 → 近大远小
        return p;
    }

    // ---------- 正交投影矩阵 ----------
    // 与透视不同，正交投影没有"近大远小"，平行线保持平行。
    // 平行光（方向光）的阴影贴图用它：把一个立方体范围
    // [l,r]×[b,t]×[n,f] 线性映射到 NDC 立方体 [-1,1]³。
    // 推导：每个轴做一次"平移到中心 + 缩放到 [-1,1]"。
    inline Mat4 ortho(float l, float r, float b, float t, float n, float f)
    {
        Mat4 m = Mat4::identity();
        m.m[0][0] = 2.0f / (r - l);
        m.m[1][1] = 2.0f / (t - b);
        m.m[2][2] = -2.0f / (f - n); // -Z 朝前，与透视一致
        m.m[0][3] = -(r + l) / (r - l);
        m.m[1][3] = -(t + b) / (t - b);
        m.m[2][3] = -(f + n) / (f - n);
        return m;
    }

    // ---------- 视口变换 ----------
    // NDC [-1,1]² 映射到屏幕像素 [0,w)×[0,h)；z 从 [-1,1] 映到 [0,1]
    // 留给深度缓冲用（阶段 4）
    inline Mat4 viewport(int w, int h)
    {
        Mat4 v = Mat4::identity();
        v.m[0][0] = w * 0.5f;
        v.m[0][3] = w * 0.5f;
        v.m[1][1] = h * 0.5f;
        v.m[1][3] = h * 0.5f;
        v.m[2][2] = 0.5f;
        v.m[2][3] = 0.5f;
        return v;
    }

} // namespace transform
