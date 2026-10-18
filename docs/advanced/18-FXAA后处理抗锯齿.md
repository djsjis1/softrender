# 18 · FXAA 后处理抗锯齿

> MSAA 需要多样本缓冲。FXAA（Fast Approximate Anti-Aliasing）是纯后处理方案——在最终 8 位图像上检测亮度边缘，沿边缘方向模糊。不需要几何信息，开销极低（~1ms/帧），但边缘略模糊。

## 一、MSAA vs FXAA

| | MSAA | FXAA |
|---|---|---|
| 需要什么 | 多样本深度+颜色缓冲 | 最终 8 位 LDR 图像 |
| 开销 | 中-高（N 倍覆盖+深度测试） | 极低（1 pass 全屏） |
| 纹理/透明物体 | 不处理 | ✅ 能处理 |
| 边缘精度 | 精确 | 近似，可能略模糊 |
| 场景适用 | 前向渲染 | 后处理，对任何输入都有效 |

## 二、算法步骤

1. **3×3 邻域亮度**：对每个像素取周围 9 个像素的感知亮度 `lum = 0.299R + 0.587G + 0.114B`
2. **对比度判断**：`max - min < threshold` 则跳过（平坦区不需要 AA）
3. **边缘方向**：比较水平/垂直梯度更大者确定主方向
4. **沿边搜索**：沿边缘方向向两端搜索，找亮度回到 min/max 的端点
5. **亚像素偏移**：根据搜索距离计算 `pixelOffset`，在最陡一侧偏移采样

## 三、关键参数

```cpp
const float FXAA_EDGE_THRESHOLD = 1.0f / 8.0f;   // 最小对比度（低于此不模糊）
const float FXAA_EDGE_THRESHOLD_MIN = 1.0f / 128.0f; // 最低亮度下限
const int FXAA_SEARCH_STEPS = 8;                    // 边缘搜索步数
```

## 四、代码入口

```cpp
#include "core/post_processing.h"
Framebuffer aaResult(W, H);
postprocess::fxaa(srcFramebuffer, aaResult);
```

## 五、动手实验

1. 对 `stage10_shadow` 输出跑一遍 FXAA，对比边缘平滑度（特别是在斜线和球体边缘）
2. 把 `FXAA_SEARCH_STEPS` 从 8 改到 2——长边缘处理不完整，出现残余锯齿
3. 把 `FXAA_EDGE_THRESHOLD` 改大——更多区域被误判为边缘而模糊

---

上一篇：`advanced/17-SSAO`
下一篇：`advanced/19-渲染队列`
