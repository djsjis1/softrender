// ============================================================
// 阶段 05：MVP 变换链 + OBJ 加载
//
// 【源码阅读】先读这些文件：
//   math/vec.h            — Vec3 向量运算
//   math/mat4.h           — Mat4 矩阵乘法、逆矩阵
//   math/transform.h      — modelMatrix / viewMatrix / perspectiveProjection
//   scene/obj_loader.h    — OBJ 模型解析
//   raster/triangle.h     — 三角形填充（本阶段还没有深度测试）
//
// 【运行验证】生成 output/stage05_transform.tga：
//   左：随机色实心面（无深度测试，穿插可见——阶段 06 解决）
//   右：白色线框，看清透视"近大远小"
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/render/raster/triangle.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/io/obj_loader.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/render/draw2d/shape2d.h"

using namespace sr;

int main() {
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(24, 24, 32));

    // 载入模型：优先外部 OBJ，回退到程序化立方体
    Mesh mesh;
    if (sr::io::loadObjMesh("assets/model.obj", mesh))
        std::printf("已加载 assets/model.obj: %d 顶点 %d 三角形\n",
                    int(mesh.vertices.size()), mesh.triangleCount());
    else {
        mesh = sr::meshgen::cube(1.0f);
        std::printf("未找到 assets/model.obj，使用程序化立方体\n");
    }

    // ---------- 构建 MVP ----------
    const float PI = 3.14159265f;
    // Model：绕 Y 和 X 转一点，让立方体呈现三个可见面
    Mat4 model = Mat4::rotateY(PI * 0.2f) * Mat4::rotateX(PI * 0.15f);
    // View：相机在 (0,0,3) 看向原点
    Mat4 view = sr::transform::lookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
    // Projection：45° 视场角
    Mat4 proj = sr::transform::perspective(PI / 4.0f, float(W) / H, 0.1f, 100.0f);
    Mat4 mvp = proj * view * model;

    std::mt19937 rng(7);
    std::uniform_int_distribution<int> ch(80, 255);

    // 把一个模型空间顶点变换到屏幕坐标；w 一并返回供调试
    auto project = [&](const Vec3f& p, int offsetX) -> Vec2f {
        Vec4f clip = mvp * Vec4f(p, 1.0f);
        // 透视除法：裁剪空间 -> NDC。这一步实现了"近大远小"
        Vec3f ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
        // 视口变换：NDC -> 屏幕像素（半屏宽，方便左右并排）
        float sx = (ndc.x * 0.5f + 0.5f) * (W / 2) + offsetX;
        float sy = (ndc.y * 0.5f + 0.5f) * H;
        return {sx, sy};
    };

    // ---------- 左半屏：随机色实心面 ----------
    for (int i = 0; i < mesh.triangleCount(); ++i) {
        Vec3f p0 = mesh.vertices[mesh.indices[i * 3 + 0]].position;
        Vec3f p1 = mesh.vertices[mesh.indices[i * 3 + 1]].position;
        Vec3f p2 = mesh.vertices[mesh.indices[i * 3 + 2]].position;
        Color c(uint8_t(ch(rng)), uint8_t(ch(rng)), uint8_t(ch(rng)));
        fillTriangle(fb, project(p0, 0), project(p1, 0), project(p2, 0), c);
    }

    // ---------- 右半屏：线框 ----------
    for (int i = 0; i < mesh.triangleCount(); ++i) {
        Vec3f p0 = mesh.vertices[mesh.indices[i * 3 + 0]].position;
        Vec3f p1 = mesh.vertices[mesh.indices[i * 3 + 1]].position;
        Vec3f p2 = mesh.vertices[mesh.indices[i * 3 + 2]].position;
        sr::draw2d::wireTriangle(fb, project(p0, W / 2), project(p1, W / 2), project(p2, W / 2),
                     sr::colors::green);
    }

    // 中间分隔线
    sr::draw2d::drawLine(fb, W / 2, 0, W / 2, H - 1, Color(80, 80, 90));

    std::filesystem::create_directories("output");
    const char* path = "output/stage05_transform.tga";
    if (!sr::io::writeTga(path, fb)) {
        std::printf("写文件失败: %s\n", path);
        return 1;
    }
    std::printf("已生成 %s (%dx%d)\n", path, W, H);
    return 0;
}
