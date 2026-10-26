#pragma once
#include <cassert>
#include <cstddef>

#include "softrenderer/core/color.h"

// ============================================================
// BasicFramebuffer：帧缓冲，本质就是一块 width * height 的像素数组。
//
// 【坐标约定（非常重要，后面所有阶段都遵守）】
//   - x 向右增长，y 向上增长（数学坐标系，和 OpenGL 一致）
//   - (0,0) 在左下角
//   - 内部存储按行优先，第 0 行是图像最底部一行
// TGA 写出时会按此约定设置图像原点标志位，看图不会上下颠倒。
//
// 【为什么是模板】像素存储格式（RGBA8888 / RGB565 / 灰度）与渲染逻辑正交：
// 前者决定"一个像素占几字节"，后者只关心"往 (x,y) 写一个颜色"。
// 参数化之后，格式进入类型 ⇒ 每像素零分支、零虚调用，且只有实际用到的
// 格式才会被实例化（裸机上不会把多套光栅化代码同时塞进 flash）。
//
// 【对外仍是单一名字】`Framebuffer` 是 BasicFramebuffer<Color> 的别名，
// 既有调用点（22 个示例、io 写出、后处理、MSAA）全部无需改动。
// 其他格式：`BasicFramebuffer<Color565>`、`BasicFramebuffer<Gray8>`
// （见 core/pixel_format.h）。
//
// 【PixelT 契约】kBytes / fromColor / fromFloat / toColor / operator==，
// 详见 core/pixel_format.h 的头注释。
//
// 【写入路径】setPixel / clear 接受 Color（运算域），内部在写入那一刻
// 量化成 PixelT —— 这是全库唯一的量化点。
// ============================================================
namespace sr
{

template <class PixelT = Color>
class BasicFramebuffer
{
   public:
    using Pixel = PixelT;

    // 初始化列表必须与成员声明顺序一致（pixels_ → width_ → height_ → owns_），
    // 否则 GCC/Clang 会以 -Wreorder -Werror 拒绝编译。
    //
    // 分配放在构造函数体内而非初始化列表：参数非法的断言必须先执行，
    // 否则 size_t(负数) 会先膨胀成天文数字并在 new 处抛 bad_alloc。
    BasicFramebuffer(int width, int height, const Color& fill = colors::black)
        : pixels_(nullptr), width_(width), height_(height), owns_(true)
    {
        assert(width > 0 && height > 0);
        pixels_ = new PixelT[size_t(width) * height];
        clear(fill);
    }

    // 外部存储（非拥有）：裸机/嵌入式把像素数组放进静态内存、内存池或
    // 指定 RAM 段，构造路径不经堆。storage 必须能容纳 width*height 个
    // PixelT（按 alignof(PixelT) 对齐），且生命周期覆盖本对象。
    // 不做任何填充：初始内容由调用方随后 clear() 决定。
    BasicFramebuffer(int width, int height, PixelT* storage)
        : pixels_(storage), width_(width), height_(height), owns_(false)
    {
        assert(width > 0 && height > 0);
        assert(storage != nullptr);
    }

    ~BasicFramebuffer()
    {
        if (owns_)
            delete[] pixels_;
    }

    // 禁止拷贝，允许移动（外部存储对象可移动：只转移指针与所属权）
    BasicFramebuffer(const BasicFramebuffer&) = delete;
    BasicFramebuffer& operator=(const BasicFramebuffer&) = delete;
    BasicFramebuffer(BasicFramebuffer&& other) noexcept
        : pixels_(other.pixels_), width_(other.width_), height_(other.height_), owns_(other.owns_)
    {
        other.pixels_ = nullptr;
        other.width_ = other.height_ = 0;
        other.owns_ = false;
    }
    BasicFramebuffer& operator=(BasicFramebuffer&& other) noexcept
    {
        if (this != &other)
        {
            if (owns_)
                delete[] pixels_;
            pixels_ = other.pixels_;
            width_ = other.width_;
            height_ = other.height_;
            owns_ = other.owns_;
            other.pixels_ = nullptr;
            other.width_ = other.height_ = 0;
            other.owns_ = false;
        }
        return *this;
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // 单个像素的存储字节数；整块缓冲的字节数为 width * height * bytesPerPixel()。
    static constexpr size_t bytesPerPixel() { return PixelT::kBytes; }
    size_t byteSize() const { return size_t(width_) * size_t(height_) * PixelT::kBytes; }

    // 全屏清为某个颜色（实时渲染每帧开头都要做）。
    // 量化在循环外做一次，避免每个像素重复换算。
    void clear(const Color& c = colors::black)
    {
        const PixelT stored = PixelT::fromColor(c);
        const size_t n = size_t(width_) * size_t(height_);
        for (size_t i = 0; i < n; ++i)
            pixels_[i] = stored;
    }

    // 写一个像素（接受运算域颜色，内部量化）。越界直接忽略。
    void setPixel(int x, int y, const Color& c)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return;
        pixels_[size_t(y) * width_ + x] = PixelT::fromColor(c);
    }

    // 读一个像素，统一还原成 RGBA8888。越界返回黑色。
    Color getPixel(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return colors::black;
        return pixels_[size_t(y) * width_ + x].toColor();
    }

    // 整行原始指针：供光栅化内层循环免边界检查直写。
    // 【调用契约】Release 下不做检查，调用方必须保证 0 <= y < height()，
    // 且横向访问限制在 [0, width()) 内（由 renderer.h 的包围盒 clamp 保证）。
    PixelT* row(int y)
    {
        assert(y >= 0 && y < height_);
        return pixels_ + size_t(y) * width_;
    }
    const PixelT* row(int y) const
    {
        assert(y >= 0 && y < height_);
        return pixels_ + size_t(y) * width_;
    }

    PixelT* data() { return pixels_; }
    const PixelT* data() const { return pixels_; }

   private:
    PixelT* pixels_;
    int width_;
    int height_;
    // true = 构造时自行分配并在析构释放；false = 外部存储，析构不释放。
    bool owns_;
};

// 默认格式：RGBA8888。既有代码全部通过这个别名使用帧缓冲。
using Framebuffer = BasicFramebuffer<Color>;

}  // namespace sr
