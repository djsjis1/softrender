// ============================================================
// render-only 外部消费工程
//
// 只请求 SoftRenderer::render：验证「使用核心渲染无需同时接入文件解析、
// 线程或窗口」。本文件不得出现 sr::io / sr::ResourceCache / ThreadPool /
// platform 的任何符号。
// ============================================================
#include <cstdio>
#include <cstdlib>

#include <softrenderer/core/framebuffer.h>
#include <softrenderer/core/image.h>
#include <softrenderer/math/mat3.h>
#include <softrenderer/render/draw2d/renderer2d.h>
#include <softrenderer/resource/texture.h>
#include <softrenderer/version.h>

namespace
{
int fail(const char *what)
{
    std::fprintf(stderr, "[consumer_render] FAILED: %s\n", what);
    return 1;
}
} // namespace

int main()
{
    // 2D 图元 + 视图变换（core + math + render/draw2d），全程无文件 I/O。
    sr::Framebuffer fb(64, 64, sr::Color(0, 0, 0));
    sr::Renderer2D canvas;
    canvas.begin(fb, sr::Color(0, 0, 0));
    canvas.setViewTransform(sr::Mat3::translate(4.0f, 6.0f));
    canvas.setColor(sr::Color(255, 0, 0));
    canvas.fillRect(0.0f, 0.0f, 10.0f, 8.0f);
    canvas.end();

    const sr::Color hit = fb.getPixel(5, 7);
    if (hit.r != 255 || hit.g != 0)
    {
        return fail("Renderer2D::fillRect did not reach the framebuffer");
    }
    if (fb.getPixel(60, 60).r != 0)
    {
        return fail("pixel outside the rect was modified");
    }

    // resource：内存中的纹理，不涉及任何文件读写
    sr::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(size_t(4) * 4, sr::Color(0, 255, 0));
    sr::Texture tex(img);
    if (!tex.valid() || tex.width() != 4)
    {
        return fail("Texture construction from Image failed");
    }
    if (tex.sample(0.5f, 0.5f).y < 0.5f)
    {
        return fail("Texture sampling returned unexpected color");
    }

    if (SOFTRENDERER_VERSION_MAJOR != 2)
    {
        return fail("unexpected major version");
    }

    std::printf("[consumer_render] ok (version %s)\n", SOFTRENDERER_VERSION);
    return 0;
}
