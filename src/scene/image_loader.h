#pragma once
#include <string>
#include "core/tga.h"

// ============================================================
// 通用图像加载器 —— 直接使用 stb_image
//
// 支持格式：PNG, JPG, BMP, TGA, PSD, GIF, HDR, PIC, PNM 等
// 统一输出 tga::Image（RGBA 像素数组，y=0 在底部）。
//
// stb_image 源码在 third/stb/stb_image.h
// 实现在 third/stb/stb_image_impl.cpp（CMake 编译为 stb_image 库）
// ============================================================

// stb_image C 接口声明
extern "C"
{
    unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
    void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);
}

namespace imageloader
{

    // 加载任意 stb_image 支持的格式，输出到 tga::Image
    inline bool loadImage(const std::string &path, tga::Image &out)
    {
        int w, h, channels;
        stbi_set_flip_vertically_on_load(1);                                 // 翻转为自底向上（匹配我们的约定）
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4); // 强制 4 通道 RGBA
        if (!data)
            return false;

        out.width = w;
        out.height = h;
        out.pixels.resize(static_cast<size_t>(w) * h);
        for (int i = 0; i < w * h; ++i)
        {
            out.pixels[i] = Color(data[i * 4 + 0], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]);
        }
        stbi_image_free(data);
        return true;
    }

} // namespace imageloader
