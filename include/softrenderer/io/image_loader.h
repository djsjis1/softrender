#pragma once
#include <cstdint>
#include <string>

#include "softrenderer/core/image.h"

// ============================================================
// 通用图像加载器 —— 基于 stb_image
//
// 【职责】把任意 stb_image 支持的图像文件解码为 sr::Image。
// 【支持格式】PNG, JPG, BMP, TGA, PSD, GIF, HDR, PIC, PNM 等
// 【输出约定】统一 RGBA 四通道，y=0 在底部（与 Framebuffer 一致）
//
// 【安全】图像文件属不可信外部输入：实现里对解码器回报的宽高做
// 合法性与上限校验，像素计数全程用 size_t，避免有符号溢出。
//
// 【分层】stb_image 只出现在 src/io/image_loader.cpp 中；实现在
// src/io/image_loader.cpp，由 SoftRenderer::io 静态库提供。
//
// 用法：
// @code
//   sr::Image img;
//   if (sr::io::loadImage("assets/hero.png", img))
//       Texture tex(img);
// @endcode
// ============================================================

namespace sr::io
{

// 允许加载的单边像素上限。
//
// 【为什么需要上限】解码器面对损坏或恶意构造的文件时，可能从文件头里
// 读出一个极大的宽高并回报成功，直接拿去 resize 会触发巨额分配。
// 【为何取 16384】远超常规纹理尺寸（4K=3840、8K=7680）；同时
// 16384² × 4 字节 = 1 GB，是单个分配仍可接受的量级。
inline constexpr int32_t kMaxImageDimension = 16384;

// 加载任意 stb_image 支持的格式，输出到 sr::Image。
// 失败返回 false 并记一条 warn 日志（区分「无法解码」与「尺寸非法」）。
bool loadImage(const std::string &path, sr::Image &out);

} // namespace sr::io
