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

    // 读一个像素。越界返回 colors::black。
    //
    // 【为什么读路径也要兜底】setPixel 的注释已说明越界是预期会发生的正常情况
    // （光栅化时三角形常部分在屏幕外）。既然写路径会越界，读路径同样会。
    // 而 assert 在 Release（NDEBUG）下被编译掉，届时越界读是**未定义行为**。
    // 保留 assert 供 Debug 期发现逻辑错误，同时用运行时兜底消除 Release 下的 UB。
    //
    // 【为什么不抛异常】cpp_project_rules.md 要求「项目统一选择一种错误处理
    // 策略，禁止混用」。本项目现有代码全是「返回 bool / 静默兜底」风格，无异常使用。
    //
    // 【性能】getPixel 不在光栅化热路径上（热路径走 row(y) 直写），
    // 多一次分支的代价可忽略。
    Color getPixel(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return colors::black;
        return pixels_[size_t(y) * width_ + x];
    }

    // 整行原始指针：供光栅化内层循环免边界检查直写。
    //
    // 【调用契约】Release 下**不做任何检查**，调用方必须保证 0 <= y < height()，
    // 且对返回指针的横向访问必须限制在 [0, width()) 内。违反则越界写内存。
    // renderer.h 第 102-108 行通过把包围盒 clamp 到帧缓冲尺寸来满足此契约；
    // 修改包围盒计算或行指针用法时必须重新确认 clamp 仍然生效
    // （已录入 engineering_rules.md 红线约束）。
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
