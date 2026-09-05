// ============================================================
// 阶段 3：数学库 + MVP 变换 + OBJ 加载
//
// 生成 output/stage3_cube.tga：把一个 3D 立方体经过
// Model -> View -> Projection -> 透视除法 -> Viewport
// 投影到 2D 屏幕。分两块画：
//   左：随机色实心面（还没有深度测试，会看到穿插——这正是
//       阶段 4 要解决的问题，先留个悬念）
//   右：白色线框，直观看清透视的"近大远小"
//
// 若 assets/model.obj 存在则加载它，否则用程序化立方体。
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include "core/framebuffer.h"
#include "core/tga.h"
#include "math/transform.h"
#include "raster/triangle.h"
#include "scene/mesh.h"
#include "scene/obj_loader.h"

int main() {
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(24, 24, 32));

    // 载入模型：优先外部 OBJ，回退到程序化立方体
    Mesh mesh;
    if (objloader::load("assets/model.obj", mesh))
        std::printf("已加载 assets/model.obj: %d 顶点 %d 三角形\n",
                    int(mesh.vertices.size()), mesh.triangleCount());
    else {
        mesh = meshgen::cube(1.0f);
        std::printf("未找到 assets/model.obj，使用程序化立方体\n");
    }

    // ---------- 构建 MVP ----------
    const float PI = 3.14159265f;
    // Model：绕 Y 和 X 转一点，让立方体呈现三个可见面
    Mat4 model = Mat4::rotateY(PI * 0.2f) * Mat4::rotateX(PI * 0.15f);
    // View：相机在 (0,0,3) 看向原点
    Mat4 view = transform::lookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
    // Projection：45° 视场角
    Mat4 proj = transform::perspective(PI / 4.0f, float(W) / H, 0.1f, 100.0f);
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
        wireTriangle(fb, project(p0, W / 2), project(p1, W / 2), project(p2, W / 2),
                     colors::green);
    }

    // 中间分隔线
    drawLine(fb, W / 2, 0, W / 2, H - 1, Color(80, 80, 90));

    std::filesystem::create_directories("output");
    const char* path = "output/stage3_cube.tga";
    if (!tga::write(path, fb)) {
        std::printf("写文件失败: %s\n", path);
        return 1;
    }
    std::printf("已生成 %s (%dx%d)\n", path, W, H);
    return 0;
}
