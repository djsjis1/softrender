// ============================================================
// draw2d 模块单元测试：2D 图元、位图合成与 Renderer2D 状态栈
//
// 所有像素写入都必须经过 draw2d::plotBlend，scissor 与混合才生效。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>

#include "softrenderer/core/color.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/image.h"
#include "softrenderer/math/mat3.h"
#include "softrenderer/render/draw2d/blit2d.h"
#include "softrenderer/render/draw2d/renderer2d.h"
#include "softrenderer/render/draw2d/shape2d.h"

using namespace sr;

TEST(Shape2D, FillRectCoversArea)
{
    Framebuffer fb(20, 20, sr::colors::black);
    sr::draw2d::fillRect(fb, 5, 5, 4, 4, sr::colors::red);
    EXPECT_EQ(fb.getPixel(5, 5).r, 255);
    EXPECT_EQ(fb.getPixel(8, 8).r, 255);
    EXPECT_EQ(fb.getPixel(9, 9).r, 0); // 越出矩形
    EXPECT_EQ(fb.getPixel(0, 0).r, 0);
}

TEST(Shape2D, FillCircleCenterAndOutside)
{
    Framebuffer fb(40, 40, sr::colors::black);
    sr::draw2d::fillCircle(fb, 20.0f, 20.0f, 5.0f, sr::colors::green);
    EXPECT_EQ(fb.getPixel(20, 20).g, 255);
    EXPECT_EQ(fb.getPixel(30, 30).g, 0);
}

TEST(Shape2D, ClipRestrictsDrawing)
{
    Framebuffer fb(40, 40, sr::colors::black);
    sr::draw2d::ClipRect clip(0, 0, 20, 40); // 只允许左半屏
    sr::draw2d::fillRect(fb, 10, 10, 20, 5, sr::colors::red, BlendMode::Opaque, &clip);
    EXPECT_EQ(fb.getPixel(15, 12).r, 255); // 裁剪区内
    EXPECT_EQ(fb.getPixel(25, 12).r, 0);   // 裁剪区外被丢弃
}

TEST(Shape2D, FillPolygonCoversInterior)
{
    Framebuffer fb(40, 40, sr::colors::black);
    std::vector<Vec2f> tri = {{5, 5}, {35, 5}, {20, 35}};
    sr::draw2d::fillPolygon(fb, tri, sr::colors::blue);
    EXPECT_EQ(fb.getPixel(20, 12).b, 255); // 三角形内部
    EXPECT_EQ(fb.getPixel(2, 38).b, 0);    // 外部
}

TEST(Shape2D, AlphaBlendMixes)
{
    Framebuffer fb(4, 4, Color(0, 0, 0));
    sr::draw2d::fillRect(fb, 0, 0, 4, 4, Color(200, 200, 200, 128), BlendMode::AlphaBlend);
    Color c = fb.getPixel(1, 1);
    EXPECT_GT(c.r, 60);
    EXPECT_LT(c.r, 160); // 约一半混合
}

// ==================== 2D 位图合成 ====================

TEST(Blit2D, BlitImageCopies)
{
    sr::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(16, Color(10, 200, 30));
    Framebuffer fb(10, 10, sr::colors::black);
    sr::draw2d::blitImage(fb, img, 3, 3);
    EXPECT_EQ(fb.getPixel(3, 3).g, 200);
    EXPECT_EQ(fb.getPixel(6, 6).g, 200);
    EXPECT_EQ(fb.getPixel(8, 8).g, 0);
}

TEST(Blit2D, TintScales)
{
    Color c = sr::draw2d::tintPixel(Color(200, 100, 50, 255), Color(255, 0, 0, 255));
    EXPECT_EQ(c.r, 200);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

// ==================== Renderer2D ====================

TEST(Renderer2D, FillRectThroughTransform)
{
    Framebuffer fb(60, 60, sr::colors::black);
    Renderer2D r;
    r.begin(fb);
    r.setBlend(BlendMode::Opaque);
    r.setColor(sr::colors::white);
    r.translate(20.0f, 10.0f);
    r.fillRect(0.0f, 0.0f, 10.0f, 10.0f);
    EXPECT_EQ(fb.getPixel(25, 15).r, 255); // 平移后落点
    EXPECT_EQ(fb.getPixel(5, 5).r, 0);
    r.end();
}

TEST(Renderer2D, ScissorClips)
{
    Framebuffer fb(60, 60, sr::colors::black);
    Renderer2D r;
    r.begin(fb);
    r.setBlend(BlendMode::Opaque);
    r.setColor(sr::colors::red);
    r.setScissor(Recti(0, 0, 30, 60));
    r.fillRect(20.0f, 20.0f, 30.0f, 10.0f);
    EXPECT_EQ(fb.getPixel(25, 25).r, 255); // 裁剪区内
    EXPECT_EQ(fb.getPixel(40, 25).r, 0);   // 裁剪区外
    r.end();
}

TEST(Renderer2D, PushPopRestoresColor)
{
    Framebuffer fb(4, 4);
    Renderer2D r;
    r.begin(fb);
    r.setColor(sr::colors::red);
    r.push();
    r.setColor(sr::colors::blue);
    EXPECT_EQ(r.color.b, 255);
    r.pop();
    EXPECT_EQ(r.color.r, 255); // 恢复
    EXPECT_EQ(r.color.b, 0);
    r.end();
}

TEST(Renderer2D, MeasureTextWidth)
{
    Renderer2D r;
    Vec2f m = r.measureText("ABC", 1.0f);
    EXPECT_NEAR(m.x, 6.0f * 3 - 1.0f, 1e-4f);
    EXPECT_NEAR(m.y, 7.0f, 1e-4f);
}

TEST(Renderer2D, TextDrawsPixels)
{
    Framebuffer fb(80, 20, sr::colors::black);
    Renderer2D r;
    r.begin(fb);
    r.setBlend(BlendMode::Opaque);
    r.setColor(sr::colors::white);
    r.text(2.0f, 4.0f, "A", 2.0f);
    int lit = 0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 80; ++x)
            if (fb.getPixel(x, y).r > 0)
                ++lit;
    EXPECT_GT(lit, 10);
    r.end();
}

