#pragma once
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "softrenderer/core/image.h"
#include "softrenderer/math/vec.h"

// ============================================================
// 程序化纹理：不依赖外部图片也能演示纹理映射
// ============================================================
namespace sr::texgen
{

    // 棋盘格：checks×checks 个格子，两色交替。
    // checks>size 时格子不足一格，cell 夹到 1 保证不除零；size/checks 非正
    // 时返回空图像（valid()==false），由调用方按无效纹理处理。
    inline sr::Image checkerboard(int size = 256, int checks = 8,
                                   Color a = Color(230, 230, 230),
                                   Color b = Color(40, 40, 40))
    {
        sr::Image img;
        if (size <= 0 || checks <= 0)
            return img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        const int cell = std::max(1, size / checks);
        // 每列奇偶位预先算好：原来逐像素做 x/cell，2048² 就是四百万次整数除法
        // 注意：不能写 colOdd(size_t(size))——那是函数声明（most vexing parse）。
        std::vector<uint8_t> colOdd(static_cast<size_t>(size));
        for (int x = 0; x < size; ++x)
            colOdd[size_t(x)] = uint8_t((x / cell) & 1);
        for (int y = 0; y < size; ++y)
        {
            const uint8_t rowOdd = uint8_t((y / cell) & 1);
            Color *dstRow = img.pixels.data() + size_t(y) * size_t(size);
            for (int x = 0; x < size; ++x)
                dstRow[x] = (rowOdd ^ colOdd[size_t(x)]) ? a : b;
        }
        return img;
    }

    // 彩色 UV 网格：便于观察 UV 走向（红=u，绿=v）+ 黑色网格线
    inline sr::Image uvGrid(int size = 256, int lines = 10)
    {
        sr::Image img;
        if (size <= 0 || lines <= 0)
            return img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        const int step = std::max(1, size / lines);
        // 每列红分量与每行绿分量预先算好：避开逐像素的 x*255/size 除法
        std::vector<uint8_t> red(static_cast<size_t>(size)), green(static_cast<size_t>(size));
        std::vector<uint8_t> lineAt(static_cast<size_t>(size));
        for (int i = 0; i < size; ++i)
        {
            red[size_t(i)] = uint8_t(i * 255 / size);
            green[size_t(i)] = red[size_t(i)];
            lineAt[size_t(i)] = uint8_t(i % step == 0 ? 1 : 0);
        }
        for (int y = 0; y < size; ++y)
        {
            const bool rowIsLine = lineAt[size_t(y)] != 0;
            Color *dstRow = img.pixels.data() + size_t(y) * size_t(size);
            for (int x = 0; x < size; ++x)
                dstRow[x] = (rowIsLine || lineAt[size_t(x)] != 0) ? Color(20, 20, 20)
                                                                 : Color(red[size_t(x)], green[size_t(y)], 60);
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
        if (size <= 0 || cells <= 0 || !std::isfinite(strength))
            return img;
        img.width = size;
        img.height = size;
        img.pixels.resize(size_t(size) * size);
        float cell = float(size) / float(cells);
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
