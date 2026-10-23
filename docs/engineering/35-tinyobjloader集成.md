# 35 · tinyobjloader 集成

> OBJ 模型解析不是渲染原理，是文件格式解析。直接用开源库 **tinyobjloader**——单头文件，性能极好，支持完整 OBJ 规范。

## 一、集成

源码 `third/tiny_obj_loader.h`，实现 `src/io/tinyobj_impl.cpp`。

## 二、接口

```cpp
#include <softrenderer/scene/obj_loader.h>
Mesh mesh;
if (objloader::load("model.obj", mesh)) { /* 使用 mesh */ }
```

内部调用 tinyobjloader 的 `ObjReader::ParseFromFile`，自动三角化多边形，转换为我们 `Mesh` 结构。

## 三、比自己写的优势

- 支持 mtllib/usemtl（材质库）
- 支持负索引、多边形自动三角化
- 错误报告更完善
- 性能更好（C 风格解析，无 map 查找开销）

---

上一篇：[34 · 骨骼动画与顶点蒙皮](../advanced/34-骨骼动画与顶点蒙皮.md)
下一篇：[36 · stb_image_write 图像写出](36-stb_image_write图像写出.md)
