// ============================================================
// 群星闪耀：用软渲染器生成一张星空图
//
// 效果：
//   - 深空背景（带微弱星云渐变）
//   - 数百颗随机星星（不同大小/亮度/色温）
//   - 几颗超亮星带十字衍射光芒
//   - Bloom 后处理让亮星产生光晕
//
// 输出：output/stars.tga
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <vector>

#include "core/color.h"
#include "core/float_framebuffer.h"
#include "core/framebuffer.h"
#include "core/tga.h"
#include "math/vec.h"

// 简单 2D 噪声（value noise）
static float hash21(float x, float y)
{
    float n = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return n - std::floor(n);
}

static float smoothNoise(float x, float y)
{
    int ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hash21(float(ix), float(iy));
    float b = hash21(float(ix + 1), float(iy));
    float c = hash21(float(ix), float(iy + 1));
    float d = hash21(float(ix + 1), float(iy + 1));
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float fbm(float x, float y, int octaves = 4)
{
    float val = 0, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i)
    {
        val += amp * smoothNoise(x * freq, y * freq);
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return val;
}

// 画一个发光点（高斯衰减）
static void drawStar(FloatFramebuffer& fb, float cx, float cy, float radius, Vec3f color, float intensity)
{
    int W = fb.width(), H = fb.height();
    int minX = std::max(0, (int)(cx - radius * 3));
    int maxX = std::min(W - 1, (int)(cx + radius * 3));
    int minY = std::max(0, (int)(cy - radius * 3));
    int maxY = std::min(H - 1, (int)(cy + radius * 3));

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            float dx = x - cx, dy = y - cy;
            float dist2 = dx * dx + dy * dy;
            float sigma2 = radius * radius;
            float g = std::exp(-dist2 / (2.0f * sigma2));
            Vec3f c = color * (g * intensity);
            Vec3f existing = fb.getPixel(x, y);
            fb.setPixel(x, y, existing + c);  // 加法混合
        }
    }
}

// 画十字衍射光芒
static void drawDiffraction(FloatFramebuffer& fb, float cx, float cy, float length, Vec3f color, float intensity)
{
    int W = fb.width(), H = fb.height();
    // 水平 + 垂直 + 45度 共4条线
    float angles[] = {0, 3.14159f / 2, 3.14159f / 4, -3.14159f / 4};
    for (float angle : angles)
    {
        float dx = std::cos(angle), dy = std::sin(angle);
        for (float t = 0; t < length; t += 0.5f)
        {
            float px = cx + dx * t, py = cy + dy * t;
            float px2 = cx - dx * t, py2 = cy - dy * t;
            float fade = 1.0f - t / length;
            fade = fade * fade * fade;  // 立方衰减
            float width = 1.5f * fade + 0.3f;
            // 画一个小高斯
            for (float w = -width; w <= width; w += 0.5f)
            {
                float nx = px + (-dy) * w, ny = py + dx * w;
                float nx2 = px2 + (-dy) * w, ny2 = py2 + dx * w;
                float gw = std::exp(-w * w / (width * width * 0.5f));
                float g = fade * gw * intensity * 0.3f;
                int ix = (int)nx, iy = (int)ny;
                int ix2 = (int)nx2, iy2 = (int)ny2;
                if (ix >= 0 && ix < W && iy >= 0 && iy < H)
                {
                    Vec3f e = fb.getPixel(ix, iy);
                    fb.setPixel(ix, iy, e + color * g);
                }
                if (ix2 >= 0 && ix2 < W && iy2 >= 0 && iy2 < H)
                {
                    Vec3f e = fb.getPixel(ix2, iy2);
                    fb.setPixel(ix2, iy2, e + color * g);
                }
            }
        }
    }
}

int main()
{
    const int W = 1920, H = 1080;
    FloatFramebuffer fb(W, H);
    fb.clear(Vec3f{0, 0, 0});

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> uf(0.0f, 1.0f);

    // ---- 1. 星云背景（FBM 噪声）----
    printf("绘制星云背景...\n");
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            float nx = x / float(W) * 4.0f;
            float ny = y / float(H) * 4.0f;
            float n1 = fbm(nx + 0.5f, ny + 1.3f, 5);
            float n2 = fbm(nx * 1.5f + 3.7f, ny * 1.5f + 2.1f, 4);
            // 紫蓝色星云
            Vec3f nebula1 = Vec3f{0.15f, 0.05f, 0.3f} * std::pow(n1, 2.0f) * 0.4f;
            // 橙红色星云
            Vec3f nebula2 = Vec3f{0.3f, 0.08f, 0.02f} * std::pow(n2, 3.0f) * 0.3f;
            // 深空底色（极暗蓝）
            Vec3f bg = Vec3f{0.005f, 0.005f, 0.015f};
            fb.setPixel(x, y, bg + nebula1 + nebula2);
        }
    }

    // ---- 2. 暗星（大量小星）----
    printf("绘制暗星（800颗）...\n");
    for (int i = 0; i < 800; ++i)
    {
        float x = uf(rng) * W;
        float y = uf(rng) * H;
        float brightness = 0.1f + uf(rng) * 0.4f;
        float size = 0.3f + uf(rng) * 0.6f;
        // 色温：蓝白到橙红
        float temp = uf(rng);
        Vec3f color;
        if (temp < 0.3f)
            color = Vec3f{0.6f, 0.7f, 1.0f};  // 蓝
        else if (temp < 0.7f)
            color = Vec3f{1.0f, 1.0f, 0.9f};  // 白
        else
            color = Vec3f{1.0f, 0.8f, 0.5f};  // 橙
        drawStar(fb, x, y, size, color, brightness);
    }

    // ---- 3. 亮星（中等大小，带微弱光晕）----
    printf("绘制亮星（80颗）...\n");
    for (int i = 0; i < 80; ++i)
    {
        float x = uf(rng) * W;
        float y = uf(rng) * H;
        float brightness = 1.0f + uf(rng) * 3.0f;
        float size = 1.0f + uf(rng) * 1.5f;
        float temp = uf(rng);
        Vec3f color;
        if (temp < 0.25f)
            color = Vec3f{0.5f, 0.7f, 1.5f};
        else if (temp < 0.5f)
            color = Vec3f{1.0f, 1.0f, 1.2f};
        else if (temp < 0.75f)
            color = Vec3f{1.5f, 1.2f, 0.8f};
        else
            color = Vec3f{1.5f, 0.6f, 0.3f};
        drawStar(fb, x, y, size, color, brightness);
    }

    // ---- 4. 超亮星（带十字衍射光芒）----
    printf("绘制超亮星+衍射光芒（12颗）...\n");
    for (int i = 0; i < 12; ++i)
    {
        float x = 100 + uf(rng) * (W - 200);
        float y = 100 + uf(rng) * (H - 200);
        float brightness = 5.0f + uf(rng) * 10.0f;
        float size = 2.0f + uf(rng) * 2.0f;
        float temp = uf(rng);
        Vec3f color;
        if (temp < 0.3f)
            color = Vec3f{0.7f, 0.85f, 2.0f};
        else if (temp < 0.6f)
            color = Vec3f{2.0f, 2.0f, 2.5f};
        else
            color = Vec3f{2.0f, 1.5f, 0.8f};

        // 核心亮点
        drawStar(fb, x, y, size, color, brightness);
        // 外层光晕
        drawStar(fb, x, y, size * 4, color * 0.3f, brightness * 0.2f);
        // 十字衍射
        float diffLen = 30 + uf(rng) * 60;
        drawDiffraction(fb, x, y, diffLen, color, brightness * 0.5f);
    }

    // ---- 5. 色调映射 + Gamma ----
    printf("色调映射...\n");
    Framebuffer ldr(W, H);
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            Vec3f hdr = fb.getPixel(x, y);
            // ACES 近似
            auto aces = [](float v) {
                float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
                return std::clamp((v * (a * v + b)) / (v * (c * v + d) + e), 0.0f, 1.0f);
            };
            Vec3f mapped{aces(hdr.x), aces(hdr.y), aces(hdr.z)};
            // Gamma
            auto gamma = [](float v) { return std::pow(std::max(v, 0.0f), 1.0f / 2.2f); };
            ldr.setPixel(x, y, Color::fromFloat(gamma(mapped.x), gamma(mapped.y), gamma(mapped.z)));
        }
    }

    // ---- 输出 ----
    std::filesystem::create_directories("output");
    tga::write("output/stars.tga", ldr);
    printf("已生成 output/stars.tga (%dx%d)\n", W, H);
    return 0;
}
