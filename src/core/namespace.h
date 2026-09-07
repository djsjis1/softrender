#pragma once

// ============================================================
// SoftRenderer 公共符号的目标命名空间
//
// 【文件职责】
//   声明 `namespace sr` 为渲染器本体全部公共符号的目标命名空间，
//   并记录迁移约定与当前进度。本文件不含任何类型定义。
//
// 【为什么需要它】
//   本项目是 header-only 库，头文件里的每个符号都会直接进入使用者的编译单元。
//   实测有 60 个顶层类型位于全局命名空间，其中 Position / Scale / Light /
//   Animation / Texture / Mesh / Vertex / Color / Camera 这类领域通用名，
//   下游使用者几乎必然已经定义过 → 编译期重定义硬错误，无法绕过。
//
//   这不是理论风险：`namespace sr2d` 的引入原因，正是 2D 图元名
//   （fillRect / fillCircle / SpritePalette）与 examples/ 中已有的全局同名符号
//   发生了真实冲突。
//
// 【迁移策略】见 docs/adr/ADR-001-namespace-convergence.md
//   按**引用面大小**分类，而非一刀切：
//     - 引用面小的类型 → 直接完整迁移，不留全局别名（立即消除撞名）
//     - 引用面大的类型 → 迁移后临时保留 `using sr::X;` 别名，按模块分批删除
//   关键认识：`using sr::Position;` 仍然把 Position 注入全局命名空间，
//   **别名桥不解决撞名问题**，它只是让大改动可以分批提交的工具，
//   不是对下游的兼容承诺。因此对高冲突名字不使用别名桥。
//
// 【已有的其他命名空间】不需要迁移，它们本来就不在全局作用域：
//   sr2d（2D 图元与合成）、colors（颜色常量）、transform（投影变换）、
//   tga / imagewriter / imageloader（图像 I/O）、meshgen / texgen（资源生成）、
//   objloader / gltfloader（模型加载）、raster_fast（实验性光栅化）、
//   fxaa / postprocess（后处理）、regression（测试工具）、
//   sr 内已有的 ArenaAllocator / Profiler / ResourceManager / Log / formatLog
//
// 【使用方式】
//   #include "core/namespace.h"     // 需要引用迁移约定时
//   #include "scene/components.h"   // 直接用类型时，各头文件自行保证 sr 已声明
//
//   sr::Position p;                 // 迁移完成的类型
//   Framebuffer fb(640, 480);       // 尚未迁移的类型（仍在全局作用域）
// ============================================================

namespace sr
{
    // 本命名空间的内容由各模块头文件分别填充（namespace 可跨文件重开）。
    // 此处保持为空，仅作为约定的锚点与文档载体。
}
