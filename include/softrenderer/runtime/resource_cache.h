#pragma once
#include <array>
#include <cstddef>
#include <utility>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>

// ============================================================
// ResourceCache：按 key 缓存共享资源（弱引用 + 线程安全）
//
// 【职责边界】只负责"同一个 key 只创建一次"和生命周期管理，
// 不认识任何文件格式：资源怎么来由调用方给一个工厂回调。
// 因此 runtime 不依赖 io，io 也不需要反向依赖缓存。
//
// 【失败语义】工厂返回空指针表示失败，失败结果不写入缓存：
// 下次调用会重新尝试，避免把一次瞬时失败固化成永久失败。
//
// 【并发首次加载】同一个 key 的并发 miss 只构建一次（single-flight）：
// 加载模型/贴图可能耗时几百毫秒，N 个线程各自构建一份是 N 倍浪费。
// 实现用 64 个分片锁：同 key 串行构建，不同 key 只在哈希冲突时短暂互斥，
// 且不像 map<key, mutex> 那样需要管理锁的生命周期。
// 锁顺序固定为 分片锁 → 全局锁，不存在反向获取。
//
// 用法：
// @code
//   auto texture = sr::ResourceCache::instance().load<Texture>("brick.png", [] {
//       sr::Image image;
//       if (!sr::io::loadImage("brick.png", image))
//           return std::shared_ptr<Texture>{};
//       return std::make_shared<Texture>(std::move(image));
//   });
// @endcode
// ============================================================

namespace sr
{

class ResourceCache
{
   public:
    static ResourceCache &instance()
    {
        static ResourceCache cache;
        return cache;
    }

    // 取得资源：命中且未过期则复用，否则调用 factory 创建。
    // factory 返回 nullptr 表示创建失败——失败不进入缓存。
    template <typename T>
    std::shared_ptr<T> load(const std::string &key, const std::function<std::shared_ptr<T>()> &factory)
    {
        const std::string entryKey = std::string(typeid(T).name()) + ":" + key;
        if (auto hit = lookup<T>(entryKey))
            return hit;

        // 同一 key 串行构建：后来的线程在此等待并复用首个完成者的结果，
        // 而不是各自重建一份昂贵资源。不同 key 走各自分片，不会被慢 factory 阻塞。
        std::lock_guard<std::mutex> guard(stripeLock(entryKey));
        if (auto hit = lookup<T>(entryKey))
            return hit;

        std::shared_ptr<T> created = factory ? factory() : nullptr;
        if (!created)
            return nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_[entryKey] = created;
        }
        return created;
    }

    // 清理所有引用计数已归零的条目
    void purge()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();)
            it = it->second.expired() ? entries_.erase(it) : std::next(it);
    }

    // 清空缓存（强制释放本缓存持有的引用）
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

   private:
    ResourceCache() = default;

    // 命中则返回共享指针；过期条目顺手清掉。
    template <typename T>
    std::shared_ptr<T> lookup(const std::string &entryKey)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(entryKey);
        if (it == entries_.end())
            return nullptr;
        if (auto existing = it->second.lock())
            return std::static_pointer_cast<T>(existing);
        entries_.erase(it);
        return nullptr;
    }

    std::mutex &stripeLock(const std::string &entryKey)
    {
        return stripes_[std::hash<std::string>{}(entryKey) & (kStripeCount - 1)];
    }

    static constexpr size_t kStripeCount = 64;
    std::array<std::mutex, kStripeCount> stripes_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<void>> entries_;
};

} // namespace sr
