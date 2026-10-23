// ============================================================
// tinyobjloader 实现单元
//
// 【为什么单独一个文件】`TINYOBJLOADER_IMPLEMENTATION` 只能定义一次；
//   集中到本单元，`third_party_impl` 是唯一编译落点。
//
// 【可见性】src/io/ 私有实现，不安装；公共头不出现 tinyobj 类型
//   （`sr::io::loadObjMesh` 返回自有 `sr::Mesh` 与错误文本）。
// ============================================================
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
