#include <cstdio>
#include "core/color.h"
#include "core/framebuffer.h"
#include "core/image_write.h"
#include "raster/shape2d.h"

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(30, 30, 40)); // 深色背景

    // ── 1. 渐变矩形（天空效果）──
    draw2d::fillRectGradient(fb, 0, 400, W, 200,
        Color(20, 20, 60), Color(60, 40, 80), false);

    // ── 2. 圆角矩形面板 ──
    draw2d::fillRoundedRect(fb, 50, 420, 300, 150, 20, Color(40, 40, 60));
    draw2d::drawRoundedRect(fb, 50, 420, 300, 150, 20, Color(100, 100, 140), 2);

    // ── 3. 实心圆 + 描边圆 ──
    draw2d::fillCircle(fb, 600.0f, 500.0f, 60.0f, Color(200, 60, 60));
    draw2d::drawCircle(fb, 600, 500, 80, Color(255, 100, 100), 2);

    // ── 4. 椭圆 ──
    draw2d::fillEllipse(fb, 200.0f, 250.0f, 80.0f, 40.0f, Color(60, 180, 100));
    draw2d::drawEllipse(fb, 200.0f, 250.0f, 100.0f, 50.0f, Color(100, 220, 140), 2);

    // ── 5. 三角形（多边形）──
    std::vector<Vec2f> tri = {{400, 350}, {500, 150}, {600, 350}};
    draw2d::fillPolygon(fb, tri, Color(220, 180, 40));
    draw2d::drawPolygon(fb, tri, Color(255, 220, 80), true, 2);

    // ── 6. 半透明叠加（AlphaBlend）──
    draw2d::fillCircle(fb, 350.0f, 300.0f, 80.0f,
        Color(255, 100, 100, 128), BlendMode::AlphaBlend);
    draw2d::fillCircle(fb, 420.0f, 300.0f, 80.0f,
        Color(100, 100, 255, 128), BlendMode::AlphaBlend);

    // ── 7. 加法混合（发光效果）──
    for (int i = 0; i < 5; ++i)
    {
        float cx = 650.0f + i * 20.0f;
        float cy = 150.0f;
        draw2d::fillCircle(fb, cx, cy, 40.0f - i * 5.0f,
            Color(255, 200, 50, 60), BlendMode::Additive);
    }

    // ── 8. 描边矩形 ──
    draw2d::drawRect(fb, 50, 50, 200, 100, Color(180, 180, 200), 3);

    // ── 9. 混合画线 ──
    draw2d::drawLine(fb, 0, 580, 800, 580, Color(255, 255, 255, 100),
        BlendMode::AlphaBlend);

    // ── 10. 散点 ──
    for (int i = 0; i < 20; ++i)
    {
        int px = 500 + i * 12;
        int py = 450 + (i % 3) * 15;
        draw2d::drawPoint(fb, px, py, Color(200, 200, 255), 3);
    }

    imagewriter::writeImage("output/hello_2d.png", fb);
    std::printf("output/hello_2d.png written (%dx%d)\n", W, H);

    return 0;
}
