#pragma once
#include <algorithm>
#include <cassert>
#include <vector>

#include "core/color.h"

// ============================================================
// Framebuffer：帧缓冲，本质就是一块 width * height 的像素数组。
//
// 坐标约定（非常重要，后面所有阶段都遵守）：
//   - x 向右增长，y 向上增长（数学坐标系，和 OpenGL 一致）
//   - (0,0) 在左下角
//   - 内部存储按行优先，第 0 行是图像最底部一行
// TGA 写出时会按此约定设置图像原点标志位，看图不会上下颠倒。
// ============================================================
class Framebuffer
{
   public:
    Framebuffer(int width, int height, Color fill = colors::black)
        : width_(width), height_(height), pixels_(size_t(width) * height, fill)
    {
        assert(width > 0 && height > 0);
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // 全屏清为某个颜色（实时渲染每帧开头都要做）
    void clear(Color c = colors::black) { std::fill(pixels_.begin(), pixels_.end(), c); }

    // 写一个像素。越界直接忽略——光栅化时三角形可能部分在屏幕外，
    // 这里兜底最简单（更高效的做法是先做裁剪，阶段 7 会讲）。
    void setPixel(int x, int y, Color c)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return;
        pixels_[size_t(y) * width_ + x] = c;
    }

    Color getPixel(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        return pixels_[size_t(y) * width_ + x];
    }

    // 整行原始指针：供光栅化内层循环免边界检查直写（调用方需保证 y 合法）
    Color* row(int y)
    {
        assert(y >= 0 && y < height_);
        return pixels_.data() + size_t(y) * width_;
    }

    const Color* data() const { return pixels_.data(); }

   private:
    int width_;
    int height_;
    std::vector<Color> pixels_;
};
