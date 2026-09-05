#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "core/log.h"
#include "core/tga.h"

// ============================================================
// 通用图像加载器 —— 直接使用 stb_image
//
// 【职责】把任意 stb_image 支持的图像文件解码为 tga::Image。
// 【支持格式】PNG, JPG, BMP, TGA, PSD, GIF, HDR, PIC, PNM 等
// 【输出约定】统一 RGBA 四通道，y=0 在底部（与 Framebuffer 一致）
//
// 导出函数：
//   - loadImage(path, out) → bool，解码失败或尺寸非法返回 false
//   - kMaxImageDimension   → 允许的单边像素上限
//
// 用法示例：
// @code
//   tga::Image img;
//   if (imageloader::loadImage("assets/hero.png", img))
//   {
//       Texture tex = Texture::fromImage(img);   // 具体接口见 scene/texture.h
//   }
// @endcode
//
// 【实现位置】stb_image 头文件在 third/stb/stb_image.h，其实现符号由
// **third_party_impl** 静态库提供，定义处是 third/tinygltf/tiny_gltf_impl.cpp
// （#define STB_IMAGE_IMPLEMENTATION）。
//
// ⚠ 该文件名有误导性：它**只提供 stb_image + stb_image_write**，不含 tinygltf。
// 三个实现单元的真实分工是：
//   third/tinygltf/tiny_gltf_impl.cpp  → stb_image + stb_image_write
//   src/scene/gltf_loader_impl.cpp     → tinygltf
//   third/tiny_obj_loader_impl.cpp     → tinyobjloader
//
// 本文件只声明 stbi_* 的 C 接口，**不定义** *_IMPLEMENTATION（否则符号重定义）。
//
// 【安全】图像文件属不可信外部输入：本文件对 stbi_load 回报的宽高
// 做合法性与上限校验，且像素计数全程用 size_t，避免有符号溢出。
// ============================================================

// stb_image C 接口声明（实现由 third_party_impl 提供，定义处见上方头注释）
extern "C"
{
    unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
    void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);
}

namespace imageloader
{

    // 允许加载的单边像素上限。
    //
    // 【为什么需要上限】stbi_load 面对损坏或恶意构造的文件时，可能从文件头
    // 里读出一个极大的宽高并回报成功。直接拿去 resize 会触发巨额分配
    // （甚至 std::bad_alloc 终止进程）。engineering_rules.md 要求
    // 「外部输入视为不可信：长度与偏移量均需校验」。
    //
    // 【为何取 16384】远超任何常规纹理尺寸（4K=3840、8K=7680），
    // 对正常使用不构成限制；同时 16384² × 4 字节 = 1 GB，是单个分配
    // 仍可接受的量级，不会在合法路径上直接打死进程。
    inline constexpr int32_t kMaxImageDimension = 16384;

    // 加载任意 stb_image 支持的格式，输出到 tga::Image。
    // 失败返回 false 并记一条 warn 日志（区分「无法解码」与「尺寸非法」）。
    inline bool loadImage(const std::string &path, tga::Image &out)
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
