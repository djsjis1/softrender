# 36 · stb_image_write 图像写出

> 图像写出直接用 **stb_image_write**——与 stb_image 配套的写出库，支持 PNG/BMP/TGA/JPG/HDR。

## 一、用法

```cpp
#include "core/image_write.h"
imagewriter::writeImage("output.png", framebuffer);  // 自动按扩展名选格式
imagewriter::writeImage("output.jpg", framebuffer, 95); // JPG 质量 95
```

## 二、支持格式

PNG（无损）、BMP、TGA、JPG（有损）、HDR（浮点）。

## 三、对应文件

| 文件 | 作用 |
|---|---|
| `third/stb/stb_image_write.h` | 头文件 |
| `third/stb/stb_image_write_impl.cpp` | 实现 |
| `src/core/image_write.h` | 统一接口 |

---

上一篇：[35 · tinyobjloader 集成](35-tinyobjloader集成.md)
下一篇：[37 · 四元数与旋转插值](37-四元数与旋转插值.md)
