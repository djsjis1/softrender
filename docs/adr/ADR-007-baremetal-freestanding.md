# ADR-007：裸机（freestanding）支持策略

- 日期：2026-10-26
- 状态：已采纳

## 背景与选择

目标设备包含无操作系统的环境（STM32 + ILI9341/ST7789 直驱是 ADR-005 就
明确的场景）。2.0 分层后 core/render/scene 已是 header-only 且不依赖
io/runtime/platform，但仍有两处宿主假设：缓冲类构造必然堆分配
（`new[]` / `std::vector`），且 CMake 层没有异常/RTTI 关闭路径。

备选 A（提供自定义 allocator 模板参数）：接口膨胀传染所有缓冲类与
Renderer；裸机场景的真实需求是「缓冲放在我指定的内存」，不是「换一个
分配器实现」。拒绝。

备选 B（采用）：**外部存储构造 + 编译期开关 + 门禁三层**。

1. **外部存储构造**（非拥有）：`BasicFramebuffer(w, h, PixelT* storage)`、
   `DepthBuffer(w, h, float* storage)`、`Renderer(w, h, float* depthStorage)`。
   构造零分配、析构不释放（`owns_` 标志区分），像素放静态内存/内存池/指定
   RAM 段由调用方决定。移动语义保留（转移指针与所属权）。
2. **`SR_NO_EXCEPTIONS` CMake 选项**：core 接口以 `-fno-exceptions -fno-rtti`
   （MSVC 仅 `/GR-`，其 STL 不支持关异常）编译并传导给全部组件；
   io/runtime/tests/examples 自动关闭——io 的第三方解析器与 runtime 的
   线程池异常传播依赖异常设施，属宿主组件。
3. **门禁**：`sr_freestanding_headers_check` 目标把 core/math/render/
   resource/scene 的全部公共头以 `-fno-exceptions -fno-rtti` 逐个编译
   （GCC/Clang，CI Linux 作业执行）。公共头新增 throw/RTTI/线程依赖
   会在构建期失败，而不是等下游裸机项目踩坑。

裸机承诺的边界（文档同步声明）：**仅 core + math + render + resource +
scene**；`<random>`（后处理/粒子）属标准库头、无 OS 依赖，可用；堆分配
在「自带 std::vector」意义上仍存在（教学库不做无堆 STL 替换），外部存储
构造覆盖的是**渲染目标缓冲**这一大头。

## 影响

- `DepthBuffer` 存储从 `std::vector<float>` 改为裸指针 + 禁拷贝/可移动；
  `FloatFramebuffer` 同样补外部存储构造（像素 + 可选深度，定尺寸契约）；
  `FloatFramebuffer::resize` 的移动赋值路径不受影响（有测试锁定）。
- `Renderer::setExternalVertexCache` 把顶点缓存（post-transform cache）也接到外部
  存储：drawMesh 全程零堆。**`baremetal_smoke` 集成测试**（全局 operator new 熔断
  下渲染两帧）是这一承诺的可执行证明，公共头新增任何渲染期堆依赖都会被它抓住。
- 公共头中禁止 `std::fopen` 等 insecure CRT 函数（profiler.h 已改
  `std::ofstream`），core 接口不再定义 `_CRT_SECURE_NO_WARNINGS`。
