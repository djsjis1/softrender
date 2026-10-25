#include "softrenderer/io/tga.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// TGA 读写：手写 18 字节文件头 + 裸像素流。
namespace sr::io
{

namespace
{
// 外部输入不可信：文件头里的宽高只有 16 位，但 65535×65535 就要分配 12 GB。
// 先按像素数卡上限（8192×8192），再按声明字节数与实际文件长度卡——
// 后者能拦住“头部声明巨大、文件只有几字节”的放大攻击。
constexpr int64_t kMaxTgaPixels = int64_t(8192) * 8192;
}  // namespace

    // ---------- 写出：未压缩 24 位 BGR ----------
    bool writeTga(const std::string &path, const Framebuffer &fb)
    {
        const int w = fb.width(), h = fb.height();
        // TGA 头里的宽高只有 16 位：超范围会被静默截断，读回来尺寸错乱。
        if (w <= 0 || h <= 0 || w > 65535 || h > 65535)
            return false;

        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;

        uint8_t header[18] = {};
        header[2] = 2; // 未压缩真彩色
        header[12] = uint8_t(w & 0xFF);
        header[13] = uint8_t((w >> 8) & 0xFF);
        header[14] = uint8_t(h & 0xFF);
        header[15] = uint8_t((h >> 8) & 0xFF);
        header[16] = 24; // 24 位 BGR
        header[17] = 0;  // 原点左下角

        bool ok = std::fwrite(header, 1, sizeof(header), f) == sizeof(header);

        // 逐像素写 BGR。fb 第 0 行是底部行，正好匹配 TGA 左下角原点，顺序直写
        std::vector<uint8_t> row(size_t(w) * 3);
        for (int y = 0; ok && y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const Color c = fb.getPixel(x, y);
                row[size_t(x) * 3 + 0] = c.b;
                row[size_t(x) * 3 + 1] = c.g;
                row[size_t(x) * 3 + 2] = c.r;
            }
            ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
        }

        // fclose 会报告延迟写入的错误（磁盘满、网络盘断开）：
        // 旧实现不看返回值，写出失败也返回 true。
        if (std::fclose(f) != 0)
            ok = false;
        return ok;
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
        if (int64_t(w) * int64_t(h) > kMaxTgaPixels)
        {
            std::fclose(f);
            return false; // 超出像素上限
        }
        std::fseek(f, idLength, SEEK_CUR); // 跳过图像 ID 区

        const int bytesPP = bpp / 8;
        // 未压缩：先验证文件真的有这么多数据，再分配缓冲。
        if (imageType == 2)
        {
            const long long needed = static_cast<long long>(w) * h * bytesPP;
            const long here = std::ftell(f);
            if (here < 0 || std::fseek(f, 0, SEEK_END) != 0)
            {
                std::fclose(f);
                return false;
            }
            const long end = std::ftell(f);
            if (std::fseek(f, here, SEEK_SET) != 0 || end < 0 || static_cast<long long>(end - here) < needed)
            {
                std::fclose(f);
                return false;
            }
        }
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
