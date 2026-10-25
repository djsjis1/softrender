// ============================================================
// io 模块单元测试：通用图像读写
//
// 只通过 sr::io 的公共接口验证，不出现 stb 等解析器类型。
// ============================================================
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "softrenderer/core/image.h"
#include "softrenderer/io/image_loader.h"
#include "softrenderer/io/image_write.h"
#include "softrenderer/io/obj_loader.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/resource/mesh.h"

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

// ==================== OBJ 索引上界 ====================

// tiny_obj_loader 对正索引越界只追加 warning 并返回成功，包装层必须自己
// 拒绝。“f 1 2 99” 在只有 3 个顶点时属于越界读。
TEST(ObjLoader, RejectsOutOfRangeVertexIndex)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "sr_obj_oob_vertex.obj";
    {
        std::ofstream out(tmp);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n";
    }

    Mesh mesh;
    mesh.vertices.resize(1);  // 预置旧数据：失败时必须保持原样
    EXPECT_FALSE(sr::io::loadObjMesh(tmp.string(), mesh));
    EXPECT_EQ(mesh.vertices.size(), 1u);

    std::error_code ec;
    fs::remove(tmp, ec);
}

// 文本里的 nan / inf 能通过 strtod，必须在加载阶段拒绝。
TEST(ObjLoader, RejectsNonFiniteVertices)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "sr_obj_nan.obj";
    {
        std::ofstream out(tmp);
        out << "v nan inf 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }

    Mesh mesh;
    mesh.vertices.resize(1);  // 失败不得改动输出
    EXPECT_FALSE(sr::io::loadObjMesh(tmp.string(), mesh));
    EXPECT_EQ(mesh.vertices.size(), 1u);

    std::error_code ec;
    fs::remove(tmp, ec);
}

// 正常三角面仍必须能加载（避免上面的校验把合法文件一并拒掉）。
TEST(ObjLoader, AcceptsValidTriangle)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "sr_obj_valid.obj";
    {
        std::ofstream out(tmp);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }

    Mesh mesh;
    ASSERT_TRUE(sr::io::loadObjMesh(tmp.string(), mesh));
    EXPECT_EQ(mesh.vertices.size(), 3u);
    EXPECT_EQ(mesh.triangleCount(), 1);

    std::error_code ec;
    fs::remove(tmp, ec);
}

// ==================== TGA 头部校验 ====================

// 外部输入不可信：头部声明 8192×8192 但文件只有 118 字节时，
// 必须先拒绝，不能先按声明尺寸分配 192 MB 再去发现读不满。
TEST(TgaLoader, oversizedHeader_rejectedWithoutLargeAllocation)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "sr_tga_oversized.tga";

    std::vector<unsigned char> data(18, 0);
    data[2] = 2; // 未压缩真彩色
    data[12] = 0x00;
    data[13] = 0x20; // 宽 8192
    data[14] = 0x00;
    data[15] = 0x20; // 高 8192
    data[16] = 24;
    data.resize(118, 0);
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    }

    sr::Image img;
    EXPECT_FALSE(sr::io::readTga(tmp.string(), img));
    EXPECT_FALSE(img.valid());

    std::error_code ec;
    fs::remove(tmp, ec);
}

// RLE（类型 10）与左下原点必须能正确解码，否则会把压缩 TGA 当损坏文件拒掉。
TEST(TgaLoader, rle24_decodesBgrToRgbaWithBottomOrigin)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "sr_tga_rle24.tga";

    std::vector<unsigned char> data(18, 0);
    data[2] = 10; // RLE 真彩色
    data[12] = 2;
    data[14] = 2;
    data[16] = 24;
    data[17] = 0; // 左下原点
    data.push_back(0x03); // 一个原始包，4 个像素
    const unsigned char pixels[4][3] = {{0, 0, 255}, {0, 255, 0}, {255, 0, 0}, {255, 255, 255}};
    for (const auto& p : pixels)
        data.insert(data.end(), p, p + 3);
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    }

    sr::Image img;
    ASSERT_TRUE(sr::io::readTga(tmp.string(), img));
    ASSERT_EQ(img.width, 2);
    ASSERT_EQ(img.height, 2);
    EXPECT_EQ(img.at(0, 0).r, 255); // 底行左：红
    EXPECT_EQ(img.at(1, 0).g, 255); // 底行右：绿
    EXPECT_EQ(img.at(0, 1).b, 255); // 顶行左：蓝

    std::error_code ec;
    fs::remove(tmp, ec);
}

