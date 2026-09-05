# 32 · 延迟渲染 G-Buffer

> 前向渲染：每个三角形×每个光源都算光照 → O(triangles × lights × pixels)。延迟渲染：先存几何信息，再逐像素算光照 → O(triangles + pixels × lights)。多光源场景下延迟渲染快得多。

## 一、两趟 Pass

**Pass 1（Geometry Pass）**：光栅化时不计算光照，只把世界坐标、法线、albedo、metallic、roughness 写入 G-Buffer。

**Pass 2（Lighting Pass）**：遍历 G-Buffer 每个像素，读取几何信息，对每个光源算 PBR 光照。光源数量只影响 Pass 2 的循环次数，与三角形数无关。

## 二、G-Buffer 布局

| RT | 内容 | 格式 |
|---|---|---|
| RT0 | worldPosition.xyz | Vec3f |
| RT1 | normal.xyz + roughness | Vec4f |
| RT2 | albedo.rgb + metallic | Vec4f |
| Depth | 深度 | float |

## 三、优缺点

| | 前向 | 延迟 |
|---|---|---|
| 多光源 | 慢（O(T×L)） | 快（O(P×L)） |
| 透明物体 | 天然支持 | 需要额外 pass |
| MSAA | 简单 | 复杂（需 per-sample G-Buffer） |
| 内存 | 低 | 高（多 RT） |

## 四、对应代码

| 概念 | 代码 |
|---|---|
| G-Buffer 结构 | `core/gbuffer.h`：`GBuffer` |
| 写入 | `gbuf.write(x, y, z, pos, normal, rough, albedo, metal)` |
| 光照 Pass | `lightingPass(gbuf, lightDir, lightColor, camPos, ambient, out)` |

---

上一篇：[31 · ECS 场景管理](../engineering/31-ECS场景管理.md)
下一篇：[33 · IBL 环境光照与 Cubemap](33-IBL环境光照与Cubemap.md)
