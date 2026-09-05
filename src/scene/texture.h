#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "core/tga.h"
#include "math/vec.h"

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
class Texture
{
public:
    enum class Filter
    {
        Nearest,
        Bilinear,
        Trilinear
    };

    Texture() = default;
    explicit Texture(tga::Image img) : image_(std::move(img)) { generateMipmaps(); }

    bool valid() const { return image_.valid(); }
    int width() const { return image_.width; }
    int height() const { return image_.height; }
    int mipLevels() const { return int(mips_.size()); }

    // 从 TGA 文件加载
    bool loadFromFile(const std::string &path)
    {
        if (!tga::read(path, image_))
            return false;
        generateMipmaps();
        return true;
    }

    // 直接用一张已有图像
    void setImage(tga::Image img)
    {
        image_ = std::move(img);
        generateMipmaps();
    }

    // 设置采样过滤器
    void setFilter(Filter f) { filter_ = f; }
    Filter getFilter() const { return filter_; }

    // ---------- 最近邻采样 ----------
    Vec3f sampleNearest(float u, float v) const
    {
        if (!valid())
            return {1, 0, 1};
        int x = wrap(int(u * image_.width), image_.width);
        int y = wrap(int(v * image_.height), image_.height);
        return toVec(image_.at(x, y));
    }

    // ---------- 双线性采样（level 0）----------
    Vec3f sampleBilinear(float u, float v) const
    {
        return sampleBilinearLevel(u, v, 0);
    }

    // ---------- 三线性采样（需要屏幕空间 UV 导数）----------
    // dudx, dudy, dvdx, dvdy 是 UV 对屏幕像素的偏导数近似。
    // 在光栅化器中可用相邻像素的 UV 差来估算。
    Vec3f sampleTrilinear(float u, float v, float dudx, float dudy, float dvdx, float dvdy) const
    {
        if (!valid() || mips_.empty())
            return sampleBilinear(u, v);
        // 计算 LOD（参考 OpenGL 规范）
        float px = dudx * image_.width, py = dudy * image_.width;
        float qx = dvdx * image_.height, qy = dvdy * image_.height;
        float rho = std::sqrt(std::max(px * px + qx * qx, py * py + qy * qy));
        float lod = std::log2(std::max(rho, 1e-6f));
        lod = std::clamp(lod, 0.0f, float(mips_.size() - 1));

        int level0 = int(std::floor(lod));
        int level1 = std::min(level0 + 1, int(mips_.size()) - 1);
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

private:
    tga::Image image_;
    std::vector<tga::Image> mips_; // mips_[0] = 原始图，mips_[i] = 缩 2^i 倍
    Filter filter_ = Filter::Trilinear;

    // 对指定 mipmap 层做双线性采样
    Vec3f sampleBilinearLevel(float u, float v, int level) const
    {
        const tga::Image &img = mips_[level];
        int w = img.width, h = img.height;
        float fx = u * w - 0.5f;
        float fy = v * h - 0.5f;
        int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        float tx = fx - x0, ty = fy - y0;
        Vec3f c00 = toVec(img.at(wrap(x0, w), wrap(y0, h)));
        Vec3f c10 = toVec(img.at(wrap(x0 + 1, w), wrap(y0, h)));
        Vec3f c01 = toVec(img.at(wrap(x0, w), wrap(y0 + 1, h)));
        Vec3f c11 = toVec(img.at(wrap(x0 + 1, w), wrap(y0 + 1, h)));
        Vec3f cx0 = c00 * (1 - tx) + c10 * tx;
        Vec3f cx1 = c01 * (1 - tx) + c11 * tx;
        return cx0 * (1 - ty) + cx1 * ty;
    }

    // 生成 mipmap 链：每级用 box filter（2x2 平均）缩半
    void generateMipmaps()
    {
        mips_.clear();
        if (!image_.valid())
            return;
        mips_.push_back(image_);
        int w = image_.width, h = image_.height;
        while (w > 1 || h > 1)
        {
            int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
            tga::Image down;
            down.width = nw;
            down.height = nh;
            down.pixels.resize(size_t(nw) * nh);
            for (int y = 0; y < nh; ++y)
            {
                for (int x = 0; x < nw; ++x)
                {
                    // 2x2 box filter（clamp 边缘）
                    int sx = x * 2, sy = y * 2;
                    Color c00 = mips_.back().at(std::min(sx, w - 1), std::min(sy, h - 1));
                    Color c10 = mips_.back().at(std::min(sx + 1, w - 1), std::min(sy, h - 1));
                    Color c01 = mips_.back().at(std::min(sx, w - 1), std::min(sy + 1, h - 1));
                    Color c11 = mips_.back().at(std::min(sx + 1, w - 1), std::min(sy + 1, h - 1));
                    down.pixels[size_t(y) * nw + x] = Color(
                        uint8_t((c00.r + c10.r + c01.r + c11.r + 2) / 4),
                        uint8_t((c00.g + c10.g + c01.g + c11.g + 2) / 4),
                        uint8_t((c00.b + c10.b + c01.b + c11.b + 2) / 4));
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
    static Vec3f toVec(const Color &c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; }
};

// ============================================================
// 程序化纹理：不依赖外部图片也能演示纹理映射
// ============================================================
namespace texgen
{

    // 棋盘格：checks×checks 个格子，两色交替
    inline tga::Image checkerboard(int size = 256, int checks = 8,
                                   Color a = Color(230, 230, 230),
                                   Color b = Color(40, 40, 40))
    {
        tga::Image img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        int cell = size / checks;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                bool odd = ((x / cell) + (y / cell)) & 1;
                img.pixels[size_t(y) * size + x] = odd ? a : b;
            }
        return img;
    }

    // 彩色 UV 网格：便于观察 UV 走向（红=u，绿=v）+ 黑色网格线
    inline tga::Image uvGrid(int size = 256, int lines = 10)
    {
        tga::Image img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        int step = size / lines;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                uint8_t r = uint8_t(x * 255 / size);
                uint8_t g = uint8_t(y * 255 / size);
                Color c(r, g, 60);
                if (x % step == 0 || y % step == 0)
                    c = Color(20, 20, 20); // 网格线
                img.pixels[size_t(y) * size + x] = c;
            }
        return img;
    }

    // 程序化法线贴图（切线空间）：用半球凸起的高度场求法线并编码为 RGB。
    //
    // 法线贴图约定：每个纹素存一个切线空间法线 (nx,ny,nz)，
    // 编码为颜色 = (n * 0.5 + 0.5) * 255。平坦区域法线 (0,0,1)
    // 对应经典的淡蓝紫色 (128,128,255)。
    //
    // 这里用 cells×cells 个圆形凸起（半球）拼成，凸起处法线向外偏，
    // 光照下会看到明显的凹凸颗粒感。
    inline tga::Image normalMapBumps(int size = 256, int cells = 8, float strength = 1.0f)
    {
        tga::Image img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        float cell = float(size) / cells;
        float radius = cell * 0.42f;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                float cx = (std::floor(x / cell) + 0.5f) * cell;
                float cy = (std::floor(y / cell) + 0.5f) * cell;
                float dx = x - cx, dy = y - cy;
                float d = std::sqrt(dx * dx + dy * dy);
                Vec3f n{0, 0, 1};
                if (d < radius)
                {
                    // 半球面：z = sqrt(r^2 - d^2)，法线 = 归一化(dx,dy,z)
                    float z = std::sqrt(std::max(0.0f, radius * radius - d * d));
                    n = Vec3f{dx * strength, dy * strength, z}.normalized();
                }
                img.pixels[size_t(y) * size + x] =
                    Color(uint8_t((n.x * 0.5f + 0.5f) * 255.0f),
                          uint8_t((n.y * 0.5f + 0.5f) * 255.0f),
                          uint8_t((n.z * 0.5f + 0.5f) * 255.0f));
            }
        return img;
    }

} // namespace texgen
