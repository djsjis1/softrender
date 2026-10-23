/** @file main.cpp
 *  @brief 仅请求 core 组件的下游程序。
 *
 *  验证 find_package(SoftRenderer COMPONENTS core) 的依赖闭包自洽：
 *  只用 core 公共头（几何缓冲 / 颜色 / 重心坐标），不触碰 io、runtime 或线程。
 */
#include <softrenderer/version.h>

#include <cmath>

#include "softrenderer/core/color.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/image.h"
#include "softrenderer/math/barycentric.h"

using namespace sr;

static_assert(SOFTRENDERER_VERSION_MAJOR == 2);

int main()
{
    Framebuffer fb(4, 4, sr::colors::black);
    fb.setPixel(1, 2, sr::colors::red);
    if (fb.getPixel(1, 2).r != 255)
        return 1;

    sr::Image image;
    image.width = 2;
    image.height = 1;
    image.pixels.assign(2, Color(0, 255, 0, 255));
    if (!image.valid())
        return 2;

    const Vec3f weights = sr::barycentric({0, 0}, {2, 0}, {0, 2}, {0.5f, 0.5f});
    if (std::abs(weights.x - 0.5f) > 1e-5f || std::abs(weights.y - 0.25f) > 1e-5f)
        return 3;
    return 0;
}
