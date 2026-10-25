// ============================================================
// 阶段 03：像素勇者 · 实时动画（仅 Windows）
//
// 【源码阅读】先读这些文件：
//   platform/win32_window.h  — Win32 GDI 窗口：present(fb) 显示帧缓冲
//   core/framebuffer.h       — 帧缓冲 + clear()
//   raster/shape2d.h         — 画线（地面轮廓等）
//
// 【运行验证】弹出 800×600 窗口：
//   勇者在黄昏草原上来回行走（2 帧动画），SPACE 跳跃，ESC 退出
//   太阳落下/升起、云朵飘动、天空颜色随时间变化
// ============================================================
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/image_write.h"
#include "platform/win32_window.h"
#include "softrenderer/render/draw2d/blit2d.h"
#include "softrenderer/render/draw2d/shape2d.h"

using namespace sr;

// 勇者 16x20，两帧走路动画（每行严格 16 个字符）：
//   帧 0：站姿（双腿并拢）
//   帧 1：迈步（双腿分开，右腿前跨）
// 含义：H=金发 S=皮肤 E=眼睛 C=红斗篷 A=铠甲 P=裤子 B=靴子 M=剑柄 I=剑刃
const std::vector<std::vector<std::string>> kHeroFrames = {
    {
        // 帧 0：站姿
        ".....HHHHHH.....", "....HHHHHHHH....", "..C.HHHHHHHH....", ".CC.HSSSSSSH....", ".CC.HSSSSSSH....",
        ".CC.HSSSESSH....", ".CC.HSSSSSSH....", "..C.HSSSSSSH....", ".....HSSSH.....",  "..AAAAAAAAAA....",
        ".AAAAAAAAAAA.M..", ".AAAAAAAAAAA.M..", ".AAAAAAAAAAAAM..", "..AAAAAAAAAA.M..", "..PP....PP...I..",
        "..PP....PP...I..", "..PP....PP...I..", "..BB....BB...I..", "..BB....BB...I..", "..BB....BB......",
    },
    {
        // 帧 1：迈步（右腿前跨，双腿分开更宽）
        ".....HHHHHH.....", "....HHHHHHHH....", "..C.HHHHHHHH....", ".CC.HSSSSSSH....", ".CC.HSSSSSSH....",
        ".CC.HSSSESSH....", ".CC.HSSSSSSH....", "..C.HSSSSSSH....", ".....HSSSH.....",  "..AAAAAAAAAA....",
        ".AAAAAAAAAAA.M..", ".AAAAAAAAAAA.M..", ".AAAAAAAAAAAAM..", "..AAAAAAAAAA.M..", "..PP......PP.I..",
        "..PP......PP.I..", "..PP......PP.I..", "..BB......BB.I..", "..BB......BB.I..", "..BB......BB....",
    },
};

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H);
    Win32Window window(W, H, "Pixel Hero | SPACE=Jump ESC=Exit");

    // 保存动画帧的目录（已存在时忽略失败）
    CreateDirectoryA("output", nullptr);
    CreateDirectoryA("output\\anim", nullptr);

    const std::vector<sr::draw2d::SpritePalette> palette = {
        {'H', Color(238, 186, 48)},   // 金发
        {'S', Color(255, 214, 170)},  // 皮肤
        {'E', Color(40, 40, 52)},     // 眼睛
        {'C', Color(196, 52, 48)},    // 红斗篷
        {'A', Color(148, 168, 200)},  // 银蓝铠甲
        {'P', Color(58, 68, 110)},    // 裤子
        {'B', Color(92, 62, 42)},     // 靴子
        {'M', Color(196, 150, 62)},   // 剑柄
        {'I', Color(224, 232, 244)},  // 剑刃
    };

    const int scale = 12;
    const int heroW = 16 * scale;
    const float PI = 3.14159265f;
    const float DAY = 18.0f;  // 太阳起落周期（秒）

    using clock = std::chrono::high_resolution_clock;
    auto lastTime = clock::now();
    auto saveTime = lastTime;
    auto fpsTime = lastTime;
    float t = 0.0f;  // 累计时间，驱动所有动画
    int frames = 0;  // FPS 统计
    int saved = 0;   // 已保存的 PNG 帧数

    // 跳跃状态：按 SPACE 起跳，0.7 秒抛物线落地
    bool jumping = false;
    float jumpT = 0.0f;

    while (window.pumpMessages())
    {
        // ---- 时间步长（秒） ----
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        t += dt;
        if (Win32Window::keyDown(VK_ESCAPE))
            break;

        // ---- 跳跃输入 ----
        if (Win32Window::keyDown(VK_SPACE) && !jumping)
        {
            jumping = true;
            jumpT = 0.0f;
        }
        if (jumping)
        {
            jumpT += dt;
            if (jumpT > 0.7f)
                jumping = false;
        }
        int jumpDy = jumping ? int(90.0f * std::sin(PI * jumpT / 0.7f)) : 0;

        // ---- 背景：天空（颜色随太阳高度变化） ----
        // 太阳高度：0~1 之间正弦起伏，周期 18 秒
        float sunPhase = 0.5f - 0.5f * std::cos(2.0f * PI * t / DAY);
        int sunY = int(430.0f - 320.0f * sunPhase);
        float dayK = std::clamp(sunPhase * 1.6f, 0.0f, 1.0f);  // 1=白天黄昏, 0=夜晚
        const Color skyDay(255, 150, 90), skyNight(24, 30, 64);
        for (int y = 0; y < H; ++y)
        {
            float k = float(y) / float(H - 1);
            Color base = Color::lerp(skyDay, skyNight, 1.0f - dayK);  // 高度比例调亮度
            Color c = Color::lerp(base, skyNight, k);
            sr::draw2d::drawLine(fb, 0, y, W - 1, y, c);
        }

        // ---- 太阳：高于地平线才画，颜色随高度变暗 ----
        if (sunY > 120)
        {
            Color sunCore = Color::lerp(Color(255, 214, 96), Color(255, 120, 60), 1.0f - dayK);
            sr::draw2d::fillCircle(fb, 620, float(sunY), 58, Color(250, 150, 70));  // 外圈光晕
            sr::draw2d::fillCircle(fb, 620, float(sunY), 34, sunCore);              // 亮核
        }

        // ---- 云：两朵速度不同，从左飘到右再绕回 ----
        const Color cloud = Color::lerp(Color(250, 190, 150), Color(60, 66, 96), 1.0f - dayK);
        int cloudX1 = int(120 + 20.0f * t) % (W + 300) - 150;
        sr::draw2d::fillRect(fb, cloudX1, 450, 90, 14, cloud);
        sr::draw2d::fillRect(fb, cloudX1 + 30, 436, 56, 14, cloud);
        sr::draw2d::fillRect(fb, cloudX1 + 20, 464, 60, 12, cloud);
        int cloudX2 = int(480 + 12.0f * t) % (W + 300) - 150;
        sr::draw2d::fillRect(fb, cloudX2, 500, 70, 12, cloud);
        sr::draw2d::fillRect(fb, cloudX2 + 20, 490, 44, 10, cloud);

        // ---- 地面 + 地平线 + 小草（草丛 3 帧/秒明暗交替） ----
        for (int y = 0; y < 100; ++y)
            sr::draw2d::drawLine(fb, 0, y, W - 1, y, Color(38, 28, 42));
        sr::draw2d::drawLine(fb, 0, 100, W - 1, 100, Color::lerp(Color(255, 170, 90), Color(80, 70, 90), 1.0f - dayK));
        const int grass[][2] = {{60, 97}, {150, 98}, {260, 96}, {330, 99}, {420, 97}, {560, 98}, {700, 96}};
        bool grassBright = (int(t * 3.0f) % 2) == 0;
        for (const auto& g : grass)
        {
            Color gc = grassBright ? Color(110, 130, 70) : Color(70, 90, 50);
            fb.setPixel(g[0], g[1], gc);
            fb.setPixel(g[0] + 1, g[1] + 1, gc);
        }

        // ---- 勇者：9 秒一个来回，3 帧/秒走路 ----
        float walkCycle = 2.0f * PI * t / 9.0f;
        int heroX = int(70 + (W - heroW - 140) * (0.5f - 0.5f * std::cos(walkCycle)));
        int heroY = 100 + jumpDy;       // 脚踩地面，跳跃时抬高
        int frame = int(t * 3.0f) % 2;  // 走路帧 0/1 交替

        // 影子：跳跃时缩小变淡（高度越高影子越小）
        float shadowScale = 1.0f - float(jumpDy) / 140.0f;
        int shW = int((heroW - 40) * shadowScale);
        if (shW > 0)
            sr::draw2d::fillRect(fb, heroX + (heroW - shW) / 2, 94, shW, 6, Color(20, 14, 20));

        drawCharSprite(fb, kHeroFrames[frame], palette, heroX, heroY, scale);

        // ---- 显示 ----
        window.present(fb);

        // ---- 保存动画帧：每 0.5 秒一张 PNG，最多 40 张 ----
        if (std::chrono::duration<float>(now - saveTime).count() >= 0.5f && saved < 40)
        {
            saveTime = now;
            char path[64];
            std::snprintf(path, sizeof(path), "output/anim/frame_%03d.png", saved);
            sr::io::writeImage(path, fb);
            std::printf("saved %s\n", path);
            saved++;
        }

        // ---- 标题栏 FPS ----
        frames++;
        if (std::chrono::duration<float>(now - fpsTime).count() >= 0.5f)
        {
            float fps = frames / std::chrono::duration<float>(now - fpsTime).count();
            char title[128];
            std::snprintf(title, sizeof(title), "Pixel Hero | %.0f FPS | SPACE=Jump ESC=Exit | saved %d", fps, saved);
            window.setTitle(title);
            frames = 0;
            fpsTime = now;
        }
    }
    std::printf("共保存 %d 帧到 output/anim/，可用工具合成 GIF\n", saved);
    return 0;
}
