#pragma once
#include <string>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/image.h"

// ============================================================
// TGA 图片读写（手写实现，不依赖任何库）
//
// 为什么选 TGA：格式极其简单——18 字节文件头 + 裸像素数据，
// 无压缩时不需要任何编解码知识，非常适合自制渲染器出图。
//
// 文件头关键字段：
//   [2]  imageType   2 = 未压缩真彩色, 10 = RLE 压缩真彩色
//   [12] width  (2 字节小端)   [14] height (2 字节小端)
//   [16] bpp    每像素位数：24 (BGR) 或 32 (BGRA)
//   [17] descriptor 第 5 位 = 1 表示原点在左上角；我们写 0（左下角），
//        与 Framebuffer 的 y 向上约定一致。
// 注意 TGA 的通道顺序是 BGR(A)，不是 RGB(A)！
//
// 【分层】本头只声明接口；实现在 src/io/tga.cpp，由 SoftRenderer::io
// 静态库提供。核心渲染层不需要链接它。
//
// 【错误文本】两个函数的 error 参数约定相同：非空时失败写入一句可读原因
// （含路径与具体分支），成功则清空；传 nullptr 即可忽略。返回 true = 成功。
// ============================================================

namespace sr::io
{

// 写出未压缩 24 位 BGR。失败（如路径不可写）返回 false。
bool writeTga(const std::string &path, const Framebuffer &fb, std::string *error = nullptr);

// 读取未压缩(2)与 RLE(10)、24/32 位的 TGA，统一转为 RGBA、
// 第 0 行在底部的 sr::Image。格式不符或读取失败返回 false。
bool readTga(const std::string &path, sr::Image &out, std::string *error = nullptr);

} // namespace sr::io
