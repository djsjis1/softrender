#include "softrenderer/io/image_write.h"

#include <cctype>
#include <vector>

#include "stb_bridge.h"

// 通用图像写出：按扩展名分派到 stb_image_write 的 C 接口。
namespace sr::io {

// 从 Framebuffer 提取 RGBA 字节数组（自底向上 → 自顶向下翻转，因为 stb 默认自顶向下）
static std::vector<unsigned char> extractRGBA(const Framebuffer& fb, bool flipY = true)
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
bool writeImage(const std::string& path, const Framebuffer& fb, int jpgQuality, std::string* error)
{
    if (error)
        error->clear();
    int w = fb.width(), h = fb.height();
    auto rgba = extractRGBA(fb, true);

    // 判断扩展名。tolower 参数必须是 unsigned char：
    // 非 ASCII 路径里的负值 char 直接传进去是未定义行为。
    auto ext = path.substr(path.rfind('.') + 1);
    for (auto& c : ext)
        c = char(std::tolower(static_cast<unsigned char>(c)));

    bool ok = false;
    const char* format = "PNG";
    if (ext == "png")
    {
        ok = stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
    }
    else if (ext == "bmp")
    {
        format = "BMP";
        ok = stbi_write_bmp(path.c_str(), w, h, 4, rgba.data()) != 0;
    }
    else if (ext == "tga")
    {
        format = "TGA";
        ok = stbi_write_tga(path.c_str(), w, h, 4, rgba.data()) != 0;
    }
    else if (ext == "jpg" || ext == "jpeg")
    {
        format = "JPG";
        ok = stbi_write_jpg(path.c_str(), w, h, 4, rgba.data(), jpgQuality) != 0;
    }
    else
    {
        // 默认 PNG
        ok = stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
    }

    if (!ok && error)
        *error = "写出图像失败：'" + path + "'（格式 " + format + "，路径不可写或内存不足）";
    return ok;
}

// 写出 HDR 浮点图像（需要 float 数据）
bool writeHDR(const std::string& path, const float* rgbData, int w, int h, std::string* error)
{
    if (error)
        error->clear();
    // 参数非法时直接拒绝：把空指针交给 stb 会直接崩，且它没有返回值之外的报错通道。
    if (rgbData == nullptr || w <= 0 || h <= 0)
    {
        if (error)
            *error = "写出 HDR 失败：'" + path + "' 参数非法（数据为空或尺寸非正）";
        return false;
    }
    const bool ok = stbi_write_hdr(path.c_str(), w, h, 3, rgbData) != 0;
    if (!ok && error)
        *error = "写出 HDR 失败：'" + path + "'（路径不可写或内存不足）";
    return ok;
}

} // namespace sr::io
