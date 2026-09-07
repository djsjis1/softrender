#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include "core/float_framebuffer.h"
#include "math/vec.h"

// ============================================================
// 后处理效果：DOF 景深 + 运动模糊
//
// DOF（Depth of Field）：根据深度对像素做模糊，
//   焦点平面清晰，近/远处模糊。模拟相机光圈效果。
//
// 运动模糊：根据速度向量对像素做方向性模糊。
//   需要前一帧位置或速度缓冲。
//
// 参考：GPU Gems 3 Ch.28 (DOF)、Ch.27 (Motion Blur)
// ============================================================

namespace postprocess {

// FloatFramebuffer 已迁入 namespace sr（ADR-001 第 2 批），而本命名空间仍在全局。
// 在 namespace postprocess 内部的名字查找只会回落到**全局**，不会进入 sr，
// 所以必须显式引入。用 using-**声明**而非 using-directive：只引入确实需要的
// 一个名字，避免把整个 sr 暴露到本命名空间而引入意外歧义。
// （Vec3f / Color 仍在全局，靠回落正常解析，无需引入。）
using sr::FloatFramebuffer;

// ---- DOF 景深 ----
// focusDist: 焦点距离（视图空间 z）
// focusRange: 焦点清晰范围
// maxBlur: 最大模糊半径（像素）
inline void depthOfField(const FloatFramebuffer& color,
                         const std::vector<float>& depth,
                         int W, int H,
                         float focusDist, float focusRange, float maxBlur,
                         FloatFramebuffer& out) {
    out.resize(W, H); // 假设 FloatFramebuffer 有 resize 或者我们直接写 pixels
    // 简化：直接操作 out 的内部数据
    // 这里用简单的 box blur，blur 半径由 CoC (Circle of Confusion) 决定
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            size_t idx = size_t(y) * W + x;
            float d = depth[idx];
            if (d >= 1e8f) {
                // 背景不模糊
                out.setPixel(x, y, color.getPixel(x, y));
                continue;
            }
            // CoC: 离焦点越远，模糊越大
            float coc = std::abs(d - focusDist) / focusRange;
            coc = std::min(coc, 1.0f);
            int radius = int(coc * maxBlur);
            if (radius <= 0) {
                out.setPixel(x, y, color.getPixel(x, y));
                continue;
            }
            // Box blur
            Vec3f sum{0, 0, 0};
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    int nx = std::clamp(x + dx, 0, W - 1);
                    int ny = std::clamp(y + dy, 0, H - 1);
                    sum = sum + color.getPixel(nx, ny);
                    ++count;
                }
            }
            out.setPixel(x, y, sum * (1.0f / count));
        }
    }
}

// ---- 运动模糊（简化版：径向模糊模拟）----
// 以屏幕中心为原点，沿径向方向模糊，模拟相机前进的运动模糊
inline void radialMotionBlur(const FloatFramebuffer& color,
                             int W, int H,
                             float strength, // 0~1
                             int samples,    // 采样数
                             FloatFramebuffer& out) {
    float cx = W * 0.5f, cy = H * 0.5f;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            // 径向方向
            float dx = (x - cx) / float(W);
            float dy = (y - cy) / float(H);
            float dist = std::sqrt(dx * dx + dy * dy);
            float blur = dist * strength;

            Vec3f sum{0, 0, 0};
            for (int s = 0; s < samples; ++s) {
                float t = (float(s) / (samples - 1) - 0.5f) * blur;
                int sx = std::clamp(int(x + dx * t * W), 0, W - 1);
                int sy = std::clamp(int(y + dy * t * H), 0, H - 1);
                sum = sum + color.getPixel(sx, sy);
            }
            out.setPixel(x, y, sum * (1.0f / samples));
        }
    }
}

// ---- 暗角（Vignette）----
inline void vignette(FloatFramebuffer& fb, float strength = 0.3f) {
    int W = fb.width(), H = fb.height();
    float cx = W * 0.5f, cy = H * 0.5f;
    float maxDist = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = (x - cx) / maxDist;
            float dy = (y - cy) / maxDist;
            float d = std::sqrt(dx * dx + dy * dy);
            float factor = 1.0f - d * d * strength;
            factor = std::max(factor, 0.0f);
            Vec3f c = fb.getPixel(x, y);
            fb.setPixel(x, y, c * factor);
        }
    }
}

// ---- 色差（Chromatic Aberration）----
inline void chromaticAberration(const FloatFramebuffer& src,
                                int W, int H, float offset,
                                FloatFramebuffer& out) {
    float cx = W * 0.5f, cy = H * 0.5f;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = (x - cx) / float(W);
            float dy = (y - cy) / float(H);
            // R 通道向外偏移，B 通道向内偏移
            int rx = std::clamp(int(x + dx * offset), 0, W - 1);
            int ry = std::clamp(int(y + dy * offset), 0, H - 1);
            int bx = std::clamp(int(x - dx * offset), 0, W - 1);
            int by = std::clamp(int(y - dy * offset), 0, H - 1);
            Vec3f cr = src.getPixel(rx, ry);
            Vec3f cg = src.getPixel(x, y);
            Vec3f cb = src.getPixel(bx, by);
            out.setPixel(x, y, Vec3f{cr.x, cg.y, cb.z});
        }
    }
}

} // namespace postprocess
