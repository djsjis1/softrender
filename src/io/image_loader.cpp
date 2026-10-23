#include "softrenderer/io/image_loader.h"

#include <memory>

#include "softrenderer/runtime/log.h"

#include "stb_bridge.h"

// 通用图像加载：stb_image 解码 + 尺寸上限校验 + RGBA 重排。
namespace sr::io
{

    // 加载任意 stb_image 支持的格式，输出到 sr::Image。
    // 失败返回 false 并记一条 warn 日志（区分「无法解码」与「尺寸非法」）。
    bool loadImage(const std::string &path, sr::Image &out)
    {
        // 声明即初始化：stbi_load 失败时不保证写入这三个出参，
        // 未初始化的值一旦被后续逻辑读到就是 UB。
        int w = 0, h = 0, channels = 0;
        stbi_set_flip_vertically_on_load(1);                                 // 翻转为自底向上（匹配我们的约定）
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4); // 强制 4 通道 RGBA
        if (data == nullptr)
        {
            // 日志用 fmt 风格的 {} 占位符：core/log.h 的两种模式都按此语法按顺序替换。
            // 可直接传 std::string —— 内置模式已改为变参模板（sr::formatLog），
            // 不再走 C 可变参，因此没有「非 POD 经 ... 传递」的 UB（旧版会报 C4840）。
            SR_LOG_WARN("imageloader: 无法解码图像 '{}'（文件不存在或格式不支持）", path);
            return false;
        }

        // RAII 归还解码缓冲：stbi_load 的内存必须由 stbi_image_free 释放。
        // 用 unique_ptr + 自定义删除器，使下面任何一条失败路径都不会漏放。
        // （旧实现在新增校验分支时极易遗漏：一旦提前 return 就泄露整个图像缓冲。）
        std::unique_ptr<unsigned char, void (*)(void *)> pixels(data, &stbi_image_free);

        if (w <= 0 || h <= 0 || w > kMaxImageDimension || h > kMaxImageDimension)
        {
            SR_LOG_WARN("imageloader: 图像 '{}' 尺寸非法（{}x{}，允许范围 1..{}）",
                        path, w, h, kMaxImageDimension);
            return false;
        }

        // 像素总数必须用 size_t 计算。
        //
        // 旧实现写作 for (int i = 0; i < w * h; ++i)：w 与 h 都是 int，
        // w * h 是**有符号 int 乘法**，超过约 46340² 就溢出 → 未定义行为。
        // 而紧邻上一行的 resize 已经正确用了 size_t —— 典型的「改一半」。
        const size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);

        out.width = w;
        out.height = h;
        out.pixels.resize(pixelCount);

        const unsigned char *src = pixels.get();
        for (size_t i = 0; i < pixelCount; ++i)
        {
            // i * 4 为 size_t 运算，不会溢出；偏移上界 = pixelCount*4，
            // 正好等于 stbi_load 以 desired_channels=4 分配的缓冲长度。
            const unsigned char *px = src + i * 4;
            out.pixels[i] = Color(px[0], px[1], px[2], px[3]);
        }
        return true;
    }

} // namespace imageloader
