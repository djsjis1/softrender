#pragma once
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "core/framebuffer.h"

// ============================================================
// 通用图像写出 —— 使用 stb_image_write
//
// 支持格式：PNG, BMP, TGA, HDR, JPG
// 根据文件扩展名自动选择格式。
//
// 源码：third/stb/stb_image_write.h
// 实现：third/tinygltf/tiny_gltf_impl.cpp（#define STB_IMAGE_WRITE_IMPLEMENTATION）
//       → 编入 third_party_impl 静态库
// ⚠ 旧注释曾称「实现在 third/stb/stb_image_write_impl.cpp」，该文件并不存在。
//   也注意 tiny_gltf_impl.cpp 这个文件名有误导性：它只提供 stb 的两个库，
//   tinygltf 的实现在 src/scene/gltf_loader_impl.cpp。
// 本文件只声明 stbi_write_* 的 C 接口，不定义 *_IMPLEMENTATION。
// ============================================================

extern "C" {
int stbi_write_png(const char* filename, int w, int h, int comp, const void* data, int stride_in_bytes);
int stbi_write_bmp(const char* filename, int w, int h, int comp, const void* data);
int stbi_write_tga(const char* filename, int w, int h, int comp, const void* data);
int stbi_write_jpg(const char* filename, int w, int h, int comp, const void* data, int quality);
int stbi_write_hdr(const char* filename, int w, int h, int comp, const float* data);
}

namespace imagewriter {

// 从 Framebuffer 提取 RGBA 字节数组（自底向上 → 自顶向下翻转，因为 stb 默认自顶向下）
inline std::vector<unsigned char> extractRGBA(const Framebuffer& fb, bool flipY = true)
{
    int w = fb.width(), h = fb.height();
    std::vector<unsigned char> rgba(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
    {
        int srcY = flipY ? (h - 1 - y) : y;
        for (int x = 0; x < w; ++x)
        {
            Color c = fb.getPixel(x, srcY);
            size_t i = (size_t(y) * w + x) * 4;
            rgba[i + 0] = c.r;
            rgba[i + 1] = c.g;
            rgba[i + 2] = c.b;
            rgba[i + 3] = c.a;
        }
    }
    return rgba;
}

// 根据扩展名写出图像
inline bool writeImage(const std::string& path, const Framebuffer& fb, int jpgQuality = 90)
{
    int w = fb.width(), h = fb.height();
    auto rgba = extractRGBA(fb, true);

    // 判断扩展名
    auto ext = path.substr(path.rfind('.') + 1);
    for (auto& c : ext)
        c = char(std::tolower(c));

    if (ext == "png")
    {
        return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
    }
    else if (ext == "bmp")
    {
        return stbi_write_bmp(path.c_str(), w, h, 4, rgba.data()) != 0;
    }
    else if (ext == "tga")
    {
        return stbi_write_tga(path.c_str(), w, h, 4, rgba.data()) != 0;
    }
    else if (ext == "jpg" || ext == "jpeg")
    {
        return stbi_write_jpg(path.c_str(), w, h, 4, rgba.data(), jpgQuality) != 0;
    }
    else
    {
        // 默认 PNG
        return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
    }
}

// 写出 HDR 浮点图像（需要 float 数据）
inline bool writeHDR(const std::string& path, const float* rgbData, int w, int h)
{
    return stbi_write_hdr(path.c_str(), w, h, 3, rgbData) != 0;
}

}  // namespace imagewriter
