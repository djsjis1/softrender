#pragma once
#include <cassert>
#include <cstddef>

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
        : width_(width), height_(height), pixels_(nullptr)
    {
        assert(width > 0 && height > 0);
        pixels_ = new Color[size_t(width) * height];
        clear(fill);
    }

    ~Framebuffer() { delete[] pixels_; }

    // 禁止拷贝，允许移动
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept
        : pixels_(other.pixels_), width_(other.width_), height_(other.height_)
    {
        other.pixels_ = nullptr;
        other.width_ = other.height_ = 0;
    }
    Framebuffer& operator=(Framebuffer&& other) noexcept
    {
        if (this != &other) { delete[] pixels_; pixels_ = other.pixels_; width_ = other.width_; height_ = other.height_; other.pixels_ = nullptr; other.width_ = other.height_ = 0; }
        return *this;
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // 全屏清为某个颜色（实时渲染每帧开头都要做）
    void clear(Color c = colors::black)
    {
        const size_t n = size_t(width_) * height_;
        for (size_t i = 0; i < n; ++i)
            pixels_[i] = c;
    }

    // 写一个像素。越界直接忽略
    void setPixel(int x, int y, Color c)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return;
        pixels_[size_t(y) * width_ + x] = c;
    }

    // 读一个像素。越界返回 colors::black。
    Color getPixel(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return colors::black;
        return pixels_[size_t(y) * width_ + x];
    }

    // 整行原始指针：供光栅化内层循环免边界检查直写。
    Color* row(int y)
    {
        assert(y >= 0 && y < height_);
        return pixels_ + size_t(y) * width_;
    }

    Color* data() { return pixels_; }
    const Color* data() const { return pixels_; }

   private:
    Color* pixels_;
    int width_;
    int height_;
};
