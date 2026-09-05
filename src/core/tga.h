#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include "core/framebuffer.h"

// ============================================================
// TGA 图片读写（手写实现，不依赖任何库）
//
// 为什么选 TGA：格式极其简单——18 字节文件头 + 裸像素数据，
// 无压缩时不需要任何编解码知识，非常适合自制渲染器出图；
// Windows 照片查看器、IrfanView、VSCode 插件都能打开。
//
// 文件头关键字段：
//   [2]  imageType   2 = 未压缩真彩色, 10 = RLE 压缩真彩色
//   [12] width  (2 字节小端)
//   [14] height (2 字节小端)
//   [16] bpp    每像素位数：24 (BGR) 或 32 (BGRA)
//   [17] descriptor 第 5 位 = 1 表示原点在左上角；我们写 0（左下角），
//        与 Framebuffer 的 y 向上约定一致。
// 注意 TGA 的通道顺序是 BGR(A)，不是 RGB(A)！
// ============================================================
namespace tga
{

    // ---------- 写出：未压缩 24 位 BGR ----------
    inline bool write(const std::string &path, const Framebuffer &fb)
    {
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;

        const int w = fb.width(), h = fb.height();
        uint8_t header[18] = {};
        header[2] = 2; // 未压缩真彩色
        header[12] = uint8_t(w & 0xFF);
        header[13] = uint8_t((w >> 8) & 0xFF);
        header[14] = uint8_t(h & 0xFF);
        header[15] = uint8_t((h >> 8) & 0xFF);
        header[16] = 24; // 24 位 BGR
        header[17] = 0;  // 原点左下角
        std::fwrite(header, 1, 18, f);

        // 逐像素写 BGR。fb 第 0 行是底部行，正好匹配 TGA 左下角原点，顺序直写
        std::vector<uint8_t> row(size_t(w) * 3);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                Color c = fb.getPixel(x, y);
                row[size_t(x) * 3 + 0] = c.b;
                row[size_t(x) * 3 + 1] = c.g;
                row[size_t(x) * 3 + 2] = c.r;
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        return true;
    }

    // ---------- 读取：支持未压缩(2)与 RLE(10)，24/32 位 ----------
    // 阶段 5 加载纹理时使用。读取结果统一存成 RGBA 的 Image。
    struct Image
    {
        int width = 0, height = 0;
        std::vector<Color> pixels; // 行优先，第 0 行为图像底部（同 Framebuffer 约定）

        Color at(int x, int y) const { return pixels[size_t(y) * width + x]; }
        bool valid() const { return width > 0 && height > 0; }
    };

    inline bool read(const std::string &path, Image &out)
    {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;

        uint8_t header[18];
        if (std::fread(header, 1, 18, f) != 18)
        {
            std::fclose(f);
            return false;
        }

        const uint8_t idLength = header[0];
        const uint8_t imageType = header[2];
        const int w = header[12] | (header[13] << 8);
        const int h = header[14] | (header[15] << 8);
        const int bpp = header[16];
        const bool topOrigin = (header[17] & 0x20) != 0; // 第 5 位：原点在顶部

        if ((imageType != 2 && imageType != 10) || (bpp != 24 && bpp != 32) || w <= 0 || h <= 0)
        {
            std::fclose(f);
            return false; // 只支持真彩色 24/32 位
        }
        std::fseek(f, idLength, SEEK_CUR); // 跳过图像 ID 区

        const int bytesPP = bpp / 8;
        std::vector<uint8_t> raw(size_t(w) * h * bytesPP);

        if (imageType == 2)
        {
            // 未压缩：直接读
            if (std::fread(raw.data(), 1, raw.size(), f) != raw.size())
            {
                std::fclose(f);
                return false;
            }
        }
        else
        {
            // RLE 压缩：每个包 1 字节头，最高位=1 表示后面 1 个像素重复 n 次，
            // =0 表示后面跟 n 个原始像素（n = 低 7 位 + 1）
            size_t filled = 0;
            while (filled < raw.size())
            {
                int packet = std::fgetc(f);
                if (packet == EOF)
                {
                    std::fclose(f);
                    return false;
                }
                int count = (packet & 0x7F) + 1;
                if (packet & 0x80)
                { // RLE 包
                    uint8_t px[4];
                    if (std::fread(px, 1, bytesPP, f) != size_t(bytesPP))
                    {
                        std::fclose(f);
                        return false;
                    }
                    for (int i = 0; i < count && filled < raw.size(); ++i)
                        for (int b = 0; b < bytesPP; ++b)
                            raw[filled++] = px[b];
                }
                else
                { // 原始包
                    size_t n = size_t(count) * bytesPP;
                    if (filled + n > raw.size())
                        n = raw.size() - filled;
                    if (std::fread(raw.data() + filled, 1, n, f) != n)
                    {
                        std::fclose(f);
                        return false;
                    }
                    filled += n;
                }
            }
        }
        std::fclose(f);

        // BGR(A) -> RGBA，并统一为"第 0 行在底部"
        out.width = w;
        out.height = h;
        out.pixels.resize(size_t(w) * h);
        for (int y = 0; y < h; ++y)
        {
            int srcRow = topOrigin ? (h - 1 - y) : y; // 顶部原点的图要翻转
            for (int x = 0; x < w; ++x)
            {
                const uint8_t *p = &raw[(size_t(srcRow) * w + x) * bytesPP];
                out.pixels[size_t(y) * w + x] =
                    Color(p[2], p[1], p[0], bytesPP == 4 ? p[3] : 255);
            }
        }
        return true;
    }

} // namespace tga
