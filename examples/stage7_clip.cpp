// ============================================================
// 阶段 7：近平面裁剪对比
//
// 生成 output/stage7_clip.tga（左右对比）：
//   左半屏：nearClip=false —— 旧做法，横跨近平面的三角形被整块
//           丢弃，地面/墙面出现大片空洞
//   右半屏：nearClip=true  —— 沿近平面正确切开，画面完整无洞
//
// 场景刻意让相机很低、很贴近地面与一堵大墙，使大量三角形跨越
// 近平面——这正是裁剪算法大显身手的场合。
// ============================================================
#include <cstdio>
#include <filesystem>
#include <vector>
#include "core/framebuffer.h"
#include "core/tga.h"
#include "raster/renderer.h"
#include "scene/camera.h"
#include "scene/mesh.h"
#include "scene/texture.h"
#include "shader/shaders.h"

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(30, 34, 44));

    Texture checker(texgen::checkerboard(256, 8, Color(220, 220, 220), Color(70, 80, 95)));
    Texture wallTex(texgen::checkerboard(256, 6, Color(200, 150, 120), Color(90, 60, 45)));

    // 地面 + 一堵竖墙（都用大网格，保证有三角形横跨近平面）
    Mesh ground = meshgen::planeGrid(40.0f, 20, 20.0f);
    // 墙：把地面网格立起来（绕 X 转 90°）并抬到前方
    Mesh wall = meshgen::planeGrid(20.0f, 16, 8.0f);

    Camera cam;
    // 相机压得很低、几乎贴地，且离墙很近——制造大量跨近平面三角形
    cam.position = {0.0f, 0.35f, 1.2f};
    cam.yaw = -3.14159265f * 0.5f;
    cam.pitch = 0.05f;
    cam.aspect = float(W / 2) / H;
    cam.nearZ = 0.1f;

    Vec3f lightDir = Vec3f{0.3f, 0.9f, 0.4f}.normalized();
    Mat4 view = cam.viewMatrix();
    Mat4 proj = cam.projMatrix();

    PhongShader shader;
    shader.view = view;
    shader.proj = proj;
    shader.lightDir = lightDir;
    shader.cameraPos = cam.position;
    shader.ambient = {0.25f, 0.25f, 0.28f};

    // 用两个半宽 Renderer 分别渲染左右两半，避免坐标偏移的复杂性
    auto renderHalf = [&](Framebuffer &target, bool clip)
    {
        Renderer r(W / 2, H);
        r.nearClip = clip;
        r.clear(target, Color(30, 34, 44));

        auto drawObj = [&](const Mesh &m, const Mat4 &model, const Texture *tex, bool doubleSided)
        {
            shader.model = model;
            shader.mvp = proj * view * model;
            shader.normalMatrix = model.inversed().transposed();
            shader.texture = tex;
            r.cullBackface = !doubleSided;
            r.drawMesh(target, m, shader);
        };

        drawObj(ground, Mat4::identity(), &checker, true);
        // 墙立在 z=-4，绕 X 轴转 90° 使其竖直
        Mat4 wallModel = Mat4::translate(0, 4.0f, -4.0f) * Mat4::rotateX(1.57079633f);
        drawObj(wall, wallModel, &wallTex, true);
    };

    Framebuffer left(W / 2, H), right(W / 2, H);
    renderHalf(left, false); // 左：无裁剪
    renderHalf(right, true); // 右：有裁剪

    // 合并到全宽画面
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W / 2; ++x)
        {
            fb.setPixel(x, y, left.getPixel(x, y));
            fb.setPixel(x + W / 2, y, right.getPixel(x, y));
        }
    }
    for (int y = 0; y < H; ++y)
        fb.setPixel(W / 2, y, Color(255, 200, 60));

    std::filesystem::create_directories("output");
    const char *path = "output/stage7_clip.tga";
    if (!tga::write(path, fb))
    {
        std::printf("写文件失败: %s\n", path);
        return 1;
    }
    std::printf("已生成 %s（左=无裁剪有空洞，右=近平面裁剪正确）\n", path);
    return 0;
}
