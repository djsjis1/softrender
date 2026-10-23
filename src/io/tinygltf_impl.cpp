// ============================================================
// tinygltf 实现单元
//
// 【为什么单独一个文件】`TINYGLTF_IMPLEMENTATION` 只能定义一次；集中到本单元后，
//   `third_party_impl` 是唯一的编译落点，避免多 TU 重复定义。
//
// 【依赖】tinygltf 的图像解码走 stb（实现见 src/io/stb_impl.cpp），
//   两者同在 third_party_impl，因此这里只展开 tinygltf 自身的实现。
//
// 【可见性】src/io/ 私有实现，不安装；公共头不出现 tinygltf 类型。
// ============================================================
#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"
