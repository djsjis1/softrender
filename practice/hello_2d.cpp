// ============================================================
// 练习入门模板 —— 复制本文件、改个名字，即可开始一个新的练习。
//
// 【约定】把使用本库的练习源码放进 practice/，每个 .cpp 就是一个独立程序：
//   practice/hello_2d.cpp  →  可执行目标 practice_hello_2d
//   输出到 build/bin/<Config>/practice_hello_2d.exe
// 新增/删除文件后重新构建即可（CMake 用 CONFIGURE_DEPENDS 自动重新配置）。
//
// 本模板演示：用 Renderer2D 画一张图并导出 PNG 到 output/。
// 想做“实时窗口”练习时，加上：
//   #include "platform/win32_window.h"
//   Win32Window window(W, H, "title");  ... window.present(fb);
// （gdi32/user32 已由 CMake 自动链接，${CMAKE_SOURCE_DIR} 已在头文件搜索路径）
// ============================================================
#include <cstdio>
#include <filesystem>

#include "core/framebuffer.h"
#include "core/image_write.h"
#include "raster/renderer2d.h"

int main()
{
    std::filesystem::create_directories("output");

    const int W = 640, H = 400;
    Framebuffer fb(W, H);

    Renderer2D r2d;
    r2d.begin(fb, Color(24, 26, 34)); // 绑定目标 + 清屏

    // 竖直渐变背景
    r2d.setBlend(BlendMode::Opaque);
    r2d.rectGradient(0, 0, (float)W, (float)H, Color(40, 60, 120), Color(18, 20, 28), false);

    // 一个半透明圆（alpha 混合）
    r2d.setBlend(BlendMode::AlphaBlend);
    r2d.setColor(Color(255, 180, 90, 200));
    r2d.fillCircle(W * 0.5f, H * 0.5f, 80.0f);

    // 变换栈：绕中心旋转的方块
    r2d.push();
    r2d.translate(W * 0.5f, H * 0.5f);
    r2d.rotate(0.6f);
    r2d.setColor(Color(120, 220, 255, 180));
    r2d.fillRect(-40.0f, -40.0f, 80.0f, 80.0f);
    r2d.pop();

    // 内置位图字体文字
    r2d.setColor(colors::white);
    r2d.text(40.0f, 40.0f, "HELLO SOFTRENDERER", 2.0f);

    r2d.end();

    imagewriter::writeImage("output/practice_hello_2d.png", fb);
    std::printf("已生成 output/practice_hello_2d.png\n");
    return 0;
}
