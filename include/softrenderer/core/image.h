#pragma once
#include <cstddef>
#include <vector>

#include "softrenderer/core/color.h"

// ============================================================
// Image：与文件格式无关的内存图像。
//
// 【为什么独立成头】像素数据是渲染与 2D 绘制的公共输入，与 TGA / PNG 等
// 具体文件格式无关。旧实现把它定义在 tga.h 内（`tga::Image`），使 Texture、
// 位图绘制与程序化生成器都被迫依赖 TGA 读写实现。
//
// 【坐标与排列约定】行优先；第 0 行位于图像底部，与 Framebuffer 的
// 「x 向右、y 向上、(0,0) 在左下角」保持一致。改动此约定会静默翻转
// 所有加载图片的显示方向。
//
// 【接口】width / height / pixels 为公开数据成员；`at` 不做边界检查，
// 越界属调用方错误（与 Framebuffer::row 同一口径）。
// ============================================================

namespace sr
{

struct Image
{
    int width = 0, height = 0;
    std::vector<Color> pixels; // 行优先，第 0 行为图像底部

    Color at(int x, int y) const { return pixels[size_t(y) * width + x]; }
    bool valid() const { return width > 0 && height > 0; }
};

} // namespace sr