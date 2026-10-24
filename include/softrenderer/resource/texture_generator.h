#pragma once
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "softrenderer/core/image.h"
#include "softrenderer/math/vec.h"

// ============================================================
// 程序化纹理：不依赖外部图片也能演示纹理映射
// ============================================================
namespace sr::texgen
{

    // 棋盘格：checks×checks 个格子，两色交替
    inline sr::Image checkerboard(int size = 256, int checks = 8,
                                   Color a = Color(230, 230, 230),
                                   Color b = Color(40, 40, 40))
    {
        sr::Image img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        int cell = size / checks;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                bool odd = ((x / cell) + (y / cell)) & 1;
                img.pixels[size_t(y) * size + x] = odd ? a : b;
            }
        return img;
    }

    // 彩色 UV 网格：便于观察 UV 走向（红=u，绿=v）+ 黑色网格线
    inline sr::Image uvGrid(int size = 256, int lines = 10)
    {
        sr::Image img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        int step = size / lines;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                uint8_t r = uint8_t(x * 255 / size);
                uint8_t g = uint8_t(y * 255 / size);
                Color c(r, g, 60);
                if (x % step == 0 || y % step == 0)
                    c = Color(20, 20, 20); // 网格线
                img.pixels[size_t(y) * size + x] = c;
            }
        return img;
    }

    // 程序化法线贴图（切线空间）：用半球凸起的高度场求法线并编码为 RGB。
    //
    // 法线贴图约定：每个纹素存一个切线空间法线 (nx,ny,nz)，
    // 编码为颜色 = (n * 0.5 + 0.5) * 255。平坦区域法线 (0,0,1)
    // 对应经典的淡蓝紫色 (128,128,255)。
    //
    // 这里用 cells×cells 个圆形凸起（半球）拼成，凸起处法线向外偏，
    // 光照下会看到明显的凹凸颗粒感。
    inline sr::Image normalMapBumps(int size = 256, int cells = 8, float strength = 1.0f)
    {
        sr::Image img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        float cell = float(size) / cells;
        float radius = cell * 0.42f;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                float cx = (std::floor(x / cell) + 0.5f) * cell;
                float cy = (std::floor(y / cell) + 0.5f) * cell;
                float dx = x - cx, dy = y - cy;
                float d = std::sqrt(dx * dx + dy * dy);
                Vec3f n{0, 0, 1};
                if (d < radius)
                {
                    // 半球面：z = sqrt(r^2 - d^2)，法线 = 归一化(dx,dy,z)
                    float z = std::sqrt(std::max(0.0f, radius * radius - d * d));
                    n = Vec3f{dx * strength, dy * strength, z}.normalized();
                }
                img.pixels[size_t(y) * size + x] =
                    Color(uint8_t((n.x * 0.5f + 0.5f) * 255.0f),
                          uint8_t((n.y * 0.5f + 0.5f) * 255.0f),
                          uint8_t((n.z * 0.5f + 0.5f) * 255.0f));
            }
        return img;
    }

} // namespace sr::texgen
