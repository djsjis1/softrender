# 29 · 通用图像加载器（stb_image）

> 图像加载直接用开源库 **stb_image**——一个单头文件支持 PNG/JPG/BMP/TGA/PSD/HDR/GIF 等几乎所有格式。自己写解析器纯属多此一举。

## 一、集成方式

源码在 `third/stb/stb_image.h`（v2.30，从 GitHub 下载）；它的**实现不在同目录**，而是在 `third/tinygltf/tiny_gltf_impl.cpp`（见下方警告）。

CMake 中编入统一的实现库：

```cmake
add_library(third_party_impl STATIC
    third/tinygltf/tiny_gltf_impl.cpp      # ← stb_image + stb_image_write 的实现在这里
    third/tiny_obj_loader_impl.cpp
    src/scene/gltf_loader_impl.cpp
)
target_include_directories(third_party_impl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/stb>
    # … third / third/tinygltf / third/nlohmann
)
```

> ⚠️ **不存在 `third/stb/stb_image_impl.cpp`，也不存在名为 `stb_image` 的 CMake 目标**
> （本文旧版与 `image_loader.h` / `image_write.h` 的头注释都曾这么写，均已修正）。
>
> 真实的实现单元分工与**文件名直觉相反**：
>
> | 实现单元 | `#define` | 提供的符号 |
> |---|---|---|
> | `third/tinygltf/tiny_gltf_impl.cpp` | `STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` | **stb_image + stb_image_write**（不含 tinygltf） |
> | `src/scene/gltf_loader_impl.cpp` | `TINYGLTF_IMPLEMENTATION` | **tinygltf** |
> | `third/tiny_obj_loader_impl.cpp` | `TINYOBJLOADER_IMPLEMENTATION` | **tinyobjloader** |
>
> 教训：判定第三方实现归属时必须读 `.cpp` 源文件，不能依赖文件名、
> 目录名或既有注释推断。详见 `third/README.md`。

## 二、统一接口

```cpp
#include "scene/image_loader.h"

tga::Image img;
if (imageloader::loadImage("texture.png", img)) {
    Texture tex(img);  // 自动建 mipmap
}
```

`loadImage` 内部调用 `stbi_load`，强制 4 通道 RGBA，翻转 Y 轴匹配我们的约定。

### 外部输入校验

图像文件属**不可信外部输入**（`engineering_rules.md`），`loadImage` 做了三层防护：

| 防护 | 说明 |
|---|---|
| 尺寸合法性 | `w <= 0 \|\| h <= 0` 直接返回 false |
| 尺寸上限 | 具名常量 `imageloader::kMaxImageDimension = 16384`，防止损坏/恶意文件头触发巨额 `resize` |
| 像素计数用 `size_t` | 旧版写作 `for (int i = 0; i < w * h; ++i)`，`w * h` 是**有符号 int 乘法**，超过约 46340² 就溢出 → UB（而紧邻上一行的 `resize` 已正确用了 `size_t`，属「改一半」） |

另用 `std::unique_ptr<unsigned char, void(*)(void*)>` 做 **RAII 释放**：
新增的校验分支若提前 return，旧的手动 `stbi_image_free` 会漏放整个图像缓冲。

失败路径会记一条 warn 日志，区分「无法解码」与「尺寸非法」（旧版是静默 `return false`，
调用方无从区分原因）。

> ⚠️ 日志参数可直接传 `std::string`。`core/log.h` 的内置模式已改为变参模板
> （`sr::formatLog`），不再走 C 可变参，因此没有「非 POD 类型经 `...` 传递」的 UB
> （旧版会报 MSVC C4840）。详见 `docs/architecture.md` §6.2。

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
