#pragma once
#include <string>

#include "softrenderer/core/framebuffer.h"

// ============================================================
// 通用图像写出 —— 使用 stb_image_write
//
// 支持格式：PNG, BMP, TGA, HDR, JPG；按文件扩展名自动选择。
//
// 【分层】stb 的 C 接口只出现在 src/io/ 的实现单元里，公共头不暴露
// 任何第三方符号。实现在 src/io/image_write.cpp，由 SoftRenderer::io
// 静态库提供。
// ============================================================

namespace sr::io
{

// 根据扩展名写出图像（未知扩展名默认 PNG）。
// 内部会把 Framebuffer 自底向上的像素翻转成 stb 需要的自顶向下。
bool writeImage(const std::string &path, const Framebuffer &fb, int jpgQuality = 90);

// 写出 HDR 浮点图像（rgbData 为 w*h*3 的 float 数组）。
bool writeHDR(const std::string &path, const float *rgbData, int w, int h);

} // namespace sr::io
