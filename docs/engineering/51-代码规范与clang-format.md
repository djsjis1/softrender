# 51 · 代码规范与 clang-format

> 企业级库必须有统一的代码风格。本项目提供 `.clang-format` 配置，基于 Google 风格调整。

## 一、使用方式

```bash
# 格式化单个文件
clang-format -i src/core/profiler.h

# 格式化所有源文件
find src/ -name "*.h" -o -name "*.cpp" | xargs clang-format -i

# VS Code 中：保存时自动格式化（settings.json）
"editor.formatOnSave": true,
"C_Cpp.clang_format_style": "file"
```

## 二、核心规则

| 规则 | 值 | 说明 |
|---|---|---|
| 缩进 | 4 空格 | 不用 Tab |
| 列宽 | 120 | 比 Google 的 80 宽，适合渲染代码 |
| 大括号 | Allman 风格 | 函数/类/控制语句后换行 |
| 指针/引用 | 左对齐 | `float* p` 而非 `float *p` |
| Include 排序 | 自动 | 系统头 → 第三方 → 项目头 |
| 命名空间 | 不缩进 | `namespace sr {` 内容不额外缩进 |

## 三、命名约定

| 类别 | 约定 | 示例 |
|---|---|---|
| 类/结构体 | PascalCase | `Framebuffer`, `DepthBuffer` |
| 函数/方法 | camelCase | `drawMesh`, `testAndSet` |
| 成员变量 | snake_case + 下划线后缀 | `width_`, `pixels_` |
| 常量/宏 | UPPER_SNAKE | `SR_PROFILE_SCOPE` |
| 命名空间 | 小写 | `sr`, `gltfloader`, `postprocess` |
| 模板参数 | 单字母或 PascalCase | `T`, `Samples` |

## 四、文件组织

```
src/
├── core/       基础设施（帧缓冲、深度、颜色、日志、profiler）
├── math/       数学（向量、矩阵、四元数、变换）
├── raster/     光栅化（画线、三角形、裁剪、Renderer）
├── scene/      场景（网格、纹理、相机、加载器、ECS 组件）
├── shader/     着色器（基类、Blinn-Phong、PBR、阴影）
└── test/       测试工具
```

每个头文件必须 `#pragma once`，include 顺序：标准库 → 项目头文件。

## 五、注释规范

- 每个文件顶部用 `// ====...====` 块注释说明职责
- 关键算法用数学公式注释（LaTeX 风格）
- 公共 API 用行内注释说明参数含义
- 不用 `/* */` 块注释（除文件头）

---

上一篇：[50 · glTF 加载管线](50-glTF加载管线.md)
