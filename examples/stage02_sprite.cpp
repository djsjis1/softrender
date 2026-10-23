// ============================================================
// 阶段 02：字符模板精灵 — 像素画放大渲染
//
// 【源码阅读】先读这些文件：
//   core/framebuffer.h  — 帧缓冲坐标约定（y 向上）
//   core/color.h        — Color::lerp 逐行渐变
//   raster/shape2d.h    — sr::draw2d::drawLine 画轮廓
//
// 【运行验证】生成 output/stage02_sprite.png：
//   黄昏天空渐变 + 像素勇者（16x20 字符画，12 倍放大）+ 地面影子
//
// 注意：字符画从上往下读，Framebuffer y 向上，渲染 sprite 时做了 y 翻转。
// ============================================================
#include <cstdio>
#include <string>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/image_write.h"
#include "softrenderer/render/draw2d/shape2d.h"

using namespace sr;

// 字符 → 颜色 映射（'.' 表示透明像素）
struct SpritePalette
{
    char ch;
    Color color;
};

// 勇者 16x20 字符画（每行严格 16 个字符，方便用肉眼对齐）
// 含义：H=金发 S=皮肤 E=眼睛 C=红斗篷 A=铠甲 P=裤子 B=靴子 M=剑柄 I=剑刃
const std::vector<std::string> kHero = {
    ".....HHHHHH.....",  //  头顶
    "....HHHHHHHH....",  //
    "..C.HHHHHHHH....",  //  斗篷从左侧飘出
    ".CC.HSSSSSSH....",  //
    ".CC.HSSSSSSH....",  //
    ".CC.HSSSESSH....",  //  眼睛
    ".CC.HSSSSSSH....",  //
    "..C.HSSSSSSH....",  //
    ".....HSSSH.....",   //  下巴
    "..AAAAAAAAAA....",  //  铠甲（肩部渐宽）
    ".AAAAAAAAAAA.M..",  //  剑柄
    ".AAAAAAAAAAA.M..",  //
    ".AAAAAAAAAAAAM..",  //
    "..AAAAAAAAAA.M..",  //
    "..PP....PP...I..",  //  腿 + 剑刃
    "..PP....PP...I..",  //
    "..PP....PP...I..",  //
    "..BB....BB...I..",  //  靴子
    "..BB....BB...I..",  //
    "..BB....BB......",  //
};

// 把一个 sprite 画到 fb 上：左下角 (ox,oy)，每个像素放大 scale×scale 块
void drawSprite(Framebuffer& fb, const std::vector<std::string>& sprite, const std::vector<SpritePalette>& palette,
                int ox, int oy, int scale)
{
    const int w = int(sprite[0].size());
    const int h = int(sprite.size());
    for (int row = 0; row < h; ++row)
    {
        for (int col = 0; col < w; ++col)
        {
            char ch = sprite[row][col];
            if (ch == '.')
                continue;  // 透明像素跳过

            // 查调色板；查不到就给品红色，方便发现字符拼写错误
            Color c = sr::colors::magenta;
            for (const auto& p : palette)
                if (p.ch == ch)
                {
                    c = p.color;
                    break;
                }

            // y 翻转：sprite 第 0 行在最顶部，而画布 y 向上，
            // 所以顶部行要画在 oy + (h-1-row)*scale 处
            int px = ox + col * scale;
            int py = oy + (h - 1 - row) * scale;
            for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx)
                    fb.setPixel(px + dx, py + dy, c);
        }
    }
}

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H);

    // ---------- 1. 黄昏天空：每行一条水平线，颜色从地平线渐变到顶部 ----------
    // y=0 是地面（底部），t 从 0→1 对应 黄昏橙 → 深蓝紫
    const Color skyBottom(255, 150, 90), skyTop(40, 58, 130);
    for (int y = 0; y < H; ++y)
    {
        float t = float(y) / float(H - 1);
        sr::draw2d::drawLine(fb, 0, y, W - 1, y, Color::lerp(skyBottom, skyTop, t));
    }

    // ---------- 2. 像素方块太阳：两圈"光晕"，用勾股距离判断圆形范围 ----------
    {
        const int sx = 620, sy = 410;
        for (int dy = -58; dy <= 58; ++dy)
            for (int dx = -58; dx <= 58; ++dx)
                if (dx * dx + dy * dy <= 58 * 58)
                    fb.setPixel(sx + dx, sy + dy, Color(250, 150, 70));  // 外圈
        for (int dy = -34; dy <= 34; ++dy)
            for (int dx = -34; dx <= 34; ++dx)
                if (dx * dx + dy * dy <= 34 * 34)
                    fb.setPixel(sx + dx, sy + dy, Color(255, 214, 96));  // 亮核
    }

    // ---------- 3. 两朵被夕阳染红的云 ----------
    {
        const Color cloud(250, 190, 150);
        sr::draw2d::fillRect(fb, 120, 450, 90, 14, cloud);  // 云 1：上中下三块拼
        sr::draw2d::fillRect(fb, 150, 436, 56, 14, cloud);
        sr::draw2d::fillRect(fb, 140, 464, 60, 12, cloud);
        sr::draw2d::fillRect(fb, 480, 500, 70, 12, cloud);  // 云 2：小一点
        sr::draw2d::fillRect(fb, 500, 490, 44, 10, cloud);
    }

    // ---------- 4. 地面 + 地平线 + 小草 ----------
    for (int y = 0; y < 100; ++y)
        sr::draw2d::drawLine(fb, 0, y, W - 1, y, Color(38, 28, 42));    // 暗色大地
    sr::draw2d::drawLine(fb, 0, 100, W - 1, 100, Color(255, 170, 90));  // 被夕阳照亮的地平线
    const int grass[][2] = {{60, 97}, {150, 98}, {260, 96}, {330, 99}, {420, 97}, {560, 98}, {700, 96}};  // 几丛小草
    for (const auto& g : grass)
    {
        fb.setPixel(g[0], g[1], Color(110, 130, 70));
        fb.setPixel(g[0] + 1, g[1] + 1, Color(110, 130, 70));
    }

    // ---------- 5. 勇者：水平居中，脚踩地面 ----------
    const int scale = 12;
    const int heroW = int(kHero[0].size()) * scale;
    const int hx = (W - heroW) / 2;  // 水平居中
    const int hy = 100;              // 脚底贴在地面上

    // 影子：脚下一层暗色（没有 alpha 混合，直接画深色块）
    sr::draw2d::fillRect(fb, hx + 20, hy - 6, heroW - 40, 6, Color(20, 14, 20));

    std::vector<SpritePalette> palette = {
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
    drawSprite(fb, kHero, palette, hx, hy, scale);

    // ---------- 6. 导出 PNG ----------
    const std::string path = "output/stage02_sprite.png";
    if (!sr::io::writeImage(path, fb))
    {
        std::printf("写文件失败: %s\n", path.c_str());
        return 1;
    }
    std::printf("已生成 %s (%dx%d)\n", path.c_str(), W, H);
    return 0;
}
