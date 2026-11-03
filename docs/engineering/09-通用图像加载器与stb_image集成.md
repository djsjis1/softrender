# 10 · 图像读写：stb_image / stb_image_write 集成

> 图像加载与写出直接用开源库 **stb_image** 与 **stb_image_write**——单头文件支持 PNG/JPG/BMP/TGA/PSD/HDR/GIF 等几乎所有格式。自己写解析器纯属多此一举。

## 一、集成方式

源码在 `third/stb/stb_image.h` 与 `third/stb/stb_image_write.h`；它们的**实现不在同目录**，而在 `src/io/stb_impl.cpp`（见下方警告）。

CMake 中编入统一的实现库：

```cmake
add_library(third_party_impl STATIC
    src/io/stb_impl.cpp      # ← stb_image + stb_image_write 的实现在这里
    src/io/tinyobj_impl.cpp
    src/io/tinygltf_impl.cpp
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
> | `src/io/stb_impl.cpp` | `STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` | **stb_image + stb_image_write**（不含 tinygltf） |
> | `src/io/tinygltf_impl.cpp` | `TINYGLTF_IMPLEMENTATION` | **tinygltf** |
> | `src/io/tinyobj_impl.cpp` | `TINYOBJLOADER_IMPLEMENTATION` | **tinyobjloader** |
>
> 教训：判定第三方实现归属时必须读 `.cpp` 源文件，不能依赖文件名、目录名或既有注释推断。详见 `third/README.md`。

## 二、加载接口

```cpp
#include <softrenderer/io/image_loader.h>

tga::Image img;
std::string error;
if (imageloader::loadImage("texture.png", img, &error)) {
    Texture tex(img);  // 自动建 mipmap
}
```

`loadImage` 内部调用 `stbi_load`，强制 4 通道 RGBA，翻转 Y 轴匹配项目约定。失败时若传入 `error` 指针则写入含路径的可读原因。

### 外部输入校验

图像文件属**不可信外部输入**，`loadImage` 做了三层防护：

| 防护 | 说明 |
|---|---|
| 尺寸合法性 | `w <= 0 \|\| h <= 0` 直接返回 false |
| 尺寸上限 | 具名常量 `imageloader::kMaxImageDimension = 16384`，防止损坏/恶意文件头触发巨额 `resize` |
| 像素计数用 `size_t` | 旧版写作 `for (int i = 0; i < w * h; ++i)`，`w * h` 是**有符号 int 乘法**，超过约 46340² 就溢出 → UB |

另用 `std::unique_ptr<unsigned char, void(*)(void*)>` 做 **RAII 释放**：新增的校验分支若提前 return，手动 `stbi_image_free` 会漏放整个图像缓冲。

失败路径会记一条 warn 日志，区分「无法解码」与「尺寸非法」。

## 三、写出接口

```cpp
#include <softrenderer/io/image_write.h>
imagewriter::writeImage("output.png", framebuffer);     // 自动按扩展名选格式
imagewriter::writeImage("output.jpg", framebuffer, 95); // JPG 质量 95
```

写出同样返回 bool，可选 `std::string* error` 获取原因。

## 四、支持格式

| 方向 | 格式 |
|---|---|
| 读（stb_image） | PNG、JPEG、BMP、TGA、PSD、GIF、HDR、PIC、PNM (PPM/PGM) |
| 写（stb_image_write） | PNG（无损）、BMP、TGA、JPG（有损）、HDR（浮点） |

## 五、对应文件

| 文件 | 作用 |
|---|---|
| `third/stb/stb_image.h` | stb_image 头文件 |
| `third/stb/stb_image_write.h` | stb_image_write 头文件 |
| `src/io/stb_impl.cpp` | 两者实现（`STB_IMAGE_IMPLEMENTATION` / `STB_IMAGE_WRITE_IMPLEMENTATION` 在此处） |
| `include/softrenderer/io/image_loader.h` | 统一加载接口（薄包装） |
| `include/softrenderer/io/image_write.h` | 统一写出接口 |
