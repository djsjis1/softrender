// ============================================================
// stb 实现单元（第三方实现宏的唯一落点之一）
//
// 【为什么单独一个文件】
//   stb_image / stb_image_write 是"头文件即实现"的库，`STB_*_IMPLEMENTATION`
//   宏只能在整个程序里定义一次，否则会符号重定义或在 ASAN 下报 odr-violation。
//   把它们集中在这里，`third_party_impl` 静态库就是唯一编译这些实现的单元。
//
// 【命名】文件名即实现的库，不再用 "tiny_gltf_impl" 之类与实际内容不符的名字。
//
// 【可见性】本文件只属于 softrenderer_io 的实现，不安装；公共头不得出现 stb 符号
//   （`src/io/stb_bridge.h` 提供私有 C 声明）。
// ============================================================
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
