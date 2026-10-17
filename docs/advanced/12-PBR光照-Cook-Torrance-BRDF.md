# 12 · PBR 光照：Cook-Torrance BRDF

> Blinn-Phong 是经验模型——"看起来对"但不守恒能量。PBR（Physically Based Rendering）基于微表面理论，用金属度/粗糙度两个直觉参数统一描述所有材质。本篇讲清 Cook-Torrance BRDF 的三个因子。

## 一、为什么需要 PBR

Blinn-Phong 的问题：(1) 不守恒能量（高光+漫反射可能超过入射光）；(2) 金属和非金属用不同公式；(3) 粗糙度无法物理直觉地控制高光形状。PBR 用**微表面模型**统一解决。

## 二、Cook-Torrance 镜面 BRDF

$$f_r = \frac{D \cdot G \cdot F}{4 (N \cdot V)(N \cdot L)}$$

三个因子各有物理含义：

- **D（法线分布函数）**：微表面法线有多少朝向半程向量 H？用 GGX 分布：粗糙度越大，分布越散，高光越弥散。
- **G（几何遮蔽函数）**：微表面之间互相遮挡了多少？Smith-Schlick 近似。
- **F（菲涅尔）**：掠射角反射增强。Schlick 近似：$F = F_0 + (1-F_0)(1-V\cdot H)^5$。

## 三、金属度工作流

- **金属**：$F_0$ = albedo（金属反射有颜色，如金=黄），漫反射 = 0。
- **非金属**：$F_0 \approx 0.04$（白色弱反射），漫反射 = albedo。

一个 `metallic` 参数在两者间线性混合，统一了所有材质。

## 四、对应代码

| 概念 | 代码 |
|---|---|
| PBR 着色器 | `src/shader/pbr_shader.h`：`PBRShader` |
| GGX D 项 | `fragment` 中 `a2 / (π * denom²)` |
| Smith G 项 | `G1V * G1L` |
| Schlick F 项 | `F0 + (1-F0) * pow(1-VdotH, 5)` |
| 5×5 球阵列 | `examples/stage12_pbr.cpp` |

## 五、动手实验

1. 把 `roughness` 全设 0.01，所有球变成镜面——高光变成针尖。
2. 把 `metallic` 全设 1，观察漫反射消失、球体变暗（金属只靠反射环境光，没有 IBL 时很黑）。
3. 把光强 `lightColor` 从 3 改到 30，高光过曝——引出下一篇的色调映射。

## 六、小结

- PBR = D·G·F / (4·NdotV·NdotL)，能量守恒。
- GGX 控制高光形状，Schlick 控制掠射增强。
- 金属度工作流：一个参数统一金属/非金属。

---

上一篇：[11 · 抗锯齿：从锯齿到平滑](11-抗锯齿-从锯齿到平滑.md)
下一篇：[13 · 后处理管线：Bloom 与色调映射](13-后处理管线-Bloom与色调映射.md)
