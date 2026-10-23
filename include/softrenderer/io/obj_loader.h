#pragma once
#include <string>

#include "softrenderer/resource/mesh.h"

// ============================================================
// OBJ 模型加载器 —— 使用 tinyobjloader
//
// tinyobjloader 是单头文件的 Wavefront OBJ 解析器，支持 v/vt/vn/f、
// mtllib、groups、负索引、多边形三角化等。
//
// 【分层】tinyobjloader 只在 src/io/obj_loader.cpp 中可见，公共头
// 不暴露任何第三方类型。实现在 src/io/obj_loader.cpp，由
// SoftRenderer::io 静态库提供。
// ============================================================

namespace sr::io
{

// 加载 OBJ 文件到 Mesh（自动三角化、按 (v,vt,vn) 去重、缺法线时
// 计算平滑法线并补切线）。失败返回 false，out 保持不变。
bool loadObjMesh(const std::string &path, Mesh &out);

} // namespace sr::io
