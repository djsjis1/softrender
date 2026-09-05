# 29 · 通用图像加载器（stb_image）

> 图像加载直接用开源库 **stb_image**——一个单头文件支持 PNG/JPG/BMP/TGA/PSD/HDR/GIF 等几乎所有格式。自己写解析器纯属多此一举。

## 一、集成方式

源码在 `third/stb/stb_image.h`（从 GitHub 下载），实现在 `third/stb/stb_image_impl.cpp`。

CMake 中编译为静态库：

```cmake
add_library(stb_image STATIC third/stb/stb_image_impl.cpp)
target_include_directories(stb_image PUBLIC third/stb)
```

## 二、统一接口

```cpp
#include "scene/image_loader.h"

tga::Image img;
if (imageloader::loadImage("texture.png", img)) {
    Texture tex(img);  // 自动建 mipmap
}
```

`loadImage` 内部调用 `stbi_load`，强制 4 通道 RGBA，翻转 Y 轴匹配我们的约定。

## 三、支持格式

PNG, JPEG, BMP, TGA, PSD, GIF, HDR, PIC, PNM (PPM/PGM) —— stb_image 全覆盖。

## 四、对应文件

| 文件 | 作用 |
|---|---|
| `third/stb/stb_image.h` | stb_image 头文件 |
| `third/stb/stb_image_impl.cpp` | 实现（仅此一处 `#define STB_IMAGE_IMPLEMENTATION`） |
| `src/scene/image_loader.h` | 统一加载接口（薄包装） |

---

上一篇：[28 · 第三方库管理策略](28-第三方库管理策略.md)
下一篇：[30 · 性能基准框架](30-性能基准框架.md)
