#pragma once
// ============================================================
// 【使用层 / 平台相关】
// 本文件不属于渲染器本体（src/），而是"如何把渲染结果显示出来"
// 的一个平台后端。渲染器只产出内存里的 Framebuffer；要不要开窗、
// 用什么 API 开窗，都是使用层的自由。
//
// 想移植到别的平台：照此文件的公开接口
//   (构造, pumpMessages, present, setTitle, keyDown, consumeMouseDelta)
// 另写一个 SDL2Window / GLFWWindow / X11Window 即可，渲染器一行不改。
// ============================================================
#ifdef _WIN32
// windows.h 默认会定义 min/max 宏，会破坏 std::min/std::max({...})。
// 定义 NOMINMAX 禁用它；WIN32_LEAN_AND_MEAN 精简包含、加快编译。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "core/framebuffer.h"

// ============================================================
// Win32Window：用纯 Win32 + GDI 显示 framebuffer 的最小实时窗口。
//
// 原理：软渲染器把画面画进内存里的 Framebuffer，我们再用
// SetDIBitsToDevice 把这块像素一次性"贴"到窗口客户区。
//
// 两个坐标系对齐要点：
//   - 我们的 Framebuffer 第 0 行在底部（y 向上）
//   - Windows DIB 当 biHeight > 0 时是"自底向上"，第 0 行也在底部
//   两者一致，直接喂数据即可，无需翻转。
//   - DIB 像素是 BGRA（32 位），需从我们的 RGBA 转一下字节序。
//
// 输入：键盘用 GetAsyncKeyState 在主循环轮询；鼠标用 WndProc
//   累积左键拖拽的位移，供相机 look 使用。
// ============================================================
class Win32Window
{
public:
    Win32Window(int width, int height, const std::string &title)
        : width_(width), height_(height), bgra_(size_t(width) * height * 4)
    {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        WNDCLASSW wc = {};
        wc.lpfnWndProc = &Win32Window::wndProcThunk;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SoftRendererWindow";
        RegisterClassW(&wc);

        // 根据客户区目标尺寸反推窗口外框尺寸
        RECT rc{0, 0, width, height};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        std::wstring wtitle(title.begin(), title.end());
        hwnd_ = CreateWindowW(wc.lpszClassName, wtitle.c_str(), WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                              rc.bottom - rc.top, nullptr, nullptr, hInst, this);
        ShowWindow(hwnd_, SW_SHOW);
        hdc_ = GetDC(hwnd_);

        // 预填 DIB 头
        bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi_.bmiHeader.biWidth = width;
        bmi_.bmiHeader.biHeight = height; // 正数 = 自底向上，匹配我们的约定
        bmi_.bmiHeader.biPlanes = 1;
        bmi_.bmiHeader.biBitCount = 32;
        bmi_.bmiHeader.biCompression = BI_RGB;
    }

    ~Win32Window()
    {
        if (hdc_)
            ReleaseDC(hwnd_, hdc_);
        if (hwnd_)
            DestroyWindow(hwnd_);
    }

    bool isOpen() const { return !shouldClose_; }

    // 处理消息队列，返回是否继续运行
    bool pumpMessages()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                shouldClose_ = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return !shouldClose_;
    }

    // 把 framebuffer 贴到窗口
    void present(const Framebuffer &fb)
    {
        const Color *src = fb.data();
        size_t n = size_t(width_) * height_;
        for (size_t i = 0; i < n; ++i)
        {
            // RGBA -> BGRA
            bgra_[i * 4 + 0] = src[i].b;
            bgra_[i * 4 + 1] = src[i].g;
            bgra_[i * 4 + 2] = src[i].r;
            bgra_[i * 4 + 3] = 255;
        }
        SetDIBitsToDevice(hdc_, 0, 0, width_, height_, 0, 0, 0, height_,
                          bgra_.data(), &bmi_, DIB_RGB_COLORS);
    }

    void setTitle(const std::string &title)
    {
        std::wstring w(title.begin(), title.end());
        SetWindowTextW(hwnd_, w.c_str());
    }

    // 键盘轮询（VK_* 或 'W' 之类的字符码）
    static bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    // 取出并清零累计的鼠标拖拽位移（仅左键按下时累积）
    void consumeMouseDelta(int &dx, int &dy)
    {
        dx = mouseDX_;
        dy = mouseDY_;
        mouseDX_ = 0;
        mouseDY_ = 0;
    }

private:
    static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        Win32Window *self = nullptr;
        if (msg == WM_NCCREATE)
        {
            auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            self = reinterpret_cast<Win32Window *>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<Win32Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self)
            return self->wndProc(hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_DESTROY:
            shouldClose_ = true;
            PostQuitMessage(0);
            return 0;
        case WM_LBUTTONDOWN:
            dragging_ = true;
            lastX_ = GET_X_LPARAM(lp);
            lastY_ = GET_Y_LPARAM(lp);
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            dragging_ = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (dragging_)
            {
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                mouseDX_ += x - lastX_;
                mouseDY_ += y - lastY_;
                lastX_ = x;
                lastY_ = y;
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // GET_X_LPARAM 在 windowsx.h，这里自己展开避免多包含头
    static int GET_X_LPARAM(LPARAM lp) { return (int)(short)LOWORD(lp); }
    static int GET_Y_LPARAM(LPARAM lp) { return (int)(short)HIWORD(lp); }

    int width_, height_;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    BITMAPINFO bmi_ = {};
    std::vector<uint8_t> bgra_;
    bool shouldClose_ = false;

    // 鼠标状态
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    int mouseDX_ = 0, mouseDY_ = 0;
};
#endif // _WIN32
