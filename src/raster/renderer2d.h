#pragma once
#include <string>
#include <vector>

#include "core/blend.h"
#include "core/framebuffer.h"
#include "core/tga.h"
#include "math/mat3.h"
#include "math/rect.h"
#include "raster/blit2d.h"
#include "raster/shape2d.h"
#include "scene/camera.h"
#include "scene/texture.h"

// ============================================================
// Renderer2D：与 3D Renderer 对等的 2D 渲染器（立即模式）。
//
// 3D 的 Renderer 围绕“三角形 + 深度 + 着色器”组织；Renderer2D 围绕
// “2D 变换栈 + 混合状态 + 裁剪区”组织，直接把图元/位图/文字画到屏幕。
// 二者共用同一个 Framebuffer，可自由叠加（先画 3D 场景，再画 2D HUD）。
//
// 【立即模式用法】
//   Renderer2D r2d;
//   r2d.begin(fb, Color(20,20,30));   // 绑定目标 + 清屏 + 复位状态
//   r2d.setColor(colors::white);
//   r2d.fillRect(10, 10, 200, 40);    // 用户空间坐标
//   r2d.push(); r2d.translate(300,200); r2d.rotate(0.5f);
//   r2d.fillRect(-40,-40,80,80);      // 在变换后的局部坐标系里画
//   r2d.pop();
//   r2d.text(20, 60, "SCORE 1234", 2);
//   r2d.end();
//
// 【变换栈】push/pop 保存并恢复 颜色/混合/变换/裁剪，语义类似 Canvas 的
// save()/restore()。translate/rotate/scale 都是在当前局部坐标系上“后乘”，
// 与 Canvas 2D 一致：先 translate 再 rotate，是绕新原点旋转。
//
// 【坐标】默认用户空间 = 设备像素（y 向上，原点左下）。可用 setCamera
// 挂一个 Camera2D，让用户空间变成“世界坐标”（可缩放/平移/翻转 y）。
// ============================================================

// ------------------------------------------------------------
// 内置 5x7 位图字体（ASCII）。每个字形 7 行 × 5 列，'X'=亮 '.'=灭。
// 行 0 在字形顶部。小写自动映射到大写。够用于 HUD/分数/标签。
// ------------------------------------------------------------
inline const char* const* glyph5x7(char c)
{
    struct G
    {
        char ch;
        const char* rows[7];
    };
    static const G table[] = {
        {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
        {'!', {"..X..", "..X..", "..X..", "..X..", "..X..", ".....", "..X.."}},
        {'"', {".X.X.", ".X.X.", ".....", ".....", ".....", ".....", "....."}},
        {'#', {".X.X.", ".X.X.", "XXXXX", ".X.X.", "XXXXX", ".X.X.", ".X.X."}},
        {'$', {"..X..", ".XXXX", "X.X..", ".XXX.", "..X.X", "XXXX.", "..X.."}},
        {'%', {"XX...", "XX..X", "...X.", "..X..", ".X...", "X..XX", "X..XX"}},
        {'&', {".XX..", "X..X.", "X..X.", ".XX..", "X..X.", "X..X.", ".XXX."}},
        {'\'', {"..X..", "..X..", ".....", ".....", ".....", ".....", "....."}},
        {'(', {"...X.", "..X..", ".X...", ".X...", ".X...", "..X..", "...X."}},
        {')', {".X...", "..X..", "...X.", "...X.", "...X.", "..X..", ".X..."}},
        {'*', {".....", "X...X", ".X.X.", "..X..", ".X.X.", "X...X", "....."}},
        {'+', {".....", "..X..", "..X..", "XXXXX", "..X..", "..X..", "....."}},
        {',', {".....", ".....", ".....", ".....", "..XX.", "..XX.", ".XX.."}},
        {'-', {".....", ".....", ".....", ".XXX.", ".....", ".....", "....."}},
        {'.', {".....", ".....", ".....", ".....", ".....", "..XX.", "..XX."}},
        {'/', {"....X", "....X", "...X.", "..X..", ".X...", "X....", "X...."}},
        {'0', {".XXX.", "X...X", "X..XX", "X.X.X", "XX..X", "X...X", ".XXX."}},
        {'1', {"..X..", ".XX..", "..X..", "..X..", "..X..", "..X..", ".XXX."}},
        {'2', {".XXX.", "X...X", "....X", "...X.", "..X..", ".X...", "XXXXX"}},
        {'3', {".XXX.", "X...X", "....X", "..XX.", "....X", "X...X", ".XXX."}},
        {'4', {"...X.", "..XX.", ".X.X.", "X..X.", "XXXXX", "...X.", "...X."}},
        {'5', {"XXXXX", "X....", "XXXX.", "....X", "....X", "X...X", ".XXX."}},
        {'6', {"..XX.", ".X...", "X....", "XXXX.", "X...X", "X...X", ".XXX."}},
        {'7', {"XXXXX", "....X", "...X.", "..X..", ".X...", ".X...", ".X..."}},
        {'8', {".XXX.", "X...X", "X...X", ".XXX.", "X...X", "X...X", ".XXX."}},
        {'9', {".XXX.", "X...X", "X...X", ".XXXX", "....X", "...X.", ".XX.."}},
        {':', {".....", "..XX.", "..XX.", ".....", "..XX.", "..XX.", "....."}},
        {';', {".....", "..XX.", "..XX.", ".....", "..XX.", "..XX.", ".X..."}},
        {'<', {"....X", "...X.", "..X..", ".X...", "..X..", "...X.", "....X"}},
        {'=', {".....", ".....", "XXXXX", ".....", "XXXXX", ".....", "....."}},
        {'>', {"X....", ".X...", "..X..", "...X.", "..X..", ".X...", "X...."}},
        {'?', {".XXX.", "X...X", "....X", "...X.", "..X..", ".....", "..X.."}},
        {'@', {".XXX.", "X...X", "X.XXX", "X.X.X", "X.XXX", "X....", ".XXX."}},
        {'A', {".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"}},
        {'B', {"XXXX.", "X...X", "X...X", "XXXX.", "X...X", "X...X", "XXXX."}},
        {'C', {".XXX.", "X...X", "X....", "X....", "X....", "X...X", ".XXX."}},
        {'D', {"XXXX.", "X...X", "X...X", "X...X", "X...X", "X...X", "XXXX."}},
        {'E', {"XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX"}},
        {'F', {"XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "X...."}},
        {'G', {".XXX.", "X...X", "X....", "X..XX", "X...X", "X...X", ".XXX."}},
        {'H', {"X...X", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"}},
        {'I', {".XXX.", "..X..", "..X..", "..X..", "..X..", "..X..", ".XXX."}},
        {'J', {"..XXX", "...X.", "...X.", "...X.", "...X.", "X..X.", ".XX.."}},
        {'K', {"X...X", "X..X.", "X.X..", "XX...", "X.X..", "X..X.", "X...X"}},
        {'L', {"X....", "X....", "X....", "X....", "X....", "X....", "XXXXX"}},
        {'M', {"X...X", "XX.XX", "X.X.X", "X.X.X", "X...X", "X...X", "X...X"}},
        {'N', {"X...X", "XX..X", "X.X.X", "X..XX", "X...X", "X...X", "X...X"}},
        {'O', {".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."}},
        {'P', {"XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...."}},
        {'Q', {".XXX.", "X...X", "X...X", "X...X", "X.X.X", "X..X.", ".XX.X"}},
        {'R', {"XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"}},
        {'S', {".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."}},
        {'T', {"XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."}},
        {'U', {"X...X", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."}},
        {'V', {"X...X", "X...X", "X...X", "X...X", "X...X", ".X.X.", "..X.."}},
        {'W', {"X...X", "X...X", "X...X", "X.X.X", "X.X.X", "XX.XX", "X...X"}},
        {'X', {"X...X", "X...X", ".X.X.", "..X..", ".X.X.", "X...X", "X...X"}},
        {'Y', {"X...X", "X...X", ".X.X.", "..X..", "..X..", "..X..", "..X.."}},
        {'Z', {"XXXXX", "....X", "...X.", "..X..", ".X...", "X....", "XXXXX"}},
        {'[', {".XXX.", ".X...", ".X...", ".X...", ".X...", ".X...", ".XXX."}},
        {'\\', {"X....", "X....", ".X...", "..X..", "...X.", "....X", "....X"}},
        {']', {".XXX.", "...X.", "...X.", "...X.", "...X.", "...X.", ".XXX."}},
        {'^', {"..X..", ".X.X.", "X...X", ".....", ".....", ".....", "....."}},
        {'_', {".....", ".....", ".....", ".....", ".....", ".....", "XXXXX"}},
        {'`', {".X...", "..X..", ".....", ".....", ".....", ".....", "....."}},
        {'{', {"...X.", "..X..", "..X..", ".X...", "..X..", "..X..", "...X."}},
        {'|', {"..X..", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."}},
        {'}', {".X...", "..X..", "..X..", "...X.", "..X..", "..X..", ".X..."}},
        {'~', {".....", ".....", ".X...", "X.X.X", "...X.", ".....", "....."}},
    };
    if (c >= 'a' && c <= 'z')
        c = char(c - 'a' + 'A'); // 小写→大写
    for (const auto& g : table)
        if (g.ch == c)
            return g.rows;
    return nullptr;
}

class Renderer2D
{
   public:
    Renderer2D() = default;

    // ---------------- 目标与帧 ----------------
    void setTarget(Framebuffer& fb) { fb_ = &fb; }

    // 绑定目标并复位变换/裁剪（不清屏）
    void begin(Framebuffer& fb)
    {
        fb_ = &fb;
        resetTransform();
        clearScissor();
    }
    // 绑定目标 + 复位 + 清屏
    void begin(Framebuffer& fb, Color bg)
    {
        begin(fb);
        fb.clear(bg);
    }
    void end() { fb_ = nullptr; }

    void clear(Color bg)
    {
        if (fb_)
            fb_->clear(bg);
    }

    // ---------------- 绘制状态 ----------------
    Color color = colors::white;             // 图元填充/描边色，也作为位图 tint
    BlendMode blend = BlendMode::AlphaBlend; // 2D 默认半透明叠加
    bool bilinear = false;                   // 位图缩放/旋转时是否双线性采样

    void setColor(Color c) { color = c; }
    void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { color = Color(r, g, b, a); }
    void setBlend(BlendMode m) { blend = m; }

    // ---------------- 变换栈（类似 Canvas save/restore）----------------
    void push() { stack_.push_back({color, blend, cur_, scissor_, hasScissor_}); }
    void pop()
    {
        if (stack_.empty())
            return;
        const State& s = stack_.back();
        color = s.color;
        blend = s.blend;
        cur_ = s.cur;
        scissor_ = s.scissor;
        hasScissor_ = s.hasScissor;
        stack_.pop_back();
    }

    void translate(float x, float y) { cur_ = cur_ * Mat3::translate(x, y); }
    void translate(const Vec2f& t) { translate(t.x, t.y); }
    void rotate(float rad) { cur_ = cur_ * Mat3::rotate(rad); }
    void scale(float sx, float sy) { cur_ = cur_ * Mat3::scale(sx, sy); }
    void scale(float s) { cur_ = cur_ * Mat3::scale(s, s); }
    void multiply(const Mat3& m) { cur_ = cur_ * m; }
    void setTransform(const Mat3& m) { cur_ = m; }
    void resetTransform() { cur_ = Mat3::identity(); }
    Mat3 transform() const { return cur_; }

    // 用户空间 → 设备像素的完整矩阵（相机基变换 * 当前局部变换）
    Mat3 deviceTransform() const { return base_ * cur_; }

    // ---------------- 相机 ----------------
    void setCamera(const Camera2D& cam) { base_ = cam.viewProj(); }
    void clearCamera() { base_ = Mat3::identity(); }

    // ---------------- 裁剪区（设备像素）----------------
    void setScissor(int x, int y, int w, int h)
    {
        scissor_ = draw2d::ClipRect(x, y, x + w, y + h);
        hasScissor_ = true;
    }
    void setScissor(const Recti& r) { setScissor(r.x, r.y, r.w, r.h); }
    void clearScissor() { hasScissor_ = false; }

    // ---------------- 矢量图元（用户空间坐标）----------------
    void point(float x, float y, float size = 1.0f)
    {
        if (!fb_)
            return;
        Vec2f p = mapPoint(x, y);
        int s = std::max(1, (int)std::round(size * avgScale()));
        draw2d::drawPoint(*fb_, (int)std::round(p.x), (int)std::round(p.y), color, s, blend, clip());
    }

    void line(float x0, float y0, float x1, float y1, float thickness = 1.0f)
    {
        if (!fb_)
            return;
        Vec2f a = mapPoint(x0, y0), b = mapPoint(x1, y1);
        draw2d::drawThickLine(*fb_, a.x, a.y, b.x, b.y, thickness * avgScale(), color, true, blend, clip());
    }

    void fillRect(float x, float y, float w, float h)
    {
        if (!fb_)
            return;
        fillUserRect(x, y, w, h);
    }

    void rect(float x, float y, float w, float h, float thickness = 1.0f)
    {
        if (!fb_)
            return;
        // 描边 = 四个薄矩形（在用户空间构造，随变换走）
        fillUserRect(x, y, w, thickness);
        fillUserRect(x, y + h - thickness, w, thickness);
        fillUserRect(x, y, thickness, h);
        fillUserRect(x + w - thickness, y, thickness, h);
    }

    void rectGradient(float x, float y, float w, float h, Color c0, Color c1, bool horizontal = false)
    {
        if (!fb_ || !axisAligned())
        {
            // 有旋转时退化为纯色（渐变方向在旋转下无良定义）
            fillRect(x, y, w, h);
            return;
        }
        Vec2f p0 = mapPoint(x, y), p1 = mapPoint(x + w, y + h);
        int X = (int)std::floor(std::min(p0.x, p1.x));
        int Y = (int)std::floor(std::min(p0.y, p1.y));
        int W = (int)std::ceil(std::max(p0.x, p1.x)) - X;
        int H = (int)std::ceil(std::max(p0.y, p1.y)) - Y;
        draw2d::fillRectGradient(*fb_, X, Y, W, H, c0, c1, horizontal, blend, clip());
    }

    void fillRoundedRect(float x, float y, float w, float h, float r)
    {
        if (!fb_)
            return;
        if (axisAligned())
        {
            Vec2f p0 = mapPoint(x, y);
            float s = avgScale();
            shape2dRoundRectFill(p0.x, p0.y, w * s, h * s, r * s);
        }
        else
        {
            fillRect(x, y, w, h); // 旋转下用普通矩形近似
        }
    }

    void roundedRect(float x, float y, float w, float h, float r, float thickness = 1.0f)
    {
        if (!fb_)
            return;
        if (axisAligned())
        {
            Vec2f p0 = mapPoint(x, y);
            float s = avgScale();
            draw2d::drawRoundedRect(*fb_, p0.x, p0.y, w * s, h * s, r * s, color,
                              std::max(1, (int)std::round(thickness * s)), blend, clip());
        }
        else
        {
            rect(x, y, w, h, thickness);
        }
    }

    void fillCircle(float cx, float cy, float r)
    {
        if (!fb_)
            return;
        Vec2f c = mapPoint(cx, cy);
        Mat3 d = deviceTransform();
        draw2d::fillEllipse(*fb_, c.x, c.y, r * d.scaleX(), r * d.scaleY(), color, blend, clip());
    }

    void circle(float cx, float cy, float r, float thickness = 1.0f)
    {
        if (!fb_)
            return;
        Vec2f c = mapPoint(cx, cy);
        float s = avgScale();
        draw2d::drawCircle(*fb_, (int)std::round(c.x), (int)std::round(c.y), (int)std::round(r * s), color,
                     std::max(1, (int)std::round(thickness * s)), blend, clip());
    }

    void fillEllipse(float cx, float cy, float rx, float ry)
    {
        if (!fb_)
            return;
        Vec2f c = mapPoint(cx, cy);
        Mat3 d = deviceTransform();
        draw2d::fillEllipse(*fb_, c.x, c.y, rx * d.scaleX(), ry * d.scaleY(), color, blend, clip());
    }

    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2)
    {
        if (!fb_)
            return;
        std::vector<Vec2f> tri = {mapPoint(x0, y0), mapPoint(x1, y1), mapPoint(x2, y2)};
        draw2d::fillPolygon(*fb_, tri, color, blend, clip());
    }

    void fillPolygon(const std::vector<Vec2f>& pts)
    {
        if (!fb_)
            return;
        std::vector<Vec2f> mapped;
        mapped.reserve(pts.size());
        for (const auto& p : pts)
            mapped.push_back(mapPoint(p.x, p.y));
        draw2d::fillPolygon(*fb_, mapped, color, blend, clip());
    }

    void polygon(const std::vector<Vec2f>& pts, bool closed = true, float thickness = 1.0f)
    {
        if (!fb_)
            return;
        std::vector<Vec2f> mapped;
        mapped.reserve(pts.size());
        for (const auto& p : pts)
            mapped.push_back(mapPoint(p.x, p.y));
        int t = std::max(1, (int)std::round(thickness * avgScale()));
        draw2d::drawPolygon(*fb_, mapped, color, closed, t, blend, clip());
    }

    // ---------------- 位图（用户空间定位，随变换缩放/旋转）----------------
    void image(const tga::Image& img, float x, float y)
    {
        if (!fb_)
            return;
        Mat3 xf = deviceTransform() * Mat3::translate(x, y);
        draw2d::blitImageTransformed(*fb_, img, xf, color, bilinear, blend, clip());
    }

    void image(const tga::Image& img, float x, float y, float dw, float dh)
    {
        if (!fb_ || !img.valid())
            return;
        Mat3 xf = deviceTransform() * Mat3::translate(x, y) *
                  Mat3::scale(dw / img.width, dh / img.height);
        draw2d::blitImageTransformed(*fb_, img, xf, color, bilinear, blend, clip());
    }

    void texture(const Texture& tex, float x, float y, float w, float h)
    {
        if (!fb_ || !axisAligned())
            return;
        Vec2f p0 = mapPoint(x, y), p1 = mapPoint(x + w, y + h);
        int X = (int)std::floor(std::min(p0.x, p1.x));
        int Y = (int)std::floor(std::min(p0.y, p1.y));
        int W = (int)std::ceil(std::max(p0.x, p1.x)) - X;
        int H = (int)std::ceil(std::max(p0.y, p1.y)) - Y;
        draw2d::blitTexture(*fb_, tex, X, Y, W, H, color, blend, clip());
    }

    // 字符模板精灵：用户空间左下角 (x,y)，每个字符像素放大 scale（用户单位）
    void sprite(const std::vector<std::string>& spr, const std::vector<draw2d::SpritePalette>& palette,
                float x, float y, float scale = 1.0f, char transparent = '.')
    {
        if (!fb_ || spr.empty())
            return;
        const int rows = (int)spr.size();
        const Mat3 d = deviceTransform(); // 一次绘制只算一次设备矩阵
        for (int r = 0; r < rows; ++r)
        {
            const std::string& line = spr[r];
            for (int c = 0; c < (int)line.size(); ++c)
            {
                char ch = line[c];
                if (ch == transparent)
                    continue;
                Color col = colors::magenta;
                for (const auto& p : palette)
                    if (p.ch == ch)
                    {
                        col = p.color;
                        break;
                    }
                Color saved = color;
                color = draw2d::tintPixel(saved, col); // 精灵色 * 当前 tint
                // 顶部行画在最高处（y 翻转）
                fillUserRectMat(d, x + c * scale, y + (rows - 1 - r) * scale, scale, scale);
                color = saved;
            }
        }
    }

    // ---------------- 文字（内置 5x7 字体）----------------
    // (x,y) 为文本块左下角；scale 为像素放大倍数（用户单位）。随变换缩放/平移。
    void text(float x, float y, const std::string& str, float scale = 1.0f)
    {
        if (!fb_ || scale <= 0.0f)
            return;
        const float advance = 6.0f * scale; // 5 列 + 1 列间距
        const Mat3 d = deviceTransform(); // 一次绘制只算一次设备矩阵
        float cx = x;
        for (char ch : str)
        {
            if (ch == ' ')
            {
                cx += advance;
                continue;
            }
            const char* const* rows = glyph5x7(ch);
            if (!rows)
            {
                cx += advance;
                continue;
            }
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 5; ++c)
                    if (rows[r][c] == 'X')
                        fillUserRectMat(d, cx + c * scale, y + (6 - r) * scale, scale, scale);
            cx += advance;
        }
    }

    // 文本尺寸（用户单位）：宽 = 字数*advance - 末间距，高 = 7*scale
    Vec2f measureText(const std::string& str, float scale = 1.0f) const
    {
        float advance = 6.0f * scale;
        float w = str.empty() ? 0.0f : advance * (float)str.size() - scale;
        return {w, 7.0f * scale};
    }

   private:
    struct State
    {
        Color color;
        BlendMode blend;
        Mat3 cur;
        draw2d::ClipRect scissor;
        bool hasScissor;
    };

    Framebuffer* fb_ = nullptr;
    Mat3 base_ = Mat3::identity();   // 相机基变换
    Mat3 cur_ = Mat3::identity();    // 当前局部变换
    std::vector<State> stack_;
    draw2d::ClipRect scissor_;
    bool hasScissor_ = false;

    const draw2d::ClipRect* clip() const { return hasScissor_ ? &scissor_ : nullptr; }
    Vec2f mapPoint(float x, float y) const { return deviceTransform().transformPoint({x, y}); }

    // 设备变换是否轴对齐（无旋转/错切）——决定用快速整数矩形还是通用多边形
    bool axisAligned() const
    {
        Mat3 d = deviceTransform();
        return std::abs(d.m[0][1]) < 1e-5f && std::abs(d.m[1][0]) < 1e-5f;
    }
    float avgScale() const
    {
        Mat3 d = deviceTransform();
        return 0.5f * (d.scaleX() + d.scaleY());
    }

    // 填充一个用户空间矩形：轴对齐走快速整数 fillRect，否则走通用四边形
    void fillUserRect(float x, float y, float w, float h) { fillUserRectMat(deviceTransform(), x, y, w, h); }

    // 复用已算好的设备矩阵 d：text/sprite 逐字形像素调用时避免重复矩阵乘
    void fillUserRectMat(const Mat3& d, float x, float y, float w, float h)
    {
        if (std::abs(d.m[0][1]) < 1e-5f && std::abs(d.m[1][0]) < 1e-5f)
        {
            Vec2f p0 = d.transformPoint({x, y});
            Vec2f p1 = d.transformPoint({x + w, y + h});
            int X = (int)std::floor(std::min(p0.x, p1.x));
            int Y = (int)std::floor(std::min(p0.y, p1.y));
            int X1 = (int)std::ceil(std::max(p0.x, p1.x));
            int Y1 = (int)std::ceil(std::max(p0.y, p1.y));
            draw2d::fillRect(*fb_, X, Y, X1 - X, Y1 - Y, color, blend, clip());
        }
        else
        {
            std::vector<Vec2f> quad = {d.transformPoint({x, y}), d.transformPoint({x + w, y}),
                                       d.transformPoint({x + w, y + h}), d.transformPoint({x, y + h})};
            draw2d::fillPolygon(*fb_, quad, color, blend, clip());
        }
    }

    void shape2dRoundRectFill(float dx, float dy, float dw, float dh, float dr)
    {
        draw2d::fillRoundedRect(*fb_, dx, dy, dw, dh, dr, color, blend, clip());
    }
};
