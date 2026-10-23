#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "softrenderer/math/vec.h"

// ============================================================
// RenderQueue：渲染队列管理
//
// 现代渲染器不能"来一个画一个"——需要排序：
//   1. 不透明物体：从近到远排序（利用 Early-Z 剔除被遮挡片元）
//   2. 透明物体：从远到近排序（画家算法，保证混合正确）
//   3. 按材质/Shader 分组（减少状态切换，GPU 上极重要）
//
// 本实现提供一个通用的 RenderQueue，每个 DrawItem 带：
//   - 排序键（SortKey）：编码 layer + 距离 + 材质 ID
//   - 绘制回调（或索引）
//
// 参考：Unity 的 RenderingCommandBuffer / Unreal 的 FMeshBatch 思路。
// ============================================================

// 排序层：控制大优先级
namespace sr
{

enum class RenderLayer : uint16_t
{
    Background = 0,      // 天空盒等
    Opaque = 1000,       // 不透明物体
    AlphaTest = 2000,    // 镂空（如树叶）
    Transparent = 3000,  // 半透明
    Overlay = 4000       // UI / 后处理叠加
};

struct DrawItem
{
    RenderLayer layer = RenderLayer::Opaque;
    int materialId = 0;         // 同材质的排一起
    float sortDistance = 0.0f;  // 到相机的距离（不透明用近→远，透明用远→近）
    int meshIndex = -1;         // 指向外部 mesh 数组
    int objectIndex = -1;       // 指向外部 object 数组

    // 组合排序键：layer(16bit) | materialId(16bit) | distance 排序方向
    uint64_t sortKey(bool reverseDistance) const
    {
        uint64_t key = uint64_t(uint16_t(layer)) << 48;
        key |= uint64_t(uint16_t(materialId)) << 32;
        // 不透明：近→远（distance 小排前面）
        // 透明：远→近（distance 大排前面 → 翻转浮点位）
        // 用 memcpy 取位模式（避免 float→uint32_t 的严格别名违规）
        uint32_t distBits;
        std::memcpy(&distBits, &sortDistance, sizeof(distBits));
        if (reverseDistance)
        {
            // 翻转：把 float 位模式取反（对正数有效）
            distBits = ~distBits;
        }
        key |= distBits;
        return key;
    }
};

class RenderQueue
{
   public:
    void clear() { items_.clear(); }

    void add(const DrawItem& item) { items_.push_back(item); }

    // 排序：按 layer → materialId → distance
    // 不透明层：近→远；透明层：远→近
    void sort(const Vec3f& cameraPos, const std::vector<Vec3f>& objectPositions)
    {
        // 先填距离
        for (auto& item : items_)
        {
            if (item.objectIndex >= 0 && item.objectIndex < int(objectPositions.size()))
                item.sortDistance = (objectPositions[item.objectIndex] - cameraPos).lengthSq();
        }
        // 排序
        std::sort(items_.begin(), items_.end(), [](const DrawItem& a, const DrawItem& b) {
            bool aReverse = (a.layer >= RenderLayer::Transparent);
            bool bReverse = (b.layer >= RenderLayer::Transparent);
            // 不同层直接比 layer
            if (a.layer != b.layer)
                return a.layer < b.layer;
            // 同层：先比 materialId
            if (a.materialId != b.materialId)
                return a.materialId < b.materialId;
            // 同材质：比距离
            if (aReverse != bReverse)
                return aReverse;  // 透明的排后面
            if (aReverse)
                return a.sortDistance > b.sortDistance;  // 远→近
            else
                return a.sortDistance < b.sortDistance;  // 近→远
        });
    }

    const std::vector<DrawItem>& items() const { return items_; }
    size_t size() const { return items_.size(); }

    // 统计
    int countOpaque() const
    {
        return int(std::count_if(items_.begin(), items_.end(),
                                 [](const DrawItem& d) { return d.layer < RenderLayer::Transparent; }));
    }
    int countTransparent() const
    {
        return int(std::count_if(items_.begin(), items_.end(),
                                 [](const DrawItem& d) { return d.layer >= RenderLayer::Transparent; }));
    }

   private:
    std::vector<DrawItem> items_;
};

}  // namespace sr
