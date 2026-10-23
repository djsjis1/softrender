#include "softrenderer/io/tga.h"

#include <cstdio>
#include <vector>

// TGA 读写：手写 18 字节文件头 + 裸像素流。
namespace sr::io
{

    // ---------- 写出：未压缩 24 位 BGR ----------
    bool writeTga(const std::string &path, const Framebuffer &fb)
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
    // 读取结果统一存成 sr::Image（RGBA，第 0 行在底部，与 Framebuffer 约定一致）。
    bool readTga(const std::string &path, sr::Image &out)
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
