#pragma once

// ============================================================
// SoftRenderer 的命名空间约定（本文件不含任何类型定义）
//
// 【文件职责】
//   记录公共符号的归属约定，作为 2.0.0 命名空间收敛的唯一说明入口。
//   需要引用该约定的文档与测试从这里取，不要各自复述一份。
//
// 【正式用法】
//   #include <softrenderer/core/framebuffer.h>
//
//   sr::Framebuffer fb(640, 480);             // 公共类：sr
//   sr::draw2d::fillRect(fb, 10, 10, 40, 20); // 2D 图元：sr::draw2d
//   sr::io::writeTga("out.tga", img);         // 文件读写：sr::io
//
//   注意 Framebuffer 在全局命名空间里并不存在：下游可以自由定义自己的
//   Framebuffer / Mesh / Texture / Camera 而不会与本库冲突。
//
// 【顶层命名空间】
//   sr —— 全部公共类、结构体、枚举与自由函数（render / scene / resource / core / math）
//
// 【具名子命名空间（功能分组，仍然在 sr 之内）】
//   sr::draw2d      2D 图元、位图合成、Renderer2D
//   sr::raster      快速/增量光栅化实现（rasterizer_fast.h）
//   sr::postprocess FXAA、HDR/Bloom、SSAO 等后处理自由函数
//   sr::animation   动画采样（NodePose / sampleAnimation）
//   sr::keyframe    skeleton.h 的简化欧拉角关键帧动画
//   sr::io          TGA/图像/OBJ/glTF 加载与写出
//   sr::colors      常用颜色常量
//   sr::transform   投影与观察矩阵构造函数
//   sr::meshgen     程序化网格生成
//   sr::texgen      程序化纹理生成
//
// 【规则】
//   1. 禁止在全局命名空间新增类型、枚举或自由函数；新符号一律放入 sr 或上述子命名空间。
//   2. 禁止用 `using sr::X;` 之类的别名桥把名字重新注入全局 —— 它解决不了撞名。
//   3. 文件格式细节（tinygltf / tinyobj / stb）只在 src/io/ 的实现里出现，
//      公共头不得暴露解析器类型。
//
// 【门禁】tests/architecture/namespace_sentinel.cpp 在全局作用域先声明
//   Framebuffer / Mesh / Texture / Camera / Shader 等同名类型，再包含全部公共头。
//   任何回归到全局命名空间（或改用别名桥）的改动都会在该文件编译失败。
//
// 历史：ADR-001 记录了收敛策略，ADR-004 记录了 2.0.0 的模块边界与最终落地结果。
// ============================================================

namespace sr
{
    // 本命名空间的内容由各模块头文件分别填充（namespace 可跨文件重开）。
    // 此处保持为空，仅作为约定的锚点与文档载体。
}
