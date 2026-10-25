// ============================================================
// draw2d 模块单元测试：2D 图元、位图合成与 Renderer2D 状态栈
//
// 所有像素写入都必须经过 draw2d::plotBlend，scissor 与混合才生效。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

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

// 字体无关的不变量：scale=2 必须恰好等于把每个字模点放大成 2×2，
// 即亮像素数刚好是 scale=1 的 4 倍。轴对齐快速路径写错一个方向就会破坏它。
TEST(Renderer2D, TextScaleAmplifiesGlyphPixels)
{
    auto countLit = [](const Framebuffer& fb) {
        int lit = 0;
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x)
                if (fb.getPixel(x, y).r > 0)
                    ++lit;
        return lit;
    };

    Framebuffer small(64, 64, sr::colors::black);
    Framebuffer large(64, 64, sr::colors::black);
    Renderer2D a;
    a.begin(small);
    a.setBlend(BlendMode::Opaque);
    a.setColor(sr::colors::white);
    a.text(4.0f, 4.0f, "A", 1.0f);
    a.end();

    Renderer2D b;
    b.begin(large);
    b.setBlend(BlendMode::Opaque);
    b.setColor(sr::colors::white);
    b.text(4.0f, 4.0f, "A", 2.0f);
    b.end();

    const int smallLit = countLit(small);
    ASSERT_GT(smallLit, 0);
    EXPECT_EQ(countLit(large), smallLit * 4);
}

// 非有限的坐标/半径不得进入 int(floor(...))，也不得画出任何像素。
TEST(Shape2D, NonFiniteParametersAreIgnored)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Framebuffer fb(32, 32, sr::colors::black);

    sr::draw2d::fillCircle(fb, nan, 16.0f, 8.0f, sr::colors::white);
    sr::draw2d::fillCircle(fb, 16.0f, 16.0f, nan, sr::colors::white);
    sr::draw2d::fillEllipse(fb, inf, 16.0f, 6.0f, 4.0f, sr::colors::white);
    sr::draw2d::fillRoundedRect(fb, 4.0f, 4.0f, nan, 12.0f, 3.0f, sr::colors::white);
    sr::draw2d::drawRoundedRect(fb, 4.0f, 4.0f, 12.0f, inf, 3.0f, sr::colors::white);
    sr::draw2d::drawEllipse(fb, 16.0f, 16.0f, 1e30f, nan, sr::colors::white);

    int painted = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (fb.getPixel(x, y).r != 0)
                ++painted;
    EXPECT_EQ(painted, 0);
}

// 完全透明的 AA 线段不得画出任何像素（旧实现未把源 alpha 乘进边缘权重）。
TEST(Shape2D, AntiAliasedLineRespectsSourceAlpha)
{
    Framebuffer fb(32, 32, sr::colors::black);
    sr::draw2d::drawLineAA(fb, 0, 0, 31, 13, Color(255, 255, 255, 0));

    int lit = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (fb.getPixel(x, y).r != 0 || fb.getPixel(x, y).g != 0 || fb.getPixel(x, y).b != 0)
                ++lit;
    EXPECT_EQ(lit, 0) << "完全透明的线不应画出任何像素";
}

// 旋转态下的贴图与渐变不得静默丢弃（旧实现 texture 直接 return、渐变退化纯色）。
TEST(Blit2D, RotatedTextureAndGradientAreDrawn)
{
    sr::Image img;
    img.width = 2;
    img.height = 2;
    img.pixels.assign(4, Color(255, 0, 0, 255));
    const Texture tex(img);

    Framebuffer fb(64, 64, Color(0, 0, 0));
    const Mat3 xf = Mat3::translate(32.0f, 32.0f) * Mat3::rotate(0.6f) * Mat3::scale(40.0f, 40.0f) *
                    Mat3::translate(-0.5f, -0.5f);
    sr::draw2d::blitTextureTransformed(fb, tex, xf);
    int lit = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (fb.getPixel(x, y).r > 0)
                ++lit;
    EXPECT_GT(lit, 800) << "旋转贴图未绘制";

    // Renderer2D 层：旋转后渐变必须仍有两种颜色，而不是纯色
    Framebuffer fb2(64, 64, Color(0, 0, 0));
    Renderer2D r2d;
    r2d.begin(fb2);
    r2d.translate(32.0f, 32.0f);
    r2d.rotate(0.5f);
    r2d.rectGradient(-20.0f, -20.0f, 40.0f, 40.0f, Color(0, 0, 0), Color(255, 255, 255), false);
    r2d.end();
    int minV = 255, maxV = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            const int v = fb2.getPixel(x, y).r;
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
    EXPECT_LT(minV, 40) << "渐变暗端缺失";
    EXPECT_GT(maxV, 200) << "渐变亮端缺失（旧实现会退化成纯色）";
}

// 目标矩形远超屏幕时必须先裁剪：既不能漏画可见部分，也不能重复/越界写入。
TEST(Blit2D, BlitTextureClipsToTarget)
{
    sr::Image img;
    img.width = 2;
    img.height = 2;
    img.pixels.assign(4, Color(10, 20, 30, 255));
    const Texture tex(img);

    Framebuffer fb(4, 4, Color(0, 0, 0));
    sr::draw2d::blitTexture(fb, tex, -100, -100, 1000, 1000);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            EXPECT_EQ(fb.getPixel(x, y).r, 10) << x << "," << y;

    // 完全在屏外的贴图不得改动任何像素
    Framebuffer untouched(4, 4, Color(7, 7, 7));
    sr::draw2d::blitTexture(untouched, tex, 1000, 1000, 8, 8);
    EXPECT_EQ(untouched.getPixel(0, 0).r, 7);
    EXPECT_EQ(untouched.getPixel(3, 3).r, 7);

    // 部分重叠：只画重叠区
    Framebuffer partial(4, 4, Color(0, 0, 0));
    sr::draw2d::blitTexture(partial, tex, 2, 2, 8, 8);
    EXPECT_EQ(partial.getPixel(2, 2).r, 10);
    EXPECT_EQ(partial.getPixel(3, 3).r, 10);
    EXPECT_EQ(partial.getPixel(1, 1).r, 0);
}

// 纹理贴图必须保留自身 alpha 参与混合（旧实现走 Vec3f 采样后当成不透明）。
TEST(Blit2D, BlitTexturePreservesAlpha)
{
    sr::Image img;
    img.width = 2;
    img.height = 2;
    img.pixels.assign(4, Color(255, 0, 0, 128));
    const Texture tex(img);

    Framebuffer fb(4, 4, Color(0, 0, 255, 255));
    sr::draw2d::blitTexture(fb, tex, 0, 0, 4, 4, sr::colors::white, BlendMode::AlphaBlend);

    const Color c = fb.getPixel(1, 1);
    EXPECT_GT(c.r, 0) << "前景红未混入";
    EXPECT_GT(c.b, 0) << "背景蓝被完全覆盖，说明纹理 alpha 丢失";
}

// 2D 合成必须用 ClampToEdge：2×2 四色图放大到 8×8 时，
// Repeat 会把对边颜色带进边缘列（图集直接串色），四角应保持各自的纯色。
TEST(Blit2D, EdgeSamplingDoesNotWrapToOppositeSide)
{
    sr::Image img;
    img.width = 2;
    img.height = 2;
    // 行 0：左红、右绿；行 1：左蓝、右白
    img.pixels = {Color(255, 0, 0), Color(0, 255, 0), Color(0, 0, 255), Color(255, 255, 255)};
    const Texture tex(img);

    Framebuffer fb(8, 8, Color(0, 0, 0));
    sr::draw2d::blitTexture(fb, tex, 0, 0, 8, 8);

    const Color leftBottom = fb.getPixel(0, 0);
    EXPECT_GT(leftBottom.r, 200) << "左下应为红";
    EXPECT_LT(leftBottom.g, 60) << "左下混入了右侧绿（Repeat 绕回）";

    const Color rightBottom = fb.getPixel(7, 0);
    EXPECT_GT(rightBottom.g, 200) << "右下应为绿";
    EXPECT_LT(rightBottom.r, 60) << "右下混入了左侧红（Repeat 绕回）";

    const Color rightTop = fb.getPixel(7, 7);
    EXPECT_GT(rightTop.r, 200) << "右上应为白";
    EXPECT_GT(rightTop.g, 200);
    EXPECT_GT(rightTop.b, 200);
}

// drawRect 与 blitFramebuffer 此前零调用（全仓只有定义、示例与压测都没用过），
// 属于从未被执行过的公共 API。这里固定它们的输出行为。
TEST(Draw2D, DrawRectOutlinesWithoutFillingCenter)
{
    Framebuffer fb(16, 16, colors::black);
    draw2d::drawRect(fb, 2, 2, 12, 12, colors::white, 2);
    EXPECT_EQ(fb.getPixel(2, 2).r, 255);  // 左下角边框
    EXPECT_EQ(fb.getPixel(13, 2).r, 255); // 右下角边框
    EXPECT_EQ(fb.getPixel(8, 8).r, 0);    // 中心保持背景

    // 厚度超过边长时夹到 min(w,h)：整块填满而不是越界
    Framebuffer small(8, 8, colors::black);
    draw2d::drawRect(small, 0, 0, 8, 8, colors::white, 99);
    EXPECT_EQ(small.getPixel(4, 4).r, 255);
}

TEST(Draw2D, BlitFramebufferRespectsOffset)
{
    Framebuffer dst(8, 8, colors::black);
    Framebuffer src(4, 4, colors::white);
    draw2d::blitFramebuffer(dst, src, 2, 2);

    EXPECT_EQ(dst.getPixel(2, 2).r, 255); // 覆盖区左下
    EXPECT_EQ(dst.getPixel(5, 5).r, 255); // 覆盖区右上
    EXPECT_EQ(dst.getPixel(1, 1).r, 0);   // 偏移之外
    EXPECT_EQ(dst.getPixel(6, 6).r, 0);   // 4x4 只覆盖 [2,6)
}

// 缩放 blit 的源坐标必须落回图内，且象限对应不翻转（图像与帧缓冲都是 y 向上）。
TEST(Draw2D, BlitImageScaledMapsQuadrants)
{
    Image img;
    img.width = 2;
    img.height = 2;
    // 行优先、第 0 行在底部：(0,0)=左下，(1,0)=右下，(0,1)=左上，(1,1)=右上
    img.pixels = {colors::red, colors::green, colors::blue, colors::white};

    Framebuffer fb(8, 8, colors::black);
    draw2d::blitImageScaled(fb, img, 0, 0, 8, 8);

    EXPECT_EQ(fb.getPixel(1, 1).r, 255); // 左下 → 红
    EXPECT_EQ(fb.getPixel(1, 1).g, 0);
    EXPECT_EQ(fb.getPixel(6, 1).g, 255); // 右下 → 绿
    EXPECT_EQ(fb.getPixel(1, 6).b, 255); // 左上 → 蓝
}

// 三次贝塞尔必须命中两个端点（分段数不为 1 时容易只画到中间）。
TEST(Draw2D, BezierCubicHitsEndpoints)
{
    Framebuffer fb(32, 32, colors::black);
    draw2d::drawBezierCubic(fb, 2, 2, 10, 28, 20, 4, 30, 30, colors::white, 40);

    EXPECT_GT(fb.getPixel(2, 2).r, 0);   // 起点
    EXPECT_GT(fb.getPixel(30, 30).r, 0); // 终点
}

// 粗线的实际宽度必须等于请求的 thickness。旧实现用 -half..half 铺平行线，
// 偶数厚度会多出一条（thickness=4 画出 5 像素宽），与注释「thickness×thickness」不符。
TEST(Draw2D, LineThickMatchesRequestedWidth)
{
    for (int thickness = 2; thickness <= 5; ++thickness)
    {
        Framebuffer fb(32, 16, colors::black);
        draw2d::drawLineThick(fb, 4, 8, 20, 8, colors::white, thickness);

        int rows = 0;
        for (int y = 0; y < 16; ++y)
            if (fb.getPixel(10, y).r > 0)
                ++rows;
        EXPECT_EQ(rows, thickness) << "thickness=" << thickness;
    }
}

// dash + gap == 0 时 period 为 0，`step % period` 是整数除零。
// drawLineDashed 此前零调用，所以这个入口一次都没被验证过。
TEST(Draw2D, DashedLineHandlesZeroPeriod)
{
    Framebuffer fb(16, 16, colors::black);
    draw2d::drawLineDashed(fb, 0, 8, 15, 8, colors::white, 0, 0);

    int lit = 0;
    for (int x = 0; x < 16; ++x)
        if (fb.getPixel(x, 8).r > 0)
            ++lit;
    EXPECT_GT(lit, 0); // 退化为实线，而不是什么都不画
}

// 负 gap 会被夹到 0：此时 dash=2、period=2，`step % 2` 恒小于 dash，
// 结果退化为实线（与 dash=2、gap=0 等价），而不是周期为负数。
TEST(Draw2D, DashedLineTreatsNegativeGapAsZero)
{
    Framebuffer fb(16, 16, colors::black);
    draw2d::drawLineDashed(fb, 0, 8, 15, 8, colors::white, 2, -5);

    int lit = 0;
    for (int x = 0; x < 16; ++x)
        if (fb.getPixel(x, 8).r > 0)
            ++lit;
    EXPECT_EQ(lit, 16);
}

