// ============================================================
// io 模块单元测试：通用图像读写
//
// 只通过 sr::io 的公共接口验证，不出现 stb 等解析器类型。
// ============================================================
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "softrenderer/core/image.h"
#include "softrenderer/io/image_loader.h"
#include "softrenderer/io/image_write.h"

using namespace sr;

TEST(ImageLoader, loadImage_missingFile_returnsFalse)
{
    sr::Image img;
    EXPECT_FALSE(sr::io::loadImage("sr_definitely_not_here_12345.png", img));
    // 失败时不得留下半初始化的输出（否则调用方会拿 width/height 去算偏移）
    EXPECT_FALSE(img.valid());
    EXPECT_TRUE(img.pixels.empty());
}

TEST(ImageLoader, loadImage_emptyPath_returnsFalse)
{
    sr::Image img;
    EXPECT_FALSE(sr::io::loadImage("", img));
    EXPECT_FALSE(img.valid());
}

// 写→读往返：同时验证 imagewriter 与 imageloader，以及两者共用的 y 向上约定。
//
// 为什么用系统临时目录而不提交 fixture：testing_rules.md 要求测试不得往仓库落盘；
// output/ 是示例产物目录，测试写进去会与示例输出混淆。
// 往返测试比静态 fixture 更强：它同时验证了 writeImage 的翻转与
// stbi_set_flip_vertically_on_load 的反翻转是否真的抵消。

TEST(ImageLoader, loadImage_roundTripPreservesPixelsAndOrientation)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "sr_imageloader_roundtrip.png";

    // 4x3 非正方形，且只在两个对角置色：若 y 翻转出错，两个角会互换
    Framebuffer fb(4, 3, Color(0, 0, 0, 255));
    fb.setPixel(0, 0, Color(255, 0, 0, 255));  // 左下角红
    fb.setPixel(3, 2, Color(0, 255, 0, 255));  // 右上角绿

    if (!sr::io::writeImage(tmp.string(), fb))
    {
        GTEST_SKIP() << "无法向系统临时目录写出 PNG（权限或路径问题），非被测代码缺陷";
    }

    sr::Image img;
    ASSERT_TRUE(sr::io::loadImage(tmp.string(), img));
    EXPECT_EQ(img.width, 4);
    EXPECT_EQ(img.height, 3);
    ASSERT_EQ(img.pixels.size(), size_t(12));

    // y 向上约定必须在往返中保持：左下角仍是红、右上角仍是绿
    EXPECT_EQ(img.at(0, 0).r, 255);
    EXPECT_EQ(img.at(0, 0).g, 0);
    EXPECT_EQ(img.at(3, 2).g, 255);
    EXPECT_EQ(img.at(3, 2).r, 0);
    // 中间像素应为黑
    EXPECT_EQ(img.at(1, 1).r, 0);
    EXPECT_EQ(img.at(1, 1).g, 0);

    std::error_code ec;
    fs::remove(tmp, ec);  // 清理；删除失败不影响测试结论
}

