#pragma once
#include <cstddef>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "softrenderer/core/image.h"
#include "softrenderer/math/vec.h"

// ============================================================
// Texture：纹理。持有一张图像 + mipmap 链，提供多种采样模式。
//
// 【UV 坐标】纹理空间用 (u,v) ∈ [0,1] 定位：u 向右，v 向上，
// (0,0) 在左下角（与我们的图像存储约定一致）。
//
// 【采样模式】
//   Nearest  ：最近邻，放大马赛克
//   Bilinear ：双线性，放大平滑
//   Trilinear：三线性（mipmap 层间也插值），远处无闪烁
//
// 【Mipmap 原理】
// 远处一个像素覆盖很多纹素，只采 4 个会漏信息（摩尔纹/闪烁）。
// Mipmap 预存逐级缩小的纹理链（每级长宽减半），采样时根据
// 像素覆盖的纹素面积选合适层级（LOD），在该层做双线性，
// 再与相邻层插值（trilinear）。
//
// LOD 计算参考 OpenGL 规范：
//   λ = log2(max(|dU/dx|, |dU/dy|, |dV/dx|, |dV/dy|) * texSize)
// 我们传入屏幕空间 UV 导数的近似值来计算。
//
// 【环绕方式】Repeat（重复平铺）。
// ============================================================
namespace sr
{

class Texture
{
public:
    // 为什么指定底层类型：cpp_project_rules.md 要求「枚举值必须有明确的底层类型」。
    // 三个采样模式用 uint8_t 足够，且作为 Texture 成员时占用可预测。
    enum class Filter : uint8_t
    {
        Nearest,
        Bilinear,
        Trilinear
    };

    Texture() = default;
    explicit Texture(sr::Image img) : image_(std::move(img)) { generateMipmaps(); }

    bool valid() const { return image_.valid(); }
    int width() const { return image_.width; }
    int height() const { return image_.height; }

    // 直接用一张已有图像
    void setImage(sr::Image img)
    {
        image_ = std::move(img);
        generateMipmaps();
    }

    // 设置采样过滤器
    void setFilter(Filter f) { filter_ = f; }
    Filter getFilter() const { return filter_; }

    // 可用的 mip 层数（含第 0 层原图）。无效图像为 0。
    int mipLevels() const { return image_.valid() ? int(mips_.size()) + 1 : 0; }

    // ---------- 最近邻采样 ----------
    Vec3f sampleNearest(float u, float v) const
    {
        if (!valid())
            return missingColor();
        // 非有限 UV 直接拒绝；否则 float→int 是 UB，插值权重也会变 NaN。
        if (!repeatUv(u, v))
            return missingColor();
        int x = wrap(int(u * image_.width), image_.width);
        int y = wrap(int(v * image_.height), image_.height);
        return toVec(image_.at(x, y));
    }

    // ---------- 双线性采样（level 0）----------
    Vec3f sampleBilinear(float u, float v) const { return sampleBilinearLevel(u, v, 0); }

    // ---------- 三线性采样（需要屏幕空间 UV 导数）----------
    // dudx, dudy, dvdx, dvdy 是 UV 对屏幕像素的偏导数近似。
    // 在光栅化器中可用相邻像素的 UV 差来估算。
    Vec3f sampleTrilinear(float u, float v, float dudx, float dudy, float dvdx, float dvdy) const
    {
        // 无 mip 链（默认构造或无效图像）时不能退回 sampleBilinear：
        // 那条路径最终仍是第 0 层，会把空存储当作有效层。两条路都必须
        // 在访问层数据之前拒绝。
        if (mipLevels() == 0)
            return missingColor();
        // 计算 LOD（参考 OpenGL 规范）
        const float lod = lodFor(dudx, dudy, dvdx, dvdy);

        int level0 = int(std::floor(lod));
        int level1 = std::min(level0 + 1, mipLevels() - 1);
        float frac = lod - level0;

        Vec3f c0 = sampleBilinearLevel(u, v, level0);
        if (level0 == level1)
            return c0;
        Vec3f c1 = sampleBilinearLevel(u, v, level1);
        return c0 * (1.0f - frac) + c1 * frac;
    }

    // 统一采样入口（根据 filter_ 设置自动选择）
    Vec3f sample(float u, float v, float dudx = 0, float dudy = 0,
                 float dvdx = 0, float dvdy = 0) const
    {
        switch (filter_)
        {
        case Filter::Nearest:
            return sampleNearest(u, v);
        case Filter::Bilinear:
            return sampleBilinear(u, v);
        case Filter::Trilinear:
            return sampleTrilinear(u, v, dudx, dudy, dvdx, dvdy);
        }
        return sampleBilinear(u, v);
    }

    // ---------- 带 alpha 的颜色采样 ----------
    // Vec3f 接口按设计只返回 RGB，2D 贴图用它会把纹理自身的透明度当成不透明，
    // 因此 UI/精灵合成需要这条保留 RGBA 的入口。导数用于选 mip 层。
    Color sampleColor(float u, float v, float dudx = 0, float dudy = 0, float dvdx = 0, float dvdy = 0) const
    {
        if (mipLevels() == 0)
            return missingColorRGBA();
        switch (filter_)
        {
        case Filter::Nearest:
            return sampleNearestColor(u, v);
        case Filter::Bilinear:
            return sampleColorLevel(u, v, 0);
        case Filter::Trilinear:
            break;
        }
        const float lod = lodFor(dudx, dudy, dvdx, dvdy);
        const int level0 = int(std::floor(lod));
        const int level1 = std::min(level0 + 1, mipLevels() - 1);
        const Color c0 = sampleColorLevel(u, v, level0);
        if (level0 == level1)
            return c0;
        return Color::lerp(c0, sampleColorLevel(u, v, level1), lod - float(level0));
    }

    // 显式按 mip 层双线性采样（层号被夹取）。供需要自己选层的调用方
    // （如 cubemap 按粗糙度选层）使用。
    Vec3f sampleLevel(float u, float v, int level) const { return sampleBilinearLevel(u, v, level); }

    // 屏幕空间导数 → mip 层号（浮点，已夹到 [0, mipLevels-1]）。
    // 轴对齐 blit 每像素的导数相同，调用方应在循环外算一次。
    float lodForPixel(float dudx, float dudy, float dvdx, float dvdy) const
    {
        return lodFor(dudx, dudy, dvdx, dvdy);
    }

    // 按已算好的 lod 采样（带 alpha）。与 sampleColor 等价，
    // 只是把逐像素重复的 sqrt/log2 mip 选择提到循环外。
    Color sampleColorWithLod(float u, float v, float lod) const
    {
        if (mipLevels() == 0)
            return missingColorRGBA();
        const float clamped = std::isfinite(lod) ? std::clamp(lod, 0.0f, float(mipLevels() - 1)) : 0.0f;
        const int level0 = int(float(clamped));
        const float frac = clamped - float(level0);
        // lod 为整数时不存在层间插值（常见于 1:1 blit）：省掉一次完整双线性采样。
        if (frac <= 0.0f)
            return sampleColorLevel(u, v, level0);
        const int level1 = std::min(level0 + 1, mipLevels() - 1);
        const Color c0 = sampleColorLevel(u, v, level0);
        if (level0 == level1)
            return c0;
        return Color::lerp(c0, sampleColorLevel(u, v, level1), frac);
    }

    // ---------- ClampToEdge 采样（2D / UI / 图集专用）----------
    //
    // Repeat 寻址在“放大一张小图”时必然发生跨边界取样：2×2 纹理放大到 8×8，
    // 最右列会混入 37.5% 的左边缘颜色（图集直接串色）。GPU 采样 UI 贴图用的是
    // ClampToEdge，这里提供同语义的入口；3D 表面纹理仍走 Repeat。
    Color sampleNearestClamped(float u, float v) const
    {
        if (!valid())
            return missingColorRGBA();
        if (!std::isfinite(u) || !std::isfinite(v))
            return missingColorRGBA();
        const int x = std::clamp(int(std::floor(u * float(image_.width))), 0, image_.width - 1);
        const int y = std::clamp(int(std::floor(v * float(image_.height))), 0, image_.height - 1);
        return image_.at(x, y);
    }

    Color sampleColorClamped(float u, float v, float lod) const
    {
        if (mipLevels() == 0)
            return missingColorRGBA();
        if (!std::isfinite(u) || !std::isfinite(v))
            return missingColorRGBA();
        const float clamped = std::isfinite(lod) ? std::clamp(lod, 0.0f, float(mipLevels() - 1)) : 0.0f;
        const int level0 = int(float(clamped));
        const float frac = clamped - float(level0);
        if (frac <= 0.0f)
            return sampleColorLevelClamped(u, v, level0);
        const int level1 = std::min(level0 + 1, mipLevels() - 1);
        const Color c0 = sampleColorLevelClamped(u, v, level0);
        if (level0 == level1)
            return c0;
        return Color::lerp(c0, sampleColorLevelClamped(u, v, level1), frac);
    }

private:
    sr::Image image_;
    // mips_[i] = 第 i+1 层（缩 2^(i+1) 倍）。第 0 层直接用 image_，
    // 不再存一份副本：512x512 纹理的 mip0 副本是 1 MB，与存储量相当。
    std::vector<sr::Image> mips_;
    Filter filter_ = Filter::Trilinear;

    // 取指定层（已夹取）。level==0 走 image_，否则 mips_[level-1]。
    const sr::Image &levelImage(int level) const
    {
        return level == 0 ? image_ : mips_[size_t(level - 1)];
    }

    // 对指定 mipmap 层做双线性采样。level 被夹到实际存在的层，
    // 使调用方传入任何层号都不会越界。
    Vec3f sampleBilinearLevel(float u, float v, int level) const
    {
        if (mipLevels() == 0)
            return missingColor();
        if (!repeatUv(u, v))
            return missingColor();
        const sr::Image &img = levelImage(std::clamp(level, 0, mipLevels() - 1));
        if (!img.valid())
            return missingColor();
        int w = img.width, h = img.height;
        // wrap 后索引必在 [0,w)×[0,h)，直接用行指针，省掉 4 次 at() 的边界分支。
        const Color *pixels = img.pixels.data();
        int xa = 0, xb = 0, ya = 0, yb = 0;
        float tx = 0.0f, ty = 0.0f;
        bilinearSpan(u * float(w) - 0.5f, w, xa, xb, tx);
        bilinearSpan(v * float(h) - 0.5f, h, ya, yb, ty);
        const Color c00 = pixels[size_t(ya) * size_t(w) + size_t(xa)];
        const Color c10 = pixels[size_t(ya) * size_t(w) + size_t(xb)];
        const Color c01 = pixels[size_t(yb) * size_t(w) + size_t(xa)];
        const Color c11 = pixels[size_t(yb) * size_t(w) + size_t(xb)];
        const Vec3f c00v = toVec(c00), c10v = toVec(c10), c01v = toVec(c01), c11v = toVec(c11);
        Vec3f cx0 = c00v * (1 - tx) + c10v * tx;
        Vec3f cx1 = c01v * (1 - tx) + c11v * tx;
        return cx0 * (1 - ty) + cx1 * ty;
    }

    // 生成 mipmap 链：从第 0 层开始逐级用 box filter（2x2 平均）缩半。
    // 第 0 层不复制，直接从 image_ 采样。
    void generateMipmaps()
    {
        mips_.clear();
        if (!image_.valid())
            return;
        int w = image_.width, h = image_.height;
        while (w > 1 || h > 1)
        {
            int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
            const sr::Image &source = mips_.empty() ? image_ : mips_.back();
            sr::Image down;
            down.width = nw;
            down.height = nh;
            down.pixels.resize(size_t(nw) * nh);
            if (w > 1 && h > 1)
            {
                // 宽高都 >1 时，2x / 2x+1 / 2y / 2y+1 必定在边界内
                // （nw = w/2 → 2x+1 ≤ w-1），因此不必逐像素做四次 min。
                for (int y = 0; y < nh; ++y)
                {
                    const Color *row0 = source.pixels.data() + size_t(2 * y) * size_t(w);
                    const Color *row1 = source.pixels.data() + size_t(2 * y + 1) * size_t(w);
                    Color *dstRow = down.pixels.data() + size_t(y) * size_t(nw);
                    for (int x = 0; x < nw; ++x)
                    {
                        const Color c00 = row0[2 * x], c10 = row0[2 * x + 1];
                        const Color c01 = row1[2 * x], c11 = row1[2 * x + 1];
                        dstRow[x] = Color(
                            uint8_t((c00.r + c10.r + c01.r + c11.r + 2) >> 2),
                            uint8_t((c00.g + c10.g + c01.g + c11.g + 2) >> 2),
                            uint8_t((c00.b + c10.b + c01.b + c11.b + 2) >> 2),
                            // alpha 必须一起平均：三参 Color 构造会把 alpha 置成 255，
                            // 那样半透明纹理一旦采到低层 mip 就凭空变不透明。
                            uint8_t((c00.a + c10.a + c01.a + c11.a + 2) >> 2));
                    }
                }
            }
            else
            {
                // 退化到单行/单列时索引可能越界，保留逐像素 clamp。
                for (int y = 0; y < nh; ++y)
                {
                    const Color *row0 = source.pixels.data() + size_t(std::min(2 * y, h - 1)) * size_t(w);
                    const Color *row1 = source.pixels.data() + size_t(std::min(2 * y + 1, h - 1)) * size_t(w);
                    Color *dstRow = down.pixels.data() + size_t(y) * size_t(nw);
                    for (int x = 0; x < nw; ++x)
                    {
                        const int sx0 = std::min(2 * x, w - 1);
                        const int sx1 = std::min(2 * x + 1, w - 1);
                        const Color c00 = row0[sx0], c10 = row0[sx1];
                        const Color c01 = row1[sx0], c11 = row1[sx1];
                        dstRow[x] = Color(
                            uint8_t((c00.r + c10.r + c01.r + c11.r + 2) >> 2),
                            uint8_t((c00.g + c10.g + c01.g + c11.g + 2) >> 2),
                            uint8_t((c00.b + c10.b + c01.b + c11.b + 2) >> 2),
                            uint8_t((c00.a + c10.a + c01.a + c11.a + 2) >> 2));
                    }
                }
            }
            mips_.push_back(std::move(down));
            w = nw;
            h = nh;
        }
    }

    static int wrap(int i, int n)
    {
        i %= n;
        return i < 0 ? i + n : i;
    }

    // x86-64 基线只有 SSE2，没有 roundsd，std::floor(float) 会降到库函数调用
    // （~20 cycles），而采样热路径每像素要调用 4~6 次。
    // 在 |x| < 2^23（float 能精确表示的整数范围）内用截断+修正代替：
    // cvttss2si + 比较，代价不到库调用的四分之一。
    static float floorFast(float x)
    {
        if (std::abs(x) < 8388608.0f)
        {
            const int truncated = int(x);
            return float(truncated) > x ? float(truncated - 1) : float(truncated);
        }
        return std::floor(x);
    }

    // 双线性的四邻域索引。u/v 已折到 [0,1) 时 base 只可能是 -1..n-1，
    // 因此不需要四次 %（整数除法的延迟远高于一次可预测分支）。
    static void bilinearSpan(float f, int n, int &i0, int &i1, float &frac)
    {
        const int base = int(floorFast(f));
        frac = f - float(base);
        i0 = base < 0 ? base + n : base;
        i1 = base + 1 >= n ? base + 1 - n : base + 1;
    }

    // Repeat 环绕下只关心小数部分。将任意有限 UV 折到 [0,1) 后，
    // u*width 必定落在 int 范围内，从而消除 float→int 的未定义行为；
    // 非有限 UV 返回 false，由调用方拒绝。
    static bool repeatUv(float &u, float &v)
    {
        if (!std::isfinite(u) || !std::isfinite(v))
            return false;
        u -= floorFast(u);
        v -= floorFast(v);
        return true;
    }

    // 屏幕空间 UV 导数 → mip 层号（夹到实际存在的层）。
    // 导数来自屏幕空间差分，退化三角形会产生 NaN/Inf：此时按第 0 层处理。
    float lodFor(float dudx, float dudy, float dvdx, float dvdy) const
    {
        if (!std::isfinite(dudx) || !std::isfinite(dudy) || !std::isfinite(dvdx) || !std::isfinite(dvdy))
            return 0.0f;
        const float px = dudx * image_.width, py = dudy * image_.width;
        const float qx = dvdx * image_.height, qy = dvdy * image_.height;
        const float rho = std::sqrt(std::max(px * px + qx * qx, py * py + qy * qy));
        if (!std::isfinite(rho) || rho <= 0.0f)
            return 0.0f;
        const float lod = std::log2(rho);
        return std::clamp(lod, 0.0f, float(mipLevels() - 1));
    }

    Color sampleNearestColor(float u, float v) const
    {
        if (!valid())
            return missingColorRGBA();
        if (!repeatUv(u, v))
            return missingColorRGBA();
        return image_.at(wrap(int(u * image_.width), image_.width),
                         wrap(int(v * image_.height), image_.height));
    }

    // 指定 mip 层的双线性采样（含 alpha）
    Color sampleColorLevel(float u, float v, int level) const
    {
        if (mipLevels() == 0)
            return missingColorRGBA();
        if (!repeatUv(u, v))
            return missingColorRGBA();
        const sr::Image &img = levelImage(std::clamp(level, 0, mipLevels() - 1));
        if (!img.valid())
            return missingColorRGBA();
        const int w = img.width, h = img.height;
        // wrap 后索引必在合法范围，直接用行指针避开 at() 的四次边界分支。
        const Color *pixels = img.pixels.data();
        int xa = 0, xb = 0, ya = 0, yb = 0;
        float tx = 0.0f, ty = 0.0f;
        bilinearSpan(u * float(w) - 0.5f, w, xa, xb, tx);
        bilinearSpan(v * float(h) - 0.5f, h, ya, yb, ty);
        const Color c00 = pixels[size_t(ya) * size_t(w) + size_t(xa)];
        const Color c10 = pixels[size_t(ya) * size_t(w) + size_t(xb)];
        const Color c01 = pixels[size_t(yb) * size_t(w) + size_t(xa)];
        const Color c11 = pixels[size_t(yb) * size_t(w) + size_t(xb)];
        return Color::lerp(Color::lerp(c00, c10, tx), Color::lerp(c01, c11, tx), ty);
    }

    // 指定 mip 层的双线性采样，越界取边缘像素（ClampToEdge）
    Color sampleColorLevelClamped(float u, float v, int level) const
    {
        if (mipLevels() == 0)
            return missingColorRGBA();
        const sr::Image &img = levelImage(std::clamp(level, 0, mipLevels() - 1));
        if (!img.valid())
            return missingColorRGBA();
        const int w = img.width, h = img.height;
        // 像素中心对齐后夹到 [0, n-1]：边缘像素不会被对边的颜色污染。
        const float fx = std::clamp(u * float(w) - 0.5f, 0.0f, float(w - 1));
        const float fy = std::clamp(v * float(h) - 0.5f, 0.0f, float(h - 1));
        const int x0 = int(fx), y0 = int(fy);
        const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const float tx = fx - float(x0), ty = fy - float(y0);
        const Color *pixels = img.pixels.data();
        const Color c00 = pixels[size_t(y0) * size_t(w) + size_t(x0)];
        const Color c10 = pixels[size_t(y0) * size_t(w) + size_t(x1)];
        const Color c01 = pixels[size_t(y1) * size_t(w) + size_t(x0)];
        const Color c11 = pixels[size_t(y1) * size_t(w) + size_t(x1)];
        return Color::lerp(Color::lerp(c00, c10, tx), Color::lerp(c01, c11, tx), ty);
    }

    // 无效纹理的统一返回色（品红，与旧 sampleNearest 的兜底一致）。
    static Vec3f missingColor() { return {1, 0, 1}; }
    static Color missingColorRGBA() { return Color(255, 0, 255, 255); }
    static Vec3f toVec(const Color &c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; }
};

}  // namespace sr
